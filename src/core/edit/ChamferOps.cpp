// src/core/edit/ChamferOps.cpp
#include "core/edit/ChamferOps.hpp"
#include "core/edit/GeometryOps.hpp"   // intersectInfiniteLines

#include <cmath>

namespace cad {
namespace {

// Tolerância geométrica padrão para comparações no plano XY (igual a GeometryOps).
constexpr double kEps = 1e-9;

// Comprimento no plano XY.
inline double length2D(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

} // namespace

// Chanfro reto entre duas linhas (ver header).
ChamferResult chamferLines(const Line& l1, const Line& l2, double d1, double d2) {
    ChamferResult res;
    if (d1 < 0.0 || d2 < 0.0) return res;  // recuos inválidos

    const Point3& a0 = l1.start();
    const Point3& a1 = l1.end();
    const Point3& b0 = l2.start();
    const Point3& b1 = l2.end();

    // Vértice: interseção das retas infinitas. Paralelas/degeneradas -> nullopt.
    const auto corner = intersectInfiniteLines(a0, a1, b0, b1);
    if (!corner) return res;
    const Point3 V = *corner;

    // Direção unitária saindo de V em direção ao extremo MAIS DISTANTE da linha
    // (o lado que permanece após o aparo). Devolve também esse extremo distante
    // e a distância de V até ele (comprimento disponível para o recuo).
    auto resolveLeg = [&](const Point3& p, const Point3& q,
                          Point3& farOut, double& availOut) -> Vec3 {
        const double dp = (p.x - V.x) * (p.x - V.x) + (p.y - V.y) * (p.y - V.y);
        const double dq = (q.x - V.x) * (q.x - V.x) + (q.y - V.y) * (q.y - V.y);
        farOut = (dp >= dq) ? p : q;
        Vec3 d{farOut.x - V.x, farOut.y - V.y, 0.0};
        availOut = length2D(d);
        return (availOut > kEps) ? Vec3{d.x / availOut, d.y / availOut, 0.0} : Vec3{};
    };

    Point3 far1, far2;
    double avail1 = 0.0, avail2 = 0.0;
    const Vec3 u1 = resolveLeg(a0, a1, far1, avail1);
    const Vec3 u2 = resolveLeg(b0, b1, far2, avail2);
    if (length2D(u1) < kEps || length2D(u2) < kEps) return res;  // linha degenerada

    // Cada recuo precisa caber no comprimento disponível a partir do vértice.
    if (d1 > avail1 + kEps || d2 > avail2 + kEps) return res;

    // Pontos de tangência: V recuado por d1/d2 na direção do extremo distante.
    const Point3 t1{V.x + u1.x * d1, V.y + u1.y * d1, 0.0};
    const Point3 t2{V.x + u2.x * d2, V.y + u2.y * d2, 0.0};

    // Linhas aparadas: do extremo distante (preservado) até o ponto de tangência.
    res.line1 = Line{far1, t1};
    res.line2 = Line{far2, t2};
    res.bevel = Line{t1, t2};
    res.ok    = true;
    return res;
}

} // namespace cad
