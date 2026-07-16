// src/core/geometry/Ellipse.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/math/Constants.hpp"

namespace cad {

// Elipse no plano de trabalho XY (normal +Z implícita). O eixo maior é dado por
// um vetor do centro até a extremidade desse eixo: a direção define a orientação
// e o comprimento define o semi-eixo maior (a). O semi-eixo menor (b) é obtido
// por b = a * ratio, na direção perpendicular (major girado 90° CCW).
// Elipses fora do plano (3D) exigiriam um OCS/normal — fora do escopo 2D atual.
//
// ARCO ELÍPTICO: o intervalo paramétrico [m_t0, m_t1] (ângulos PARAMÉTRICOS em
// radianos, varredura CCW) recorta um trecho da elipse. Por padrão a elipse é
// COMPLETA (m_t0 = 0, m_t1 = kTwoPi). O parâmetro t NÃO é o ângulo geométrico
// real, mas o ângulo da forma paramétrica: ponto(t) = center + cos(t)*major +
// sin(t)*minor. Use fromCenterAxesArc(...) para construir um arco elíptico.
class Ellipse final : public Entity {
public:
    Ellipse() = default;

    // center: centro da elipse.
    // majorAxisEndpointVector: vetor do centro até a extremidade do eixo MAIOR
    //   (seu comprimento = semi-eixo maior a; sua direção = orientação da elipse).
    // ratio: b/a (semi-eixo menor / maior), com 0 < ratio <= 1.
    Ellipse(Point3 center, Vec3 majorAxisEndpointVector, double ratio)
        : m_center(center), m_major(majorAxisEndpointVector), m_ratio(ratio) {}

    // Conveniência: constrói a partir do comprimento absoluto do semi-eixo menor,
    // calculando o ratio (= minorLen / |majorVec|). Resulta numa elipse COMPLETA.
    static Ellipse fromCenterAxes(Point3 center, Vec3 majorVec, double minorLen);

    // Conveniência: constrói um ARCO ELÍPTICO a partir do comprimento absoluto do
    // semi-eixo menor e do intervalo paramétrico [t0, t1] (radianos, CCW). Para
    // uma elipse completa use fromCenterAxes(...) ou passe t1 - t0 == kTwoPi.
    static Ellipse fromCenterAxesArc(Point3 center, Vec3 majorVec, double minorLen,
                                     double t0, double t1);

    const Point3& center() const { return m_center; }
    const Vec3&   major()  const { return m_major; }
    double        ratio()  const { return m_ratio; }

    // Intervalo paramétrico do arco (radianos). Para elipse completa:
    // startParam() == 0 e endParam() == kTwoPi.
    double startParam() const { return m_t0; }
    double endParam()   const { return m_t1; }

    // true se o intervalo NÃO cobre a volta completa (i.e., é um arco elíptico).
    bool isArc() const;

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "ELLIPSE"; }

private:
    Point3 m_center{};
    Vec3   m_major{1.0, 0.0, 0.0};  // vetor do centro à extremidade do eixo maior
    double m_ratio{1.0};            // b/a, em (0, 1]
    double m_t0{0.0};               // ângulo paramétrico inicial (rad)
    double m_t1{kTwoPi};            // ângulo paramétrico final (rad); kTwoPi = completa
};

} // namespace cad
