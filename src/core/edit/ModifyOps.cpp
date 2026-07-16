// src/core/edit/ModifyOps.cpp
#include "core/edit/ModifyOps.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/math/Segment.hpp"
#include <algorithm>
#include <cmath>

namespace cad {
namespace {

// Tolerância de comprimento: trechos mais curtos que isto são considerados nulos.
constexpr double kLenEps = 1e-9;

// Direção 2D (no plano XY) de a -> b. Z é ignorado.
inline Vec3 dir2D(const Point3& a, const Point3& b) {
    return Vec3{b.x - a.x, b.y - a.y, 0.0};
}

// Projeção escalar do ponto p sobre o eixo que passa por origin com direção d
// (no plano XY). Retorna a coordenada paramétrica t tal que o pé da projeção é
// origin + t * d_normalizado. Assume d com comprimento > 0.
inline double projectScalar2D(const Point3& p, const Point3& origin, const Vec3& d) {
    const double lenSq = d.x * d.x + d.y * d.y;
    if (lenSq <= 0.0) return 0.0;
    const double dot = (p.x - origin.x) * d.x + (p.y - origin.y) * d.y;
    return dot / std::sqrt(lenSq); // distância com sinal ao longo de d.
}

// Ponto sobre o eixo (origin, dHat) a uma distância com sinal s da origem.
// dHat deve estar normalizado (no plano XY).
inline Point3 pointAtScalar2D(const Point3& origin, const Vec3& dHat, double s) {
    return Point3{origin.x + dHat.x * s, origin.y + dHat.y * s, 0.0};
}

// Comprimento 2D (XY) entre dois pontos.
inline double dist2D(const Point3& a, const Point3& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

std::vector<Line> breakLine(const Line& l, const Point3& p1, const Point3& p2) {
    std::vector<Line> out;

    const Point3& a = l.start();
    const Point3& b = l.end();

    // Linha degenerada: nada que quebrar de forma significativa.
    if (dist2D(a, b) <= kLenEps) return out;

    // Projeta p1 e p2 sobre o segmento (já fixados ao intervalo pela função).
    const Point3 c1 = closestPointOnSegment2D(p1, a, b);
    const Point3 c2 = closestPointOnSegment2D(p2, a, b);

    // Eixo paramétrico ao longo da linha (com sinal, a partir de a).
    const Vec3 d = dir2D(a, b);
    double s1 = projectScalar2D(c1, a, d);
    double s2 = projectScalar2D(c2, a, d);
    if (s1 > s2) std::swap(s1, s2); // garante s1 <= s2 (intervalo a remover).

    const Vec3 dHat = d.normalized();

    // Parte que sobra ANTES do intervalo removido: de a até c1.
    const Point3 lo = pointAtScalar2D(a, dHat, s1);
    if (dist2D(a, lo) > kLenEps) out.emplace_back(a, lo);

    // Parte que sobra DEPOIS do intervalo removido: de c2 até b.
    const Point3 hi = pointAtScalar2D(a, dHat, s2);
    if (dist2D(hi, b) > kLenEps) out.emplace_back(hi, b);

    return out;
}

std::optional<Line> joinLines(const Line& a, const Line& b, double tol) {
    const Vec3 da = dir2D(a.start(), a.end());
    const Vec3 db = dir2D(b.start(), b.end());

    const double lenA = std::sqrt(da.x * da.x + da.y * da.y);
    const double lenB = std::sqrt(db.x * db.x + db.y * db.y);
    if (lenA <= kLenEps || lenB <= kLenEps) return std::nullopt; // degenerada.

    const Vec3 ua = da.normalized();
    const Vec3 ub = db.normalized();

    // Colinearidade angular: as direções devem ser paralelas (mesma ou oposta),
    // logo o componente z do produto vetorial 2D deve ser ~0 (dentro de tol).
    const double crossZ = ua.x * ub.y - ua.y * ub.x;
    if (std::abs(crossZ) > tol) return std::nullopt;

    // Mesma RETA: o vetor que liga as origens deve ser paralelo às direções,
    // isto é, b.start() não pode estar deslocado lateralmente de a.
    const Vec3 between = dir2D(a.start(), b.start());
    const double offset = ua.x * between.y - ua.y * between.x; // distância lateral.
    if (std::abs(offset) > tol) return std::nullopt;

    // Projeta os quatro extremos no eixo comum (origem a.start(), direção ua) e
    // verifica se os dois segmentos se tocam/encostam/sobrepõem (sem buraco).
    const double a0 = projectScalar2D(a.start(), a.start(), ua); // == 0
    const double a1 = projectScalar2D(a.end(),   a.start(), ua);
    const double b0 = projectScalar2D(b.start(), a.start(), ua);
    const double b1 = projectScalar2D(b.end(),   a.start(), ua);

    const double aMin = std::min(a0, a1), aMax = std::max(a0, a1);
    const double bMin = std::min(b0, b1), bMax = std::max(b0, b1);

    // Lacuna entre os intervalos [aMin,aMax] e [bMin,bMax]: se for positiva além
    // da tolerância, as linhas não se encostam -> não há junção.
    const double gap = std::max(aMin, bMin) - std::min(aMax, bMax);
    if (gap > tol) return std::nullopt;

    // Linha resultante: do extremo mais distante ao outro, ao longo de ua.
    const double lo = std::min(aMin, bMin);
    const double hi = std::max(aMax, bMax);
    return Line{pointAtScalar2D(a.start(), ua, lo),
                pointAtScalar2D(a.start(), ua, hi)};
}

Line lengthenLine(const Line& l, double delta, bool fromEnd) {
    const Point3& a = l.start();
    const Point3& b = l.end();

    const double len = dist2D(a, b);
    if (len <= kLenEps) return l; // sem direção definida; nada a fazer.

    // Comprimento alvo, fixado num piso ~0 para não inverter a linha.
    double newLen = len + delta;
    if (newLen < kLenEps) newLen = kLenEps;

    const Vec3 dHat = dir2D(a, b).normalized();

    if (fromEnd) {
        // Move a ponta end() ao longo da direção, mantendo start() fixo.
        return Line{a, pointAtScalar2D(a, dHat, newLen)};
    }
    // Move a ponta start() ao longo da direção oposta, mantendo end() fixo.
    return Line{pointAtScalar2D(b, dHat, -newLen), b};
}

Arc lengthenArc(const Arc& a, double delta, bool fromEnd) {
    const double r = a.radius();
    if (r <= kLenEps) return a;
    const double dAng = delta / r;     // comprimento de arco -> ângulo
    if (fromEnd) return Arc{a.center(), r, a.startAngle(), a.endAngle() + dAng};
    return Arc{a.center(), r, a.startAngle() - dAng, a.endAngle()};
}

std::unique_ptr<Entity> joinEntities(const Entity& a, const Entity& b, double tol) {
    const auto* la = dynamic_cast<const Line*>(&a);
    const auto* lb = dynamic_cast<const Line*>(&b);
    if (la && lb) {
        if (const std::optional<Line> merged = joinLines(*la, *lb, tol))
            return std::make_unique<Line>(*merged);          // colineares -> 1 linha
        const Point3 A1 = la->start(), A2 = la->end(), B1 = lb->start(), B2 = lb->end();
        auto near = [&](const Point3& p, const Point3& q) {
            return std::hypot(p.x - q.x, p.y - q.y) <= tol;
        };
        std::vector<Point3> v;                               // extremo comum = vértice do meio
        if      (near(A2, B1)) v = {A1, A2, B2};
        else if (near(A2, B2)) v = {A1, A2, B1};
        else if (near(A1, B1)) v = {A2, A1, B2};
        else if (near(A1, B2)) v = {A2, A1, B1};
        else return nullptr;                                 // não se tocam
        return std::make_unique<Polyline>(v, false);
    }
    return nullptr;   // arcos/outros: ainda não
}

} // namespace cad
