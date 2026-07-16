// src/core/edit/InquiryOps.cpp
#include "core/edit/InquiryOps.hpp"

#include <cmath>

#include "core/geometry/Entity.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/math/Constants.hpp"

namespace cad {

double distance(const Point3& a, const Point3& b) {
    return (b - a).length();
}

double polygonArea(const std::vector<Point3>& pts) {
    const std::size_t n = pts.size();
    if (n < 3) return 0.0;
    // Shoelace no plano XY; soma dos produtos cruzados das arestas, incluindo
    // a aresta de fechamento (último -> primeiro).
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Point3& p = pts[i];
        const Point3& q = pts[(i + 1) % n];
        acc += p.x * q.y - q.x * p.y;
    }
    return std::fabs(acc) * 0.5;
}

double polygonPerimeter(const std::vector<Point3>& pts, bool closed) {
    const std::size_t n = pts.size();
    if (n < 2) return 0.0;
    double total = 0.0;
    for (std::size_t i = 0; i + 1 < n; ++i)
        total += distance(pts[i], pts[i + 1]);
    if (closed)
        total += distance(pts[n - 1], pts[0]);
    return total;
}

double entityLength(const Entity& e) {
    if (const auto* line = dynamic_cast<const Line*>(&e)) {
        return distance(line->start(), line->end());
    }
    if (const auto* circle = dynamic_cast<const Circle*>(&e)) {
        return kTwoPi * circle->radius();
    }
    if (const auto* arc = dynamic_cast<const Arc*>(&e)) {
        // Varredura CCW de startAngle a endAngle, normalizada para [0, 2*pi).
        double sweep = arc->endAngle() - arc->startAngle();
        sweep = std::fmod(sweep, kTwoPi);
        if (sweep < 0.0) sweep += kTwoPi;
        return arc->radius() * sweep;
    }
    if (const auto* poly = dynamic_cast<const Polyline*>(&e)) {
        // sampledPoints() já tessela os arcos e já inclui o ponto de fechamento
        // quando a polilinha é fechada — basta somar as arestas consecutivas.
        return polygonPerimeter(poly->sampledPoints(), /*closed=*/false);
    }
    return 0.0;
}

double entityArea(const Entity& e) {
    if (const auto* circle = dynamic_cast<const Circle*>(&e)) {
        const double r = circle->radius();
        return kPi * r * r;
    }
    if (const auto* poly = dynamic_cast<const Polyline*>(&e)) {
        if (poly->closed())
            return polygonArea(poly->sampledPoints());
        return 0.0;
    }
    return 0.0;
}

} // namespace cad
