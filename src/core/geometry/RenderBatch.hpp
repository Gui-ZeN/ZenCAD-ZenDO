// src/core/geometry/RenderBatch.hpp
#pragma once
#include "core/math/Vec.hpp"
#include <vector>

namespace cad {

// Destino headless de emissão geométrica. O backend OpenGL consome este buffer
// e o envia à GPU; o kernel apenas acumula segmentos — SEM dependência gráfica.
// Cada par consecutivo em lineVertices forma um segmento (GL_LINES).
struct RenderBatch {
    std::vector<Point3> lineVertices;   // pares -> GL_LINES
    std::vector<Point3> fillVertices;   // trincas -> GL_TRIANGLES (áreas preenchidas)

    void addSegment(const Point3& a, const Point3& b) {
        lineVertices.push_back(a);
        lineVertices.push_back(b);
    }
    void addTriangle(const Point3& a, const Point3& b, const Point3& c) {
        fillVertices.push_back(a);
        fillVertices.push_back(b);
        fillVertices.push_back(c);
    }
    std::size_t segmentCount() const { return lineVertices.size() / 2; }
    void clear() { lineVertices.clear(); fillVertices.clear(); }
};

} // namespace cad
