// src/core/geometry/PointEntity.hpp
#pragma once
#include "core/geometry/Entity.hpp"

namespace cad {

// Entidade ponto: marca uma única posição no plano de trabalho XY. Desenhada
// como um "+" (dois segmentos curtos cruzados) para ser visível na tela, já que
// um ponto matemático não teria área. Serve de nó de captura (OSNAP Node).
class PointEntity final : public Entity {
public:
    PointEntity() = default;
    explicit PointEntity(Point3 pos) : m_pos(pos) {}

    const Point3& position() const { return m_pos; }

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "POINT"; }

private:
    Point3 m_pos{};
};

} // namespace cad
