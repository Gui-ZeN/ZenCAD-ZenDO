// src/core/geometry/MLeader.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include <string>
#include <vector>

namespace cad {

// Multileader (estilo AutoCAD): UM único texto compartilhado por VÁRIAS linhas
// de chamada. Cada chamada é uma polilinha independente, e cada uma tem sua
// PRÓPRIA ponta de seta apontando para um objeto distinto.
//
//   m_leaders   — coleção de polilinhas de chamada. Cada elemento é UMA
//                 polilinha: seu PRIMEIRO ponto é a ponta da seta, que aponta ao
//                 longo de leader[0] -> leader[1]. Uma polilinha com 1 ponto não
//                 tem direção e, portanto, não recebe seta.
//   m_textPos   — âncora (canto inferior-esquerdo) do texto único.
//   m_text      — texto compartilhado por todas as chamadas.
//   m_textHeight — altura das maiúsculas (mesma convenção do Leader/Dimension).
//   m_arrowSize  — tamanho da seta; < 0 = automático = m_textHeight (mesma regra
//                  de seta "automática" do Leader/Dimension).
class MLeader final : public Entity {
public:
    MLeader() = default;
    MLeader(std::vector<std::vector<Point3>> leaders, Point3 textPos,
            std::string text = {}, double textHeight = 2.5)
        : m_leaders(std::move(leaders))
        , m_textPos(textPos)
        , m_text(std::move(text))
        , m_textHeight(textHeight) {}

    const std::vector<std::vector<Point3>>& leaders()    const { return m_leaders; }
    const Point3&                           textPos()    const { return m_textPos; }
    const std::string&                      text()       const { return m_text; }
    double                                  textHeight() const { return m_textHeight; }

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "MULTILEADER"; }

private:
    std::vector<std::vector<Point3>> m_leaders{};   // polilinhas de chamada (>=1 ponto cada)
    Point3                           m_textPos{};   // âncora do texto único
    std::string                      m_text{};      // texto compartilhado
    double                           m_textHeight{2.5};
    double                           m_arrowSize{-1.0}; // < 0 = automático = m_textHeight
};

} // namespace cad
