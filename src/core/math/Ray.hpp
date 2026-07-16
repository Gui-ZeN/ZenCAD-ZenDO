// src/core/math/Ray.hpp
#pragma once
#include "core/math/Vec.hpp"
#include "core/math/AABB.hpp"

namespace cad {

// Raio de seleção. Em 3D usa origin + direction; em 2D a direção aponta para
// dentro da tela e o picking usa `origin` como ponto no plano de trabalho XY.
struct Ray {
    Point3 origin{};
    Vec3   direction{0.0, 0.0, -1.0};
};

inline AABB AABB::fromRay(const Ray& r, double tol) {
    return AABB::fromCenterHalf(r.origin, Vec3{tol, tol, tol});
}

} // namespace cad
