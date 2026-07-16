// src/core/math/AABB.hpp
#pragma once
#include "core/math/Vec.hpp"
#include <algorithm>
#include <limits>

namespace cad {

struct Ray; // fwd — fromRay() é definido em Ray.hpp

// Caixa envolvente alinhada aos eixos. Alimenta Quadtree/Octree e o broad-phase
// de picking. Os testes de interseção/contém operam no plano XY (suficiente p/
// o índice 2D); a coordenada Z é preservada mas não restringe a seleção.
struct AABB {
    Point3 min{ std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max() };
    Point3 max{ std::numeric_limits<double>::lowest(),
                std::numeric_limits<double>::lowest(),
                std::numeric_limits<double>::lowest() };

    bool valid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    void expand(const Point3& p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }
    void expand(const AABB& b) {
        if (b.valid()) { expand(b.min); expand(b.max); }
    }

    bool intersects(const AABB& o) const {
        return !(o.min.x > max.x || o.max.x < min.x ||
                 o.min.y > max.y || o.max.y < min.y);
    }
    bool contains(const AABB& o) const {
        return o.min.x >= min.x && o.max.x <= max.x &&
               o.min.y >= min.y && o.max.y <= max.y;
    }
    bool contains(const Point3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }

    Point3 center() const {
        return { (min.x + max.x) * 0.5, (min.y + max.y) * 0.5, (min.z + max.z) * 0.5 };
    }

    static AABB fromCenterHalf(const Point3& c, const Vec3& half) {
        AABB b; b.min = c - half; b.max = c + half; return b;
    }
    // Caixa de sondagem ao redor da origem do raio (broad-phase de picking).
    static AABB fromRay(const Ray& r, double tol);
};

} // namespace cad
