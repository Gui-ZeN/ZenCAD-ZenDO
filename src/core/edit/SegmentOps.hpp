// src/core/edit/SegmentOps.hpp
#pragma once
#include <cmath>

#include "core/math/Vec.hpp"

namespace cad {

// Interseção de dois segmentos finitos AB e CD (2D). Se cruzam dentro de ambos,
// retorna true e o ponto em `out`. Segmentos paralelos/colineares -> false.
// Usado pelo aparo/extensão por FENCE (a linha-cerca cruza as entidades).
inline bool segmentIntersect(const Point3& a, const Point3& b,
                             const Point3& c, const Point3& d, Point3& out) {
    const double rx = b.x - a.x, ry = b.y - a.y;
    const double sx = d.x - c.x, sy = d.y - c.y;
    const double denom = rx * sy - ry * sx;
    if (std::fabs(denom) < 1e-12) return false;            // paralelos/colineares
    const double t = ((c.x - a.x) * sy - (c.y - a.y) * sx) / denom;
    const double u = ((c.x - a.x) * ry - (c.y - a.y) * rx) / denom;
    if (t < -1e-9 || t > 1.0 + 1e-9 || u < -1e-9 || u > 1.0 + 1e-9) return false;
    out = Point3{a.x + t * rx, a.y + t * ry, 0.0};
    return true;
}

} // namespace cad
