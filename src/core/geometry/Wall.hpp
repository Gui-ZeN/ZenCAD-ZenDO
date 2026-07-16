// src/core/geometry/Wall.hpp
// PAREDE — a entidade de arquitetura do ZenCAD. Desenhada pelo EIXO
// (polilinha aberta de segmentos retos) com uma ESPESSURA: as duas faces são
// geradas com junção em ESQUADRIA (miter) nos vértices e arremate reto nas
// pontas. VÃOS (Opening) vivem sobre o eixo por distância percorrida
// (station): vão livre, PORTA (folha + arco de giro embutidos) ou JANELA
// (símbolo de 3 linhas). O corpo é preenchido (fills) — parede cortada sai
// cheia na tela e no plot, como manda a prancha de arquitetura.
#pragma once
#include "core/geometry/Entity.hpp"
#include <vector>

namespace cad {

class Wall final : public Entity {
public:
    struct Opening {
        double station{0.0};    // distância ao longo do eixo até o INÍCIO do vão
        double width{0.9};
        int    kind{0};         // 0 = vão livre · 1 = PORTA · 2 = JANELA
        int    side{1};         // porta: lado de abertura (+1 = esquerda do eixo)
        bool   hingeAtEnd{false};   // porta: dobradiça no fim do vão
    };

    Wall() = default;
    Wall(std::vector<Point3> axis, double thickness)
        : m_axis(std::move(axis)), m_thickness(thickness) {}

    const std::vector<Point3>& axis() const { return m_axis; }
    double thickness() const { return m_thickness; }
    void   setThickness(double t) { if (t > 1e-9) m_thickness = t; }
    void   setAxisPoint(std::size_t i, const Point3& p) {
        if (i < m_axis.size()) m_axis[i] = p;
    }

    const std::vector<Opening>& openings() const { return m_openings; }
    void setOpenings(std::vector<Opening> o) { m_openings = std::move(o); }
    void addOpening(const Opening& o) { m_openings.push_back(o); }

    // Comprimento total do eixo e projeção de um ponto no eixo (station).
    double axisLength() const;
    double stationOf(const Point3& p) const;   // ponto mais próximo -> station

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "WALL"; }

    // Cantos das duas faces (esquerda/direita do eixo), já em esquadria.
    // Público: o Zendo (3D) extruda a parede a partir destes cantos.
    void faceCorners(std::vector<Point3>& L, std::vector<Point3>& R) const;

private:
    std::vector<Point3>  m_axis;          // eixo (>= 2 pontos, segmentos retos)
    double               m_thickness{0.15};
    std::vector<Opening> m_openings;
};

} // namespace cad
