// src/core/edit/CleanupOps.hpp
// OVERKILL / REVERSE / BLEND — limpeza e emenda (item 8 do backlog).
// Header-only e sem Qt: testável no smoke headless.
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/PointEntity.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/Spline.hpp"
#include "core/math/Vec.hpp"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace cad {
namespace cleanup {

inline bool samePt(const Point3& a, const Point3& b, double tol) {
    return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol;
}

// Propriedades visuais iguais (critério do OVERKILL: só remove o que é
// indistinguível — camada, cor, tipo e espessura de linha).
inline bool sameProps(const Entity& a, const Entity& b) {
    if (a.layer() != b.layer()) return false;
    const ColorRef ca = a.color(), cb = b.color();
    if (ca.mode != cb.mode) return false;
    if (ca.mode == ColorRef::Mode::Explicit &&
        (ca.value.r != cb.value.r || ca.value.g != cb.value.g ||
         ca.value.b != cb.value.b || ca.value.a != cb.value.a)) return false;
    if (a.lineType().name != b.lineType().name) return false;
    if (std::abs(a.lineWeight().mm - b.lineWeight().mm) > 1e-9) return false;
    return true;
}

// Geometria idêntica (por tipo; sequências valem também INVERTIDAS).
inline bool sameGeom(const Entity& a, const Entity& b, double tol) {
    if (const auto* la = dynamic_cast<const Line*>(&a)) {
        const auto* lb = dynamic_cast<const Line*>(&b);
        if (!lb) return false;
        return (samePt(la->start(), lb->start(), tol) && samePt(la->end(), lb->end(), tol)) ||
               (samePt(la->start(), lb->end(), tol)   && samePt(la->end(), lb->start(), tol));
    }
    if (const auto* ca = dynamic_cast<const Circle*>(&a)) {
        const auto* cb = dynamic_cast<const Circle*>(&b);
        if (!cb) return false;
        return samePt(ca->center(), cb->center(), tol) &&
               std::abs(ca->radius() - cb->radius()) <= tol;
    }
    if (const auto* aa = dynamic_cast<const Arc*>(&a)) {
        const auto* ab = dynamic_cast<const Arc*>(&b);
        if (!ab) return false;
        return samePt(aa->center(), ab->center(), tol) &&
               std::abs(aa->radius() - ab->radius()) <= tol &&
               std::abs(aa->startAngle() - ab->startAngle()) <= 1e-9 &&
               std::abs(aa->endAngle() - ab->endAngle()) <= 1e-9;
    }
    if (const auto* pa = dynamic_cast<const PointEntity*>(&a)) {
        const auto* pb = dynamic_cast<const PointEntity*>(&b);
        return pb && samePt(pa->position(), pb->position(), tol);
    }
    if (const auto* la = dynamic_cast<const Polyline*>(&a)) {
        const auto* lb = dynamic_cast<const Polyline*>(&b);
        if (!lb || la->closed() != lb->closed()) return false;
        const auto& va = la->vertices(); const auto& vb = lb->vertices();
        if (va.size() != vb.size()) return false;
        const std::size_t n = va.size();
        bool fwd = true, rev = true;
        for (std::size_t i = 0; i < n && (fwd || rev); ++i) {
            if (fwd && !samePt(va[i], vb[i], tol)) fwd = false;
            if (rev && !samePt(va[i], vb[n - 1 - i], tol)) rev = false;
        }
        if (!fwd && !rev) return false;
        // bulges: mesma curvatura no sentido correspondente
        const std::size_t m = la->closed() ? n : (n > 0 ? n - 1 : 0);
        for (std::size_t i = 0; i < m; ++i) {
            const double want = fwd ? lb->bulgeAt(i)
                                    : -lb->bulgeAt(la->closed() ? (i == m - 1 ? m - 1 : m - 2 - i)
                                                                : m - 1 - i);
            if (std::abs(la->bulgeAt(i) - want) > 1e-9) return false;
        }
        return true;
    }
    if (const auto* sa = dynamic_cast<const Spline*>(&a)) {
        const auto* sb = dynamic_cast<const Spline*>(&b);
        if (!sb || sa->isCV() != sb->isCV()) return false;
        const auto& va = sa->controlPoints(); const auto& vb = sb->controlPoints();
        if (va.size() != vb.size()) return false;
        const std::size_t n = va.size();
        bool fwd = true, rev = true;
        for (std::size_t i = 0; i < n && (fwd || rev); ++i) {
            if (fwd && !samePt(va[i], vb[i], tol)) fwd = false;
            if (rev && !samePt(va[i], vb[n - 1 - i], tol)) rev = false;
        }
        return fwd || rev;
    }
    return false;   // demais tipos: não deduplica (v1 honesta)
}

// ---------------------------------------------------------------- OVERKILL --
struct OverkillResult {
    std::vector<EntityId>                remove;      // duplicadas + fontes de merge
    std::vector<std::unique_ptr<Entity>> add;         // linhas resultantes dos merges
    int duplicates{0};                                // entidades duplicadas removidas
    int mergedLines{0};                               // linhas eliminadas por união
};

// Remove duplicatas exatas e une LINHAS colineares sobrepostas/encostadas.
inline OverkillResult overkill(const std::vector<std::pair<EntityId, const Entity*>>& items,
                               double tol = 1e-6) {
    OverkillResult res;
    const std::size_t n = items.size();
    std::vector<bool> alive(n, true);

    // 1) duplicatas exatas (mesmo tipo + props + geometria)
    for (std::size_t i = 0; i < n; ++i) {
        if (!alive[i]) continue;
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!alive[j]) continue;
            const Entity& a = *items[i].second;
            const Entity& b = *items[j].second;
            if (std::string(a.typeName()) != b.typeName()) continue;
            if (!sameProps(a, b) || !sameGeom(a, b, tol)) continue;
            alive[j] = false;
            res.remove.push_back(items[j].first);
            ++res.duplicates;
        }
    }

    // 2) linhas colineares que se sobrepõem/encostam -> uma linha só
    struct Seg {
        Point3 p0, p1;
        std::size_t src;                    // item de origem das props
        std::vector<EntityId> ids;          // todas as fontes absorvidas
    };
    std::vector<Seg> segs;
    for (std::size_t i = 0; i < n; ++i) {
        if (!alive[i]) continue;
        if (const auto* l = dynamic_cast<const Line*>(items[i].second))
            segs.push_back({l->start(), l->end(), i, {items[i].first}});
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < segs.size() && !changed; ++i) {
            double dx = segs[i].p1.x - segs[i].p0.x, dy = segs[i].p1.y - segs[i].p0.y;
            const double len = std::hypot(dx, dy);
            if (len < tol) continue;
            dx /= len; dy /= len;
            for (std::size_t j = i + 1; j < segs.size(); ++j) {
                if (!sameProps(*items[segs[i].src].second, *items[segs[j].src].second)) continue;
                // direções paralelas e ponto de j sobre a reta de i
                const double dxj = segs[j].p1.x - segs[j].p0.x, dyj = segs[j].p1.y - segs[j].p0.y;
                const double lj = std::hypot(dxj, dyj);
                if (lj < tol) continue;
                if (std::abs(dx * dyj / lj - dy * dxj / lj) > 1e-9) continue;   // não paralelas
                const double off = (segs[j].p0.x - segs[i].p0.x) * (-dy) +
                                   (segs[j].p0.y - segs[i].p0.y) * dx;
                if (std::abs(off) > tol) continue;                               // paralela deslocada
                // projeções na direção de i (t a partir de p0 de i)
                auto proj = [&](const Point3& p) {
                    return (p.x - segs[i].p0.x) * dx + (p.y - segs[i].p0.y) * dy;
                };
                double a0 = 0.0, a1 = len;
                double b0 = proj(segs[j].p0), b1 = proj(segs[j].p1);
                if (b0 > b1) std::swap(b0, b1);
                if (b0 > a1 + tol || a0 > b1 + tol) continue;                    // sem contato
                const double t0 = std::min(a0, b0), t1 = std::max(a1, b1);
                segs[i].p0 = Point3{segs[i].p0.x + t0 * dx, segs[i].p0.y + t0 * dy, 0.0};
                segs[i].p1 = Point3{segs[i].p0.x + (t1 - t0) * dx, segs[i].p0.y + (t1 - t0) * dy, 0.0};
                segs[i].ids.insert(segs[i].ids.end(), segs[j].ids.begin(), segs[j].ids.end());
                segs.erase(segs.begin() + j);
                changed = true;
                break;
            }
        }
    }
    for (const Seg& s : segs) {
        if (s.ids.size() < 2) continue;                 // nada foi absorvido
        res.remove.insert(res.remove.end(), s.ids.begin(), s.ids.end());
        auto ln = std::make_unique<Line>(s.p0, s.p1);
        const Entity& src = *items[s.src].second;
        ln->setLayer(src.layer()); ln->setColor(src.color());
        ln->setLineType(src.lineType()); ln->setLineWeight(src.lineWeight());
        res.add.push_back(std::move(ln));
        res.mergedLines += int(s.ids.size()) - 1;
    }
    return res;
}

// ----------------------------------------------------------------- REVERSE --
// Entidade com o sentido invertido (nullptr = tipo sem sentido a inverter).
inline std::unique_ptr<Entity> reversed(const Entity& e) {
    if (const auto* l = dynamic_cast<const Line*>(&e))
        return std::make_unique<Line>(l->end(), l->start());
    if (const auto* pl = dynamic_cast<const Polyline*>(&e)) {
        std::vector<Point3> v(pl->vertices().rbegin(), pl->vertices().rend());
        const std::size_t nv = v.size();
        std::vector<double> b;
        if (pl->hasArcs() && nv >= 2) {
            const std::size_t m = pl->closed() ? nv : nv - 1;
            b.resize(m, 0.0);
            for (std::size_t i = 0; i < m; ++i) {
                if (pl->closed())
                    b[i] = (i == m - 1) ? -pl->bulgeAt(m - 1) : -pl->bulgeAt(m - 2 - i);
                else
                    b[i] = -pl->bulgeAt(m - 1 - i);
            }
        }
        auto out = b.empty()
            ? std::make_unique<Polyline>(std::move(v), pl->closed())
            : std::make_unique<Polyline>(std::move(v), std::move(b), pl->closed());
        out->setWidth(pl->width());
        return out;
    }
    if (const auto* sp = dynamic_cast<const Spline*>(&e)) {
        std::vector<Point3> v(sp->controlPoints().rbegin(), sp->controlPoints().rend());
        return std::make_unique<Spline>(std::move(v), sp->isCV());
    }
    return nullptr;   // Arc é canônico CCW aqui; Circle/demais não têm sentido útil
}

// ------------------------------------------------------------------- BLEND --
namespace detail_blend {
// Amostra entidades ABERTAS como polilinha (Circle/polilinha fechada = vazio).
inline std::vector<Point3> sampleOpen(const Entity& e) {
    if (const auto* l = dynamic_cast<const Line*>(&e)) return {l->start(), l->end()};
    if (const auto* a = dynamic_cast<const Arc*>(&e)) {
        double sweep = a->endAngle() - a->startAngle();
        while (sweep < 0.0) sweep += 6.283185307179586;
        std::vector<Point3> pts;
        const int N = 64;
        for (int i = 0; i <= N; ++i) {
            const double ang = a->startAngle() + sweep * i / N;
            pts.emplace_back(a->center().x + a->radius() * std::cos(ang),
                             a->center().y + a->radius() * std::sin(ang), 0.0);
        }
        return pts;
    }
    if (const auto* pl = dynamic_cast<const Polyline*>(&e))
        return pl->closed() ? std::vector<Point3>{} : pl->sampledPoints();
    if (const auto* sp = dynamic_cast<const Spline*>(&e)) return sp->sample();
    return {};
}
} // namespace detail_blend

// Spline de transição TANGENTE ligando os extremos livres mais próximos de
// duas entidades abertas (estilo AutoCAD BLEND). nullptr se não aplicável.
inline std::unique_ptr<Spline> blend(const Entity& ea, const Entity& eb, double tol = 1e-9) {
    using detail_blend::sampleOpen;
    const std::vector<Point3> a = sampleOpen(ea);
    const std::vector<Point3> b = sampleOpen(eb);
    if (a.size() < 2 || b.size() < 2) return nullptr;

    // Escolhe o par de extremos (início/fim de cada) mais próximo.
    struct End { Point3 p; Point3 inner; };
    const End endsA[2] = {{a.front(), a[1]}, {a.back(), a[a.size() - 2]}};
    const End endsB[2] = {{b.front(), b[1]}, {b.back(), b[b.size() - 2]}};
    int bi = 0, bj = 0; double best = 1e300;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            const double d = std::hypot(endsA[i].p.x - endsB[j].p.x,
                                        endsA[i].p.y - endsB[j].p.y);
            if (d < best) { best = d; bi = i; bj = j; }
        }
    if (best < tol) return nullptr;   // já se tocam: nada a emendar

    // Tangente "para fora" em cada extremo escolhido.
    auto outDir = [&](const End& e) {
        double dx = e.p.x - e.inner.x, dy = e.p.y - e.inner.y;
        const double l = std::hypot(dx, dy);
        if (l < tol) return Point3{0, 0, 0};
        return Point3{dx / l, dy / l, 0.0};
    };
    const Point3 ta = outDir(endsA[bi]);
    const Point3 tb = outDir(endsB[bj]);
    const double h = best / 3.0;   // "puxada" das tangentes (G1 suave)

    std::vector<Point3> ctrl{
        endsA[bi].p,
        Point3{endsA[bi].p.x + ta.x * h, endsA[bi].p.y + ta.y * h, 0.0},
        Point3{endsB[bj].p.x + tb.x * h, endsB[bj].p.y + tb.y * h, 0.0},
        endsB[bj].p};
    return std::make_unique<Spline>(std::move(ctrl), /*cv=*/false);
}

} // namespace cleanup
} // namespace cad
