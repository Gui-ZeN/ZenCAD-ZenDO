// src/core/geometry/XLine.hpp
#pragma once
#include "core/geometry/Entity.hpp"

namespace cad {

// XLine — LINHA DE CONSTRUÇÃO infinita (construction line / "xline" do AutoCAD).
// Definida por um ponto-base e uma direção unitária; estende-se sem limites nos
// DOIS sentidos. É usada como guia/referência: o recorte visível fica a cargo da
// câmera/viewport. Precisão dupla, geometria no plano XY.
class XLine final : public Entity {
public:
    XLine() = default;

    // Constrói a partir de base + direção (a direção é normalizada).
    XLine(const Point3& base, const Vec3& dir)
        : m_base(base), m_dir(dir.normalized()) {}

    // Fábrica: linha infinita que passa por dois pontos a e b.
    static XLine fromTwoPoints(const Point3& a, const Point3& b) {
        return XLine(a, b - a);
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
    const char* typeName() const noexcept override { return "XLINE"; }

private:
    Point3 m_base{};
    Vec3   m_dir{1.0, 0.0, 0.0};
};

} // namespace cad
