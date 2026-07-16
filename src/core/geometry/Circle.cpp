// src/core/geometry/Circle.cpp
#include "core/geometry/Circle.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/geometry/Tessellation.hpp"
#include "core/math/Constants.hpp"
#include <cmath>

namespace cad {

AABB Circle::boundingBox() const {
    AABB b;
    b.expand(Point3{m_center.x - m_radius, m_center.y - m_radius, m_center.z});
    b.expand(Point3{m_center.x + m_radius, m_center.y + m_radius, m_center.z});
    return b;
}

void Circle::emitTo(RenderBatch& batch) const {
    const int    n    = segmentsForArc(m_radius, kTwoPi);
    const double step = kTwoPi / n;
    Point3 prev{m_center.x + m_radius, m_center.y, m_center.z};
    for (int i = 1; i <= n; ++i) {
        const double a = step * i;
        Point3 cur{m_center.x + m_radius * std::cos(a),
                   m_center.y + m_radius * std::sin(a),
                   m_center.z};
        batch.addSegment(prev, cur);
        prev = cur;
    }
}

HitResult Circle::hitTest(const Ray& pickRay, double tol) const {
    const Point3 p = pickRay.origin;
    const double dx = p.x - m_center.x, dy = p.y - m_center.y;
    const double d  = std::sqrt(dx * dx + dy * dy);

    HitResult r;
    const double dist = std::abs(d - m_radius);
    if (dist <= tol) {
        r.hit = true;
        r.distance = dist;
        const double a = (d > 0.0) ? std::atan2(dy, dx) : 0.0;
        r.point = Point3{m_center.x + m_radius * std::cos(a),
                         m_center.y + m_radius * std::sin(a),
                         m_center.z};
    }
    return r;
}

void Circle::transform(const Matrix4& m) {
    // Suporta translação + escala uniforme + rotação. Escala não-uniforme
    // viraria uma elipse (use Ellipse) — aqui o raio segue a escala do eixo X.
    m_center = m.transformPoint(m_center);
    m_radius = m.transformVector(Vec3{m_radius, 0.0, 0.0}).length();
}

void Circle::appendSnapPoints(std::vector<SnapPoint>& out) const {
    out.push_back({m_center, SnapType::Center});
    out.push_back({Point3{m_center.x + m_radius, m_center.y, m_center.z}, SnapType::Quadrant});
    out.push_back({Point3{m_center.x - m_radius, m_center.y, m_center.z}, SnapType::Quadrant});
    out.push_back({Point3{m_center.x, m_center.y + m_radius, m_center.z}, SnapType::Quadrant});
    out.push_back({Point3{m_center.x, m_center.y - m_radius, m_center.z}, SnapType::Quadrant});
}

std::unique_ptr<Entity> Circle::clone() const {
    return std::make_unique<Circle>(*this);
}

} // namespace cad
