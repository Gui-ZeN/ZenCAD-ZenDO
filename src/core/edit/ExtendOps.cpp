// src/core/edit/ExtendOps.cpp
#include "core/edit/ExtendOps.hpp"
#include "core/edit/GeometryOps.hpp"  // intersectInfiniteLines
#include "core/edit/IntersectOps.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Circle.hpp"
#include "core/math/Constants.hpp"

#include <cmath>
#include <vector>

namespace cad {
namespace {

// Tolerância geométrica padrão para comparações no plano XY (igual a GeometryOps).
constexpr double kEps = 1e-9;

// Distância ao quadrado no plano XY (ignora z).
inline double dist2Sq(const Point3& a, const Point3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Produto escalar restrito ao plano XY.
inline double dot2D(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y;
}

double norm2pi(double a) { a = std::fmod(a, kTwoPi); if (a < 0.0) a += kTwoPi; return a; }
double ccwDelta(double a, double b) { return norm2pi(b - a); }

} // namespace

// Estende a target até a reta infinita boundA–boundB (ver header).
std::optional<Line> extendLineToBoundary(const Line& target,
                                         const Point3& boundA,
                                         const Point3& boundB,
                                         const Point3& pickEnd) {
    const Point3& p0 = target.start();
    const Point3& p1 = target.end();

    // Linha degenerada: nada a estender.
    if (dist2Sq(p0, p1) < kEps * kEps) return std::nullopt;

    // Interseção da reta da target com a reta infinita de contorno.
    const std::optional<Point3> hit =
        intersectInfiniteLines(p0, p1, boundA, boundB);
    if (!hit) return std::nullopt;  // paralelas/degeneradas

    // Extremidade a mover: a mais próxima de pickEnd; a outra é preservada.
    const bool moveStart = dist2Sq(p0, pickEnd) <= dist2Sq(p1, pickEnd);
    const Point3& moving = moveStart ? p0 : p1;
    const Point3& fixed  = moveStart ? p1 : p0;

    // A extensão deve afastar a extremidade móvel do ponto fixo (alongar). Se a
    // interseção cair "atrás" — entre o ponto fixo e a extremidade móvel, ou no
    // mesmo lado encurtando — não é extensão válida.
    const Vec3 dirOut = moving - fixed;          // sentido fixed -> moving
    const Vec3 dirHit = *hit  - fixed;           // sentido fixed -> interseção
    const double projOut = dot2D(dirOut, dirOut);
    const double projHit = dot2D(dirHit, dirOut);

    // projHit < projOut significa que a interseção está mais perto do ponto
    // fixo do que a extremidade atual: encurtaria em vez de estender.
    if (projHit < projOut - kEps) return std::nullopt;
    if (projOut < kEps) return std::nullopt;     // segurança (já tratado acima)

    // Constrói a nova linha mantendo a extremidade fixa.
    if (moveStart) return Line{*hit, fixed};
    return Line{fixed, *hit};
}

std::optional<std::unique_ptr<Entity>> extendEntity(const Entity& target,
                                                    const Entity& boundary,
                                                    const Point3& pickEnd) {
    // --- Line: estende a ponta escolhida ao longo da reta infinita ----------
    if (const auto* ln = dynamic_cast<const Line*>(&target)) {
        const Point3 a = ln->start(), b = ln->end();
        const double dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
        if (len < kEps) return std::nullopt;
        const Vec3 dir{dx / len, dy / len, 0.0};
        const bool extEnd = dist2Sq(pickEnd, b) <= dist2Sq(pickEnd, a);  // ponta perto do pick
        const Point3 nearEnd = extEnd ? b : a;
        const Vec3   outward = extEnd ? dir : Vec3{-dir.x, -dir.y, 0.0};

        // Alcance na ESCALA DA CENA (evita perda de precisão de um 1e6): cobre o
        // contorno com folga, partindo da ponta a estender.
        const AABB bb = boundary.boundingBox();
        const Point3 bc{(bb.min.x + bb.max.x) * 0.5, (bb.min.y + bb.max.y) * 0.5, 0.0};
        const double diag = std::hypot(bb.max.x - bb.min.x, bb.max.y - bb.min.y);
        const double reach = std::hypot(nearEnd.x - bc.x, nearEnd.y - bc.y) + diag + 1.0;
        const Line longLine(Point3{nearEnd.x - dir.x * reach, nearEnd.y - dir.y * reach, 0.0},
                            Point3{nearEnd.x + dir.x * reach, nearEnd.y + dir.y * reach, 0.0});
        double bestProj = 1e300; Point3 best{}; bool found = false;
        for (const Point3& c : intersectEntities(longLine, boundary)) {
            const double proj = (c.x - nearEnd.x) * outward.x + (c.y - nearEnd.y) * outward.y;
            if (proj > 1e-6 && proj < bestProj) { bestProj = proj; best = c; found = true; }
        }
        if (!found) return std::nullopt;
        return std::make_unique<Line>(extEnd ? Line{a, best} : Line{best, b});
    }
    // --- Arc: estende a ponta ao longo do círculo até a boundary -------------
    if (const auto* ar = dynamic_cast<const Arc*>(&target)) {
        const Point3 c = ar->center();
        const double r = ar->radius();
        const double a0 = norm2pi(ar->startAngle()), a1 = norm2pi(ar->endAngle());
        const Point3 pS{c.x + r * std::cos(a0), c.y + r * std::sin(a0), 0.0};
        const Point3 pE{c.x + r * std::cos(a1), c.y + r * std::sin(a1), 0.0};
        const bool extEnd = dist2Sq(pickEnd, pE) <= dist2Sq(pickEnd, pS);

        double best = 1e300, bestAng = 0.0; bool found = false;
        for (const Point3& x : intersectEntities(Circle{c, r}, boundary)) {
            const double ang = norm2pi(std::atan2(x.y - c.y, x.x - c.x));
            const double delta = extEnd ? ccwDelta(a1, ang) : ccwDelta(ang, a0);  // além da ponta
            if (delta > 1e-6 && delta < best) { best = delta; bestAng = ang; found = true; }
        }
        if (!found) return std::nullopt;
        return std::make_unique<Arc>(extEnd ? Arc{c, r, a0, bestAng} : Arc{c, r, bestAng, a1});
    }
    return std::nullopt;
}

} // namespace cad
