// src/core/snap/Polar.hpp
#pragma once
#include <cmath>

#include "core/math/Vec.hpp"

namespace cad {

// Resultado do rastreamento polar: se ativo, `point` é o cursor projetado sobre
// o raio de ângulo `angleDeg` (múltiplo do incremento) a partir da referência.
struct PolarResult {
    bool   active{false};
    Point3 point{};
    double angleDeg{0.0};
};

// Rastreamento polar (à la AutoCAD): quando o cursor está a até `tolDeg` de um
// raio cujo ângulo é múltiplo de `incDeg` (a partir de `ref`), gruda nele —
// projetando o cursor sobre o raio (preserva a distância ao longo dele). Fora da
// tolerância, retorna inativo (cursor livre). `incDeg <= 0` desativa.
inline PolarResult polarSnap(const Point3& ref, double wx, double wy,
                             double incDeg, double tolDeg) {
    PolarResult r;
    if (incDeg <= 0.0) return r;
    const double dx = wx - ref.x, dy = wy - ref.y;
    if (std::hypot(dx, dy) < 1e-9) return r;

    constexpr double kRadToDeg = 57.29577951308232;   // 180/pi
    const double ang = std::atan2(dy, dx) * kRadToDeg;
    const double nearest = std::round(ang / incDeg) * incDeg;

    double delta = std::fmod(std::fabs(ang - nearest), 360.0);
    if (delta > 180.0) delta = 360.0 - delta;
    if (delta > tolDeg) return r;                      // fora do feixe: cursor livre

    const double a = nearest / kRadToDeg;              // rad
    const double c = std::cos(a), s = std::sin(a);
    const double dist = dx * c + dy * s;               // projeção sobre o raio
    r.active   = true;
    r.point    = Point3{ref.x + dist * c, ref.y + dist * s, 0.0};
    r.angleDeg = nearest;
    return r;
}

} // namespace cad
