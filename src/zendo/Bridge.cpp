// src/zendo/Bridge.cpp
#include "Bridge.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "app/ProjectIo.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/document/Layer.hpp"
#include "core/geometry/Hatch.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/spatial/Quadtree.hpp"

using cad::HalfEdgeMesh;
using cad::Point3;
using cad::Vec3;

namespace bridge {
namespace {

struct View { Vec3 dir, right, up; };   // dir aponta PARA DENTRO da cena

View viewFor(char nsew) {
    Vec3 d{0, 1, 0};
    switch (nsew) {
        case 'S': d = {0, 1, 0}; break;    // visto do sul, olhando norte
        case 'N': d = {0, -1, 0}; break;
        case 'L': d = {-1, 0, 0}; break;   // visto do leste, olhando oeste
        case 'O': d = {1, 0, 0}; break;
        default: break;
    }
    const Vec3 up{0, 0, 1};
    return {d, d.cross(up).normalized(), up};
}

struct Tri { Point3 a, b, c; };

// Möller–Trumbore: raio (o + t·dv) contra o triângulo; t>eps.
bool rayTri(const Point3& o, const Vec3& dv, const Tri& t, double& tOut) {
    constexpr double kEps = 1e-9;
    const Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
    const Vec3 pv = dv.cross(e2);
    const double det = e1.dot(pv);
    if (std::abs(det) < kEps) return false;
    const double inv = 1.0 / det;
    const Vec3 tv = o - t.a;
    const double u = tv.dot(pv) * inv;
    if (u < -kEps || u > 1.0 + kEps) return false;
    const Vec3 qv = tv.cross(e1);
    const double v = dv.dot(qv) * inv;
    if (v < -kEps || u + v > 1.0 + kEps) return false;
    const double tt = e2.dot(qv) * inv;
    if (tt <= 1e-6) return false;
    tOut = tt;
    return true;
}

// A amostra em P (profundidade >= cutD) está oculta por material além do corte?
bool occluded(const Point3& P, const View& vw, const std::vector<Tri>& occ,
              double cutD) {
    const Vec3 dv = vw.dir * -1.0;              // de volta ao observador
    const Point3 o = P + dv * 1e-4;
    for (const Tri& t : occ) {
        double tt = 0.0;
        if (!rayTri(o, dv, t, tt)) continue;
        const Point3 h = o + dv * tt;
        if (h.dot(vw.dir) >= cutD - 1e-9) return true;   // material mantido
    }
    return false;
}

// Projeta as ARESTAS visíveis (recortadas em profundidade >= cutD) em UV.
int emitVisibleEdges(const std::vector<const HalfEdgeMesh*>& meshes,
                     const View& vw, double cutD,
                     const std::vector<Tri>& occ,
                     cad::DrawingManager& doc) {
    int count = 0;
    auto uv = [&](const Point3& p) -> Point3 {
        return {p.dot(vw.right), p.dot(vw.up), 0.0};
    };
    for (const HalfEdgeMesh* m : meshes) {
        std::vector<Point3> lines;
        m->edgeLines(lines);
        for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
            Point3 a = lines[i], b = lines[i + 1];
            // recorte em profundidade (corte): mantém só o lado de lá
            double da = a.dot(vw.dir) - cutD, db = b.dot(vw.dir) - cutD;
            if (da < 0.0 && db < 0.0) continue;
            if (da < 0.0 || db < 0.0) {
                const double u = da / (da - db);      // ponto que cruza o plano
                const Point3 x = a + (b - a) * u;
                if (da < 0.0) a = x; else b = x;
            }
            const Point3 pa = uv(a), pb = uv(b);
            const double lenScreen = std::hypot(pb.x - pa.x, pb.y - pa.y);
            if (lenScreen < 1e-4) continue;           // aresta de topo (ponto)
            const double len3 = (b - a).length();
            const int K = std::clamp(int(len3 / 0.04), 8, 120);
            int run0 = -1;
            auto flush = [&](int i0, int i1) {
                if (i1 <= i0) return;                 // amostra isolada: pula
                const double u0 = double(i0) / K, u1 = double(i1) / K;
                auto ln = std::make_unique<cad::Line>(
                    uv(a + (b - a) * u0), uv(a + (b - a) * u1));
                ln->setLayer("ARESTAS");
                doc.addEntity(std::move(ln));
                ++count;
            };
            for (int s = 0; s <= K; ++s) {
                const Point3 P = a + (b - a) * (double(s) / K);
                const bool vis = !occluded(P, vw, occ, cutD);
                if (vis && run0 < 0) run0 = s;
                if (!vis && run0 >= 0) { flush(run0, s - 1); run0 = -1; }
            }
            if (run0 >= 0) flush(run0, K);
        }
    }
    return count;
}

// Encadeia segmentos de corte em laços fechados (chaves quantizadas).
std::vector<std::vector<Point3>> chainLoops(
    std::vector<std::pair<Point3, Point3>> segs) {
    auto key = [](const Point3& p) {
        return std::pair<long long, long long>{llround(p.x * 1e6),
                                               llround(p.y * 1e6)};
    };
    std::multimap<std::pair<long long, long long>, std::size_t> byStart;
    std::vector<bool> used(segs.size(), false);
    for (std::size_t i = 0; i < segs.size(); ++i) {
        byStart.insert({key(segs[i].first), i});
        byStart.insert({key(segs[i].second), i});
    }
    std::vector<std::vector<Point3>> loops;
    for (std::size_t i = 0; i < segs.size(); ++i) {
        if (used[i]) continue;
        used[i] = true;
        std::vector<Point3> loop{segs[i].first, segs[i].second};
        bool closed = false;
        for (int guard = 0; guard < int(segs.size()) && !closed; ++guard) {
            const auto k = key(loop.back());
            std::size_t next = SIZE_MAX;
            for (auto [it, end] = byStart.equal_range(k); it != end; ++it)
                if (!used[it->second]) { next = it->second; break; }
            if (next == SIZE_MAX) break;
            used[next] = true;
            const bool fwd = key(segs[next].first) == k;
            loop.push_back(fwd ? segs[next].second : segs[next].first);
            closed = key(loop.back()) == key(loop.front());
        }
        if (closed && loop.size() >= 4) {
            loop.pop_back();                    // fecha pela flag da polilinha
            loops.push_back(std::move(loop));
        }
    }
    return loops;
}

Result writeOut(cad::DrawingManager& doc, const QString& outPath,
                int loops, int segs) {
    cad::LayoutTable lt;
    lt.addDefault("Prancha 1");
    cad::StyleTable st;
    cad::ProjectSettings ps;
    ps.currentLayer = "ARESTAS";
    QString err;
    Result r;
    if (!cad::saveProject(outPath, doc, lt, st, ps, &err)) {
        r.error = err;
        return r;
    }
    r.ok = true;
    r.cutLoops = loops;
    r.edgeSegs = segs;
    return r;
}

std::unique_ptr<cad::DrawingManager> makeOutDoc() {
    const cad::AABB world =
        cad::AABB::fromCenterHalf({0, 0, 0}, {1e6, 1e6, 1e6});
    auto doc = std::make_unique<cad::DrawingManager>(
        std::make_unique<cad::Quadtree>(world, 12, 16));
    cad::Layer cut;
    cut.name = "CORTE";
    cut.color = {235, 230, 215, 255};
    doc->layers().add(cut);
    cad::Layer ar;
    ar.name = "ARESTAS";
    ar.color = {150, 145, 130, 255};
    doc->layers().add(ar);
    return doc;
}

std::vector<Tri> collectTris(const std::vector<const HalfEdgeMesh*>& meshes) {
    std::vector<Tri> occ;
    for (const HalfEdgeMesh* m : meshes) {
        std::vector<Point3> tris;
        m->triangulate(tris);
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3)
            occ.push_back({tris[i], tris[i + 1], tris[i + 2]});
    }
    return occ;
}

} // namespace

Result exportElevation(const std::vector<const HalfEdgeMesh*>& meshes,
                       char nsew, const QString& outPath) {
    const View vw = viewFor(nsew);
    const std::vector<Tri> occ = collectTris(meshes);
    auto doc = makeOutDoc();
    const int segs = emitVisibleEdges(meshes, vw, -1e300, occ, *doc);
    if (segs == 0) return {false, 0, 0, QStringLiteral("nada visível")};
    return writeOut(*doc, outPath, 0, segs);
}

// núcleo do corte: recebe a VISTA pronta (dir/right/up ortonormais) e o
// offset do plano ao longo de dir — eixo e plano arbitrário são só dois
// jeitos de montar esses parâmetros.
static Result sectionWithView(const std::vector<const HalfEdgeMesh*>& meshes,
                              const View& vw, double cutD,
                              const QString& outPath) {
    const std::vector<Tri> occ = collectTris(meshes);
    auto doc = makeOutDoc();
    auto uv = [&](const Point3& p) -> Point3 {
        return {p.dot(vw.right), p.dot(vw.up), 0.0};
    };

    // 1) laços de CORTE: plano × triângulos de cada sólido, encadeados
    int nLoops = 0;
    for (const HalfEdgeMesh* m : meshes) {
        std::vector<Point3> tris;
        m->triangulate(tris);
        std::vector<std::pair<Point3, Point3>> segs;
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
            const Point3 p[3] = {tris[i], tris[i + 1], tris[i + 2]};
            double d[3];
            for (int k = 0; k < 3; ++k) d[k] = p[k].dot(vw.dir) - cutD;
            std::vector<Point3> x;
            for (int k = 0; k < 3; ++k) {
                const int j = (k + 1) % 3;
                if ((d[k] > 0) != (d[j] > 0)) {
                    const double u = d[k] / (d[k] - d[j]);
                    x.push_back(uv(p[k] + (p[j] - p[k]) * u));
                }
            }
            if (x.size() == 2 &&
                std::hypot(x[1].x - x[0].x, x[1].y - x[0].y) > 1e-9)
                segs.push_back({x[0], x[1]});
        }
        for (auto& loop : chainLoops(std::move(segs))) {
            auto h = std::make_unique<cad::Hatch>(
                std::vector<std::vector<Point3>>{loop},
                cad::HatchPattern::Solid, 0.0, 1.0, 0.5);
            h->setLayer("CORTE");
            doc->addEntity(std::move(h));
            auto pl = std::make_unique<cad::Polyline>(std::move(loop), true);
            pl->setLayer("CORTE");
            doc->addEntity(std::move(pl));
            ++nLoops;
        }
    }

    // 2) fundo projetado: arestas além do plano, com visibilidade
    const int segs = emitVisibleEdges(meshes, vw, cutD, occ, *doc);
    if (nLoops == 0 && segs == 0)
        return {false, 0, 0, QStringLiteral("o plano não corta o modelo")};
    return writeOut(*doc, outPath, nLoops, segs);
}

Result exportSection(const std::vector<const HalfEdgeMesh*>& meshes,
                     char axis, double pos, int lookSign,
                     const QString& outPath) {
    View vw;
    const double sign = lookSign >= 0 ? 1.0 : -1.0;
    if (axis == 'X' || axis == 'x') vw.dir = {sign, 0, 0};
    else                            vw.dir = {0, sign, 0};
    vw.up = {0, 0, 1};
    vw.right = vw.dir.cross(vw.up).normalized();
    return sectionWithView(meshes, vw, pos * sign, outPath);
}

// R31: corte por plano ARBITRÁRIO (n, d) — mesma convenção do clip visual da
// R30 (descarta dot(P,n) > d): o desenho olha em -n̂ e mostra o material
// MANTIDO, exatamente o que o usuário vê na tela. O `up` é derivado de forma
// ortonormal (semente Z, ou Y quando o plano é quase horizontal — o corte
// "planta baixa" que o eixo antigo nem sabia fazer).
Result exportSectionPlane(const std::vector<const HalfEdgeMesh*>& meshes,
                          const cad::Vec3& n, double d,
                          const QString& outPath) {
    const double len = n.length();
    if (len < 1e-9)
        return {false, 0, 0, QStringLiteral("normal do plano nula")};
    View vw;
    vw.dir = n * (-1.0 / len);
    const Vec3 upSeed =
        std::abs(vw.dir.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    vw.right = vw.dir.cross(upSeed).normalized();
    vw.up = vw.right.cross(vw.dir).normalized();
    return sectionWithView(meshes, vw, -(d / len), outPath);
}

} // namespace bridge
