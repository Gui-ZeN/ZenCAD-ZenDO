// src/core/geometry/Wipeout.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include <vector>

namespace cad {

// WIPEOUT — polígono opaco usado como "máscara": preenche a área do contorno com
// triângulos (canal de fill) cobrindo tudo o que estiver atrás, e ainda desenha a
// borda (canal de linhas). É equivalente ao WIPEOUT do AutoCAD: o contorno é um
// polígono fechado e simples no plano XY.
//
// Diferente do Hatch::Solid, esta entidade NÃO tem padrões/famílias de linhas —
// existe apenas para esconder geometria, então sempre emite o preenchimento sólido
// + a borda do contorno.
class Wipeout final : public Entity {
public:
    Wipeout() = default;

    // Construtor principal: recebe o contorno (polígono fechado implícito — o
    // fechamento último->primeiro é feito na emissão/triangulação).
    explicit Wipeout(std::vector<Point3> contorno)
        : m_contour(std::move(contorno)) {}

    // --- Getter -----------------------------------------------------------
    const std::vector<Point3>& contour() const { return m_contour; }

    // --- Contrato Entity (assinaturas espelhadas de Entity.hpp) -----------
    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;

    // accept(): o EntityVisitor NÃO possui visit(const Wipeout&) nem um overload
    // catch-all, e o escopo proíbe editá-lo. Portanto, o corpo é vazio — não há
    // double-dispatch para esta entidade (igual ao que faria qualquer primitiva
    // ainda não registrada no visitor). Ver detalhe no Wipeout.cpp.
    void      accept(EntityVisitor& v) const override;

    const char* typeName() const noexcept override { return "WIPEOUT"; }

private:
    std::vector<Point3> m_contour;  // contorno (polígono fechado, plano XY)
};

} // namespace cad
