// src/core/geometry/Line.hpp
#pragma once
#include "core/geometry/Entity.hpp"

namespace cad {

// Segmento de reta entre dois pontos 3D. Primitiva de referência: implementa
// todo o contrato de Entity e serve de molde para as demais (Circle, Arc...).
class Line final : public Entity {
public:
    Line() = default;
    Line(const Point3& a, const Point3& b) : m_p0(a), m_p1(b) {}

    const Point3& start() const { return m_p0; }
    const Point3& end()   const { return m_p1; }

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "LINE"; }

private:
    Point3 m_p0{};
    Point3 m_p1{};
};

} // namespace cad
