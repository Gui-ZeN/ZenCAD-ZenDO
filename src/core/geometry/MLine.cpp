// src/core/geometry/MLine.cpp
#include "core/geometry/MLine.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/math/Segment.hpp"
#include <cmath>
#include <limits>

namespace cad {

namespace {
// Normal unitária (no plano XY) ao segmento a->b, rotacionada +90° (à esquerda).
// Retorna {0,0,0} se o segmento for degenerado.
Vec3 segNormal(const Point3& a, const Point3& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0) return Vec3{};
    return Vec3{-dy / len, dx / len, 0.0};   // perpendicular à esquerda
}
} // namespace

void MLine::buildOffsets(std::vector<Point3>& left, std::vector<Point3>& right) const {
    left.clear();
    right.clear();
    const std::size_t n = m_verts.size();
    if (n < 2) return;

    const double h = m_width * 0.5;
    left.resize(n);
    right.resize(n);

    for (std::size_t i = 0; i < n; ++i) {
        // Normais dos segmentos que chegam (prev->i) e que saem (i->next).
        const bool hasPrev = m_closed || i > 0;
        const bool hasNext = m_closed || i + 1 < n;

        Vec3 nIn{}, nOut{};
        if (hasPrev) {
            const Point3& a = m_verts[(i + n - 1) % n];
            nIn = segNormal(a, m_verts[i]);
        }
        if (hasNext) {
            const Point3& b = m_verts[(i + 1) % n];
            nOut = segNormal(m_verts[i], b);
        }

        // Normal usada no vértice: média (miter simples) quando há dois segmentos;
        // nas pontas abertas, a normal do único segmento existente.
        Vec3 nv = nIn + nOut;
        if (nv.lengthSq() <= 0.0) nv = hasNext ? nOut : nIn;  // colinear oposto/degenerado
        nv = nv.normalized();

        const Point3& v = m_verts[i];
        left[i]  = Point3{v.x + nv.x * h, v.y + nv.y * h, v.z};
        right[i] = Point3{v.x - nv.x * h, v.y - nv.y * h, v.z};
    }
}

AABB MLine::boundingBox() const {
    AABB b;
    std::vector<Point3> left, right;
    buildOffsets(left, right);
    if (left.empty()) {
        // Sem bordas: apenas o(s) vértice(s) do eixo expandido(s) por w/2.
        const double h = m_width * 0.5;
        for (const Point3& v : m_verts) {
            b.expand(Point3{v.x - h, v.y - h, v.z});
            b.expand(Point3{v.x + h, v.y + h, v.z});
        }
        return b;
    }
    for (const Point3& p : left)  b.expand(p);
    for (const Point3& p : right) b.expand(p);
    return b;
}

void MLine::emitTo(RenderBatch& batch) const {
    std::vector<Point3> left, right;
    buildOffsets(left, right);
    if (left.empty()) return;

    const std::size_t n = left.size();
    const std::size_t segs = m_closed ? n : n - 1;

    // Duas polilinhas paralelas (bordas).
    for (std::size_t i = 0; i < segs; ++i) {
        batch.addSegment(left[i],  left[(i + 1) % n]);
        batch.addSegment(right[i], right[(i + 1) % n]);
    }

    if (m_closed) return;
    // Tampas nas pontas (ligam as duas bordas) quando aberta.
    batch.addSegment(left.front(), right.front());
    batch.addSegment(left.back(),  right.back());
}

HitResult MLine::hitTest(const Ray& pickRay, double tol) const {
    HitResult r;
    std::vector<Point3> left, right;
    buildOffsets(left, right);
    if (left.empty()) return r;

    const std::size_t n = left.size();
    const std::size_t segs = m_closed ? n : n - 1;

    const Point3 p = pickRay.origin;
    double bestDist = std::numeric_limits<double>::max();
    Point3 bestPt;

    auto test = [&](const Point3& a, const Point3& b) {
        const Point3 c  = closestPointOnSegment2D(p, a, b);
        const double dx = p.x - c.x, dy = p.y - c.y;
        const double d  = std::sqrt(dx * dx + dy * dy);
        if (d < bestDist) { bestDist = d; bestPt = c; }
    };

    for (std::size_t i = 0; i < segs; ++i) {
        test(left[i],  left[(i + 1) % n]);
        test(right[i], right[(i + 1) % n]);
    }

    if (bestDist <= tol) { r.hit = true; r.distance = bestDist; r.point = bestPt; }
    return r;
}

void MLine::transform(const Matrix4& m) {
    for (Point3& v : m_verts) v = m.transformPoint(v);
    // Simples: apenas o eixo é transformado; a largura é mantida.
}

void MLine::appendSnapPoints(std::vector<SnapPoint>& out) const {
    // Vértices do EIXO como Endpoint.
    for (const Point3& v : m_verts) out.push_back({v, SnapType::Endpoint});
    if (m_verts.size() < 2) return;

    // Meios das arestas do EIXO como Midpoint.
    const std::size_t n = m_verts.size();
    const std::size_t segs = m_closed ? n : n - 1;
    for (std::size_t i = 0; i < segs; ++i) {
        const Point3& a = m_verts[i];
        const Point3& b = m_verts[(i + 1) % n];
        out.push_back({Point3{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5},
                       SnapType::Midpoint});
    }
}

std::unique_ptr<Entity> MLine::clone() const {
    return std::make_unique<MLine>(*this);
}

} // namespace cad
