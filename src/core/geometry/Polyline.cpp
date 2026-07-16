// src/core/geometry/Polyline.cpp
#include "core/geometry/Polyline.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/geometry/Bulge.hpp"
#include "core/math/Segment.hpp"
#include <cmath>
#include <limits>

namespace cad {

std::vector<Point3> Polyline::sampledPoints() const {
    std::vector<Point3> out;
    const std::size_t n = m_verts.size();
    if (n < 2) return m_verts;
    out.push_back(m_verts.front());
    const std::size_t segs = m_closed ? n : n - 1;
    for (std::size_t i = 0; i < segs; ++i) {
        const Point3& a = m_verts[i];
        const Point3& b = m_verts[(i + 1) % n];
        const double bu = bulgeAt(i);
        if (std::fabs(bu) < 1e-12) {
            out.push_back(b);
        } else {
            const std::vector<Point3> arc = bulgeSamples(a, b, bu);
            for (std::size_t k = 1; k < arc.size(); ++k) out.push_back(arc[k]);
        }
    }
    return out;
}

AABB Polyline::boundingBox() const {
    AABB b;
    for (const Point3& v : sampledPoints()) b.expand(v);  // inclui a curvatura dos arcos
    return b;
}

void Polyline::emitTo(RenderBatch& batch) const {
    const std::vector<Point3> pts = sampledPoints();
    // Largura > 0: faixa preenchida (triângulos) com junções em miter simples.
    if (m_width > 1e-9 && pts.size() >= 2) {
        const double hw = m_width * 0.5;
        const std::size_t n = pts.size();
        auto segNormal = [](const Point3& a, const Point3& b) -> Point3 {
            const double dx = b.x - a.x, dy = b.y - a.y, l = std::hypot(dx, dy);
            return l < 1e-12 ? Point3{0, 0, 0} : Point3{-dy / l, dx / l, 0};  // perp. à esquerda
        };
        std::vector<Point3> L(n), R(n);
        for (std::size_t i = 0; i < n; ++i) {
            Point3 nrm;
            if (i == 0)            nrm = segNormal(pts[0], pts[1]);
            else if (i == n - 1)   nrm = segNormal(pts[n - 2], pts[n - 1]);
            else {                                                   // normal média (miter)
                const Point3 a = segNormal(pts[i - 1], pts[i]);
                const Point3 b = segNormal(pts[i], pts[i + 1]);
                const double sx = a.x + b.x, sy = a.y + b.y, sl = std::hypot(sx, sy);
                nrm = sl < 1e-9 ? a : Point3{sx / sl, sy / sl, 0};
            }
            L[i] = Point3{pts[i].x + nrm.x * hw, pts[i].y + nrm.y * hw, 0};
            R[i] = Point3{pts[i].x - nrm.x * hw, pts[i].y - nrm.y * hw, 0};
        }
        for (std::size_t i = 1; i < n; ++i) {
            batch.addTriangle(L[i - 1], R[i - 1], R[i]);
            batch.addTriangle(L[i - 1], R[i], L[i]);
        }
        return;
    }
    for (std::size_t i = 1; i < pts.size(); ++i)
        batch.addSegment(pts[i - 1], pts[i]);
}

HitResult Polyline::hitTest(const Ray& pickRay, double tol) const {
    HitResult r;
    const std::vector<Point3> pts = sampledPoints();
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

void Polyline::transform(const Matrix4& m) {
    for (Point3& v : m_verts) v = m.transformPoint(v);
    // Reflexão (determinante 2D negativo) inverte o lado dos arcos -> nega o bulge.
    const double det = m.m[0] * m.m[5] - m.m[1] * m.m[4];
    if (det < 0.0)
        for (double& b : m_bulges) b = -b;
}

void Polyline::appendSnapPoints(std::vector<SnapPoint>& out) const {
    for (const Point3& v : m_verts) out.push_back({v, SnapType::Endpoint});
    if (m_verts.size() < 2) return;
    const std::size_t segCount = m_closed ? m_verts.size() : m_verts.size() - 1;
    for (std::size_t i = 0; i < segCount; ++i) {
        const Point3& a = m_verts[i];
        const Point3& b = m_verts[(i + 1) % m_verts.size()];
        const double bu = bulgeAt(i);
        if (std::fabs(bu) < 1e-12) {
            out.push_back({Point3{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, 0.0}, SnapType::Midpoint});
        } else {
            const BulgeArc arc = bulgeToArc(a, b, bu);          // meio do arco + centro
            const double am = arc.startAng + arc.sweep * 0.5;
            out.push_back({Point3{arc.center.x + arc.radius * std::cos(am),
                                  arc.center.y + arc.radius * std::sin(am), 0.0}, SnapType::Midpoint});
            out.push_back({arc.center, SnapType::Center});
        }
    }
    // Centroide da área (OSNAP Geometric Center) — só p/ contorno FECHADO.
    if (m_closed && m_verts.size() >= 3) {
        double a2 = 0.0, cx = 0.0, cy = 0.0;
        for (std::size_t i = 0; i < m_verts.size(); ++i) {
            const Point3& p = m_verts[i];
            const Point3& q = m_verts[(i + 1) % m_verts.size()];
            const double cr = p.x * q.y - q.x * p.y;
            a2 += cr;
            cx += (p.x + q.x) * cr;
            cy += (p.y + q.y) * cr;
        }
        if (std::fabs(a2) > 1e-12)
            out.push_back({Point3{cx / (3.0 * a2), cy / (3.0 * a2), 0.0},
                           SnapType::GeomCenter});
    }
}

std::unique_ptr<Entity> Polyline::clone() const {
    return std::make_unique<Polyline>(*this);
}

} // namespace cad
