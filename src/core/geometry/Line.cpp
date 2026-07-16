// src/core/geometry/Line.cpp
#include "core/geometry/Line.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/math/Segment.hpp"
#include <cmath>

namespace cad {

AABB Line::boundingBox() const {
    AABB b;
    b.expand(m_p0);
    b.expand(m_p1);
    return b;
}

void Line::emitTo(RenderBatch& batch) const {
    batch.addSegment(m_p0, m_p1);
}

HitResult Line::hitTest(const Ray& pickRay, double tol) const {
    // Distância (no plano XY) do ponto-cursor ao segmento p0->p1.
    const Point3 p       = pickRay.origin;
    const Point3 closest = closestPointOnSegment2D(p, m_p0, m_p1);
    const double dx = p.x - closest.x, dy = p.y - closest.y;
    const double dist = std::sqrt(dx * dx + dy * dy);

    HitResult r;
    if (dist <= tol) { r.hit = true; r.distance = dist; r.point = closest; }
    return r;
}

void Line::transform(const Matrix4& m) {
    m_p0 = m.transformPoint(m_p0);
    m_p1 = m.transformPoint(m_p1);
}

void Line::appendSnapPoints(std::vector<SnapPoint>& out) const {
    out.push_back({m_p0, SnapType::Endpoint});
    out.push_back({m_p1, SnapType::Endpoint});
    out.push_back({Point3{(m_p0.x + m_p1.x) * 0.5,
                          (m_p0.y + m_p1.y) * 0.5,
                          (m_p0.z + m_p1.z) * 0.5}, SnapType::Midpoint});
}

std::unique_ptr<Entity> Line::clone() const {
    return std::make_unique<Line>(*this);
}

} // namespace cad
