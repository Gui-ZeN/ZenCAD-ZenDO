// src/core/geometry/XLine.cpp
#include "core/geometry/XLine.hpp"
#include "core/geometry/RenderBatch.hpp"
#include <cmath>

namespace cad {

namespace {
// Meio-comprimento usado para "materializar" a linha infinita como um segmento
// gigante. O viewport recorta pela câmera, então este valor só precisa ser
// grande o bastante para cobrir qualquer zoom razoável.
constexpr double kHalfSpan = 1e5;
}

AABB XLine::boundingBox() const {
    // Caixa PEQUENA em torno do ponto-base (±0.5): impede que a linha infinita
    // domine o "zoom extends" e o índice espacial (Quadtree/Octree).
    return AABB::fromCenterHalf(m_base, Vec3{0.5, 0.5, 0.5});
}

void XLine::emitTo(RenderBatch& batch) const {
    // Um único segmento muito longo nos DOIS sentidos a partir da base.
    const Point3 a = m_base - m_dir * kHalfSpan;
    const Point3 b = m_base + m_dir * kHalfSpan;
    batch.addSegment(a, b);
}

HitResult XLine::hitTest(const Ray& pickRay, double tol) const {
    // Distância (no plano XY) do cursor à RETA infinita, via projeção do ponto
    // sobre a direção. Não há clamp: o parâmetro t pode ser qualquer real.
    const Point3 p = pickRay.origin;
    const double t =
        (p.x - m_base.x) * m_dir.x + (p.y - m_base.y) * m_dir.y;

    const Point3 closest{m_base.x + m_dir.x * t,
                         m_base.y + m_dir.y * t,
                         m_base.z + m_dir.z * t};
    const double dx = p.x - closest.x, dy = p.y - closest.y;
    const double dist = std::sqrt(dx * dx + dy * dy);

    HitResult r;
    if (dist <= tol) { r.hit = true; r.distance = dist; r.point = closest; }
    return r;
}

void XLine::transform(const Matrix4& m) {
    m_base = m.transformPoint(m_base);
    m_dir  = m.transformVector(m_dir).normalized();
}

void XLine::appendSnapPoints(std::vector<SnapPoint>& out) const {
    // Só o ponto-base (Endpoint). Interseções com outras entidades são
    // resolvidas pelo SnapEngine.
    out.push_back({m_base, SnapType::Endpoint});
}

std::unique_ptr<Entity> XLine::clone() const {
    return std::make_unique<XLine>(*this);
}

} // namespace cad
