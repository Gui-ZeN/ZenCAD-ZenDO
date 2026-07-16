// src/core/edit/TrimOps.cpp
#include "core/edit/TrimOps.hpp"
#include "core/edit/IntersectOps.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/math/Constants.hpp"
#include <algorithm>
#include <cmath>

namespace cad {
namespace {

constexpr double kEps = 1e-7;

double norm2pi(double a) {
    a = std::fmod(a, kTwoPi);
    if (a < 0.0) a += kTwoPi;
    return a;
}
// Distância angular CCW de a até b, em [0, 2π).
double ccwDelta(double a, double b) { return norm2pi(b - a); }
double angleOf(const Point3& c, const Point3& p) { return std::atan2(p.y - c.y, p.x - c.x); }

// --- Line target: remove o intervalo (em t∈[0,1]) que contém o pick ----------
std::vector<std::unique_ptr<Entity>> trimLine(const Line& ln, const std::vector<Point3>& xs,
                                              const Point3& pick) {
    const Point3 a = ln.start(), b = ln.end();
    const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    if (len2 < kEps) return {};
    auto tOf = [&](const Point3& p) { return ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2; };
    auto at  = [&](double t) { return Point3{a.x + t * dx, a.y + t * dy, 0.0}; };

    std::vector<double> bnd{0.0, 1.0};
    for (const Point3& x : xs) {
        const double t = tOf(x);
        if (t > kEps && t < 1.0 - kEps) bnd.push_back(t);
    }
    if (bnd.size() == 2) return {};                  // corte não cruza o interior
    std::sort(bnd.begin(), bnd.end());

    const double tp = std::clamp(tOf(pick), 0.0, 1.0);
    // Remove só o vão que contém o clique (entre os 2 cortes vizinhos). O resto
    // permanece CONTÍNUO: no máx. 2 peças — [início→corte] e [corte→fim] — sem
    // fragmentar nos demais pontos de corte (como o TRIM do AutoCAD).
    std::size_t rm = 0; double bestd = 1e300;
    for (std::size_t i = 0; i + 1 < bnd.size(); ++i) {
        const double c = (bnd[i] + bnd[i + 1]) * 0.5;
        const double d = std::abs(tp - c);
        if (d < bestd) { bestd = d; rm = i; }
    }
    std::vector<std::unique_ptr<Entity>> out;
    const double loCut = bnd[rm], hiCut = bnd[rm + 1];
    if (loCut > kEps)        out.push_back(std::make_unique<Line>(at(0.0), at(loCut)));
    if (hiCut < 1.0 - kEps)  out.push_back(std::make_unique<Line>(at(hiCut), at(1.0)));
    return out;
}

// --- Circle target: remove o arco que contém o pick -> sobra 1 Arc ------------
std::vector<std::unique_ptr<Entity>> trimCircle(const Circle& ci, const std::vector<Point3>& xs,
                                                const Point3& pick) {
    std::vector<double> ang;
    for (const Point3& x : xs) ang.push_back(norm2pi(angleOf(ci.center(), x)));
    std::sort(ang.begin(), ang.end());
    ang.erase(std::unique(ang.begin(), ang.end(),
                          [](double u, double v) { return std::abs(u - v) < 1e-6; }),
              ang.end());
    if (ang.size() < 2) return {};                   // 1 ponto (tangente) não apara círculo

    const double ap = norm2pi(angleOf(ci.center(), pick));
    const std::size_t n = ang.size();
    for (std::size_t i = 0; i < n; ++i) {
        const double s = ang[i], e = ang[(i + 1) % n];
        if (ccwDelta(s, ap) <= ccwDelta(s, e) + 1e-12) {   // pick no arco s->e -> remove
            std::vector<std::unique_ptr<Entity>> out;
            out.push_back(std::make_unique<Arc>(ci.center(), ci.radius(), e, s));  // complemento
            return out;
        }
    }
    return {};
}

// --- Arc target: remove o sub-arco que contém o pick -------------------------
std::vector<std::unique_ptr<Entity>> trimArc(const Arc& ar, const std::vector<Point3>& xs,
                                             const Point3& pick) {
    const double a0 = ar.startAngle();
    const double sweep = ccwDelta(a0, ar.endAngle()) < kEps ? kTwoPi : ccwDelta(a0, ar.endAngle());
    std::vector<double> bnd{0.0, sweep};             // parâmetro = avanço CCW desde a0
    for (const Point3& x : xs) {
        const double d = ccwDelta(a0, norm2pi(angleOf(ar.center(), x)));
        if (d > kEps && d < sweep - kEps) bnd.push_back(d);
    }
    if (bnd.size() == 2) return {};
    std::sort(bnd.begin(), bnd.end());

    const double dp = std::clamp(ccwDelta(a0, norm2pi(angleOf(ar.center(), pick))), 0.0, sweep);
    std::size_t rm = 0; double bestd = 1e300;     // remove só o sub-arco do clique
    for (std::size_t i = 0; i + 1 < bnd.size(); ++i) {
        const double c = (bnd[i] + bnd[i + 1]) * 0.5;
        const double d = std::abs(dp - c);
        if (d < bestd) { bestd = d; rm = i; }
    }
    // Como na linha: sobra no máx. 2 sub-arcos contínuos ao redor do vão.
    std::vector<std::unique_ptr<Entity>> out;
    const double loCut = bnd[rm], hiCut = bnd[rm + 1];
    if (loCut > kEps)
        out.push_back(std::make_unique<Arc>(ar.center(), ar.radius(), a0, a0 + loCut));
    if (hiCut < sweep - kEps)
        out.push_back(std::make_unique<Arc>(ar.center(), ar.radius(), a0 + hiCut, a0 + sweep));
    return out;
}

// --- Polyline target: parte nos cortes e remove o trecho do clique ----------
// Parâmetro ao longo da polilinha: s = índiceDoSegmento + t (t em [0,1]).
// (Trata os trechos como retas/cordas — bulges são ignorados nesta versão.)
std::vector<std::unique_ptr<Entity>> trimPolyline(const Polyline& pl,
                                                  const std::vector<Point3>& xs,
                                                  const Point3& pick) {
    const std::vector<Point3>& V = pl.vertices();
    const std::size_t n = V.size();
    if (n < 2) return {};
    const bool closed = pl.closed();
    const std::size_t segs = closed ? n : n - 1;

    auto vert = [&](std::size_t i) { return V[i % n]; };
    auto paramOf = [&](const Point3& p) {            // ponto -> s, no segmento mais próximo
        double best = 1e300, bestS = 0.0;
        for (std::size_t i = 0; i < segs; ++i) {
            const Point3 a = vert(i), b = vert(i + 1);
            const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
            double t = l2 < 1e-12 ? 0.0 : ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2;
            t = std::clamp(t, 0.0, 1.0);
            const double qx = a.x + t * dx, qy = a.y + t * dy;
            const double d = std::hypot(p.x - qx, p.y - qy);
            if (d < best) { best = d; bestS = double(i) + t; }
        }
        return bestS;
    };
    auto pointAt = [&](double s) {                   // s -> ponto na polilinha
        std::size_t i = std::min<std::size_t>(std::size_t(std::floor(s)), segs - 1);
        const double t = s - double(i);
        const Point3 a = vert(i), b = vert(i + 1);
        return Point3{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), 0.0};
    };
    auto buildPiece = [&](double lo, double hi) -> std::unique_ptr<Entity> {
        std::vector<Point3> pts;
        pts.push_back(pointAt(lo));
        for (std::size_t k = std::size_t(std::ceil(lo - 1e-9)); double(k) < hi - 1e-9; ++k) {
            if (double(k) > lo + 1e-9) pts.push_back(vert(k));
        }
        pts.push_back(pointAt(hi));
        if (pts.size() < 2) return nullptr;
        return std::make_unique<Polyline>(pts, false);
    };

    std::vector<double> cuts;
    for (const Point3& x : xs) {
        const double s = paramOf(x);
        if (s > 1e-6 && s < double(segs) - 1e-6) cuts.push_back(s);
    }
    const double sp = paramOf(pick);

    std::vector<std::unique_ptr<Entity>> out;
    if (closed) {
        if (cuts.empty()) return {};
        std::sort(cuts.begin(), cuts.end());
        // Acha o intervalo (entre cortes consecutivos, com volta) que contém o pick;
        // remove-o e devolve o COMPLEMENTO como uma polilinha aberta.
        const std::size_t m = cuts.size();
        for (std::size_t i = 0; i < m; ++i) {
            const double lo = cuts[i], hi = (i + 1 < m) ? cuts[i + 1] : cuts[0] + double(segs);
            const double spw = (sp >= lo) ? sp : sp + double(segs);
            if (spw >= lo - 1e-9 && spw <= hi + 1e-9) {            // pick neste trecho -> remove
                // complemento: de hi (mod segs) dando a volta até lo
                std::vector<Point3> pts;
                pts.push_back(pointAt(std::fmod(hi, double(segs))));
                double k = std::ceil(hi + 1e-9);
                const double end = lo + double(segs);
                for (; k < end - 1e-9; ++k)
                    pts.push_back(vert(std::size_t(std::fmod(k, double(segs)))));
                pts.push_back(pointAt(lo));
                if (pts.size() >= 2) out.push_back(std::make_unique<Polyline>(pts, false));
                break;
            }
        }
        return out;
    }
    // Aberta: limites 0 e segs + cortes internos; remove só o intervalo do clique.
    std::vector<double> bnd{0.0, double(segs)};
    bnd.insert(bnd.end(), cuts.begin(), cuts.end());
    std::sort(bnd.begin(), bnd.end());
    if (bnd.size() == 2) return {};
    std::size_t rm = 0; double bestd = 1e300;
    for (std::size_t i = 0; i + 1 < bnd.size(); ++i) {
        const double c = (bnd[i] + bnd[i + 1]) * 0.5, d = std::abs(sp - c);
        if (d < bestd) { bestd = d; rm = i; }
    }
    // Sobra no máx. 2 sub-polilinhas contínuas ao redor do vão (sem fragmentar
    // nos demais cortes); buildPiece atravessa os vértices intermediários.
    const double loCut = bnd[rm], hiCut = bnd[rm + 1];
    if (loCut > 1e-7)
        if (auto piece = buildPiece(0.0, loCut)) out.push_back(std::move(piece));
    if (hiCut < double(segs) - 1e-7)
        if (auto piece = buildPiece(hiCut, double(segs))) out.push_back(std::move(piece));
    return out;
}

} // namespace

std::vector<std::unique_ptr<Entity>> splitEntityAt(const Entity& target,
                                                   const std::vector<Point3>& cutPoints,
                                                   const Point3& pick) {
    if (cutPoints.empty()) return {};
    if (const auto* ln = dynamic_cast<const Line*>(&target))   return trimLine(*ln, cutPoints, pick);
    if (const auto* ci = dynamic_cast<const Circle*>(&target)) return trimCircle(*ci, cutPoints, pick);
    if (const auto* ar = dynamic_cast<const Arc*>(&target))    return trimArc(*ar, cutPoints, pick);
    if (const auto* pl = dynamic_cast<const Polyline*>(&target)) return trimPolyline(*pl, cutPoints, pick);
    return {};   // demais: ainda não suportado
}

std::vector<std::unique_ptr<Entity>> trimEntity(const Entity& target, const Entity& cutter,
                                                const Point3& pick) {
    return splitEntityAt(target, intersectEntities(target, cutter), pick);
}

} // namespace cad
