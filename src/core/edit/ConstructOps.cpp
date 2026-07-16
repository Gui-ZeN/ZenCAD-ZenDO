// src/core/edit/ConstructOps.cpp
#include "core/edit/ConstructOps.hpp"

#include "core/math/Constants.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cad {

namespace {

// Tolerância geométrica para comparações com zero (determinantes, paralelismo).
constexpr double kEps = 1e-9;

// Normaliza um ângulo para o intervalo [0, 2*pi).
double normalizeAngle(double a) {
    a = std::fmod(a, kTwoPi);
    if (a < 0.0) a += kTwoPi;
    return a;
}

// Varredura CCW de `start` até `end`, em [0, 2*pi). Se start == end (mod 2*pi),
// considera-se volta completa (2*pi) — não relevante aqui, mas evita 0 espúrio.
double ccwSweep(double start, double end) {
    double sweep = normalizeAngle(end - start);
    return sweep;
}

} // namespace

Polyline rectangleFrom2Points(const Point3& a, const Point3& c) {
    // Cantos no plano XY, z = 0, em ordem CCW/CW dependendo da disposição de a e c.
    std::vector<Point3> verts;
    verts.reserve(4);
    verts.emplace_back(a.x, a.y, 0.0);
    verts.emplace_back(c.x, a.y, 0.0);
    verts.emplace_back(c.x, c.y, 0.0);
    verts.emplace_back(a.x, c.y, 0.0);
    return Polyline(std::move(verts), /*closed=*/true);
}

Arc3Result arcFrom3Points(const Point3& p1, const Point3& p2, const Point3& p3) {
    Arc3Result result;

    // Centro do círculo via interseção das mediatrizes. Usando coordenadas no
    // plano XY: resolve-se o sistema linear obtido das equações
    //   |P - p1|^2 = |P - p2|^2  e  |P - p2|^2 = |P - p3|^2.
    // Isso lineariza para:
    //   2*(p2-p1)·P = |p2|^2 - |p1|^2
    //   2*(p3-p2)·P = |p3|^2 - |p2|^2
    const double ax = p1.x, ay = p1.y;
    const double bx = p2.x, by = p2.y;
    const double cx = p3.x, cy = p3.y;

    // Determinante proporcional à área do triângulo (zero => colinear).
    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::fabs(d) < kEps) {
        // Pontos colineares: mediatrizes paralelas, sem círculo definido.
        result.ok = false;
        return result;
    }

    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;

    const double ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
    const double uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;

    const Point3 center{ux, uy, 0.0};
    const double radius = std::hypot(ax - ux, ay - uy);

    // Ângulos dos três pontos em relação ao centro.
    const double a1 = std::atan2(ay - uy, ax - ux);     // p1
    const double am = std::atan2(by - uy, bx - ux);     // p2 (intermediário)
    const double a3 = std::atan2(cy - uy, cx - ux);     // p3

    // Tenta o sentido CCW de p1 -> p3; verifica se p2 cai dentro da varredura.
    double start = normalizeAngle(a1);
    double end   = normalizeAngle(a3);

    const double sweepToMid = ccwSweep(start, am);
    const double sweepTotal = ccwSweep(start, end);

    if (sweepToMid > sweepTotal) {
        // p2 não está na varredura CCW de p1->p3; inverte os limites para que o
        // arco (sempre CCW) passe por p2.
        std::swap(start, end);
    }

    result.arc = Arc(center, radius, start, end);
    result.ok  = true;
    return result;
}

Arc3Result arcStartEndRadius(const Point3& s, const Point3& e, double r) {
    Arc3Result res;
    const double dx = e.x - s.x, dy = e.y - s.y;
    const double d = std::hypot(dx, dy);
    const double ar = std::fabs(r);
    if (d < kEps || ar * 2.0 < d - kEps) return res;  // raio pequeno demais
    const Point3 mid{(s.x + e.x) * 0.5, (s.y + e.y) * 0.5, 0.0};
    const double h = std::sqrt(std::max(0.0, ar * ar - (d * 0.5) * (d * 0.5)));
    const double nx = -dy / d, ny = dx / d;           // perpendicular à corda
    Point3 center{mid.x + nx * h, mid.y + ny * h, 0.0};
    double a0 = std::atan2(s.y - center.y, s.x - center.x);
    double a1 = std::atan2(e.y - center.y, e.x - center.x);
    if (ccwSweep(normalizeAngle(a0), normalizeAngle(a1)) > kPi) {
        center = Point3{mid.x - nx * h, mid.y - ny * h, 0.0};  // lado do arco menor
        a0 = std::atan2(s.y - center.y, s.x - center.x);
        a1 = std::atan2(e.y - center.y, e.x - center.x);
    }
    res.arc = Arc(center, ar, a0, a1);
    res.ok = true;
    return res;
}

Arc3Result arcStartEndAngle(const Point3& s, const Point3& e, double angleRad) {
    Arc3Result res;
    const double dx = e.x - s.x, dy = e.y - s.y;
    const double d = std::hypot(dx, dy);
    if (d < kEps || std::fabs(angleRad) < kEps) return res;
    const double r = (d * 0.5) / std::sin(std::fabs(angleRad) * 0.5);  // corda = 2r sin(a/2)
    const double dcen = (d * 0.5) / std::tan(angleRad * 0.5);          // sinal define o lado
    const Point3 mid{(s.x + e.x) * 0.5, (s.y + e.y) * 0.5, 0.0};
    const double nx = -dy / d, ny = dx / d;
    const Point3 center{mid.x + nx * dcen, mid.y + ny * dcen, 0.0};
    res.arc = Arc(center, r, std::atan2(s.y - center.y, s.x - center.x),
                  std::atan2(e.y - center.y, e.x - center.x));
    res.ok = true;
    return res;
}

Arc3Result arcStartEndDirection(const Point3& s, const Point3& e, const Point3& dirPt) {
    Arc3Result res;
    double dx = dirPt.x - s.x, dy = dirPt.y - s.y;
    const double dl = std::hypot(dx, dy);
    if (dl < kEps) return res;
    dx /= dl; dy /= dl;                 // tangente unitária d em s
    const double mx = -dy, my = dx;     // normal à esquerda de d
    const double vx = s.x - e.x, vy = s.y - e.y;
    const double denom = vx * mx + vy * my;
    if (std::fabs(denom) < kEps) return res;        // e na direção de d (reta)
    const double r = -(vx * vx + vy * vy) / (2.0 * denom);   // raio com sinal
    const Point3 center{s.x + r * mx, s.y + r * my, 0.0};
    const double angS = std::atan2(s.y - center.y, s.x - center.x);
    const double angE = std::atan2(e.y - center.y, e.x - center.x);
    // r>0: tangente CCW = +d (arco CCW s->e); r<0: usar o arco complementar.
    res.arc = (r >= 0.0) ? Arc(center, r, angS, angE) : Arc(center, -r, angE, angS);
    res.ok = true;
    return res;
}

Circle circle2Points(const Point3& a, const Point3& b) {
    const Point3 center{(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, 0.0};
    const double r = std::hypot(b.x - a.x, b.y - a.y) * 0.5;
    return Circle(center, r);
}

Circle circle3Points(const Point3& a, const Point3& b, const Point3& c, bool& ok) {
    const Arc3Result r = arcFrom3Points(a, b, c);
    ok = r.ok;
    if (!ok) return Circle(a, 0.0);
    return Circle(r.arc.center(), r.arc.radius());
}

Polyline rectangleChamfer(const Point3& a, const Point3& c, double dist) {
    const double w = c.x - a.x, h = c.y - a.y;
    const double sx = (w >= 0.0) ? 1.0 : -1.0;
    const double sy = (h >= 0.0) ? 1.0 : -1.0;
    // Limita o chanfro a quase metade do menor lado (evita auto-interseção).
    double d = dist;
    const double lim = 0.499 * std::min(std::fabs(w), std::fabs(h));
    if (d > lim) d = lim;
    if (d < 0.0) d = 0.0;

    const double dx = sx * d, dy = sy * d;
    std::vector<Point3> v;
    v.reserve(8);
    v.emplace_back(a.x + dx, a.y,      0.0);  // aresta inferior, perto de P0
    v.emplace_back(c.x - dx, a.y,      0.0);  // aresta inferior, perto de P1
    v.emplace_back(c.x,      a.y + dy, 0.0);  // aresta direita, perto de P1
    v.emplace_back(c.x,      c.y - dy, 0.0);  // aresta direita, perto de P2
    v.emplace_back(c.x - dx, c.y,      0.0);  // aresta superior, perto de P2
    v.emplace_back(a.x + dx, c.y,      0.0);  // aresta superior, perto de P3
    v.emplace_back(a.x,      c.y - dy, 0.0);  // aresta esquerda, perto de P3
    v.emplace_back(a.x,      a.y + dy, 0.0);  // aresta esquerda, perto de P0
    return Polyline(std::move(v), /*closed=*/true);
}

Polyline rectangleFillet(const Point3& a, const Point3& c, double r) {
    const double xmin = std::min(a.x, c.x), xmax = std::max(a.x, c.x);
    const double ymin = std::min(a.y, c.y), ymax = std::max(a.y, c.y);
    double rr = r;
    const double lim = 0.499 * std::min(xmax - xmin, ymax - ymin);
    if (rr > lim) rr = lim;
    if (rr < kEps) return rectangleFrom2Points(a, c);   // sem raio = retângulo reto

    // 8 vértices CCW; os trechos de canto recebem bulge de um quarto de volta.
    std::vector<Point3> v{
        {xmin + rr, ymin, 0.0}, {xmax - rr, ymin, 0.0},   // aresta inferior
        {xmax, ymin + rr, 0.0}, {xmax, ymax - rr, 0.0},   // aresta direita
        {xmax - rr, ymax, 0.0}, {xmin + rr, ymax, 0.0},   // aresta superior
        {xmin, ymax - rr, 0.0}, {xmin, ymin + rr, 0.0}};  // aresta esquerda
    const double k = std::tan(kPi / 8.0);                 // bulge de 90° = tan(22,5°)
    std::vector<double> b{0.0, k, 0.0, k, 0.0, k, 0.0, k};  // cantos = trechos ímpares
    return Polyline(std::move(v), std::move(b), /*closed=*/true);
}

Polyline regularPolygon(const Point3& center, int sides, double radius, double rotation) {
    if (sides < 3) {
        // Polígono inválido: retorna polilinha vazia.
        return Polyline({}, /*closed=*/false);
    }

    std::vector<Point3> verts;
    verts.reserve(static_cast<std::size_t>(sides));

    const double step = kTwoPi / static_cast<double>(sides);
    for (int i = 0; i < sides; ++i) {
        const double ang = rotation + step * static_cast<double>(i);
        verts.emplace_back(center.x + radius * std::cos(ang),
                           center.y + radius * std::sin(ang),
                           0.0);
    }

    return Polyline(std::move(verts), /*closed=*/true);
}

} // namespace cad
