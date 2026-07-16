// src/core/geometry/Region.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include <vector>

namespace cad {

// REGION — área 2D fechada delimitada por um ou mais loops (polígonos no plano XY).
// Convenção dos loops (espelha o Hatch, que também guarda std::vector<std::vector<Point3>>):
//   - loops[0]  = contorno externo (fronteira da área);
//   - loops[1..] = furos (ilhas vazias dentro do contorno externo).
// Cada loop é um polígono fechado IMPLÍCITO (o fechamento último->primeiro é feito
// na emissão/cálculo; NÃO repetir o 1º vértice no fim) — mesmo contrato esperado por
// BooleanOps.hpp (polygonBoolean), o que torna a REGION usável em operações booleanas.
//
// Diferente do Hatch, a REGION NÃO desenha preenchimento por padrão: ela representa o
// CONTORNO da área. O cálculo de área (shoelace) trata externo menos furos.
class Region final : public Entity {
public:
    Region() = default;

    // Construtor principal: loops[0] = contorno externo; loops[1..] = furos.
    explicit Region(std::vector<std::vector<Point3>> loops)
        : m_loops(std::move(loops)) {}

    // --- Getter -----------------------------------------------------------
    const std::vector<std::vector<Point3>>& loops() const { return m_loops; }

    // --- API específica de REGION -----------------------------------------
    // Área da região = |área do contorno externo| - soma das |áreas dos furos|.
    // Cada loop é medido por shoelace e tomado em valor absoluto (independe da
    // orientação CW/CCW de cada loop). Loops com < 3 vértices contribuem 0.
    double area() const;

    // Fábrica opcional (conveniência/legibilidade): idêntica ao construtor.
    static Region fromLoops(std::vector<std::vector<Point3>> loops) {
        return Region(std::move(loops));
    }

    // --- Contrato Entity (assinaturas espelhadas EXATAMENTE de Entity.hpp) -
    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;

    // accept(): o EntityVisitor (Entity.hpp) NÃO declara visit(const Region&) e o
    // escopo proíbe editá-lo; também não existe overload catch-all. Por isso o corpo
    // é definido fora-de-linha (Region.cpp) e fica VAZIO — exatamente o padrão já
    // adotado por Wipeout (entidade igualmente ausente do visitor). Ver detalhe lá.
    void      accept(EntityVisitor& v) const override;

    const char* typeName() const noexcept override { return "REGION"; }

private:
    std::vector<std::vector<Point3>> m_loops;  // [0]=externo, [1..]=furos (plano XY)
};

} // namespace cad
