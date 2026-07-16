// src/core/edit/TangentOps.cpp
#include "core/edit/TangentOps.hpp"

#include "core/edit/GeometryOps.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"

#include <cmath>
#include <vector>

namespace cad {
namespace {

// Tolerância geométrica para validar tangência efetiva (raio realizado vs. r).
constexpr double kTol = 1e-7;

// Comprimento no plano XY.
inline double length2D(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

// Distância no plano XY entre dois pontos.
inline double dist2D(const Point3& a, const Point3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

// Distância (sem sinal) de um ponto p à RETA INFINITA que passa por a–b.
// Retorna -1 se a reta for degenerada (a == b).
double distPointToLine(const Point3& a, const Point3& b, const Point3& p) {
    const Vec3   d   = b - a;
    const double len = length2D(d);
    if (len < kTol) return -1.0;
    // |cross2D(d, p-a)| / |d|.
    const double cz = d.x * (p.y - a.y) - d.y * (p.x - a.x);
    return std::fabs(cz) / len;
}

// ---------------------------------------------------------------------------
//  Geradores de lugar geométrico do centro (candidatos por entidade).
// ---------------------------------------------------------------------------

// As duas retas paralelas a (p0,p1), deslocadas por +r e -r. Cada candidata é
// devolvida como um par de pontos (a0,a1) representando a reta infinita.
struct LineCand { Point3 a0, a1; };

std::vector<LineCand> offsetLineCandidates(const Line& ln, double r) {
    const Point3& p0  = ln.start();
    const Point3& p1  = ln.end();
    const Vec3    dir = p1 - p0;
    const double  len = length2D(dir);
    std::vector<LineCand> out;
    if (len < kTol) return out;  // linha degenerada -> sem candidatos

    // Normal unitária à reta.
    const Vec3 n{-dir.y / len, dir.x / len, 0.0};
    for (double s : {+1.0, -1.0}) {
        const Vec3 off = n * (r * s);
        out.push_back(LineCand{
            Point3{p0.x + off.x, p0.y + off.y, 0.0},
            Point3{p1.x + off.x, p1.y + off.y, 0.0}});
    }
    return out;
}

// Os dois círculos concêntricos candidatos: raio (R+r) e |R-r|. O de raio ~0 é
// descartado (centro único degenerado, sem utilidade prática como lugar).
std::vector<Circle> offsetCircleCandidates(const Circle& c, double r) {
    std::vector<Circle> out;
    out.push_back(Circle{c.center(), c.radius() + r});
    const double inner = std::fabs(c.radius() - r);
    if (inner > kTol) out.push_back(Circle{c.center(), inner});
    return out;
}

// ---------------------------------------------------------------------------
//  Interseções entre lugares geométricos -> pontos-candidatos a centro.
// ---------------------------------------------------------------------------

// Interseção reta infinita (a0,a1) × círculo (centro,raio): 0, 1 ou 2 pontos.
// Projeta o centro do círculo sobre a reta e resolve pela distância.
std::vector<Point3> intersectLineCircle(const Point3& a0, const Point3& a1,
                                        const Point3& center, double radius) {
    std::vector<Point3> out;
    const Vec3   d   = a1 - a0;
    const double len = length2D(d);
    if (len < kTol) return out;
    const Vec3 u{d.x / len, d.y / len, 0.0};  // direção unitária da reta

    // Projeção do centro sobre a reta (pé da perpendicular) e distância a ela.
    const double t = (center.x - a0.x) * u.x + (center.y - a0.y) * u.y;
    const Point3 foot{a0.x + u.x * t, a0.y + u.y * t, 0.0};
    const double dPerp = dist2D(foot, center);

    if (dPerp > radius + kTol) return out;          // não intercepta
    if (dPerp > radius - kTol) {                     // tangente -> 1 ponto
        out.push_back(foot);
        return out;
    }
    // Secante -> 2 pontos simétricos ao pé da perpendicular.
    const double h = std::sqrt(std::max(0.0, radius * radius - dPerp * dPerp));
    out.push_back(Point3{foot.x + u.x * h, foot.y + u.y * h, 0.0});
    out.push_back(Point3{foot.x - u.x * h, foot.y - u.y * h, 0.0});
    return out;
}

// Interseção círculo × círculo: 0, 1 ou 2 pontos. (Concêntricos coincidentes
// teriam infinitas soluções -> tratados como 0.)
std::vector<Point3> intersectCircleCircle(const Point3& c0, double r0,
                                          const Point3& c1, double r1) {
    std::vector<Point3> out;
    const double dx = c1.x - c0.x;
    const double dy = c1.y - c0.y;
    const double dCenters = std::sqrt(dx * dx + dy * dy);

    if (dCenters < kTol) return out;                  // concêntricos
    if (dCenters > r0 + r1 + kTol) return out;        // separados
    if (dCenters < std::fabs(r0 - r1) - kTol) return out;  // um dentro do outro

    // a = distância de c0 ao ponto médio dos cruzamentos ao longo da linha
    // dos centros; h = meia-corda perpendicular.
    const double a = (dCenters * dCenters + r0 * r0 - r1 * r1) / (2.0 * dCenters);
    const double hSq = r0 * r0 - a * a;
    const double h = std::sqrt(std::max(0.0, hSq));

    const double ux = dx / dCenters;
    const double uy = dy / dCenters;
    const Point3 mid{c0.x + ux * a, c0.y + uy * a, 0.0};

    if (h < kTol) {                                   // tangentes -> 1 ponto
        out.push_back(mid);
        return out;
    }
    // Normal à linha dos centros.
    const double nx = -uy;
    const double ny = ux;
    out.push_back(Point3{mid.x + nx * h, mid.y + ny * h, 0.0});
    out.push_back(Point3{mid.x - nx * h, mid.y - ny * h, 0.0});
    return out;
}

// ---------------------------------------------------------------------------
//  Validação de tangência: o círculo (center, r) é de fato tangente a ent?
//  Tangente a uma reta: distância(centro, reta) == r.
//  Tangente a um círculo: distância(centros) == R+r (externa) ou |R-r| (int.).
// ---------------------------------------------------------------------------
bool isTangent(const Entity& ent, const Point3& center, double r) {
    if (const auto* ln = dynamic_cast<const Line*>(&ent)) {
        const double d = distPointToLine(ln->start(), ln->end(), center);
        if (d < 0.0) return false;  // reta degenerada
        return std::fabs(d - r) < kTol;
    }
    if (const auto* ci = dynamic_cast<const Circle*>(&ent)) {
        const double d = dist2D(center, ci->center());
        return std::fabs(d - (ci->radius() + r)) < kTol ||
               std::fabs(d - std::fabs(ci->radius() - r)) < kTol;
    }
    return false;  // tipo não suportado
}

} // namespace

// ============================================================================
//  API principal — círculo TTR (tangente-tangente-raio).
// ============================================================================
std::optional<Circle> circleTanTanRadius(const Entity& a, const Entity& b,
                                         double r, const Point3& nearPt) {
    if (r <= kTol) return std::nullopt;  // raio inválido

    const auto* la = dynamic_cast<const Line*>(&a);
    const auto* ca = dynamic_cast<const Circle*>(&a);
    const auto* lb = dynamic_cast<const Line*>(&b);
    const auto* cb = dynamic_cast<const Circle*>(&b);

    // Apenas Line/Circle são suportados em cada operando.
    if (!la && !ca) return std::nullopt;
    if (!lb && !cb) return std::nullopt;

    // Gera os lugares-geométricos-candidato de cada operando e cruza todos os
    // pares, coletando todos os pontos-candidato a centro.
    std::vector<Point3> centers;

    // Pré-computa candidatos por operando.
    std::vector<LineCand> aLines, bLines;
    std::vector<Circle>   aCircs, bCircs;
    if (la) aLines = offsetLineCandidates(*la, r);
    if (ca) aCircs = offsetCircleCandidates(*ca, r);
    if (lb) bLines = offsetLineCandidates(*lb, r);
    if (cb) bCircs = offsetCircleCandidates(*cb, r);

    // Line × Line.
    for (const auto& A : aLines)
        for (const auto& B : bLines)
            if (auto p = intersectInfiniteLines(A.a0, A.a1, B.a0, B.a1))
                centers.push_back(*p);

    // Line(a) × Circle(b).
    for (const auto& A : aLines)
        for (const auto& B : bCircs)
            for (const auto& p :
                 intersectLineCircle(A.a0, A.a1, B.center(), B.radius()))
                centers.push_back(p);

    // Circle(a) × Line(b).
    for (const auto& A : aCircs)
        for (const auto& B : bLines)
            for (const auto& p :
                 intersectLineCircle(B.a0, B.a1, A.center(), A.radius()))
                centers.push_back(p);

    // Circle × Circle.
    for (const auto& A : aCircs)
        for (const auto& B : bCircs)
            for (const auto& p : intersectCircleCircle(
                     A.center(), A.radius(), B.center(), B.radius()))
                centers.push_back(p);

    // Filtra por tangência efetiva às DUAS entidades e escolhe o centro mais
    // próximo de nearPt.
    bool   found = false;
    Point3 best{};
    double bestD = 0.0;
    for (const auto& c : centers) {
        if (!isTangent(a, c, r) || !isTangent(b, c, r)) continue;
        const double d = dist2D(c, nearPt);
        if (!found || d < bestD) {
            found = true;
            best  = c;
            bestD = d;
        }
    }

    if (!found) return std::nullopt;
    return Circle{Point3{best.x, best.y, 0.0}, r};
}

} // namespace cad
