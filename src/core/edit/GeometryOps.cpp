// src/core/edit/GeometryOps.cpp
#include "core/edit/GeometryOps.hpp"
#include "core/math/Constants.hpp"

#include <cmath>

namespace cad {
namespace {

// Tolerância geométrica padrão para comparações no plano XY.
constexpr double kEps = 1e-9;

// Produto vetorial 2D (componente z do cross 3D): a.x*b.y - a.y*b.x.
inline double cross2D(const Vec3& a, const Vec3& b) {
    return a.x * b.y - a.y * b.x;
}

// Produto escalar restrito ao plano XY.
inline double dot2D(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y;
}

// Comprimento no plano XY.
inline double length2D(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

// Lado de p em relação à reta orientada a->b: >0 à esquerda, <0 à direita.
inline double sideOfLine(const Point3& a, const Point3& b, const Point3& p) {
    return cross2D(b - a, p - a);
}

} // namespace

// Interseção das duas retas infinitas a0–a1 e b0–b1 (ver header).
std::optional<Point3> intersectInfiniteLines(const Point3& a0, const Point3& a1,
                                             const Point3& b0, const Point3& b1) {
    const Vec3   da = a1 - a0;           // direção da reta A
    const Vec3   db = b1 - b0;           // direção da reta B
    const double denom = cross2D(da, db);
    if (std::fabs(denom) < kEps) return std::nullopt;  // paralelas/degeneradas

    // a0 + t*da = b0 + s*db  ->  t = cross(b0-a0, db) / cross(da, db).
    const double t = cross2D(b0 - a0, db) / denom;
    return Point3{a0.x + da.x * t, a0.y + da.y * t, 0.0};
}

// Desloca a linha perpendicularmente para o lado do ponto side (ver header).
Line offsetLine(const Line& src, double dist, const Point3& side) {
    const Point3& p0 = src.start();
    const Point3& p1 = src.end();
    const Vec3    dir = p1 - p0;
    const double  len = length2D(dir);

    // Linha degenerada (pontos coincidentes): nada a deslocar.
    if (len < kEps) return Line{p0, p1};

    // Normal unitária à esquerda da direção p0->p1.
    Vec3 n{-dir.y / len, dir.x / len, 0.0};

    // Se side estiver à direita (sideOfLine < 0), inverte a normal.
    if (sideOfLine(p0, p1, side) < 0.0) n = n * -1.0;

    const Vec3 d = n * dist;
    return Line{Point3{p0.x + d.x, p0.y + d.y, 0.0},
                Point3{p1.x + d.x, p1.y + d.y, 0.0}};
}

// Desloca o círculo: raio +dist se side fora, -dist se dentro (ver header).
Circle offsetCircle(const Circle& src, double dist, const Point3& side) {
    const double dx = side.x - src.center().x;
    const double dy = side.y - src.center().y;
    const double distToSide = std::sqrt(dx * dx + dy * dy);

    // side fora (>= raio) -> aumenta; side dentro (< raio) -> diminui.
    double r = (distToSide >= src.radius()) ? src.radius() + dist
                                            : src.radius() - dist;
    if (r < 0.0) r = 0.0;  // raio nunca negativo
    return Circle{src.center(), r};
}

Arc offsetArc(const Arc& src, double dist, const Point3& side) {
    const double dx = side.x - src.center().x;
    const double dy = side.y - src.center().y;
    const double distToSide = std::sqrt(dx * dx + dy * dy);
    double r = (distToSide >= src.radius()) ? src.radius() + dist
                                            : src.radius() - dist;
    if (r < 0.0) r = 0.0;
    return Arc{src.center(), r, src.startAngle(), src.endAngle()};  // concêntrico, ângulos iguais
}

Polyline offsetPolyline(const Polyline& src, double dist, const Point3& side) {
    const std::vector<Point3>& v = src.vertices();
    const std::size_t n = v.size();
    if (n < 2) return src;
    const bool closed = src.closed();

    auto leftN = [](const Point3& a, const Point3& b) -> Vec3 {
        const Vec3 d{b.x - a.x, b.y - a.y, 0.0};
        const double l = std::sqrt(d.x * d.x + d.y * d.y);
        return l < kEps ? Vec3{0, 0, 0} : Vec3{-d.y / l, d.x / l, 0.0};  // normal à esquerda
    };
    // Sinal (lado) coerente para TODA a polilinha. Para polígono FECHADO usa
    // ponto-em-polígono + winding (clicar FORA expande, DENTRO contrai); para
    // aberta, o lado do 1º segmento. (Decidir só pelo 1º segmento invertia quando
    // o clique caía fora do polígono mas no lado "interno" da 1ª aresta.)
    double sgn;
    if (closed) {
        double a2 = 0.0;                                   // área com sinal (>0 = CCW)
        for (std::size_t i = 0; i < n; ++i) {
            const Point3& a = v[i];
            const Point3& b = v[(i + 1) % n];
            a2 += a.x * b.y - b.x * a.y;
        }
        bool inside = false;                               // ray casting
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const bool cross = (v[i].y > side.y) != (v[j].y > side.y);
            if (cross && side.x < (v[j].x - v[i].x) * (side.y - v[i].y) /
                                      (v[j].y - v[i].y) + v[i].x)
                inside = !inside;
        }
        const double expandSgn = (a2 > 0.0) ? -1.0 : 1.0;  // sgn*normalEsq aponta p/ FORA
        sgn = inside ? -expandSgn : expandSgn;
    } else {
        sgn = (sideOfLine(v[0], v[1], side) < 0.0) ? -1.0 : 1.0;
    }

    std::vector<Point3> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3 nIn  = (i > 0)   ? leftN(v[i - 1], v[i])
                        : closed    ? leftN(v[n - 1], v[0]) : Vec3{0, 0, 0};
        const Vec3 nOut = (i + 1 < n) ? leftN(v[i], v[i + 1])
                        : closed    ? leftN(v[n - 1], v[0]) : Vec3{0, 0, 0};
        Vec3 nm; double scale = dist;
        if (!closed && (i == 0 || i == n - 1)) {
            nm = (i == 0) ? nOut : nIn;                 // ponta aberta: normal do trecho da ponta
        } else {
            const Vec3 s{nIn.x + nOut.x, nIn.y + nOut.y, 0.0};
            const double sl = std::sqrt(s.x * s.x + s.y * s.y);
            if (sl < kEps) { nm = nIn; }                // inversão ~180°
            else {
                nm = Vec3{s.x / sl, s.y / sl, 0.0};
                const double c = nm.x * nIn.x + nm.y * nIn.y;   // cos do meio-ângulo
                if (std::fabs(c) > 1e-3) scale = dist / c;      // miter (compensa a quina)
            }
        }
        out[i] = Point3{v[i].x + sgn * nm.x * scale, v[i].y + sgn * nm.y * scale, 0.0};
    }
    Polyline r(out, src.bulges(), closed);   // bulges preservados (offset mantém o ângulo)
    r.setWidth(src.width());
    return r;
}

// Corta a linha target na reta de corte, removendo o lado de pick (ver header).
std::optional<Line> trimLineAt(const Line& target, const Point3& cutA,
                               const Point3& cutB, const Point3& pick) {
    const Point3& a = target.start();
    const Point3& b = target.end();

    const auto x = intersectInfiniteLines(a, b, cutA, cutB);
    if (!x) return std::nullopt;  // sem interseção (paralelas)

    const Vec3   dir   = b - a;
    const double lenSq = dir.x * dir.x + dir.y * dir.y;
    if (lenSq < kEps) return std::nullopt;  // alvo degenerado

    // Parâmetro t da interseção ao longo de a->b; precisa cair em (0,1) para
    // que haja de fato algo a cortar dentro do segmento alvo.
    const double t = ((x->x - a.x) * dir.x + (x->y - a.y) * dir.y) / lenSq;
    if (t <= kEps || t >= 1.0 - kEps) return std::nullopt;

    // Parâmetro do ponto pick projetado no alvo: define qual metade descartar.
    const double tp = ((pick.x - a.x) * dir.x + (pick.y - a.y) * dir.y) / lenSq;

    // Mantém a metade OPOSTA ao lado de pick.
    if (tp < t) return Line{*x, b};   // pick na metade [a, x) -> mantém [x, b]
    return Line{a, *x};               // pick na metade (x, b] -> mantém [a, x]
}

// Concordância (fillet) tangente de raio radius entre duas linhas (ver header).
FilletResult filletLines(const Line& l1, const Line& l2, double radius) {
    FilletResult res;
    if (radius <= kEps) return res;  // raio inválido

    const Point3& a0 = l1.start();
    const Point3& a1 = l1.end();
    const Point3& b0 = l2.start();
    const Point3& b1 = l2.end();

    // Vértice: interseção das retas infinitas. Paralelas -> impossível.
    const auto corner = intersectInfiniteLines(a0, a1, b0, b1);
    if (!corner) return res;
    const Point3 C = *corner;

    // Direções unitárias saindo do vértice em direção ao extremo MAIS DISTANTE
    // de cada linha — é desse lado que o segmento aparado permanece.
    auto dirAwayFromCorner = [&](const Point3& p, const Point3& q) -> Vec3 {
        const double dp = (p.x - C.x) * (p.x - C.x) + (p.y - C.y) * (p.y - C.y);
        const double dq = (q.x - C.x) * (q.x - C.x) + (q.y - C.y) * (q.y - C.y);
        const Point3& far = (dp >= dq) ? p : q;
        Vec3 d{far.x - C.x, far.y - C.y, 0.0};
        const double len = length2D(d);
        return (len > kEps) ? Vec3{d.x / len, d.y / len, 0.0} : Vec3{};
    };

    const Vec3 u1 = dirAwayFromCorner(a0, a1);
    const Vec3 u2 = dirAwayFromCorner(b0, b1);
    if (length2D(u1) < kEps || length2D(u2) < kEps) return res;

    // Ângulo entre as direções; colinear -> sem concordância possível.
    double cosT = dot2D(u1, u2);
    if (cosT > 1.0) cosT = 1.0; else if (cosT < -1.0) cosT = -1.0;
    const double theta = std::acos(cosT);
    if (theta < kEps || std::fabs(theta - kPi) < kEps) return res;

    const double half = theta * 0.5;
    const double tanHalf = std::tan(half);
    if (std::fabs(tanHalf) < kEps) return res;

    // Distância do vértice aos pontos de tangência ao longo de cada linha.
    const double dist = radius / tanHalf;

    // Pontos de tangência sobre cada linha (a partir do vértice C, na direção
    // do extremo mais distante de cada uma).
    const Point3 t1{C.x + u1.x * dist, C.y + u1.y * dist, 0.0};
    const Point3 t2{C.x + u2.x * dist, C.y + u2.y * dist, 0.0};

    // Cada ponto de tangência precisa cair dentro do respectivo segmento; caso
    // contrário o raio é grande demais para caber nas linhas dadas.
    auto onSegment = [&](const Point3& p, const Point3& q, const Point3& t) {
        const Vec3   d  = q - p;
        const double l2 = d.x * d.x + d.y * d.y;
        if (l2 < kEps) return false;
        const double s = ((t.x - p.x) * d.x + (t.y - p.y) * d.y) / l2;
        return s >= -kEps && s <= 1.0 + kEps;
    };
    if (!onSegment(a0, a1, t1) || !onSegment(b0, b1, t2)) return res;

    // Bissetriz interna (direção média) e centro do arco.
    Vec3 bis{u1.x + u2.x, u1.y + u2.y, 0.0};
    const double bisLen = length2D(bis);
    if (bisLen < kEps) return res;  // direções opostas (já tratado, segurança)
    bis = Vec3{bis.x / bisLen, bis.y / bisLen, 0.0};

    const double centerDist = radius / std::sin(half);
    const Point3 center{C.x + bis.x * centerDist, C.y + bis.y * centerDist, 0.0};

    // Ângulos dos pontos de tangência vistos do centro do arco.
    const double ang1 = std::atan2(t1.y - center.y, t1.x - center.x);
    const double ang2 = std::atan2(t2.y - center.y, t2.x - center.x);

    // Arco CCW (convenção de Arc): escolhe a ordem start->end que gera a
    // varredura curta (a concordância, não o complemento).
    double start = ang1, end = ang2;
    double sweep = end - start;
    while (sweep <= -kPi) sweep += kTwoPi;
    while (sweep >   kPi) sweep -= kTwoPi;
    if (sweep < 0.0) { start = ang2; end = ang1; }  // garante varredura CCW curta

    res.arc   = Arc{center, radius, start, end};
    res.line1 = Line{t1, (length2D(a0 - C) >= length2D(a1 - C)) ? a0 : a1};
    res.line2 = Line{t2, (length2D(b0 - C) >= length2D(b1 - C)) ? b0 : b1};
    res.ok    = true;
    return res;
}

} // namespace cad
