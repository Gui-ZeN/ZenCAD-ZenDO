// src/core/geometry/RayLine.hpp
#pragma once
#include "core/geometry/Entity.hpp"

namespace cad {

// RayLine — RAIO (construction ray), semi-infinito. Igual à XLine, porém
// estende-se em UM único sentido: parte do ponto-base e segue por m_dir sem
// limite. Precisão dupla, geometria no plano XY.
class RayLine final : public Entity {
public:
    RayLine() = default;

    // Constrói a partir de base + direção (a direção é normalizada).
    RayLine(const Point3& base, const Vec3& dir)
        : m_base(base), m_dir(dir.normalized()) {}

    // Fábrica: raio com origem em a, apontando para b.
    static RayLine fromTwoPoints(const Point3& a, const Point3& b) {
        return RayLine(a, b - a);
    }

    const Point3& base() const { return m_base; }
    const Vec3&   dir()  const { return m_dir; }

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "RAY"; }

private:
    Point3 m_base{};
    Vec3   m_dir{1.0, 0.0, 0.0};
};

} // namespace cad
