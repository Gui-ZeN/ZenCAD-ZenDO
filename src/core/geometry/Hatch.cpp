// src/core/geometry/Hatch.cpp
#include "core/geometry/Hatch.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/math/Segment.hpp"
#include "core/math/Vec.hpp"
#include "core/math/AABB.hpp"
#include "core/math/Constants.hpp"
#include <vector>
#include <algorithm>
#include <cmath>

namespace cad {

namespace {

// Tolerância geométrica para descartar arestas horizontais e duplicatas.
constexpr double kTol = 1e-9;

// Teste ponto-em-polígono pela regra par-ímpar (ray casting horizontal) no XY.
bool pointInPolygon2D(const std::vector<Point3>& poly, double px, double py) {
    bool inside = false;
    const std::size_t n = poly.size();
    if (n < 3) return false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double yi = poly[i].y, yj = poly[j].y;
        const double xi = poly[i].x, xj = poly[j].x;
        // A aresta cruza a horizontal y=py? E o cruzamento fica à direita de px?
        if (((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

// --- Ear clipping (triangulação de polígono SIMPLES no plano XY) --------------
// Usado pelo padrão Solid: recebe o contorno já tesselado de um loop e emite
// triângulos via batch.addTriangle. Polígono simples (sem buracos) é suficiente.

// Área 2x com sinal do polígono (shoelace). >0 => CCW, <0 => CW. Serve para
// detectar a orientação e normalizá-la para CCW antes de cortar as orelhas.
double signedArea2D(const std::vector<Point3>& p) {
    double a = 0.0;
    const std::size_t n = p.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++)
        a += (p[j].x * p[i].y) - (p[i].x * p[j].y);
    return a; // = 2 * área orientada
}

// Z da componente do produto vetorial (b-a) x (c-a). Em CCW, >0 => convexo.
double crossZ(const Point3& a, const Point3& b, const Point3& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Ponto p dentro (ou na borda) do triângulo a,b,c? Coordenadas baricêntricas via
// sinais dos três cross products. Tolerante a borda (>= -kTol) para robustez.
bool pointInTriangle2D(const Point3& p, const Point3& a, const Point3& b, const Point3& c) {
    const double d1 = crossZ(a, b, p);
    const double d2 = crossZ(b, c, p);
    const double d3 = crossZ(c, a, p);
    const bool hasNeg = (d1 < -kTol) || (d2 < -kTol) || (d3 < -kTol);
    const bool hasPos = (d1 > kTol) || (d2 > kTol) || (d3 > kTol);
    return !(hasNeg && hasPos); // todos do mesmo lado (ou na borda) => dentro
}

// Triangula o contorno `poly` (XY) por ear clipping e emite as orelhas no batch.
void triangulateEarClipping(const std::vector<Point3>& poly, RenderBatch& batch) {
    const std::size_t n = poly.size();
    if (n < 3) return; // nada a preencher

    // Trabalha sobre índices num anel, garantindo orientação CCW. Se o contorno
    // veio CW, invertemos a ordem dos índices — assim "convexo" sempre é crossZ>0.
    std::vector<std::size_t> idx(n);
    if (signedArea2D(poly) >= 0.0) {
        for (std::size_t i = 0; i < n; ++i) idx[i] = i;            // já CCW
    } else {
        for (std::size_t i = 0; i < n; ++i) idx[i] = n - 1 - i;   // CW -> CCW
    }

    // Enquanto restarem > 3 vértices, acha uma orelha e a remove. Robustez: se uma
    // passada inteira não encontrar nenhuma orelha (polígono degenerado/colinear ou
    // não-simples), saímos do laço (fallback) para nunca travar.
    while (idx.size() > 3) {
        const std::size_t m = idx.size();
        bool earFound = false;

        for (std::size_t i = 0; i < m; ++i) {
            const std::size_t i0 = idx[(i + m - 1) % m];
            const std::size_t i1 = idx[i];
            const std::size_t i2 = idx[(i + 1) % m];
            const Point3& a = poly[i0];
            const Point3& b = poly[i1];
            const Point3& c = poly[i2];

            // Vértice reflexo (ou colinear) não é orelha (em CCW, convexo => >0).
            if (crossZ(a, b, c) <= kTol) continue;

            // Nenhum outro vértice do polígono pode estar dentro do triângulo a,b,c.
            bool contains = false;
            for (std::size_t k = 0; k < m; ++k) {
                const std::size_t vk = idx[k];
                if (vk == i0 || vk == i1 || vk == i2) continue;
                if (pointInTriangle2D(poly[vk], a, b, c)) { contains = true; break; }
            }
            if (contains) continue;

            // Orelha válida: emite o triângulo e remove o vértice do meio.
            batch.addTriangle(a, b, c);
            idx.erase(idx.begin() + i);
            earFound = true;
            break;
        }

        if (!earFound) return; // fallback: evita laço infinito em casos degenerados
    }

    // Último triângulo restante (3 vértices).
    batch.addTriangle(poly[idx[0]], poly[idx[1]], poly[idx[2]]);
}

// --- Preenchimento sólido COM FUROS (ilhas) -----------------------------------
// Abordagem (a mais simples e robusta sem reescrever o ear clipping para suportar
// buracos): triangula APENAS o anel externo e, para cada triângulo gerado,
// descarta-o se o seu CENTROIDE cair dentro de qualquer furo. Furos são todos os
// loops que não são o externo. O loop externo é o de MAIOR área absoluta (mais
// robusto que assumir loops[0]); todos os demais são tratados como furos.
//
// Limitações conhecidas (MVP):
//  * O descarte é por centroide: um triângulo que apenas ENCOSTA/atravessa parte
//    de um furo (sem ter o centroide dentro) não é cortado, gerando leve excesso
//    de fill rente à borda do furo. Para os casos típicos (furo bem menor que o
//    contorno, malha fina do ear clipping) o erro é pequeno e localizado.
//  * Não trata furos aninhados (furo dentro de furo = ilha sólida); todos os
//    loops != externo são considerados vazios.
//  * O contorno (linhas) de TODOS os loops continua sendo desenhado no passo 1
//    do emitTo — inclusive a borda dos furos.
//
void emitSolidWithHoles(const std::vector<std::vector<Point3>>& loops,
                        RenderBatch& batch) {
    if (loops.empty()) return;

    // Preenchimento por VARREDURA (scanline) com regra par-ímpar — robusto a
    // furos/ilhas de qualquer forma (não depende da triangulação do anel). Para
    // cada faixa horizontal entre Ys consecutivos de TODOS os loops, acha os
    // cruzamentos das arestas, pareia (par-ímpar) e emite um trapézio por vão.
    std::vector<std::pair<Point3, Point3>> edges;
    std::vector<double> ys;
    for (const auto& lp : loops) {
        const std::size_t n = lp.size();
        if (n < 3) continue;
        for (std::size_t i = 0; i < n; ++i) {
            edges.emplace_back(lp[i], lp[(i + 1) % n]);
            ys.push_back(lp[i].y);
        }
    }
    if (edges.empty()) return;
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end(),
                         [](double u, double v) { return std::fabs(u - v) < 1e-9; }),
             ys.end());

    struct Span { double xm, xb, xt; };   // x no meio / na base (y0) / no topo (y1)
    for (std::size_t bi = 0; bi + 1 < ys.size(); ++bi) {
        const double y0 = ys[bi], y1 = ys[bi + 1];
        if (y1 - y0 < 1e-9) continue;
        const double ym = (y0 + y1) * 0.5;
        std::vector<Span> xs;
        for (const auto& e : edges) {
            const double ay = e.first.y, by = e.second.y;
            const double lo = std::min(ay, by), hi = std::max(ay, by);
            if (ym <= lo || ym >= hi) continue;           // aresta cruza a faixa
            auto xat = [&](double y) {
                const double t = (y - ay) / (by - ay);
                return e.first.x + t * (e.second.x - e.first.x);
            };
            xs.push_back({xat(ym), xat(y0), xat(y1)});
        }
        std::sort(xs.begin(), xs.end(),
                  [](const Span& p, const Span& q) { return p.xm < q.xm; });
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {   // par-ímpar
            const Span& L = xs[i]; const Span& R = xs[i + 1];
            const Point3 bl{L.xb, y0, 0.0}, br{R.xb, y0, 0.0};
            const Point3 tr{R.xt, y1, 0.0}, tl{L.xt, y1, 0.0};
            batch.addTriangle(bl, br, tr);
            batch.addTriangle(bl, tr, tl);
        }
    }
}

} // namespace

AABB Hatch::boundingBox() const {
    AABB b;
    for (const auto& loop : m_loops)
        for (const auto& v : loop)
            b.expand(v);
    return b;
}

void Hatch::emitTo(RenderBatch& batch) const {
    // 1) Contorno de cada loop (arestas consecutivas + fechamento último->primeiro).
    for (const auto& loop : m_loops) {
        const std::size_t n = loop.size();
        if (n < 2) continue;
        for (std::size_t i = 0; i < n; ++i) {
            const Point3& a = loop[i];
            const Point3& b = loop[(i + 1) % n];
            batch.addSegment(a, b);
        }
    }

    // 2) Linhas de hachura: uma ou mais famílias paralelas, conforme o padrão.
    //    Cada família é desenhada por emitLineFamily (varredura + clipping par-ímpar
    //    numa direção). Todos os ângulos são somados à rotação do usuário (m_angleDeg)
    //    e o espaçamento de cada família é m_spacing * m_scale.
    const double ang     = m_angleDeg * kPi / 180.0;       // rotação do usuário (rad)
    const double spacing = m_spacing * m_scale;            // espaçamento efetivo
    const double d45     = kPi / 4;                        // 45° em radianos

    switch (m_pattern) {
        case HatchPattern::Lines:
            // 1 família na direção do usuário (45° por padrão => comportamento clássico).
            emitLineFamily(batch, ang, spacing);
            break;
        case HatchPattern::ANSI31:
            // Aço: 1 família diagonal a 45° (+ rotação do usuário).
            emitLineFamily(batch, d45 + ang, spacing);
            break;
        case HatchPattern::ANSI37:
            // Xadrez: 2 famílias a 45° e 135° (+ rotação do usuário).
            emitLineFamily(batch, d45 + ang, spacing);
            emitLineFamily(batch, 3.0 * d45 + ang, spacing);
            break;
        case HatchPattern::Grid:
            // Grade ortogonal: 2 famílias a 0° e 90° (+ rotação do usuário).
            emitLineFamily(batch, ang, spacing);
            emitLineFamily(batch, kHalfPi + ang, spacing);
            break;
        case HatchPattern::Solid:
            // Preenchimento sólido RESPEITANDO FUROS: tritura o anel externo
            // (ear clipping) e descarta os triângulos cujo centroide cai dentro
            // de um furo (ilha). O contorno de todos os loops já foi desenhado
            // no passo 1. Vide emitSolidWithHoles para o algoritmo/limitações.
            emitSolidWithHoles(m_loops, batch);
            break;
        case HatchPattern::Gradient:
            // Gradiente (MVP honesto): o RenderBatch só tem trincas de posição —
            // não há cor por vértice/triângulo. Então o FILL geométrico é igual
            // ao Solid (mesmo anel triangulado, furos respeitados). As duas cores
            // do gradiente ficam expostas por gradientColor1()/gradientColor2();
            // cabe ao renderer ler essas cores e interpolá-las ao longo da área
            // (ex.: por faixas/tiras de cor). Aqui não emitimos cor alguma.
            emitSolidWithHoles(m_loops, batch);
            break;
        case HatchPattern::Wood:
            // Madeira: veio = linhas paralelas DENSAS na direção do usuário.
            emitLineFamily(batch, ang, spacing * 0.5);
            break;
        case HatchPattern::Brick:
            // Tijolo/alvenaria: cursos horizontais + juntas verticais a cada 2
            // espaçamentos (blocos 2:1). Ambas as famílias já ficam recortadas
            // pelo contorno (par-ímpar).
            emitLineFamily(batch, ang, spacing);
            emitLineFamily(batch, kHalfPi + ang, spacing * 2.0);
            break;
        case HatchPattern::Concrete:
        case HatchPattern::Sand: {
            // Pontilhado dentro do contorno (regra par-ímpar). O concreto acrescenta
            // pequenos triângulos (agregado); a areia é só pontos finos.
            AABB bb;
            for (const auto& loop : m_loops)
                for (const auto& v : loop) bb.expand(v);
            if (!bb.valid() || spacing <= kTol) break;
            auto inside = [&](double x, double y) {
                int cross = 0;
                for (const auto& loop : m_loops) {
                    const std::size_t n = loop.size();
                    for (std::size_t i = 0; i < n; ++i) {
                        const Point3& a = loop[i];
                        const Point3& b = loop[(i + 1) % n];
                        if ((a.y > y) != (b.y > y)) {
                            const double xi = a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y);
                            if (x < xi) ++cross;
                        }
                    }
                }
                return (cross & 1) != 0;
            };
            const bool sand = (m_pattern == HatchPattern::Sand);
            const double step = spacing * (sand ? 0.6 : 1.1);
            const double rr   = spacing * (sand ? 0.05 : 0.10);
            const double sx = std::ceil(bb.min.x / step) * step;
            const double sy = std::ceil(bb.min.y / step) * step;
            int row = 0;
            for (double y = sy; y <= bb.max.y + kTol; y += step, ++row) {
                int col = 0;
                for (double x = sx; x <= bb.max.x + kTol; x += step, ++col) {
                    // dispersão determinística (sem RNG) p/ não ficar "quadriculado".
                    const double jx = (((col * 7 + row * 3) % 5) - 2) * step * 0.12;
                    const double jy = (((col * 5 + row * 11) % 5) - 2) * step * 0.12;
                    const double px = x + jx, py = y + jy;
                    if (!inside(px, py)) continue;
                    if (!sand && ((col + row) % 3 == 0)) {   // triângulo (agregado)
                        batch.addSegment(Point3{px - rr, py - rr, 0}, Point3{px + rr, py - rr, 0});
                        batch.addSegment(Point3{px + rr, py - rr, 0}, Point3{px, py + rr, 0});
                        batch.addSegment(Point3{px, py + rr, 0}, Point3{px - rr, py - rr, 0});
                    } else {                                  // ponto = "+" curto
                        batch.addSegment(Point3{px - rr, py, 0}, Point3{px + rr, py, 0});
                        batch.addSegment(Point3{px, py - rr, 0}, Point3{px, py + rr, 0});
                    }
                }
            }
            break;
        }
    }
}

void Hatch::emitLineFamily(RenderBatch& batch, double angleRad, double spacing) const {
    // Varredura no sistema girado por -angleRad. Ao girar todos os vértices por
    // -angleRad, as linhas de hachura (que tinham direção +angleRad) viram
    // horizontais (y constante). Varremos em y, calculamos interseções com todas as
    // arestas, ordenamos por x e emitimos os trechos entre pares alternados
    // (par-ímpar). Os pontos de saída são girados de volta por +angleRad.
    const double ca = std::cos(-angleRad);
    const double sa = std::sin(-angleRad);
    const double cb = std::cos(angleRad);   // rotação inversa (+angle) para emitir
    const double sb = std::sin(angleRad);

    // Vértices de cada loop no sistema girado, preservando z (interpolado depois).
    std::vector<std::vector<Point3>> rot;
    rot.reserve(m_loops.size());
    AABB rotBox;
    for (const auto& loop : m_loops) {
        std::vector<Point3> rl;
        rl.reserve(loop.size());
        for (const auto& v : loop) {
            const double rx = v.x * ca - v.y * sa;
            const double ry = v.x * sa + v.y * ca;
            rl.push_back(Point3{rx, ry, v.z});
            rotBox.expand(Point3{rx, ry, v.z});
        }
        rot.push_back(std::move(rl));
    }

    if (!rotBox.valid() || spacing <= kTol) return;

    // Linhas de varredura: y = k*spacing, cobrindo [minY, maxY] da caixa girada.
    // Ancoradas em múltiplos de spacing para estabilidade ao transladar/repetir.
    const double yStart = std::ceil(rotBox.min.y / spacing) * spacing;

    for (double y = yStart; y <= rotBox.max.y + kTol; y += spacing) {
        // Interseções (parâmetro x ao longo da linha; z interpolado na aresta).
        std::vector<std::pair<double, double>> xs;  // (x, z)
        for (const auto& loop : rot) {
            const std::size_t n = loop.size();
            if (n < 2) continue;
            for (std::size_t i = 0; i < n; ++i) {
                const Point3& a = loop[i];
                const Point3& b = loop[(i + 1) % n];
                const double y0 = a.y, y1 = b.y;
                // Ignora arestas horizontais (paralelas à varredura).
                if (std::fabs(y1 - y0) < kTol) continue;
                // A linha y cruza a aresta? Convenção semiaberta [min,max) evita
                // contar vértices duas vezes.
                const double ymin = std::min(y0, y1);
                const double ymax = std::max(y0, y1);
                if (y < ymin - kTol || y >= ymax - kTol) continue;
                const double t = (y - y0) / (y1 - y0);
                const double x = a.x + (b.x - a.x) * t;
                const double z = a.z + (b.z - a.z) * t;
                xs.emplace_back(x, z);
            }
        }
        if (xs.size() < 2) continue;
        std::sort(xs.begin(), xs.end(),
                  [](const auto& l, const auto& r) { return l.first < r.first; });

        // Pares alternados: [0,1], [2,3], ... = interior pela regra par-ímpar.
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
            const double x0 = xs[i].first,     z0 = xs[i].second;
            const double x1 = xs[i + 1].first, z1 = xs[i + 1].second;
            if (x1 - x0 <= kTol) continue;  // trecho degenerado
            // Gira de volta por +angle para o sistema do mundo.
            const Point3 a{x0 * cb - y * sb, x0 * sb + y * cb, z0};
            const Point3 b{x1 * cb - y * sb, x1 * sb + y * cb, z1};
            batch.addSegment(a, b);
        }
    }
}

HitResult Hatch::hitTest(const Ray& pickRay, double tol) const {
    const Point3 p = pickRay.origin;

    // (a) Perto de alguma aresta de contorno?
    HitResult best;
    for (const auto& loop : m_loops) {
        const std::size_t n = loop.size();
        if (n < 2) continue;
        for (std::size_t i = 0; i < n; ++i) {
            const Point3& a = loop[i];
            const Point3& b = loop[(i + 1) % n];
            const Point3  c    = closestPointOnSegment2D(p, a, b);
            const double  dx   = p.x - c.x, dy = p.y - c.y;
            const double  dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= tol && (!best.hit || dist < best.distance)) {
                best.hit = true; best.distance = dist; best.point = c;
            }
        }
    }
    if (best.hit) return best;

    // (b) Dentro de algum loop (par-ímpar)?
    for (const auto& loop : m_loops) {
        if (pointInPolygon2D(loop, p.x, p.y)) {
            HitResult r;
            r.hit = true; r.distance = 0.0; r.point = p;
            return r;
        }
    }
    return {};
}

void Hatch::transform(const Matrix4& m) {
    for (auto& loop : m_loops)
        for (auto& v : loop)
            v = m.transformPoint(v);
}

void Hatch::appendSnapPoints(std::vector<SnapPoint>& out) const {
    for (const auto& loop : m_loops)
        for (const auto& v : loop)
            out.push_back({v, SnapType::Endpoint});
}

std::unique_ptr<Entity> Hatch::clone() const {
    return std::make_unique<Hatch>(*this);
}

} // namespace cad
