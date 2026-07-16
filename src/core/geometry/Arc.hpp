// src/core/geometry/Arc.hpp
#pragma once
#include "core/geometry/Entity.hpp"

namespace cad {

// Arco circular no plano XY, varrendo de startAngle a endAngle no sentido
// anti-horário (CCW). Ângulos em radianos, medidos a partir de +X.
class Arc final : public Entity {
public:
    Arc() = default;
    Arc(const Point3& center, double radius, double startAngle, double endAngle)
        : m_center(center), m_radius(radius), m_start(startAngle), m_end(endAngle) {}

    const Point3& center()     const { return m_center; }
    double        radius()     const { return m_radius; }
    double        startAngle() const { return m_start; }
    double        endAngle()   const { return m_end; }

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "ARC"; }

private:
    Point3 m_center{};
    double m_radius{0.0};
    double m_start{0.0};
    double m_end{0.0};
};

} // namespace cad
