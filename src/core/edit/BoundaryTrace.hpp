// src/core/edit/BoundaryTrace.hpp
#pragma once
// ============================================================================
//  BoundaryTrace — detecção de contorno fechado por ponto interno.
//  (estilo BOUNDARY/HATCH "clique dentro" do AutoCAD)
//
//  Objetivo: dado um ponto `inside` clicado DENTRO de uma região, descobrir o
//  menor loop fechado que o envolve, montado a partir dos SEGMENTOS de todas as
//  entidades próximas. Tudo é tesselado via Entity::emitTo(RenderBatch&), de modo
//  que círculos/arcos/polilinhas já chegam como vários segmentos (GL_LINES).
//
//  Header-only (inline), sem Qt/OpenGL, sem mexer no CMake. Reusa
//  segmentIntersect() de SegmentOps.hpp.
//
//  --------------------------------------------------------------------------
//  ASSINATURAS REAIS DE QUE DEPENDEMOS (lidas dos headers):
//    DrawingManager::query(const AABB&) const -> std::vector<EntityId>
//    DrawingManager::getEntity(EntityId) const -> const Entity*
//    DrawingManager::forEach(Fn) const               (itera const Entity&)
//    Entity::emitTo(RenderBatch&) const              (emite segmentos)
//    Entity::boundingBox() const -> AABB
//    RenderBatch::lineVertices : std::vector<Point3> (pares = segmentos)
//    segmentIntersect(a,b,c,d,out) -> bool           (interseção 2D finita)
//    AABB::expand(...), AABB::contains(Point3), AABB::center()
//    Vec3/Point3: +,-,*,dot,cross,length,normalized
//  --------------------------------------------------------------------------
//
//  ALGORITMO (pragmático e honesto):
//    1) Coleta de segmentos: pega o boundingBox de TODAS as entidades (forEach),
//       cria uma janela generosa ao redor da cena, faz query(janela) e emite
//       cada entidade num RenderBatch. lineVertices vira lista de segmentos.
//    2) Planarização: quebra cada segmento em TODOS os pontos de interseção com
//       os demais (segmentIntersect), gerando sub-arestas curtas. Solda pontos
//       coincidentes em nós únicos com tolerância `weld` (sceneTol escalada à
//       cena). Monta um grafo planar (nós + meias-arestas).
//    3) Face tracing: lança um raio horizontal de `inside` para +X e acha a
//       primeira aresta cruzada à direita. A partir dela, caminha sempre
//       virando o "mais à direita" (next = aresta com menor ângulo horário a
//       partir da reversa) — isto percorre a face interna (limitada) que contém
//       o ponto. Fecha o loop ao voltar ao nó inicial.
//    4) Valida que o ponto está DENTRO do loop (point-in-polygon). Se não fechar
//       ou o ponto cair fora, retorna vazio.
//
//  LIMITAÇÕES (assumidas):
//    * Estritamente 2D no plano XY (z ignorado), como o resto do índice.
//    * Qualidade de arcos/círculos depende da tesselação do emitTo (loop será
//      poligonal, não com arcos verdadeiros).
//    * Não resolve auto-interseções degeneradas / segmentos colineares
//      sobrepostos (segmentIntersect já retorna false p/ colineares).
//    * Custo O(S^2) na quebra por interseção — bom para janelas pequenas
//      (uso típico: poucas entidades ao redor do clique), não para a cena toda.
//    * Regiões abertas (sem loop que cerque o ponto) -> vazio (honesto).
//    * Buracos/ilhas não são tratados: retorna apenas o loop externo da célula.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "core/document/DrawingManager.hpp"
#include "core/edit/SegmentOps.hpp"
#include "core/geometry/Entity.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/math/AABB.hpp"
#include "core/math/Vec.hpp"

namespace cad {
namespace detail_boundary {

// Segmento 2D simples (par de pontos).
struct Seg {
    Point3 a, b;
};

// Nó do grafo planar (ponto soldado + suas meias-arestas de saída).
struct Node {
    Point3 p;
    std::vector<std::size_t> out;  // índices em 'halfEdges'
};

// Meia-aresta dirigida from->to. twin é a meia-aresta reversa (to->from).
struct HalfEdge {
    std::size_t from{0};
    std::size_t to{0};
    std::size_t twin{0};
    double angle{0.0};  // atan2(dir.y, dir.x) — direção de saída do nó 'from'
};

inline double dist2(const Point3& a, const Point3& b) {
    const double dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Coleta todos os segmentos das entidades dentro de `window`.
inline std::vector<Seg> collectSegments(const DrawingManager& doc,
                                        const AABB& window) {
    std::vector<Seg> segs;
    const std::vector<EntityId> ids = doc.query(window);
    RenderBatch batch;
    for (EntityId id : ids) {
        const Entity* e = doc.getEntity(id);
        if (!e || !e->visible()) continue;
        batch.clear();
        e->emitTo(batch);  // tessela tudo em pares de lineVertices
        const auto& v = batch.lineVertices;
        for (std::size_t i = 0; i + 1 < v.size(); i += 2)
            segs.push_back(Seg{v[i], v[i + 1]});
    }
    return segs;
}

// Quebra cada segmento em todos os pontos de interseção com os demais.
// Retorna uma lista achatada de sub-segmentos curtos.
inline std::vector<Seg> splitAtIntersections(const std::vector<Seg>& segs,
                                             double tol) {
    std::vector<Seg> out;
    const double tol2 = tol * tol;
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const Point3 a = segs[i].a, b = segs[i].b;
        if (dist2(a, b) <= tol2) continue;  // degenerado

        // Coleta parâmetros t (0..1 ao longo de a->b) de cortes.
        std::vector<double> ts;
        ts.push_back(0.0);
        ts.push_back(1.0);
        const Vec3 ab = b - a;
        const double abLen2 = ab.lengthSq();
        for (std::size_t j = 0; j < segs.size(); ++j) {
            if (j == i) continue;
            Point3 ip;
            if (!segmentIntersect(a, b, segs[j].a, segs[j].b, ip)) continue;
            // projeta ip sobre a->b para obter t
            const double t = ((ip.x - a.x) * ab.x + (ip.y - a.y) * ab.y) / abLen2;
            if (t > tol && t < 1.0 - tol) ts.push_back(t);
        }
        std::sort(ts.begin(), ts.end());

        // Emite sub-segmentos entre cortes consecutivos (descartando duplicados).
        Point3 prev = a;
        double prevT = 0.0;
        for (std::size_t k = 1; k < ts.size(); ++k) {
            if (ts[k] - prevT < tol) continue;
            const Point3 cur{a.x + ab.x * ts[k], a.y + ab.y * ts[k], 0.0};
            if (dist2(prev, cur) > tol2) out.push_back(Seg{prev, cur});
            prev = cur;
            prevT = ts[k];
        }
    }
    return out;
}

// Solda pontos coincidentes e constrói o grafo planar (nós + meias-arestas).
struct Graph {
    std::vector<Node> nodes;
    std::vector<HalfEdge> halfEdges;
};

inline std::size_t weldNode(Graph& g, const Point3& p, double weld) {
    const double weld2 = weld * weld;
    for (std::size_t i = 0; i < g.nodes.size(); ++i)
        if (dist2(g.nodes[i].p, p) <= weld2) return i;
    g.nodes.push_back(Node{p, {}});
    return g.nodes.size() - 1;
}

inline Graph buildGraph(const std::vector<Seg>& segs, double weld) {
    Graph g;
    for (const Seg& s : segs) {
        const std::size_t na = weldNode(g, s.a, weld);
        const std::size_t nb = weldNode(g, s.b, weld);
        if (na == nb) continue;  // colapsou em nada

        const Point3 pa = g.nodes[na].p, pb = g.nodes[nb].p;

        HalfEdge h1;
        h1.from = na; h1.to = nb;
        h1.angle = std::atan2(pb.y - pa.y, pb.x - pa.x);
        HalfEdge h2;
        h2.from = nb; h2.to = na;
        h2.angle = std::atan2(pa.y - pb.y, pa.x - pb.x);

        const std::size_t i1 = g.halfEdges.size();
        const std::size_t i2 = i1 + 1;
        h1.twin = i2;
        h2.twin = i1;
        g.halfEdges.push_back(h1);
        g.halfEdges.push_back(h2);
        g.nodes[na].out.push_back(i1);
        g.nodes[nb].out.push_back(i2);
    }
    return g;
}

// Point-in-polygon (ray casting) no plano XY.
inline bool pointInPolygon(const std::vector<Point3>& poly, const Point3& p) {
    bool in = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Point3& a = poly[i];
        const Point3& b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
            in = !in;
    }
    return in;
}

// Diferença angular normalizada para (-pi, pi].
inline double wrapPi(double a) {
    const double TWO_PI = 6.28318530717958647692;
    while (a <= -3.14159265358979323846) a += TWO_PI;
    while (a > 3.14159265358979323846) a -= TWO_PI;
    return a;
}

// Dado que chegamos a um nó pela meia-aresta `incoming`, escolhe a próxima
// meia-aresta de saída girando o "mais à direita" (menor giro horário). Isto
// percorre a face interna (limitada) à esquerda do sentido de marcha.
//
// Convenção: queremos a face à DIREITA da meia-aresta de partida (que aponta
// para +Y do ponto interno). "Mais à direita" = entre as saídas do nó destino,
// escolher aquela cujo ângulo é o próximo no sentido horário a partir da
// direção de onde viemos.
inline std::size_t nextHalfEdge(const Graph& g, std::size_t incoming) {
    const HalfEdge& in = g.halfEdges[incoming];
    const Node& node = g.nodes[in.to];
    // direção "de volta" a partir do nó destino é a do twin (to->from)
    const double back = g.halfEdges[in.twin].angle;

    std::size_t best = in.twin;  // fallback: volta por onde veio (beco sem saída)
    double bestTurn = 1e300;
    for (std::size_t he : node.out) {
        if (he == in.twin) continue;  // não voltar imediatamente (a menos que seja a única)
        // giro horário de 'back' até a saída: queremos o MENOR positivo.
        double turn = wrapPi(back - g.halfEdges[he].angle);
        if (turn <= 0.0) turn += 6.28318530717958647692;
        if (turn < bestTurn) {
            bestTurn = turn;
            best = he;
        }
    }
    return best;
}

}  // namespace detail_boundary

// ----------------------------------------------------------------------------
//  traceBoundary — API pública.
//  Tenta achar o menor contorno fechado que envolve `inside`, montado a partir
//  dos SEGMENTOS de todas as entidades próximas (tudo tesselado via emitTo).
//  Retorna o loop (em ordem) como vector<Point3>, ou vazio se não houver região.
// ----------------------------------------------------------------------------
inline std::vector<Point3> traceBoundary(const DrawingManager& doc,
                                         const Point3& inside,
                                         double sceneTol = 1e-6) {
    using namespace detail_boundary;

    // (1) Janela da cena: boundingBox de tudo, com folga. Se vazio, nada a fazer.
    AABB scene;
    doc.forEach([&](const Entity& e) { scene.expand(e.boundingBox()); });
    if (!scene.valid()) return {};

    // Garante que o ponto clicado esteja dentro da janela; adiciona folga.
    scene.expand(inside);
    const Vec3 ext{scene.max.x - scene.min.x, scene.max.y - scene.min.y, 0.0};
    const double diag = std::max(1.0, std::sqrt(ext.x * ext.x + ext.y * ext.y));
    const Vec3 pad{diag * 0.05 + 1.0, diag * 0.05 + 1.0, 0.0};
    AABB window;
    window.expand(scene.min - pad);
    window.expand(scene.max + pad);

    // Tolerâncias escaladas à cena (solda e cortes).
    const double weld = std::max(sceneTol, diag * sceneTol);
    const double tol = weld;

    // (2) Coleta + planarização.
    std::vector<Seg> raw = collectSegments(doc, window);
    if (raw.empty()) return {};
    std::vector<Seg> split = splitAtIntersections(raw, tol);
    if (split.empty()) return {};
    Graph g = buildGraph(split, weld);
    if (g.halfEdges.empty()) return {};

    // (3) Raio horizontal de `inside` para +X: acha a 1ª aresta cruzada.
    //     Usamos um segmento longo (inside -> bem além do limite direito).
    const double far = window.max.x + diag + 10.0;
    const Point3 rayA = inside;
    const Point3 rayB{far, inside.y, 0.0};

    std::size_t startHE = static_cast<std::size_t>(-1);
    double bestX = 1e300;
    for (std::size_t i = 0; i < g.halfEdges.size(); ++i) {
        const HalfEdge& he = g.halfEdges[i];
        const Point3 p0 = g.nodes[he.from].p;
        const Point3 p1 = g.nodes[he.to].p;
        Point3 ip;
        if (!segmentIntersect(rayA, rayB, p0, p1, ip)) continue;
        if (ip.x <= inside.x + tol) continue;  // só à direita do ponto
        if (ip.x < bestX) {
            bestX = ip.x;
            // Escolhe a orientação da meia-aresta cujo sentido aponta para
            // "cima" (cruzando o raio de baixo p/ cima), de modo que a face à
            // esquerda da marcha contenha o ponto interno.
            startHE = (p1.y >= p0.y) ? i : he.twin;
        }
    }
    if (startHE == static_cast<std::size_t>(-1)) return {};

    // (4) Face tracing: caminha virando sempre "mais à direita".
    std::vector<Point3> loop;
    std::vector<std::size_t> visited;
    std::size_t cur = startHE;
    const std::size_t guard = g.halfEdges.size() * 4 + 8;  // anti-loop infinito
    for (std::size_t steps = 0; steps < guard; ++steps) {
        const HalfEdge& he = g.halfEdges[cur];
        loop.push_back(g.nodes[he.from].p);
        visited.push_back(cur);
        const std::size_t nxt = nextHalfEdge(g, cur);
        if (nxt == startHE) {
            // Fechou o loop. Valida que o ponto está dentro.
            if (loop.size() >= 3 && cad::detail_boundary::pointInPolygon(loop, inside)) return loop;
            return {};  // fechou mas não cerca o ponto (face errada/externa)
        }
        cur = nxt;
    }
    return {};  // não fechou dentro do orçamento -> região aberta/honesto
}

}  // namespace cad

// ============================================================================
//  SMOKE (conceitual — NÃO compilado aqui):
//
//    Quadrado [0,0]-[10,10] com 4 linhas:
//      Line{(0,0)->(10,0)}, Line{(10,0)->(10,10)},
//      Line{(10,10)->(0,10)}, Line{(0,10)->(0,0)}
//    Inseridas no DrawingManager 'doc'. Então:
//
//      auto loop = cad::traceBoundary(doc, cad::Point3{5,5,0});
//      // loop.size() ~ 4 (os 4 cantos do quadrado, em ordem)
//      // e pointInPolygon(loop, {5,5}) == true
//
//    Passo a passo:
//      - forEach -> scene = [0,0]-[10,10]; window com folga.
//      - collectSegments -> 4 segmentos (emitTo de cada Line).
//      - splitAtIntersections -> sem cruzamentos internos: 4 segmentos.
//      - buildGraph -> 4 nós (cantos), 8 meias-arestas.
//      - raio (5,5)->(+X): cruza a aresta direita x=10 em (10,5);
//        startHE = meia-aresta com from.y<=to.y (sobe).
//      - nextHalfEdge gira "mais à direita" em cada canto, percorrendo o
//        quadrado e voltando ao início -> loop de 4 vértices.
//      - pointInPolygon({...4 cantos...}, {5,5}) == true -> retorna o loop.
// ============================================================================
