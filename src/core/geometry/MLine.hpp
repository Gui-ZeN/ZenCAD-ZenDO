// src/core/geometry/MLine.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include <vector>

namespace cad {

// MLine — multilinha (linha dupla / paralela), típica para representar PAREDES.
// O usuário informa apenas o EIXO central (m_verts); a entidade desenha DUAS
// linhas paralelas ao eixo, deslocadas de +m_width/2 e -m_width/2 na direção
// NORMAL ao eixo (plano XY). Em vértices internos a normal usada é a MÉDIA das
// normais dos dois segmentos vizinhos (miter simples), evitando "buracos" na
// quina; nas pontas (multilinha aberta) usa-se a normal do segmento da ponta.
class MLine final : public Entity {
public:
    MLine() = default;
    // verts: eixo central; width: distância TOTAL entre as duas linhas paralelas.
    explicit MLine(std::vector<Point3> verts, double width, bool closed = false)
        : m_verts(std::move(verts)), m_width(width), m_closed(closed) {}

    const std::vector<Point3>& vertices() const { return m_verts; }
    double                     width()    const { return m_width; }
    bool                       closed()   const { return m_closed; }

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "MLINE"; }

private:
    // Calcula as duas bordas paralelas ao eixo (offset +w/2 e -w/2).
    // left[i]/right[i] correspondem ao vértice m_verts[i]. Vazio se n < 2.
    void buildOffsets(std::vector<Point3>& left, std::vector<Point3>& right) const;

    std::vector<Point3> m_verts;          // eixo central
    double              m_width{0.0};      // distância TOTAL entre as bordas
    bool                m_closed{false};
};

} // namespace cad
