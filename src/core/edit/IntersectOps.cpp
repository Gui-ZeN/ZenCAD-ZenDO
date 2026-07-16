// src/core/edit/IntersectOps.cpp
#include "core/edit/IntersectOps.hpp"

#include <cmath>
#include <optional>
#include <vector>

#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/XLine.hpp"
#include "core/geometry/RayLine.hpp"
#include "core/math/Vec.hpp"
#include "core/math/AABB.hpp"
#include "core/math/Constants.hpp"

namespace cad {
namespace {

// Tolerância numérica geral (paralelismo, tangência, pertinência).
constexpr double kEps = 1e-9;

// ----------------------------------------------------------------------------
//  Utilitários 2D (todo o trabalho ocorre no plano XY; z é ignorado/zerado).
// ----------------------------------------------------------------------------

// Produto vetorial 2D (componente z do cross) — sinal indica orientação.
inline double cross2(const Vec3& a, const Vec3& b) {
    return a.x * b.y - a.y * b.x;
}

// Normaliza um ângulo qualquer para o intervalo [0, 2π).
inline double normAngle(double a) {
    double r = std::fmod(a, kTwoPi);
    if (r < 0.0) r += kTwoPi;
    return r;
}

// Testa se 'p' (já garantido sobre a reta-suporte) cai no segmento [a,b].
// Usa projeção paramétrica com folga de tolerância nas extremidades.
bool pointOnSegment(const Vec3& p, const Vec3& a, const Vec3& b) {
    const Vec3 ab = b - a;
    const double lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < kEps * kEps) {
        // Segmento degenerado (a ~= b): só vale se p coincide com a.
        const Vec3 d = p - a;
        return (d.x * d.x + d.y * d.y) <= kEps * kEps;
    }
    const Vec3 ap = p - a;
    const double t = (ap.x * ab.x + ap.y * ab.y) / lenSq;
    return t >= -kEps && t <= 1.0 + kEps;
}

// Testa se o ângulo 'ang' cai dentro da abertura CCW do arco [start, end].
// Todos os ângulos são normalizados para [0, 2π) antes da comparação.
bool angleInArcSweep(double ang, double start, double end) {
    const double a = normAngle(ang);
    const double s = normAngle(start);
    double sweep = normAngle(end - start);
    // Arco de varredura completa (start ~= end): aceita qualquer ângulo.
    if (sweep < kEps) sweep = kTwoPi;
    double rel = normAngle(a - s);
    // Folga angular nas extremidades.
    return rel <= sweep + kEps || rel >= kTwoPi - kEps;
}

// Ângulo de um ponto 'p' em relação ao centro 'c' (medido a partir de +X).
inline double angleOf(const Vec3& p, const Vec3& c) {
    return std::atan2(p.y - c.y, p.x - c.x);
}

// Igualdade aproximada de pontos no plano XY.
bool nearlyEqual(const Vec3& a, const Vec3& b) {
    const double dx = a.x - b.x, dy = a.y - b.y;
    return (dx * dx + dy * dy) <= kEps * kEps;
}

// Acrescenta 'p' a 'out' evitando duplicatas (tangências/pontos coincidentes).
void pushUnique(std::vector<Point3>& out, const Vec3& p) {
    for (const auto& q : out)
        if (nearlyEqual(q, p)) return;
    out.push_back(p);
}

// ----------------------------------------------------------------------------
//  Primitivas de interseção (geometria pura, sem fronteira de arco).
// ----------------------------------------------------------------------------

// Segmento × segmento — 0 ou 1 ponto. (Casos colineares sobrepostos não geram
// um ponto único bem definido; são ignorados, coerente com "ponto de interseção".)
std::optional<Vec3> segSegIntersect(const Vec3& p0, const Vec3& p1,
                                     const Vec3& q0, const Vec3& q1) {
    const Vec3 r = p1 - p0;
    const Vec3 s = q1 - q0;
    const double rxs = cross2(r, s);
    if (std::fabs(rxs) < kEps) return std::nullopt;  // paralelos/colineares
    const Vec3 qp = q0 - p0;
    const double t = cross2(qp, s) / rxs;
    const double u = cross2(qp, r) / rxs;
    if (t < -kEps || t > 1.0 + kEps) return std::nullopt;
    if (u < -kEps || u > 1.0 + kEps) return std::nullopt;
    return Vec3{p0.x + t * r.x, p0.y + t * r.y, 0.0};
}

// Reta-suporte (infinita) do segmento [p0,p1] × círculo (centro c, raio rad).
// Devolve 0, 1 ou 2 pontos da reta infinita; o filtro pelo segmento é externo.
std::vector<Vec3> lineCircleRaw(const Vec3& p0, const Vec3& p1,
                                const Vec3& c, double rad) {
    std::vector<Vec3> pts;
    const Vec3 d = p1 - p0;          // direção do segmento
    const Vec3 f = p0 - c;           // origem relativa ao centro
    const double a = d.x * d.x + d.y * d.y;
    if (a < kEps * kEps) return pts; // segmento degenerado
    const double b = 2.0 * (f.x * d.x + f.y * d.y);
    const double cc = (f.x * f.x + f.y * f.y) - rad * rad;
    double disc = b * b - 4.0 * a * cc;
    if (disc < -kEps) return pts;    // sem interseção
    if (disc < 0.0) disc = 0.0;      // tangente numérica
    const double sq = std::sqrt(disc);
    const double t1 = (-b - sq) / (2.0 * a);
    pts.push_back(Vec3{p0.x + t1 * d.x, p0.y + t1 * d.y, 0.0});
    if (sq > kEps) {                 // duas raízes distintas
        const double t2 = (-b + sq) / (2.0 * a);
        pts.push_back(Vec3{p0.x + t2 * d.x, p0.y + t2 * d.y, 0.0});
    }
    return pts;
}

// Círculo × círculo — 0, 1 ou 2 pontos (geometria pura, sem fronteira).
std::vector<Vec3> circleCircleRaw(const Vec3& c0, double r0,
                                  const Vec3& c1, double r1) {
    std::vector<Vec3> pts;
    const Vec3 d = c1 - c0;
    const double dist = std::sqrt(d.x * d.x + d.y * d.y);
    if (dist < kEps) return pts;             // concêntricos (iguais ou disjuntos)
    if (dist > r0 + r1 + kEps) return pts;   // muito distantes
    if (dist < std::fabs(r0 - r1) - kEps) return pts; // um dentro do outro
    // Distância do centro c0 até a linha radical.
    const double a = (r0 * r0 - r1 * r1 + dist * dist) / (2.0 * dist);
    double h2 = r0 * r0 - a * a;
    if (h2 < 0.0) h2 = 0.0;                   // tangência numérica
    const double h = std::sqrt(h2);
    // Ponto-base sobre a linha dos centros.
    const Vec3 pm{c0.x + a * d.x / dist, c0.y + a * d.y / dist, 0.0};
    if (h < kEps) {                           // tangentes: 1 ponto
        pts.push_back(pm);
        return pts;
    }
    // Perpendicular unitária à linha dos centros.
    const double ox = -d.y / dist * h;
    const double oy =  d.x / dist * h;
    pts.push_back(Vec3{pm.x + ox, pm.y + oy, 0.0});
    pts.push_back(Vec3{pm.x - ox, pm.y - oy, 0.0});
    return pts;
}

// ----------------------------------------------------------------------------
//  Pares filtrados pelas fronteiras (segmento finito / abertura de arco).
// ----------------------------------------------------------------------------

void lineLine(const Line& l0, const Line& l1, std::vector<Point3>& out) {
    if (auto p = segSegIntersect(l0.start(), l0.end(), l1.start(), l1.end()))
        pushUnique(out, *p);
}

void lineCircle(const Line& l, const Circle& c, std::vector<Point3>& out) {
    for (const auto& p : lineCircleRaw(l.start(), l.end(), c.center(), c.radius()))
        if (pointOnSegment(p, l.start(), l.end()))
            pushUnique(out, p);
}

void lineArc(const Line& l, const Arc& arc, std::vector<Point3>& out) {
    for (const auto& p : lineCircleRaw(l.start(), l.end(), arc.center(), arc.radius())) {
        if (!pointOnSegment(p, l.start(), l.end())) continue;
        if (angleInArcSweep(angleOf(p, arc.center()), arc.startAngle(), arc.endAngle()))
            pushUnique(out, p);
    }
}

void circleCircle(const Circle& a, const Circle& b, std::vector<Point3>& out) {
    for (const auto& p : circleCircleRaw(a.center(), a.radius(), b.center(), b.radius()))
        pushUnique(out, p);
}

void circleArc(const Circle& c, const Arc& arc, std::vector<Point3>& out) {
    for (const auto& p : circleCircleRaw(c.center(), c.radius(), arc.center(), arc.radius()))
        if (angleInArcSweep(angleOf(p, arc.center()), arc.startAngle(), arc.endAngle()))
            pushUnique(out, p);
}

void arcArc(const Arc& a, const Arc& b, std::vector<Point3>& out) {
    for (const auto& p : circleCircleRaw(a.center(), a.radius(), b.center(), b.radius())) {
        if (!angleInArcSweep(angleOf(p, a.center()), a.startAngle(), a.endAngle())) continue;
        if (!angleInArcSweep(angleOf(p, b.center()), b.startAngle(), b.endAngle())) continue;
        pushUnique(out, p);
    }
}

// ----------------------------------------------------------------------------
//  Interseção de uma ARESTA de polilinha (segmento Line) contra outra entidade.
//  Reaproveita os pares Line×(…) acima.
// ----------------------------------------------------------------------------
void edgeVsEntity(const Line& edge, const Entity& other, std::vector<Point3>& out) {
    if (auto* l = dynamic_cast<const Line*>(&other))   { lineLine(edge, *l, out);   return; }
    if (auto* c = dynamic_cast<const Circle*>(&other)) { lineCircle(edge, *c, out); return; }
    if (auto* a = dynamic_cast<const Arc*>(&other))    { lineArc(edge, *a, out);    return; }
    // other == Polyline é tratado no nível superior (dupla iteração de arestas).
}

// Itera as arestas de 'poly' como segmentos Line e acumula interseções contra
// 'other'. Se 'other' também for polilinha, cada aresta dela vira um Line.
void polylineVsEntity(const Polyline& poly, const Entity& other,
                      std::vector<Point3>& out) {
    const auto& v = poly.vertices();
    if (v.size() < 2) return;
    const std::size_t n = v.size();
    const std::size_t edges = poly.closed() ? n : n - 1;

    const auto* otherPoly = dynamic_cast<const Polyline*>(&other);

    for (std::size_t i = 0; i < edges; ++i) {
        const Line edge(v[i], v[(i + 1) % n]);
        if (otherPoly) {
            const auto& w = otherPoly->vertices();
            if (w.size() < 2) continue;
            const std::size_t m = w.size();
            const std::size_t oedges = otherPoly->closed() ? m : m - 1;
            for (std::size_t j = 0; j < oedges; ++j) {
                const Line e2(w[j], w[(j + 1) % m]);
                lineLine(edge, e2, out);
            }
        } else {
            edgeVsEntity(edge, other, out);
        }
    }
}

} // namespace anônimo

// ----------------------------------------------------------------------------
//  API principal — despacho por tipo concreto via dynamic_cast.
//  Trata simetricamente (a,b) e (b,a); ordem dos argumentos é irrelevante.
// ----------------------------------------------------------------------------
// XLine/RayLine viram uma Line FINITA do tamanho do outro operando (escala da
// cena, sem 1e6) para entrar no mesmo cálculo — assim retas/raios de construção
// funcionam como aresta de corte e geram interseções de snap.
static std::optional<Line> asFiniteLine(const Entity& e, const AABB& bb) {
    const Point3 bc{(bb.min.x + bb.max.x) * 0.5, (bb.min.y + bb.max.y) * 0.5, 0.0};
    const double diag = std::hypot(bb.max.x - bb.min.x, bb.max.y - bb.min.y);
    if (const auto* xl = dynamic_cast<const XLine*>(&e)) {
        const Point3 p = xl->base(); const Vec3 d = xl->dir();
        const double R = std::hypot(p.x - bc.x, p.y - bc.y) + diag + 1.0;
        return Line{Point3{p.x - d.x * R, p.y - d.y * R, 0.0},
                    Point3{p.x + d.x * R, p.y + d.y * R, 0.0}};
    }
    if (const auto* ry = dynamic_cast<const RayLine*>(&e)) {
        const Point3 p = ry->base(); const Vec3 d = ry->dir();
        const double R = std::hypot(p.x - bc.x, p.y - bc.y) + diag + 1.0;
        return Line{p, Point3{p.x + d.x * R, p.y + d.y * R, 0.0}};   // só p/ frente
    }
    return std::nullopt;
}

std::vector<Point3> intersectEntities(const Entity& a, const Entity& b) {
    std::vector<Point3> out;

    // Retas/raios de construção: converte para Line finita e refaz.
    if (auto la = asFiniteLine(a, b.boundingBox())) return intersectEntities(*la, b);
    if (auto lb = asFiniteLine(b, a.boundingBox())) return intersectEntities(a, *lb);

    // Polilinha em qualquer posição → decompõe em arestas.
    if (auto* pa = dynamic_cast<const Polyline*>(&a)) {
        polylineVsEntity(*pa, b, out);
        return out;
    }
    if (auto* pb = dynamic_cast<const Polyline*>(&b)) {
        polylineVsEntity(*pb, a, out);
        return out;
    }

    // Classifica os dois operandos.
    const auto* la = dynamic_cast<const Line*>(&a);
    const auto* ca = dynamic_cast<const Circle*>(&a);
    const auto* aa = dynamic_cast<const Arc*>(&a);
    const auto* lb = dynamic_cast<const Line*>(&b);
    const auto* cb = dynamic_cast<const Circle*>(&b);
    const auto* ab = dynamic_cast<const Arc*>(&b);

    // Line × ...
    if (la && lb) { lineLine(*la, *lb, out);   return out; }
    if (la && cb) { lineCircle(*la, *cb, out); return out; }
    if (la && ab) { lineArc(*la, *ab, out);    return out; }
    if (lb && ca) { lineCircle(*lb, *ca, out); return out; }
    if (lb && aa) { lineArc(*lb, *aa, out);    return out; }

    // Circle × ...
    if (ca && cb) { circleCircle(*ca, *cb, out); return out; }
    if (ca && ab) { circleArc(*ca, *ab, out);    return out; }
    if (cb && aa) { circleArc(*cb, *aa, out);    return out; }

    // Arc × Arc
    if (aa && ab) { arcArc(*aa, *ab, out); return out; }

    // Par de tipos não suportado.
    return out;
}

} // namespace cad
