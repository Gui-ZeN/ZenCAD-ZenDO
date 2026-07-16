// src/core/geometry/Arc.cpp
#include "core/geometry/Arc.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/geometry/Tessellation.hpp"
#include "core/math/Constants.hpp"
#include <cmath>

namespace cad {
namespace {

// Normaliza um ângulo para [0, 2pi).
double normPos(double a) {
    a = std::fmod(a, kTwoPi);
    if (a < 0.0) a += kTwoPi;
    return a;
}
double sweepOf(double start, double end) { return normPos(end - start); }
Point3 pointAt(const Point3& c, double r, double ang) {
    return Point3{c.x + r * std::cos(ang), c.y + r * std::sin(ang), c.z};
}

} // namespace

AABB Arc::boundingBox() const {
    const double sweep = sweepOf(m_start, m_end);
    AABB b;
    b.expand(pointAt(m_center, m_radius, m_start));
    b.expand(pointAt(m_center, m_radius, m_end));
    // Inclui os extremos de eixo (0, pi/2, pi, 3pi/2) que caem dentro da abertura.
    for (int k = 0; k < 4; ++k) {
        const double axis = k * kHalfPi;
        if (normPos(axis - m_start) <= sweep)
            b.expand(pointAt(m_center, m_radius, axis));
    }
    return b;
}

void Arc::emitTo(RenderBatch& batch) const {
    const double sweep = sweepOf(m_start, m_end);
    const int    n     = segmentsForArc(m_radius, sweep);
    const double step  = sweep / n;
    Point3 prev = pointAt(m_center, m_radius, m_start);
    for (int i = 1; i <= n; ++i) {
        Point3 cur = pointAt(m_center, m_radius, m_start + step * i);
        batch.addSegment(prev, cur);
        prev = cur;
    }
}

HitResult Arc::hitTest(const Ray& pickRay, double tol) const {
    const Point3 p     = pickRay.origin;
    const double dx    = p.x - m_center.x, dy = p.y - m_center.y;
    const double d     = std::sqrt(dx * dx + dy * dy);
    const double sweep = sweepOf(m_start, m_end);
    const double a     = std::atan2(dy, dx);

    HitResult r;
    if (normPos(a - m_start) <= sweep) {
        // Dentro da abertura: distância radial até o arco.
        const double dist = std::abs(d - m_radius);
        if (dist <= tol) {
            r.hit = true;
            r.distance = dist;
            r.point = pointAt(m_center, m_radius, a);
        }
    } else {
        // Fora da abertura: extremidade mais próxima.
        const Point3 ps = pointAt(m_center, m_radius, m_start);
        const Point3 pe = pointAt(m_center, m_radius, m_end);
        const double ds = std::hypot(p.x - ps.x, p.y - ps.y);
        const double de = std::hypot(p.x - pe.x, p.y - pe.y);
        const double best = ds <= de ? ds : de;
        if (best <= tol) {
            r.hit = true;
            r.distance = best;
            r.point = ds <= de ? ps : pe;
        }
    }
    return r;
}

void Arc::transform(const Matrix4& m) {
    // Suporta translação + escala uniforme + rotação no plano. A rotação é
    // extraída do eixo X transformado e somada aos ângulos. Espelhamento/cisalha
    // mento não são tratados exatamente (precisariam inverter a orientação).
    m_center = m.transformPoint(m_center);
    const Vec3   xAxis = m.transformVector(Vec3{1.0, 0.0, 0.0});
    const double rot   = std::atan2(xAxis.y, xAxis.x);
    m_radius *= std::sqrt(xAxis.x * xAxis.x + xAxis.y * xAxis.y);
    m_start  += rot;
    m_end    += rot;
}

void Arc::appendSnapPoints(std::vector<SnapPoint>& out) const {
    const double sweep = sweepOf(m_start, m_end);
    out.push_back({m_center, SnapType::Center});
    out.push_back({pointAt(m_center, m_radius, m_start), SnapType::Endpoint});
    out.push_back({pointAt(m_center, m_radius, m_end),   SnapType::Endpoint});
    out.push_back({pointAt(m_center, m_radius, m_start + sweep * 0.5), SnapType::Midpoint});
}

std::unique_ptr<Entity> Arc::clone() const {
    return std::make_unique<Arc>(*this);
}

} // namespace cad
