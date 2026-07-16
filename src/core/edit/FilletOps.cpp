// src/core/edit/FilletOps.cpp
#include "core/edit/FilletOps.hpp"

#include "core/geometry/Entity.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/math/Constants.hpp"

#include <cmath>
#include <limits>
#include <vector>

namespace cad {
namespace {

// Tolerância geométrica padrão para comparações no plano XY (mesma de GeometryOps).
constexpr double kEps = 1e-9;

// ----------------------------------------------------------------------------
//  Helpers 2D (plano XY) — idênticos em espírito aos de GeometryOps.cpp.
// ----------------------------------------------------------------------------
inline double dot2D(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y; }
inline double cross2D(const Vec3& a, const Vec3& b) { return a.x * b.y - a.y * b.x; }
inline double length2D(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y); }

inline double dist2D(const Point3& a, const Point3& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Normaliza um vetor no plano XY (z forçado a 0); retorna {0,0,0} se degenerado.
inline Vec3 norm2D(const Vec3& v) {
    const double l = length2D(v);
    return (l > kEps) ? Vec3{v.x / l, v.y / l, 0.0} : Vec3{};
}

// ----------------------------------------------------------------------------
//  LUGAR GEOMÉTRICO (locus) dos possíveis centros do fillet, a distância
//  `radius` de UMA entidade. Para reta -> uma reta paralela deslocada; para
//  arco/círculo -> um círculo concêntrico. Geramos os DOIS loci de cada
//  entidade (os dois lados / interno e externo) e cruzamos todos os pares.
//
//  Cada locus carrega como obter, a partir de um centro candidato C, o ponto de
//  tangência sobre a entidade de origem.
// ----------------------------------------------------------------------------
enum class LocusKind { Line, Circle };

struct Locus {
    LocusKind kind;

    // --- Reta paralela (kind == Line): ponto base + direção unitária. -------
    Point3 lp{};   // um ponto sobre a reta deslocada
    Vec3   ld{};   // direção unitária da reta

    // --- Círculo concêntrico (kind == Circle): centro + raio. ---------------
    Point3 cc{};   // centro (= centro do arco/círculo de origem)
    double cr = 0.0;

    // Origem geométrica (para recuperar a tangência sobre a entidade real):
    // a entidade de arco/círculo tem centro `srcC`; a reta tem ponto/direção
    // base em (lp0, ld) — usamos a própria reta deslocada projetando o centro.
    Point3 srcC{};   // centro da entidade de origem (arco/círculo)
};

// Constrói os dois loci de uma reta: paralelas deslocadas +radius e -radius nas
// duas normais. A tangência depois é o pé da perpendicular do centro à RETA
// ORIGINAL (a reta deslocada é paralela; o pé sobre a original tem a mesma
// projeção).
void lineLoci(const Point3& a, const Point3& b, double radius,
              std::vector<Locus>& out) {
    const Vec3 dir = norm2D(b - a);
    if (length2D(dir) < kEps) return;  // reta degenerada: sem locus
    const Vec3 n{-dir.y, dir.x, 0.0};  // normal unitária à esquerda

    for (double s : {+1.0, -1.0}) {
        Locus L;
        L.kind = LocusKind::Line;
        L.lp   = Point3{a.x + n.x * radius * s, a.y + n.y * radius * s, 0.0};
        L.ld   = dir;
        L.srcC = a;  // ponto base da reta ORIGINAL (para projeção da tangência)
        out.push_back(L);
    }
}

// Constrói os dois loci de um arco/círculo: concêntricos de raio R+radius
// (externo) e |R-radius| (interno). O interno some se for ~0.
void circleLoci(const Point3& c, double R, double radius,
                std::vector<Locus>& out) {
    for (double r : {R + radius, std::fabs(R - radius)}) {
        if (r < kEps) continue;  // círculo degenerado (R == radius no interno)
        Locus L;
        L.kind = LocusKind::Circle;
        L.cc   = c;
        L.cr   = r;
        L.srcC = c;
        out.push_back(L);
    }
}

// Gera os loci de uma Entity concreta (Line/Circle/Arc). Para Arc usa o mesmo
// par de círculos concêntricos do Circle — a validação angular vem depois, no
// ponto de tangência. Retorna false se o tipo não for suportado.
bool entityLoci(const Entity& e, double radius, std::vector<Locus>& out) {
    if (auto* l = dynamic_cast<const Line*>(&e)) {
        lineLoci(l->start(), l->end(), radius, out);
        return true;
    }
    if (auto* c = dynamic_cast<const Circle*>(&e)) {
        circleLoci(c->center(), c->radius(), radius, out);
        return true;
    }
    if (auto* a = dynamic_cast<const Arc*>(&e)) {
        circleLoci(a->center(), a->radius(), radius, out);
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------
//  Interseções entre loci (até 2 pontos por par). Empurram para `out`.
// ----------------------------------------------------------------------------

// Reta × Reta -> 0 ou 1 ponto.
void interLineLine(const Locus& A, const Locus& B, std::vector<Point3>& out) {
    const double denom = cross2D(A.ld, B.ld);
    if (std::fabs(denom) < kEps) return;  // paralelas
    const Vec3 w = B.lp - A.lp;
    const double t = cross2D(w, B.ld) / denom;
    out.push_back(Point3{A.lp.x + A.ld.x * t, A.lp.y + A.ld.y * t, 0.0});
}

// Reta × Círculo -> 0, 1 ou 2 pontos.
void interLineCircle(const Locus& L, const Locus& C, std::vector<Point3>& out) {
    // Projeta o centro do círculo sobre a reta (pé perpendicular).
    const Vec3 w = C.cc - L.lp;
    const double t = dot2D(w, L.ld);                 // ld é unitário
    const Point3 foot{L.lp.x + L.ld.x * t, L.lp.y + L.ld.y * t, 0.0};
    const double d = dist2D(foot, C.cc);             // distância centro->reta
    if (d > C.cr + kEps) return;                     // sem interseção
    double h2 = C.cr * C.cr - d * d;
    if (h2 < 0.0) h2 = 0.0;
    const double h = std::sqrt(h2);
    if (h < kEps) {                                  // tangente: 1 ponto
        out.push_back(foot);
        return;
    }
    out.push_back(Point3{foot.x + L.ld.x * h, foot.y + L.ld.y * h, 0.0});
    out.push_back(Point3{foot.x - L.ld.x * h, foot.y - L.ld.y * h, 0.0});
}

// Círculo × Círculo -> 0, 1 ou 2 pontos.
void interCircleCircle(const Locus& A, const Locus& B, std::vector<Point3>& out) {
    const Vec3 dc = B.cc - A.cc;
    const double d = length2D(dc);
    if (d < kEps) return;                            // concêntricos: 0 ou infinitos
    if (d > A.cr + B.cr + kEps) return;              // longe demais
    if (d < std::fabs(A.cr - B.cr) - kEps) return;   // um dentro do outro
    // a = projeção do ponto-base radical sobre a linha dos centros.
    const double a = (A.cr * A.cr - B.cr * B.cr + d * d) / (2.0 * d);
    double h2 = A.cr * A.cr - a * a;
    if (h2 < 0.0) h2 = 0.0;
    const double h = std::sqrt(h2);
    const Vec3 ud = Vec3{dc.x / d, dc.y / d, 0.0};   // unitário A->B
    const Point3 mid{A.cc.x + ud.x * a, A.cc.y + ud.y * a, 0.0};
    const Vec3 perp{-ud.y, ud.x, 0.0};
    if (h < kEps) {                                  // tangentes: 1 ponto
        out.push_back(mid);
        return;
    }
    out.push_back(Point3{mid.x + perp.x * h, mid.y + perp.y * h, 0.0});
    out.push_back(Point3{mid.x - perp.x * h, mid.y - perp.y * h, 0.0});
}

// Despacha a interseção conforme os tipos dos dois loci.
void intersectLoci(const Locus& A, const Locus& B, std::vector<Point3>& out) {
    if (A.kind == LocusKind::Line && B.kind == LocusKind::Line)
        interLineLine(A, B, out);
    else if (A.kind == LocusKind::Line && B.kind == LocusKind::Circle)
        interLineCircle(A, B, out);
    else if (A.kind == LocusKind::Circle && B.kind == LocusKind::Line)
        interLineCircle(B, A, out);
    else
        interCircleCircle(A, B, out);
}

// ----------------------------------------------------------------------------
//  Tangência: dado o centro do fillet, o ponto de tangência sobre a entidade.
// ----------------------------------------------------------------------------

// Sobre reta: pé da perpendicular do centro à reta ORIGINAL.
Point3 tangentOnLine(const Point3& a, const Point3& b, const Point3& center) {
    const Vec3 dir = norm2D(b - a);
    const Vec3 w   = center - a;
    const double t = dot2D(w, dir);
    return Point3{a.x + dir.x * t, a.y + dir.y * t, 0.0};
}

// Sobre arco/círculo de centro C e raio R: o ponto sobre o círculo na direção
// de C para o centro do fillet. Vale tanto para tangência externa (centro do
// fillet a R+radius, ponto entre C e centro) quanto interna (centro do fillet a
// |R-radius|): em ambos os casos a tangência está na reta C->center, e o ponto
// do círculo nessa direção é C + R*normalize(center - C). Quando o fillet
// envolve o círculo por dentro (radius > R, centro do fillet do lado oposto), a
// direção C->center continua apontando para o ponto de tangência correto.
Point3 tangentOnCircle(const Point3& C, double R, const Point3& center) {
    const Vec3 u = norm2D(center - C);
    if (length2D(u) < kEps) return C;  // center coincide com C (degenerado)
    return Point3{C.x + u.x * R, C.y + u.y * R, 0.0};
}

// Verifica se um ângulo `ang` (rad) cai dentro da abertura CCW [start,end] de
// um arco. Normaliza tudo para [0, 2pi).
bool angleInArc(double ang, double start, double end) {
    auto wrap = [](double a) {
        while (a < 0.0)      a += kTwoPi;
        while (a >= kTwoPi)  a -= kTwoPi;
        return a;
    };
    const double a  = wrap(ang);
    const double s  = wrap(start);
    double       sw = end - start;              // varredura CCW do arco
    while (sw < 0.0)      sw += kTwoPi;
    while (sw > kTwoPi)   sw -= kTwoPi;
    double rel = a - s;
    while (rel < 0.0)     rel += kTwoPi;
    return rel <= sw + kEps;
}

// Se a entidade for Arc, valida que o ponto de tangência cai dentro da abertura
// angular. Para Line/Circle sempre aceita (fillet pode estender a reta).
bool tangentValidOnEntity(const Entity& e, const Point3& tan) {
    if (auto* a = dynamic_cast<const Arc*>(&e)) {
        const double ang = std::atan2(tan.y - a->center().y,
                                      tan.x - a->center().x);
        return angleInArc(ang, a->startAngle(), a->endAngle());
    }
    return true;  // Line e Circle: sem restrição angular
}

} // namespace

// ============================================================================
//  filletGeometry — implementação.
//
//  Estratégia (robusta, por offsets):
//   1. Gera os loci de centros equidistantes `radius` de cada entidade
//      (reta -> 2 paralelas; arco/círculo -> 2 concêntricos).
//   2. Cruza TODOS os pares de loci (e1 × e2); cada interseção é um centro
//      candidato do fillet.
//   3. Para cada candidato, calcula a tangência em cada entidade e valida a
//      abertura angular (se for arco).
//   4. ESCOLHE o candidato que MINIMIZA dist(center,pick1)+dist(center,pick2)
//      — ou seja, o canto que o usuário indicou com os picks.
//   5. Monta startAngle/endAngle do arco MENOR (sweep <= pi) varrendo de
//      tan1 -> tan2 no sentido CCW (convenção idêntica à de Arc).
// ============================================================================
FilletGeom filletGeometry(const Entity& e1, const Entity& e2, double radius,
                          const Point3& pick1, const Point3& pick2) {
    FilletGeom res;
    if (radius <= kEps) return res;  // raio inválido

    // (1) loci de cada entidade.
    std::vector<Locus> L1, L2;
    if (!entityLoci(e1, radius, L1)) return res;  // tipo não suportado
    if (!entityLoci(e2, radius, L2)) return res;
    if (L1.empty() || L2.empty()) return res;     // entidade degenerada

    // (2)+(3)+(4) cruza pares, valida e escolhe o melhor centro.
    double bestScore = std::numeric_limits<double>::infinity();
    Point3 bestCenter{};
    Point3 bestTan1{}, bestTan2{};
    bool   found = false;

    std::vector<Point3> hits;
    for (const Locus& a : L1) {
        for (const Locus& b : L2) {
            hits.clear();
            intersectLoci(a, b, hits);
            for (const Point3& C : hits) {
                // Tangências sobre as ENTIDADES reais (não sobre os loci).
                Point3 t1, t2;
                if (auto* l = dynamic_cast<const Line*>(&e1))
                    t1 = tangentOnLine(l->start(), l->end(), C);
                else if (auto* c = dynamic_cast<const Circle*>(&e1))
                    t1 = tangentOnCircle(c->center(), c->radius(), C);
                else if (auto* ar = dynamic_cast<const Arc*>(&e1))
                    t1 = tangentOnCircle(ar->center(), ar->radius(), C);
                else
                    continue;

                if (auto* l = dynamic_cast<const Line*>(&e2))
                    t2 = tangentOnLine(l->start(), l->end(), C);
                else if (auto* c = dynamic_cast<const Circle*>(&e2))
                    t2 = tangentOnCircle(c->center(), c->radius(), C);
                else if (auto* ar = dynamic_cast<const Arc*>(&e2))
                    t2 = tangentOnCircle(ar->center(), ar->radius(), C);
                else
                    continue;

                // Validação angular para arcos.
                if (!tangentValidOnEntity(e1, t1)) continue;
                if (!tangentValidOnEntity(e2, t2)) continue;

                // Score: soma das distâncias aos picks (canto indicado).
                const double score = dist2D(C, pick1) + dist2D(C, pick2);
                if (score < bestScore) {
                    bestScore  = score;
                    bestCenter = C;
                    bestTan1   = t1;
                    bestTan2   = t2;
                    found      = true;
                }
            }
        }
    }

    if (!found) return res;  // nenhuma concordância possível com esse raio

    // (5) ângulos do arco MENOR varrendo CCW de tan1 -> tan2.
    const double ang1 = std::atan2(bestTan1.y - bestCenter.y,
                                   bestTan1.x - bestCenter.x);
    const double ang2 = std::atan2(bestTan2.y - bestCenter.y,
                                   bestTan2.x - bestCenter.x);

    // Varredura CCW de ang1 a ang2; se > pi, o arco curto é o complemento
    // (inverte a ordem para varrer ang2 -> ang1 e manter sweep <= pi).
    double start = ang1, end = ang2;
    double sweep = end - start;
    while (sweep <= -kPi) sweep += kTwoPi;
    while (sweep >   kPi) sweep -= kTwoPi;
    if (sweep < 0.0) { start = ang2; end = ang1; }  // garante varredura CCW curta

    res.ok         = true;
    res.center     = bestCenter;
    res.radius     = radius;
    res.tan1       = bestTan1;
    res.tan2       = bestTan2;
    res.startAngle = start;
    res.endAngle   = end;
    return res;
}

std::unique_ptr<Entity> trimToTangent(const Entity& e, const Point3& tan, const Point3& pick) {
    if (const auto* l = dynamic_cast<const Line*>(&e)) {
        const Point3 a = l->start(), b = l->end();
        const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
        if (len2 < kEps) return nullptr;
        auto tOf = [&](const Point3& p) { return ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2; };
        return (tOf(pick) <= tOf(tan)) ? std::make_unique<Line>(a, tan)   // pick do lado de A
                                       : std::make_unique<Line>(tan, b);
    }
    if (const auto* c = dynamic_cast<const Circle*>(&e)) {
        return std::make_unique<Circle>(*c);   // fillet não apara círculo
    }
    if (const auto* ar = dynamic_cast<const Arc*>(&e)) {
        const Point3 ctr = ar->center();
        const double r = ar->radius();
        auto norm = [](double x) { x = std::fmod(x, kTwoPi); if (x < 0) x += kTwoPi; return x; };
        const double dTan  = norm(std::atan2(tan.y - ctr.y, tan.x - ctr.x) - ar->startAngle());
        const double dPick = norm(std::atan2(pick.y - ctr.y, pick.x - ctr.x) - ar->startAngle());
        return (dPick <= dTan)
            ? std::make_unique<Arc>(ctr, r, ar->startAngle(), ar->startAngle() + dTan)
            : std::make_unique<Arc>(ctr, r, ar->startAngle() + dTan, ar->endAngle());
    }
    return nullptr;
}

} // namespace cad
