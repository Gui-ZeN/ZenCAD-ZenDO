// src/core/geometry/Hatch.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/math/Constants.hpp"
#include <vector>

namespace cad {

// Padrões de hachura nomeados. Cada padrão é desenhado por uma ou mais "famílias"
// de linhas paralelas (vide Hatch::emitTo). Os ângulos abaixo são SEMPRE somados
// ao ângulo do usuário `m_angleDeg`, permitindo girar o padrão inteiro:
//   - Lines  : 1 família no ângulo do usuário (comportamento clássico a 45°).
//   - ANSI31 : 1 família a 45° (aço — diagonal única).
//   - ANSI37 : 2 famílias a 45° e 135° (xadrez/cross-hatch).
//   - Grid   : 2 famílias a 0° e 90° (grade ortogonal).
//   - Solid  : preenchimento sólido — o anel (loop externo MENOS furos) é
//              triangulado (ear clipping) e emitido como triângulos no canal
//              de fill (sem linhas de hachura).
//   - Gradient: preenchimento em gradiente entre duas cores (cor1 -> cor2).
//              Como o RenderBatch não carrega cor por vértice/triângulo, o fill
//              geométrico é idêntico ao Solid (mesmo anel triangulado); as duas
//              cores são expostas por getters (gradientColor1()/gradientColor2())
//              para o renderer interpolar. Vide nota no emitTo (MVP honesto).
// Materiais arquitetônicos (acrescentados ao FIM — índices 6..9):
//   - Brick   : tijolo em fiada corrida (cursos horizontais + juntas verticais
//               alternadas por curso).
//   - Concrete: concreto — pontilhado + pequenos triângulos (agregado).
//   - Wood    : madeira — linhas paralelas densas (veio).
//   - Sand    : areia — pontilhado fino.
// IMPORTANTE: novos padrões SÓ podem ser acrescentados ao FIM do enum, para não
// alterar os índices já persistidos. Índices congelados: Lines=0, ANSI31=1,
// ANSI37=2, Grid=3, Solid=4, Gradient=5, Brick=6, Concrete=7, Wood=8, Sand=9.
enum class HatchPattern { Lines, ANSI31, ANSI37, Grid, Solid, Gradient,
                          Brick, Concrete, Wood, Sand };

// Hachura (preenchimento) por linhas paralelas. Uma ou mais fronteiras fechadas
// (loops) delimitam a região; o interior é preenchido com uma ou mais famílias de
// linhas paralelas (conforme `m_pattern`), espaçadas por `m_spacing * m_scale` e
// giradas por `m_angleDeg`. O preenchimento usa a regra par-ímpar (even-odd) sobre
// as interseções de cada linha de varredura com TODAS as arestas de TODOS os loops
// — assim furos internos ficam vazios naturalmente.
class Hatch final : public Entity {
public:
    Hatch() = default;

    // Construtor clássico (compatível): preenchimento por linhas paralelas.
    // Mantém o comportamento histórico — padrão `Lines` a 45°, escala 1.
    // `angle` segue em RADIANOS (legado); é convertido para graus em `m_angleDeg`.
    explicit Hatch(std::vector<std::vector<Point3>> loops,
                   double angle   = kPi / 4,
                   double spacing = 3.0)
        : m_loops(std::move(loops)),
          m_angle(angle),
          m_spacing(spacing),
          m_pattern(HatchPattern::Lines),
          m_angleDeg(angle * 180.0 / kPi),
          m_scale(1.0) {}

    // Construtor com padrão nomeado. `angleDeg` em GRAUS gira o padrão inteiro;
    // `scale` multiplica o espaçamento base (`spacing`). O `m_angle` (rad) é mantido
    // coerente com `angleDeg` para preservar getters/transform legados.
    Hatch(std::vector<std::vector<Point3>> loops,
          HatchPattern pattern,
          double angleDeg,
          double scale   = 1.0,
          double spacing = 3.0)
        : m_loops(std::move(loops)),
          m_angle(angleDeg * kPi / 180.0),
          m_spacing(spacing),
          m_pattern(pattern),
          m_angleDeg(angleDeg),
          m_scale(scale) {}

    // --- Getters ----------------------------------------------------------
    const std::vector<std::vector<Point3>>& loops() const { return m_loops; }
    double angle()   const { return m_angle; }        // direção base (rad, legado)
    double spacing() const { return m_spacing; }       // espaçamento base
    HatchPattern pattern() const { return m_pattern; } // padrão nomeado
    double angleDeg() const { return m_angleDeg; }     // rotação do padrão (graus)
    double scale()    const { return m_scale; }        // multiplicador de espaçamento

    // Cores do gradiente (só fazem sentido com pattern == Gradient). O fill
    // geométrico não carrega cor; estas duas cores são lidas pelo renderer para
    // interpolar cor1 -> cor2 ao longo da área. Defaults: preto -> branco.
    const Rgba& gradientColor1() const { return m_gradColor1; }
    const Rgba& gradientColor2() const { return m_gradColor2; }

    // --- Setters ----------------------------------------------------------
    void setAngle(double angle)     { m_angle = angle; m_angleDeg = angle * 180.0 / kPi; }
    void setSpacing(double spacing) { m_spacing = spacing; }
    void setPattern(HatchPattern p) { m_pattern = p; }
    void setAngleDeg(double deg)    { m_angleDeg = deg; m_angle = deg * kPi / 180.0; }
    void setScale(double s)         { m_scale = s; }
    void setGradientColor1(Rgba c)  { m_gradColor1 = c; }
    void setGradientColor2(Rgba c)  { m_gradColor2 = c; }
    void setLoops(std::vector<std::vector<Point3>> l) { m_loops = std::move(l); }

    // --- Associatividade (sessão, como as cotas) ---------------------------
    // Entidades-FONTE de cada loop (mesma ordem). Quando uma muda, o
    // DrawingManager re-extrai o contorno e substitui os loops (regen).
    const std::vector<EntityId>& srcIds() const { return m_srcIds; }
    void setSrcIds(std::vector<EntityId> ids) { m_srcIds = std::move(ids); }
    bool associative() const { return !m_srcIds.empty(); }

    // --- Contrato Entity --------------------------------------------------
    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "HATCH"; }

private:
    // Desenha UMA família de linhas paralelas no ângulo `angleRad`, com `spacing`
    // entre elas, recortada pelos loops via varredura par-ímpar. Reutilizado por
    // todos os padrões. (Definido em Hatch.cpp.)
    void emitLineFamily(RenderBatch& batch, double angleRad, double spacing) const;

    std::vector<std::vector<Point3>> m_loops;   // fronteiras fechadas (polígonos)
    double m_angle{kPi / 4};                     // direção base (rad, legado)
    double m_spacing{3.0};                       // distância base entre linhas
    HatchPattern m_pattern{HatchPattern::Lines}; // padrão de hachura
    double m_angleDeg{45.0};                      // rotação do padrão (graus)
    double m_scale{1.0};                          // multiplica o espaçamento base
    Rgba   m_gradColor1{0, 0, 0, 255};            // gradiente: cor inicial (preto)
    Rgba   m_gradColor2{255, 255, 255, 255};      // gradiente: cor final (branco)
    std::vector<EntityId> m_srcIds;               // fontes dos loops (associativa)
};

} // namespace cad
