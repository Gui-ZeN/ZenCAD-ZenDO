// src/core/geometry/Leader.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include <string>
#include <vector>

namespace cad {

// Chamada/leader de anotação (estilo AutoCAD): uma polilinha que parte de uma
// ponta de seta e termina junto a um texto opcional.
//
//   m_points  — vértices da chamada; PRECISA de >=2 pontos. A PONTA DA SETA fica
//               em m_points[0] e aponta ao longo de m_points[0] -> m_points[1].
//               O texto (se houver) é desenhado perto do ÚLTIMO ponto.
//   m_text    — texto opcional anexado ao fim da chamada.
//   m_textHeight — altura das maiúsculas do texto (mesma convenção de Dimension).
//   m_arrowSize  — tamanho da seta; < 0 = automático = m_textHeight (igual à
//                  regra de seta "automática" da Dimension).
class Leader final : public Entity {
public:
    Leader() = default;
    Leader(std::vector<Point3> pts, std::string text = {}, double textHeight = 2.5)
        : m_points(std::move(pts)), m_text(std::move(text)), m_textHeight(textHeight) {}

    const std::vector<Point3>& points()     const { return m_points; }
    const std::string&         text()       const { return m_text; }
    double                     textHeight() const { return m_textHeight; }

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "LEADER"; }

private:
    std::vector<Point3> m_points{};        // polilinha da chamada (>=2 pontos)
    std::string         m_text{};          // texto opcional no último ponto
    double              m_textHeight{2.5};
    double              m_arrowSize{-1.0}; // < 0 = automático = m_textHeight
};

} // namespace cad
