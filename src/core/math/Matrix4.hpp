// src/core/math/Matrix4.hpp
#pragma once
#include "core/math/Vec.hpp"
#include <array>
#include <cmath>

namespace cad {

// Matriz 4x4 column-major (m[col*4 + row]) em double.
// Compatível com o layout esperado por OpenGL (glUniformMatrix4..., transpose=GL_FALSE).
struct Matrix4 {
    std::array<double, 16> m{};

    static Matrix4 identity() {
        Matrix4 r;
        r.m[0] = 1; r.m[5] = 1; r.m[10] = 1; r.m[15] = 1;
        return r;
    }
    static Matrix4 translation(const Vec3& t) {
        Matrix4 r = identity();
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    static Matrix4 scale(const Vec3& s) {
        Matrix4 r = identity();
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }
    static Matrix4 rotationZ(double rad) {
        Matrix4 r = identity();
        const double c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }

    // Aplica como ponto (w = 1): translação afeta o resultado.
    Point3 transformPoint(const Point3& p) const {
        return {
            m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]
        };
    }
    // Aplica como direção (w = 0): translação ignorada.
    Vec3 transformVector(const Vec3& v) const {
        return {
            m[0] * v.x + m[4] * v.y + m[8]  * v.z,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z,
            m[2] * v.x + m[6] * v.y + m[10] * v.z
        };
    }

    Matrix4 operator*(const Matrix4& o) const {
        Matrix4 r;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row) {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k) sum += m[k * 4 + row] * o.m[col * 4 + k];
                r.m[col * 4 + row] = sum;
            }
        return r;
    }
};

} // namespace cad
