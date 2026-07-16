// src/core/geometry/PointEntity.cpp
#include "core/geometry/PointEntity.hpp"
#include "core/geometry/RenderBatch.hpp"
#include <cmath>

namespace cad {
namespace {

// Meia-largura do marcador "+" (em unidades de mundo). O traço completo de cada
// segmento tem o dobro deste valor.
constexpr double kHalfMark = 2.0;

// Folga da caixa envolvente ao redor do ponto.
constexpr double kBoxHalf = 0.5;

} // namespace

AABB PointEntity::boundingBox() const {
    // Caixa minúscula centrada no ponto (±0.5 em cada eixo do plano).
    return AABB::fromCenterHalf(m_pos, Vec3{kBoxHalf, kBoxHalf, 0.0});
}

void PointEntity::emitTo(RenderBatch& batch) const {
    // Desenha um "+" : um segmento horizontal e um vertical cruzados em m_pos.
    const Point3 left {m_pos.x - kHalfMark, m_pos.y,             m_pos.z};
    const Point3 right{m_pos.x + kHalfMark, m_pos.y,             m_pos.z};
    const Point3 down {m_pos.x,             m_pos.y - kHalfMark, m_pos.z};
    const Point3 up   {m_pos.x,             m_pos.y + kHalfMark, m_pos.z};
    batch.addSegment(left, right);
    batch.addSegment(down, up);
}

HitResult PointEntity::hitTest(const Ray& pickRay, double tol) const {
    // Acerta se o cursor (origin do raio) está a no máximo 'tol' do ponto (2D).
    const Point3 p  = pickRay.origin;
    const double dx = p.x - m_pos.x, dy = p.y - m_pos.y;
    const double d  = std::sqrt(dx * dx + dy * dy);

    HitResult r;
    if (d <= tol) { r.hit = true; r.distance = d; r.point = m_pos; }
    return r;
}

void PointEntity::transform(const Matrix4& m) {
    m_pos = m.transformPoint(m_pos);
}

void PointEntity::appendSnapPoints(std::vector<SnapPoint>& out) const {
    // Nó de captura sobre o próprio ponto (OSNAP Node, como no AutoCAD).
    out.push_back({m_pos, SnapType::Node});
}

std::unique_ptr<Entity> PointEntity::clone() const {
    return std::make_unique<PointEntity>(*this);
}

} // namespace cad
