// src/core/math/Vec.hpp
#pragma once
#include <cmath>

namespace cad {

// Vetor/ponto 3D em precisão dupla (double) — base de TODA geometria.
struct Vec3 {
    double x{0.0}, y{0.0}, z{0.0};

    Vec3() = default;
    Vec3(double x_, double y_, double z_ = 0.0) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s)      const { return {x * s, y * s, z * s}; }

    double dot(const Vec3& o)   const { return x * o.x + y * o.y + z * o.z; }
    Vec3   cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    double lengthSq() const { return x * x + y * y + z * z; }
    double length()   const { return std::sqrt(lengthSq()); }
    Vec3   normalized() const {
        const double len = length();
        return len > 0.0 ? Vec3{x / len, y / len, z / len} : Vec3{};
    }
};

using Point3 = Vec3;

inline Vec3 operator*(double s, const Vec3& v) { return v * s; }

} // namespace cad
