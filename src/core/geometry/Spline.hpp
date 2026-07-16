// src/core/geometry/Spline.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include <vector>

namespace cad {

// Spline interpoladora Catmull-Rom: a curva passa por TODOS os pontos de
// controle (são, de fato, pontos de passagem). As tangentes nas extremidades
// são obtidas duplicando o primeiro e o último ponto. A tesselação (sample) é
// usada para emissão (polilinha) e para o teste de proximidade (picking).
class Spline final : public Entity {
public:
    Spline() = default;
    explicit Spline(std::vector<Point3> pts, bool cv = false)
        : m_ctrl(std::move(pts)), m_cv(cv) {}

    const std::vector<Point3>& controlPoints() const { return m_ctrl; }
    bool isCV() const { return m_cv; }   // true = B-spline por pontos de controle

    // Amostra a curva em 'perSpan' segmentos por vão entre pontos de controle
    // consecutivos. Retorna a sequência de pontos da polilinha resultante.
    // Com menos de 2 pontos de controle, devolve os próprios pontos.
    std::vector<Point3> sample(int perSpan = 16) const;

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "SPLINE"; }

private:
    std::vector<Point3> m_ctrl;  // pontos de controle (passagem se !m_cv; CV se m_cv)
    bool                m_cv{false};
};

} // namespace cad
