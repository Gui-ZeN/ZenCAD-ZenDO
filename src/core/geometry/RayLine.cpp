// src/core/geometry/RayLine.cpp
#include "core/geometry/RayLine.hpp"
#include "core/geometry/RenderBatch.hpp"
#include <cmath>

namespace cad {

namespace {
// Comprimento usado para "materializar" o raio semi-infinito como um segmento
// gigante. O viewport recorta pela câmera.
constexpr double kSpan = 1e5;
}

AABB RayLine::boundingBox() const {
    // Caixa PEQUENA em torno do ponto-base (±0.5): impede que o raio infinito
    // domine o "zoom extends" e o índice espacial.
    return AABB::fromCenterHalf(m_base, Vec3{0.5, 0.5, 0.5});
}

void RayLine::emitTo(RenderBatch& batch) const {
    // Um único segmento muito longo em UM sentido: da base até base + span*dir.
    const Point3 b = m_base + m_dir * kSpan;
    batch.addSegment(m_base, b);
}

HitResult RayLine::hitTest(const Ray& pickRay, double tol) const {
    // Projeção do cursor sobre a direção, COM clamp em t >= 0 (semi-infinito):
    // antes da base, o ponto mais próximo é a própria base.
    const Point3 p = pickRay.origin;
    double t = (p.x - m_base.x) * m_dir.x + (p.y - m_base.y) * m_dir.y;
    if (t < 0.0) t = 0.0;

    const Point3 closest{m_base.x + m_dir.x * t,
                         m_base.y + m_dir.y * t,
                         m_base.z + m_dir.z * t};
    const double dx = p.x - closest.x, dy = p.y - closest.y;
    const double dist = std::sqrt(dx * dx + dy * dy);

    HitResult r;
    if (dist <= tol) { r.hit = true; r.distance = dist; r.point = closest; }
    return r;
}

void RayLine::transform(const Matrix4& m) {
    m_base = m.transformPoint(m_base);
    m_dir  = m.transformVector(m_dir).normalized();
}

void RayLine::appendSnapPoints(std::vector<SnapPoint>& out) const {
    // Só o ponto-base (Endpoint). Interseções são resolvidas pelo SnapEngine.
    out.push_back({m_base, SnapType::Endpoint});
}

std::unique_ptr<Entity> RayLine::clone() const {
    return std::make_unique<RayLine>(*this);
}

} // namespace cad
