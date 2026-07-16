// src/core/math/Segment.hpp
#pragma once
#include "core/math/Vec.hpp"
#include <cmath>

namespace cad {

// Ponto do segmento a-b mais próximo de p (projeção fixada ao intervalo), no
// plano XY. Fonte única usada por Line/Polyline e, futuramente, pelo SnapEngine.
inline Point3 closestPointOnSegment2D(const Point3& p, const Point3& a, const Point3& b) {
    const Vec3   d     = b - a;
    const double lenSq = d.x * d.x + d.y * d.y;
    double t = 0.0;
    if (lenSq > 0.0) {
        t = ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / lenSq;
        if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
    }
    return Point3{a.x + d.x * t, a.y + d.y * t, a.z + d.z * t};
}

inline double distancePointToSegment2D(const Point3& p, const Point3& a, const Point3& b) {
    const Point3 c  = closestPointOnSegment2D(p, a, b);
    const double dx = p.x - c.x, dy = p.y - c.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace cad
