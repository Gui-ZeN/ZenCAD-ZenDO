// src/core/edit/GripOps.cpp
#include "core/edit/GripOps.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Wall.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/Spline.hpp"
#include "core/geometry/Dimension.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Ellipse.hpp"
#include "core/geometry/MText.hpp"
#include "core/geometry/BlockRef.hpp"
#include "core/geometry/Hatch.hpp"
#include "core/math/Matrix4.hpp"
#include <cmath>
#include <algorithm>

namespace {
// Clone transladado por (np - ref): grip de "mover a entidade inteira".
std::unique_ptr<cad::Entity> movedClone(const cad::Entity& e,
                                        const cad::Point3& ref,
                                        const cad::Point3& np) {
    auto c = e.clone();
    c->transform(cad::Matrix4::translation(
        cad::Vec3{np.x - ref.x, np.y - ref.y, 0.0}));
    return c;
}
} // namespace

namespace cad {

std::vector<Point3> gripsOf(const Entity& e) {
    if (const auto* w = dynamic_cast<const Wall*>(&e))
        return w->axis();                            // vértices do EIXO da parede
    if (const auto* l = dynamic_cast<const Line*>(&e))
        return {l->start(), l->end()};
    if (const auto* pl = dynamic_cast<const Polyline*>(&e))
        return pl->vertices();
    if (const auto* sp = dynamic_cast<const Spline*>(&e))   // SPLINEDIT: pontos de controle
        return sp->controlPoints();
    if (const auto* c = dynamic_cast<const Circle*>(&e))
        return {c->center(),
                Point3{c->center().x + c->radius(), c->center().y, c->center().z}};
    if (const auto* a = dynamic_cast<const Arc*>(&e)) {
        // Arco: centro (move), extremos (mudam os ângulos) e meio (muda o raio).
        auto onArc = [&](double ang) {
            return Point3{a->center().x + a->radius() * std::cos(ang),
                          a->center().y + a->radius() * std::sin(ang),
                          a->center().z};
        };
        double sweep = a->endAngle() - a->startAngle();
        while (sweep < 0) sweep += 6.283185307179586;
        return {a->center(), onArc(a->startAngle()), onArc(a->endAngle()),
                onArc(a->startAngle() + sweep * 0.5)};
    }
    if (const auto* el = dynamic_cast<const Ellipse*>(&e)) {
        // Elipse: centro (move), fim do eixo MAIOR e fim do eixo MENOR.
        const Vec3 mj = el->major();
        const double len = mj.length();
        const Vec3 mn = (len > 1e-12)
            ? Vec3{-mj.y / len, mj.x / len, 0.0} * (el->ratio() * len)
            : Vec3{0.0, 1.0, 0.0};
        return {el->center(),
                Point3{el->center().x + mj.x, el->center().y + mj.y, 0.0},
                Point3{el->center().x + mn.x, el->center().y + mn.y, 0.0}};
    }
    if (const auto* t = dynamic_cast<const MText*>(&e))
        return {t->position()};                      // move o texto inteiro
    if (const auto* b = dynamic_cast<const BlockRef*>(&e))
        return {Point3{b->xform().m[12], b->xform().m[13], 0.0}};   // inserção
    if (const auto* ht = dynamic_cast<const Hatch*>(&e)) {
        if (!ht->loops().empty() && !ht->loops().front().empty())
            return {ht->loops().front().front()};    // move a hachura inteira
        return {};
    }
    if (const auto* d = dynamic_cast<const Dimension*>(&e)) {
        // Cota: texto (DIMTEDIT) + pontos de definição p1/p2 + linha de cota p3.
        const Point3 b = d->textBasePoint();
        return {Point3{b.x + d->textOffsetX(), b.y + d->textOffsetY(), 0.0},
                d->p1(), d->p2(), d->p3()};
    }
    return {};
}

std::unique_ptr<Entity> withGripMoved(const Entity& e, int gi, const Point3& np) {
    if (const auto* w = dynamic_cast<const Wall*>(&e)) {
        if (gi < 0 || gi >= static_cast<int>(w->axis().size())) return nullptr;
        auto neu = std::unique_ptr<Wall>(static_cast<Wall*>(w->clone().release()));
        neu->setAxisPoint(static_cast<std::size_t>(gi), np);
        return neu;
    }
    if (const auto* l = dynamic_cast<const Line*>(&e)) {
        if (gi == 0) return std::make_unique<Line>(np, l->end());
        if (gi == 1) return std::make_unique<Line>(l->start(), np);
        return nullptr;
    }
    if (const auto* pl = dynamic_cast<const Polyline*>(&e)) {
        std::vector<Point3> v = pl->vertices();
        if (gi < 0 || gi >= static_cast<int>(v.size())) return nullptr;
        v[static_cast<std::size_t>(gi)] = np;
        return std::make_unique<Polyline>(std::move(v), pl->closed());
    }
    if (const auto* sp = dynamic_cast<const Spline*>(&e)) {
        std::vector<Point3> v = sp->controlPoints();
        if (gi < 0 || gi >= static_cast<int>(v.size())) return nullptr;
        v[static_cast<std::size_t>(gi)] = np;
        return std::make_unique<Spline>(std::move(v), sp->isCV());
    }
    if (const auto* c = dynamic_cast<const Circle*>(&e)) {
        if (gi == 0) return std::make_unique<Circle>(np, c->radius());   // move o centro
        if (gi == 1) {                                                   // ajusta o raio
            const double r = std::hypot(np.x - c->center().x, np.y - c->center().y);
            return std::make_unique<Circle>(c->center(), r);
        }
        return nullptr;
    }
    if (const auto* a = dynamic_cast<const Arc*>(&e)) {
        if (gi == 0) return movedClone(e, a->center(), np);          // move
        const double ang = std::atan2(np.y - a->center().y, np.x - a->center().x);
        if (gi == 1) return std::make_unique<Arc>(a->center(), a->radius(),
                                                  ang, a->endAngle());
        if (gi == 2) return std::make_unique<Arc>(a->center(), a->radius(),
                                                  a->startAngle(), ang);
        if (gi == 3) {                                               // raio pelo meio
            const double r = std::hypot(np.x - a->center().x, np.y - a->center().y);
            if (r <= 1e-9) return nullptr;
            return std::make_unique<Arc>(a->center(), r, a->startAngle(), a->endAngle());
        }
        return nullptr;
    }
    if (const auto* el = dynamic_cast<const Ellipse*>(&e)) {
        if (gi == 0) return movedClone(e, el->center(), np);
        const double majLen = el->major().length();
        if (gi == 1) {                                               // eixo maior
            const Vec3 nm{np.x - el->center().x, np.y - el->center().y, 0.0};
            if (nm.length() <= 1e-9) return nullptr;
            return std::make_unique<Ellipse>(Ellipse::fromCenterAxesArc(
                el->center(), nm, el->ratio() * majLen,
                el->startParam(), el->endParam()));
        }
        if (gi == 2) {                                               // eixo menor
            const double minorLen = std::hypot(np.x - el->center().x,
                                               np.y - el->center().y);
            if (minorLen <= 1e-9 || majLen <= 1e-9) return nullptr;
            return std::make_unique<Ellipse>(Ellipse::fromCenterAxesArc(
                el->center(), el->major(), minorLen,
                el->startParam(), el->endParam()));
        }
        return nullptr;
    }
    if (const auto* t = dynamic_cast<const MText*>(&e))
        return gi == 0 ? movedClone(e, t->position(), np) : nullptr;
    if (const auto* b = dynamic_cast<const BlockRef*>(&e))
        return gi == 0 ? movedClone(e, Point3{b->xform().m[12], b->xform().m[13], 0.0}, np)
                       : nullptr;
    if (const auto* ht = dynamic_cast<const Hatch*>(&e)) {
        if (gi != 0 || ht->loops().empty() || ht->loops().front().empty()) return nullptr;
        auto neu = movedClone(e, ht->loops().front().front(), np);
        // Mover a hachura para longe da fronteira quebra a associatividade.
        static_cast<Hatch*>(neu.get())->setSrcIds({});
        return neu;
    }
    if (const auto* d = dynamic_cast<const Dimension*>(&e)) {
        auto neu = std::unique_ptr<Dimension>(
            static_cast<Dimension*>(d->clone().release()));
        if (gi == 0) {                                   // DIMTEDIT: só o texto
            const Point3 b = d->textBasePoint();
            neu->setTextOffset(np.x - b.x, np.y - b.y);
            return neu;
        }
        // Mover um defpoint manualmente SOLTA a âncora daquele lado (senão o
        // regen associativo devolveria o ponto no próximo comando).
        if (gi == 1) {
            neu->setPoints(np, d->p2(), d->p3());
            neu->setAnchors(DimAnchor{}, d->anchorB());
            return neu;
        }
        if (gi == 2) {
            neu->setPoints(d->p1(), np, d->p3());
            neu->setAnchors(d->anchorA(), DimAnchor{});
            return neu;
        }
        if (gi == 3) {
            neu->setPoints(d->p1(), d->p2(), np);
            return neu;
        }
        return nullptr;
    }
    return nullptr;
}

std::unique_ptr<Entity> withVertexRemoved(const Entity& e, int gi) {
    if (const auto* pl = dynamic_cast<const Polyline*>(&e)) {
        std::vector<Point3> v = pl->vertices();
        if (gi < 0 || gi >= static_cast<int>(v.size()) || v.size() <= 2) return nullptr;
        v.erase(v.begin() + gi);
        return std::make_unique<Polyline>(std::move(v), pl->closed());
    }
    if (const auto* sp = dynamic_cast<const Spline*>(&e)) {
        std::vector<Point3> v = sp->controlPoints();
        if (gi < 0 || gi >= static_cast<int>(v.size()) || v.size() <= 2) return nullptr;
        v.erase(v.begin() + gi);
        return std::make_unique<Spline>(std::move(v), sp->isCV());
    }
    return nullptr;
}

std::unique_ptr<Entity> withVertexInserted(const Entity& e, const Point3& at) {
    if (const auto* sp = dynamic_cast<const Spline*>(&e)) {   // SPLINEDIT: insere ponto de controle
        std::vector<Point3> v = sp->controlPoints();
        if (v.size() < 2) return nullptr;
        double best = 1e300; std::size_t bi = 0; Point3 bp = at;
        for (std::size_t i = 0; i + 1 < v.size(); ++i) {
            const Point3 a = v[i], b = v[i + 1];
            const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
            double t = l2 < 1e-12 ? 0.0 : ((at.x - a.x) * dx + (at.y - a.y) * dy) / l2;
            t = std::clamp(t, 0.0, 1.0);
            const double qx = a.x + t * dx, qy = a.y + t * dy;
            const double d = std::hypot(at.x - qx, at.y - qy);
            if (d < best) { best = d; bi = i; bp = Point3{qx, qy, 0.0}; }
        }
        v.insert(v.begin() + bi + 1, bp);
        return std::make_unique<Spline>(std::move(v), sp->isCV());
    }
    const auto* pl = dynamic_cast<const Polyline*>(&e);
    if (!pl) return nullptr;
    std::vector<Point3> v = pl->vertices();
    if (v.size() < 2) return nullptr;
    const std::size_t segs = pl->closed() ? v.size() : v.size() - 1;
    double best = 1e300; std::size_t bi = 0; Point3 bp = at;
    for (std::size_t i = 0; i < segs; ++i) {                  // aresta mais próxima de `at`
        const Point3 a = v[i], b = v[(i + 1) % v.size()];
        const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        double t = l2 < 1e-12 ? 0.0 : ((at.x - a.x) * dx + (at.y - a.y) * dy) / l2;
        t = std::clamp(t, 0.0, 1.0);
        const double qx = a.x + t * dx, qy = a.y + t * dy;
        const double d = std::hypot(at.x - qx, at.y - qy);
        if (d < best) { best = d; bi = i; bp = Point3{qx, qy, 0.0}; }
    }
    v.insert(v.begin() + bi + 1, bp);
    return std::make_unique<Polyline>(std::move(v), pl->closed());
}

} // namespace cad
