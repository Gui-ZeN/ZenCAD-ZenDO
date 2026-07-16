// src/core/geometry/Bulge.hpp
#pragma once
#include "core/math/Vec.hpp"
#include <cmath>
#include <vector>

namespace cad {

// "Bulge" (abaulamento) — o fator do DXF LWPOLYLINE que transforma um trecho de
// polilinha (p0->p1) num ARCO: bulge = tan(ângulo_incluído / 4). bulge > 0 =
// arco à esquerda (CCW); 0 = segmento reto. Aqui só matemática pura (sem estado).

struct BulgeArc {
    Point3 center{};
    double radius{0.0};
    double startAng{0.0};   // ângulo (rad) do centro até p0
    double sweep{0.0};      // varredura com sinal (CCW positivo) = 4*atan(bulge)
    bool   ok{false};
};

// Converte (p0, p1, bulge) nos parâmetros do arco. ok=false se bulge ~0.
inline BulgeArc bulgeToArc(const Point3& p0, const Point3& p1, double bulge) {
    BulgeArc r;
    if (std::fabs(bulge) < 1e-12) return r;
    const double cot = (1.0 / bulge - bulge) * 0.5;          // = (1-b^2)/(2b) = cot(θ/2)
    const double cx = ((p0.x + p1.x) - (p1.y - p0.y) * cot) * 0.5;
    const double cy = ((p0.y + p1.y) + (p1.x - p0.x) * cot) * 0.5;
    r.center  = Point3{cx, cy, 0.0};
    r.radius  = std::hypot(p0.x - cx, p0.y - cy);
    r.startAng = std::atan2(p0.y - cy, p0.x - cx);
    r.sweep   = 4.0 * std::atan(bulge);
    r.ok      = true;
    return r;
}

// Tessela o arco de bulge em pontos (inclui p0 e p1). Se bulge ~0, devolve {p0,p1}.
inline std::vector<Point3> bulgeSamples(const Point3& p0, const Point3& p1, double bulge) {
    const BulgeArc a = bulgeToArc(p0, p1, bulge);
    if (!a.ok) return {p0, p1};
    const int n = std::max(4, static_cast<int>(std::ceil(std::fabs(a.sweep) / 0.09)));
    std::vector<Point3> out;
    out.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const double ang = a.startAng + a.sweep * (static_cast<double>(i) / n);
        out.emplace_back(a.center.x + a.radius * std::cos(ang),
                         a.center.y + a.radius * std::sin(ang), 0.0);
    }
    return out;
}

} // namespace cad
