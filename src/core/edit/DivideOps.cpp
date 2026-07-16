// src/core/edit/DivideOps.cpp
#include "core/edit/DivideOps.hpp"

#include "core/geometry/Line.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/Spline.hpp"
#include "core/math/Constants.hpp"

#include <cmath>

namespace cad {
namespace {

// Amostra a entidade como uma polilinha; informa se é fechada.
std::vector<Point3> sampleEntity(const Entity& e, bool& closed) {
    closed = false;
    std::vector<Point3> pts;
    if (const auto* l = dynamic_cast<const Line*>(&e)) {
        pts = {l->start(), l->end()};
    } else if (const auto* c = dynamic_cast<const Circle*>(&e)) {
        closed = true;
        const int N = 180;
        for (int i = 0; i < N; ++i) {
            const double a = kTwoPi * i / N;
            pts.emplace_back(c->center().x + c->radius() * std::cos(a),
                             c->center().y + c->radius() * std::sin(a), 0.0);
        }
    } else if (const auto* a = dynamic_cast<const Arc*>(&e)) {
        double sweep = a->endAngle() - a->startAngle();
        while (sweep < 0.0) sweep += kTwoPi;   // arco sempre CCW
        const int N = 180;
        for (int i = 0; i <= N; ++i) {
            const double ang = a->startAngle() + sweep * i / N;
            pts.emplace_back(a->center().x + a->radius() * std::cos(ang),
                             a->center().y + a->radius() * std::sin(ang), 0.0);
        }
    } else if (const auto* pl = dynamic_cast<const Polyline*>(&e)) {
        pts = pl->sampledPoints();   // arcos tesselados; já inclui o fechamento
        closed = pl->closed();
    } else if (const auto* sp = dynamic_cast<const Spline*>(&e)) {
        pts = sp->sample();
    }
    return pts;
}

double polyLength(const std::vector<Point3>& p) {
    double L = 0.0;
    for (std::size_t i = 1; i < p.size(); ++i)
        L += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    return L;
}

// Ponto a uma distância de arco `s` ao longo da polilinha amostrada.
// `angOut` (opcional) recebe o ângulo da tangente do trecho local.
Point3 atArcLength(const std::vector<Point3>& p, double s, double* angOut = nullptr) {
    if (angOut) *angOut = 0.0;
    if (p.empty()) return Point3{};
    if (p.size() >= 2 && angOut)
        *angOut = std::atan2(p[1].y - p[0].y, p[1].x - p[0].x);
    if (s <= 0.0) return p.front();
    double acc = 0.0;
    for (std::size_t i = 1; i < p.size(); ++i) {
        const double seg = std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
        if (acc + seg >= s) {
            const double t = seg > 1e-12 ? (s - acc) / seg : 0.0;
            if (angOut) *angOut = std::atan2(p[i].y - p[i - 1].y, p[i].x - p[i - 1].x);
            return Point3{p[i - 1].x + t * (p[i].x - p[i - 1].x),
                          p[i - 1].y + t * (p[i].y - p[i - 1].y), 0.0};
        }
        acc += seg;
    }
    if (p.size() >= 2 && angOut)
        *angOut = std::atan2(p.back().y - p[p.size() - 2].y, p.back().x - p[p.size() - 2].x);
    return p.back();
}

} // namespace

std::vector<Point3> dividePoints(const Entity& e, int segments) {
    std::vector<Point3> out;
    if (segments < 2) return out;
    bool closed = false;
    const std::vector<Point3> p = sampleEntity(e, closed);
    if (p.size() < 2) return out;
    const double L = polyLength(p);
    if (L < 1e-9) return out;
    if (closed)
        for (int i = 0; i < segments; ++i) out.push_back(atArcLength(p, L * i / segments));
    else
        for (int i = 1; i < segments; ++i) out.push_back(atArcLength(p, L * i / segments));
    return out;
}

std::vector<Point3> measurePoints(const Entity& e, double spacing) {
    std::vector<Point3> out;
    if (spacing <= 1e-9) return out;
    bool closed = false;
    const std::vector<Point3> p = sampleEntity(e, closed);
    if (p.size() < 2) return out;
    const double L = polyLength(p);
    for (double s = spacing; s <= L + 1e-9; s += spacing)
        out.push_back(atArcLength(p, s));
    return out;
}

std::vector<DivMark> divideMarks(const Entity& e, int segments) {
    std::vector<DivMark> out;
    if (segments < 2) return out;
    bool closed = false;
    const std::vector<Point3> p = sampleEntity(e, closed);
    if (p.size() < 2) return out;
    const double L = polyLength(p);
    if (L < 1e-9) return out;
    const int i0 = closed ? 0 : 1;
    for (int i = i0; i < segments; ++i) {
        DivMark m;
        m.p = atArcLength(p, L * i / segments, &m.angleRad);
        out.push_back(m);
    }
    return out;
}

std::vector<DivMark> measureMarks(const Entity& e, double spacing) {
    std::vector<DivMark> out;
    if (spacing <= 1e-9) return out;
    bool closed = false;
    const std::vector<Point3> p = sampleEntity(e, closed);
    if (p.size() < 2) return out;
    const double L = polyLength(p);
    for (double s = spacing; s <= L + 1e-9; s += spacing) {
        DivMark m;
        m.p = atArcLength(p, s, &m.angleRad);
        out.push_back(m);
    }
    return out;
}

} // namespace cad
