// src/core/geometry/Circle.hpp
#pragma once
#include "core/geometry/Entity.hpp"

namespace cad {

// Círculo no plano de trabalho XY (normal +Z implícita). Círculos fora do plano
// (3D) exigirão um OCS/normal — fora do escopo 2D atual.
class Circle final : public Entity {
public:
    Circle() = default;
    Circle(const Point3& center, double radius)
        : m_center(center), m_radius(radius) {}

    const Point3& center() const { return m_center; }
    double        radius() const { return m_radius; }

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "CIRCLE"; }

private:
    Point3 m_center{};
    double m_radius{0.0};
};

} // namespace cad
