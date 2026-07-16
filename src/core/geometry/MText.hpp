// src/core/geometry/MText.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include <string>
#include <vector>

namespace cad {

// Justificação horizontal de cada linha em relação ao ponto de inserção:
//   Left   = início da linha no ponto de inserção (offset 0);
//   Center = linha centrada     (offset -largura/2);
//   Right  = fim da linha no ponto de inserção (offset -largura).
enum class MTextJustify { Left, Center, Right };

// Texto de uma ou mais linhas desenhado com a fonte de traços (StrokeFont):
// o conteúdo vira segmentos de reta e entra no MESMO pipeline da geometria.
// O conteúdo é dividido em `\n`, empilhando as linhas de cima para baixo com
// entrelinha de ~1.4×altura: a 1ª linha fica no ponto de inserção e as
// seguintes descem em Y (no espaço local, antes da rotação).
// `m_pos` é o canto inferior-esquerdo (ponto de inserção); `m_height` é a
// altura das maiúsculas; `m_rotation` é o ângulo em radianos (CCW) no plano XY.
class MText final : public Entity {
public:
    MText() = default;
    MText(Point3 pos, std::string text, double height, double rotation = 0.0)
        : m_pos(pos), m_height(height), m_text(std::move(text)), m_rotation(rotation) {}

    const Point3&      position() const { return m_pos; }
    double             height()   const { return m_height; }
    const std::string& text()     const { return m_text; }
    double             rotation() const { return m_rotation; }
    MTextJustify       justify()  const { return m_justify; }
    void               setJustify(MTextJustify j) { m_justify = j; }
    // Fonte TTF (família, ex.: "Arial"). Vazia = StrokeFont de traços. Só tem
    // efeito com um provider registrado em core/text/TtfFont.hpp (app Qt).
    const std::string& font() const { return m_font; }
    void               setFont(std::string f) { m_font = std::move(f); }
    // Largura da CAIXA de texto (unidades de mundo): >0 = quebra automática de
    // linha por palavra nessa largura (à la MTEXT); 0 = livre (só quebra em \n).
    double boxWidth() const { return m_boxWidth; }
    void   setBoxWidth(double w) { m_boxWidth = (w > 0.0 ? w : 0.0); }
    // Negrito/itálico: só têm efeito com fonte TTF (a StrokeFont ignora).
    bool bold()   const { return m_bold; }
    bool italic() const { return m_italic; }
    void setBold(bool b)   { m_bold = b; }
    void setItalic(bool i) { m_italic = i; }
    // ANOTATIVO: a altura de modelo acompanha a escala de anotação do doc
    // (regen ao trocar a escala) e é recalculada POR VIEWPORT no papel/plot,
    // mantendo o tamanho impresso constante em mm.
    bool annotative() const { return m_annotative; }
    void setAnnotative(bool a) { m_annotative = a; }
    void setHeight(double h) { if (h > 1e-9) m_height = h; }

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "MTEXT"; }

private:
    // Gera TODOS os pontos dos traços já posicionados (todas as linhas,
    // justificadas e empilhadas). Pares consecutivos = um segmento, no mesmo
    // formato de strokeText. Usado por boundingBox() e emitTo().
    std::vector<Point3> buildStrokePoints() const;

    Point3       m_pos{};
    double       m_height{2.5};
    std::string  m_text{};
    double       m_rotation{0.0};
    MTextJustify m_justify{MTextJustify::Left};
    std::string  m_font{};      // família TTF (vazia = fonte de traços)
    double       m_boxWidth{0.0};   // largura da caixa (0 = sem quebra automática)
    bool         m_bold{false};
    bool         m_italic{false};
    bool         m_annotative{false};   // altura segue a escala de anotação
};

} // namespace cad
