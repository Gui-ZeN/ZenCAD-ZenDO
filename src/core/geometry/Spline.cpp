// src/core/geometry/Spline.cpp
#include "core/geometry/Spline.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/edit/SplineOps.hpp"
#include "core/math/Segment.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cad {
namespace {

// Avalia um vão Catmull-Rom (forma uniforme) entre p1 e p2, com p0 e p3 como
// pontos de apoio para as tangentes, no parâmetro t em [0, 1].
Point3 catmullRom(const Point3& p0, const Point3& p1,
                  const Point3& p2, const Point3& p3, double t) {
    const double t2 = t * t;
    const double t3 = t2 * t;
    // Base de Catmull-Rom (tau = 0.5):
    // q(t) = 0.5 * [ (2 p1)
    //             + (-p0 + p2) t
    //             + (2 p0 - 5 p1 + 4 p2 - p3) t^2
    //             + (-p0 + 3 p1 - 3 p2 + p3) t^3 ]
    const double b0 = 0.5 * (-t3 + 2.0 * t2 - t);
    const double b1 = 0.5 * (3.0 * t3 - 5.0 * t2 + 2.0);
    const double b2 = 0.5 * (-3.0 * t3 + 4.0 * t2 + t);
    const double b3 = 0.5 * (t3 - t2);
    return Point3{
        b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
        b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y,
        b0 * p0.z + b1 * p1.z + b2 * p2.z + b3 * p3.z};
}

} // namespace

std::vector<Point3> Spline::sample(int perSpan) const {
    // Modo CV: B-spline (a curva NÃO passa pelos pontos de controle).
    if (m_cv) return bsplinePoints(m_ctrl, 3, perSpan);
    // Com menos de 2 pontos não há vão a interpolar: devolve os próprios pontos.
    if (m_ctrl.size() < 2) return m_ctrl;
    if (perSpan < 1) perSpan = 1;

    const std::size_t n = m_ctrl.size();
    std::vector<Point3> out;
    out.reserve((n - 1) * static_cast<std::size_t>(perSpan) + 1);

    out.push_back(m_ctrl.front());
    for (std::size_t i = 0; i + 1 < n; ++i) {
        // Pontas duplicadas dão as tangentes das extremidades.
        const Point3& p0 = (i == 0)     ? m_ctrl[i]     : m_ctrl[i - 1];
        const Point3& p1 = m_ctrl[i];
        const Point3& p2 = m_ctrl[i + 1];
        const Point3& p3 = (i + 2 < n)  ? m_ctrl[i + 2] : m_ctrl[i + 1];
        for (int s = 1; s <= perSpan; ++s) {
            const double t = static_cast<double>(s) / perSpan;
            out.push_back(catmullRom(p0, p1, p2, p3, t));
        }
    }
    return out;
}

AABB Spline::boundingBox() const {
    // Sobre os pontos de controle: como a curva passa por eles e a casca convexa
    // os contém de forma aproximada, basta e é barato para o índice espacial.
    AABB b;
    for (const Point3& p : m_ctrl) b.expand(p);
    return b;
}

void Spline::emitTo(RenderBatch& batch) const {
    const std::vector<Point3> pts = sample();
    if (pts.size() < 2) return;
    for (std::size_t i = 1; i < pts.size(); ++i)
        batch.addSegment(pts[i - 1], pts[i]);
}

HitResult Spline::hitTest(const Ray& pickRay, double tol) const {
    HitResult r;
    const std::vector<Point3> pts = sample();
    if (pts.size() < 2) return r;

    const Point3 p = pickRay.origin;
    double bestDist = std::numeric_limits<double>::max();
    Point3 bestPt;

    for (std::size_t i = 1; i < pts.size(); ++i) {
        const Point3 c  = closestPointOnSegment2D(p, pts[i - 1], pts[i]);
        const double dx = p.x - c.x, dy = p.y - c.y;
        const double d  = std::sqrt(dx * dx + dy * dy);
        if (d < bestDist) { bestDist = d; bestPt = c; }
    }

    if (bestDist <= tol) { r.hit = true; r.distance = bestDist; r.point = bestPt; }
    return r;
}

void Spline::transform(const Matrix4& m) {
    for (Point3& p : m_ctrl) p = m.transformPoint(p);
}

void Spline::appendSnapPoints(std::vector<SnapPoint>& out) const {
    if (m_ctrl.empty()) return;
    out.push_back({m_ctrl.front(), SnapType::Endpoint});
    if (m_ctrl.size() > 1)
        out.push_back({m_ctrl.back(), SnapType::Endpoint});
}

std::unique_ptr<Entity> Spline::clone() const {
    return std::make_unique<Spline>(*this);
}

} // namespace cad
