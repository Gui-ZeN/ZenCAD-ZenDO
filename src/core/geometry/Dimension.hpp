// src/core/geometry/Dimension.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include <cmath>   // std::abs nos getters de cota linear (inline)
#include <string>  // m_suffix (sufixo de unidade do texto da cota)

namespace cad {

// Âncora de ASSOCIATIVIDADE de cota: referência a um ponto notável de uma
// entidade-fonte. Quando a fonte muda, o DrawingManager recalcula os pontos de
// definição da cota (regen pós-comando). OnCurve = ponto sobre círculo/arco
// preservando a DIREÇÃO centro→ponto anterior (cotas de raio/diâmetro).
// Nota: âncoras valem para a SESSÃO (EntityIds não sobrevivem a salvar/abrir).
struct DimAnchor {
    enum class Which { None, Start, End, Center, Vertex, Node, OnCurve };
    EntityId id{kInvalidId};
    Which    which{Which::None};
    int      index{0};               // índice do vértice (Which::Vertex)
    bool valid() const { return id != kInvalidId && which != Which::None; }
};

// Cota (dimensão) anotativa. Um único tipo concreto cobre as variações comuns
// via DimKind; o significado dos pontos de definição muda conforme o tipo:
//
//   Linear   — m_p1,m_p2 = pontos medidos; m_p3 = posição da linha de cota.
//              A medida é a distância projetada no eixo dominante (h ou v).
//   Aligned  — m_p1,m_p2 = pontos medidos; m_p3 = lado/offset da linha de cota.
//              A medida é a distância real |p2-p1| (linha de cota paralela a p1-p2).
//   Radius   — m_p1 = centro; m_p2 = ponto no círculo. Texto "R<valor>".
//   Diameter — m_p1 = centro; m_p2 = ponto no círculo. Texto "Ø<valor>".
//   Angular  — m_p1 = vértice; m_p2,m_p3 = pontos nos dois lados. Texto "<graus>°".
class Dimension final : public Entity {
public:
    enum class DimKind { Linear, Aligned, Radius, Diameter, Angular, Ordinate };
    // Tipo de ponta da cota: seta cheia, tique arquitetônico (barra /) ou ponto.
    enum class ArrowType { Arrow, Tick, Dot };

    Dimension() = default;
    Dimension(DimKind kind, Point3 p1, Point3 p2, Point3 p3, double textHeight)
        : m_kind(kind), m_p1(p1), m_p2(p2), m_p3(p3), m_textHeight(textHeight) {}

    // --- Construtores estáticos convenientes ------------------------------
    static Dimension linear(Point3 p1, Point3 p2, Point3 linePos, double h = 2.5) {
        return Dimension{DimKind::Linear, p1, p2, linePos, h};
    }
    static Dimension aligned(Point3 p1, Point3 p2, Point3 linePos, double h = 2.5) {
        return Dimension{DimKind::Aligned, p1, p2, linePos, h};
    }
    static Dimension radius(Point3 center, Point3 onCirc, double h = 2.5) {
        return Dimension{DimKind::Radius, center, onCirc, Point3{}, h};
    }
    static Dimension diameter(Point3 center, Point3 onCirc, double h = 2.5) {
        return Dimension{DimKind::Diameter, center, onCirc, Point3{}, h};
    }
    static Dimension angular(Point3 vertex, Point3 a, Point3 b, double h = 2.5) {
        return Dimension{DimKind::Angular, vertex, a, b, h};
    }
    // Ordinate (DIMORDINATE): p1 = ponto medido; p3 = fim do leader. Mede a
    // coordenada X (leader predominantemente vertical) ou Y (horizontal).
    static Dimension ordinate(Point3 feature, Point3 leaderEnd, double h = 2.5) {
        return Dimension{DimKind::Ordinate, feature, feature, leaderEnd, h};
    }

    // --- Encadeamento de cotas LINEARES (estilo AutoCAD) ------------------
    //
    // continueLinear: cria uma nova cota LINEAR que começa onde a cota `prev`
    //   terminou (no 2º ponto de `prev`) e vai até `nextPoint`, mantendo a
    //   MESMA orientação (horizontal/vertical) e a MESMA linha de cota (mesmo
    //   offset) de `prev`. Encadeia cotas lado a lado sobre a mesma linha.
    //
    //   Se `prev` NÃO for uma cota linear, faz fallback seguro: devolve uma
    //   cota linear simples entre os pontos disponíveis de `prev` (p2) e
    //   `nextPoint`, com a linha de cota passando pelo próprio nextPoint
    //   (offset zero) — mantém a entidade válida sem inventar geometria.
    //
    //   Premissa: o encadeamento pressupõe pontos colineares com o eixo de
    //   medição de `prev` (uso típico de cota contínua). A orientação efetiva
    //   continua sendo derivada do eixo dominante de (start,nextPoint) em
    //   linear(); para pontos sobre o mesmo eixo de `prev` isso coincide com a
    //   orientação de `prev`, como esperado.
    static Dimension continueLinear(const Dimension& prev, const Point3& nextPoint,
                                    double textH = 2.5);

    // baselineLinear: cria uma nova cota LINEAR a partir do MESMO 1º ponto de
    //   `prev` até `nextPoint`, mantendo a orientação de `prev`, mas com a
    //   linha de cota deslocada por `offsetStep` ADICIONAL além da linha de
    //   `prev` (empilhamento a partir da mesma base de medição).
    //
    //   `offsetStep` é aplicado afastando-se dos pontos medidos (mesmo sentido
    //   em que a linha de cota de `prev` já está deslocada). Fallback igual ao
    //   de continueLinear caso `prev` não seja linear (usa p1 de `prev`).
    static Dimension baselineLinear(const Dimension& prev, const Point3& nextPoint,
                                    double textH, double offsetStep);

    DimKind       kind()       const { return m_kind; }
    const Point3& p1()         const { return m_p1; }
    const Point3& p2()         const { return m_p2; }
    const Point3& p3()         const { return m_p3; }
    double        textHeight() const { return m_textHeight; }

    // --- Estilo configurável (não altera construtores/estáticos) ----------
    // Os defaults reproduzem EXATAMENTE o comportamento anterior:
    //   - seta com tamanho igual à altura do texto (era `arrow = h` no emit);
    //   - 2 casas decimais no número (era o "%.2f" fixo do fmt());
    //   - sufixo vazio (não havia unidade anexada ao número).
    //
    // m_arrowSize negativo é tratado como "automático" no emitTo(): cai de
    // volta para a altura do texto (comportamento original), preservando a
    // compatibilidade dos objetos construídos pelos estáticos/ctors.
    double             arrowSize() const { return m_arrowSize; }
    int                decimals()  const { return m_decimals; }
    const std::string& suffix()    const { return m_suffix; }
    ArrowType          arrowType() const { return m_arrowType; }

    void setArrowSize(double s) { m_arrowSize = s; }
    void setTextHeight(double h) { if (h > 1e-9) m_textHeight = h; }
    // ANOTATIVO: texto/setas seguem a escala de anotação do doc (regen ao
    // trocar) e são recalculados por viewport no papel/plot (mm constantes).
    bool annotative() const { return m_annotative; }
    void setAnnotative(bool a) { m_annotative = a; }
    void setDecimals(int d)     { m_decimals = d; }
    void setSuffix(std::string s) { m_suffix = std::move(s); }
    void setArrowType(ArrowType t) { m_arrowType = t; }
    // Fonte TTF do TEXTO da cota (vazia = traços; requer provider, como MText).
    const std::string& font() const { return m_font; }
    void setFont(std::string f) { m_font = std::move(f); }

    // --- Tolerância dimensional e raio JOGGED ------------------------------
    // plus==minus>0 → "±x"; diferentes → "+a/-b"; 0/0 = sem tolerância.
    double tolPlus()  const { return m_tolPlus; }
    double tolMinus() const { return m_tolMinus; }
    void setTolerance(double plus, double minus) { m_tolPlus = plus; m_tolMinus = minus; }
    bool jogged() const { return m_jogged; }
    void setJogged(bool j) { m_jogged = j; }

    // --- DIMTEDIT: texto da cota -------------------------------------------
    // Override: substitui o texto INTEIRO (vazio = medida real). Offset: des-
    // locamento do texto a partir da posição automática (grip de mover texto).
    const std::string& textOverride() const { return m_textOverride; }
    void setTextOverride(std::string s) { m_textOverride = std::move(s); }
    double textOffsetX() const { return m_textOffX; }
    double textOffsetY() const { return m_textOffY; }
    void setTextOffset(double dx, double dy) { m_textOffX = dx; m_textOffY = dy; }
    // Ponto-base do texto (SEM offset): meio da linha de cota / ponto no
    // círculo / meio do arco angular — âncora do grip de mover texto.
    Point3 textBasePoint() const;

    // --- Associatividade ---------------------------------------------------
    const DimAnchor& anchorA() const { return m_anchorA; }
    const DimAnchor& anchorB() const { return m_anchorB; }
    void setAnchors(const DimAnchor& a, const DimAnchor& b) { m_anchorA = a; m_anchorB = b; }
    bool associative() const { return m_anchorA.valid() || m_anchorB.valid(); }
    // Regen: substitui os pontos de definição mantendo estilo/tipo.
    void setPoints(const Point3& p1, const Point3& p2, const Point3& p3) {
        m_p1 = p1; m_p2 = p2; m_p3 = p3;
    }

    // --- Getters específicos de cota LINEAR -------------------------------
    // Estes só fazem sentido quando kind()==Linear; servem ao encadeamento
    // (continue/baseline) e a quem precise inspecionar a geometria da cota.

    // true se a cota linear é horizontal (eixo dominante X), seguindo a mesma
    // regra de emitTo()/hitTest(): horizontal quando |dx| >= |dy|.
    bool isLinearHorizontal() const {
        return std::abs(m_p2.x - m_p1.x) >= std::abs(m_p2.y - m_p1.y);
    }
    // Coordenada da linha de cota: para cota horizontal é o Y da linha (m_p3.y),
    // para vertical é o X da linha (m_p3.x).
    double linearLineCoord() const {
        return isLinearHorizontal() ? m_p3.y : m_p3.x;
    }
    // Offset assinado da linha de cota em relação aos pontos medidos, no eixo
    // perpendicular à medida (Y para horizontal, X para vertical). O sinal
    // indica para que lado a linha de cota foi afastada dos pontos medidos.
    double linearOffset() const {
        return isLinearHorizontal() ? (m_p3.y - m_p1.y) : (m_p3.x - m_p1.x);
    }

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "DIMENSION"; }

private:
    DimKind m_kind{DimKind::Linear};
    Point3  m_p1{};
    Point3  m_p2{};
    Point3  m_p3{};
    double  m_textHeight{2.5};

    // --- Campos de estilo (defaults = comportamento original) -------------
    // m_arrowSize < 0  => "automático": usa a altura do texto no emitTo(),
    // que era o tamanho de seta fixo original (`arrow = h`).
    double      m_arrowSize{-1.0};
    int         m_decimals{2};   // casas decimais do número (era "%.2f" fixo)
    std::string m_suffix{};      // sufixo de unidade, ex.: " mm" (vazio = como antes)
    ArrowType   m_arrowType{ArrowType::Arrow};   // tipo de ponta (seta por padrão)
    std::string m_font{};                        // fonte TTF do texto (vazia = traços)
    std::string m_textOverride{};                // texto forçado (vazio = medida)
    double      m_textOffX{0.0}, m_textOffY{0.0};   // deslocamento do texto (DIMTEDIT)
    double      m_tolPlus{0.0}, m_tolMinus{0.0};    // tolerância (0/0 = off)
    bool        m_jogged{false};                    // raio com zigue-zague
    bool        m_annotative{false};                // segue a escala de anotação
    DimAnchor   m_anchorA{}, m_anchorB{};        // fontes de associatividade (sessão)
};

} // namespace cad
