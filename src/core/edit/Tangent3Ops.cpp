// src/core/edit/Tangent3Ops.cpp
#include "core/edit/Tangent3Ops.hpp"

#include "core/edit/GeometryOps.hpp"   // intersectInfiniteLines
#include "core/geometry/Line.hpp"

#include <array>
#include <cmath>

namespace cad {
namespace {

// Tolerância para considerar três distâncias (raios) coincidentes e para
// detectar direções (quase) paralelas no plano XY.
constexpr double kTol = 1e-6;

// Direção unitária de uma Line (no plano XY). z é ignorado.
Vec3 lineDir(const Line& l) {
    return (l.end() - l.start()).normalized();
}

// Distância (não-negativa) de um ponto à reta INFINITA definida por p0 e a
// direção unitária d. Vale a componente perpendicular de (p - p0) no plano XY.
double pointLineDistance(const Point3& p, const Point3& p0, const Vec3& d) {
    const Vec3 w = p - p0;
    // Componente perpendicular = |w - (w·d) d|. Como d é unitária:
    const Vec3 perp = w - d * w.dot(d);
    return perp.length();
}

// As DUAS bissetrizes do ângulo entre duas retas, a partir do ponto de
// interseção apex e das direções UNITÁRIAS d1 e d2:
//   bissetriz1 ∝ d1 + d2   (bissetriz interna)
//   bissetriz2 ∝ d1 − d2   (bissetriz externa, perpendicular à primeira)
// Cada bissetriz é devolvida como um par (apex, dir). dir é normalizada.
struct Bisector {
    Point3 apex;
    Vec3   dir;
};

std::array<Bisector, 2> bisectors(const Point3& apex, const Vec3& d1,
                                  const Vec3& d2) {
    const Vec3 sum  = d1 + d2;
    const Vec3 diff = d1 - d2;
    return {Bisector{apex, sum.normalized()},
            Bisector{apex, diff.normalized()}};
}

} // namespace

std::optional<Circle> circleTanTanTan(const Entity& a, const Entity& b,
                                      const Entity& c, const Point3& nearPt) {
    // Caso suportado: as três entidades são Line.
    const auto* la = dynamic_cast<const Line*>(&a);
    const auto* lb = dynamic_cast<const Line*>(&b);
    const auto* lc = dynamic_cast<const Line*>(&c);
    if (!la || !lb || !lc)
        return std::nullopt;

    const Vec3 da = lineDir(*la);
    const Vec3 db = lineDir(*lb);
    const Vec3 dc = lineDir(*lc);

    // Vértices do triângulo: interseções dos pares de retas. Se qualquer par for
    // paralelo, não há triângulo (nem solução).
    const auto vAB = intersectInfiniteLines(la->start(), la->end(),
                                            lb->start(), lb->end());
    const auto vAC = intersectInfiniteLines(la->start(), la->end(),
                                            lc->start(), lc->end());
    const auto vBC = intersectInfiniteLines(lb->start(), lb->end(),
                                            lc->start(), lc->end());
    if (!vAB || !vAC || !vBC)
        return std::nullopt;

    // Candidatos: 2 bissetrizes do par (a,b) x 2 bissetrizes do par (a,c).
    // O centro é a interseção de uma bissetriz com a outra. As bissetrizes são
    // tratadas como retas infinitas (apex + dir).
    const auto bAB = bisectors(*vAB, da, db);
    const auto bAC = bisectors(*vAC, da, dc);

    std::optional<Circle> best;
    double bestDistToNear = 0.0;

    for (const auto& s1 : bAB) {
        for (const auto& s2 : bAC) {
            // Interseção das duas bissetrizes (cada uma é apex + dir unitária).
            const Point3 p1a = s1.apex;
            const Point3 p1b = s1.apex + s1.dir;
            const Point3 p2a = s2.apex;
            const Point3 p2b = s2.apex + s2.dir;
            const auto center = intersectInfiniteLines(p1a, p1b, p2a, p2b);
            if (!center)
                continue;  // bissetrizes paralelas: sem centro

            // Raio = distância do centro a cada uma das três retas; devem bater.
            const double rA = pointLineDistance(*center, la->start(), da);
            const double rB = pointLineDistance(*center, lb->start(), db);
            const double rC = pointLineDistance(*center, lc->start(), dc);

            if (std::abs(rA - rB) > kTol || std::abs(rA - rC) > kTol ||
                std::abs(rB - rC) > kTol)
                continue;  // distâncias não coincidem: não é tangente às três

            if (rA <= kTol)
                continue;  // raio degenerado (centro sobre as retas)

            // Seleciona o candidato cujo centro fica mais perto de nearPt.
            const double d = (*center - nearPt).length();
            if (!best || d < bestDistToNear) {
                best = Circle{*center, rA};
                bestDistToNear = d;
            }
        }
    }

    return best;
}

} // namespace cad
