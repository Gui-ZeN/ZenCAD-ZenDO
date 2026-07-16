// src/core/snap/SnapEngine.cpp
#include "core/snap/SnapEngine.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/math/AABB.hpp"
#include "core/math/Ray.hpp"
#include "core/edit/IntersectOps.hpp"

namespace cad {
namespace {
// Prioridade de desempate quando dois candidatos estão à mesma distância:
// Intersection/Endpoint/Center vencem Midpoint, que vence Quadrant.
int priority(SnapType t) {
    switch (t) {
        case SnapType::Intersection:
        case SnapType::Endpoint:
        case SnapType::Node:
        case SnapType::Insertion:
        case SnapType::Center:   return 0;
        case SnapType::Midpoint:
        case SnapType::GeomCenter:
        case SnapType::Perpendicular:
        case SnapType::Tangent:
        case SnapType::AppInt:   return 1;   // a interseção REAL vence a aparente
        case SnapType::Quadrant: return 2;
        default:                 return 3;
    }
}

// Alcance da busca por segmentos-FONTE de Extensão/Paralela, em múltiplos da
// tolerância: a fonte tipicamente está LONGE do cursor (você prolonga uma linha
// bem além dela), então a sonda padrão — centrada no cursor — não a acharia.
constexpr double kExtRange = 40.0;
} // namespace

SnapResult SnapEngine::resolve(const Point3& cursor, double tolWorld,
                               const DrawingManager& doc, const Point3* from,
                               unsigned typeMask) const {
    const AABB probe = AABB::fromCenterHalf(cursor, Vec3{tolWorld, tolWorld, tolWorld});
    const std::vector<EntityId> ids = doc.query(probe);

    SnapResult best;
    double bestDist  = tolWorld;   // só aceita dentro da tolerância
    int    bestPrio  = 99;
    int    bestClass = 99;         // 0 = snap de PONTO; 1 = "deferred" (Perp/Tangente)
    EntityId candId  = kInvalidId; // entidade do candidato corrente (associatividade)

    // Atualiza o melhor candidato. Duas CLASSES (prioridade do AutoSnap):
    //   classe 0 (pontos notáveis: endpoint/interseção/…) SEMPRE vence a
    //   classe 1 (deferred: Perpendicular/Tangente, que competem pela distância
    //   do cursor à ENTIDADE — senão "pé em qualquer lugar da linha" roubaria
    //   dos snaps de ponto perto de cruzamentos). Dentro da mesma classe vale
    //   a menor distância, com desempate pela prioridade do tipo.
    auto considerCls = [&](const Point3& p, SnapType type, double rankDist, int cls) {
        if (!(typeMask & snapBit(type))) return;          // tipo desligado no OSNAP
        if (rankDist > tolWorld) return;
        const int pr = priority(type);
        bool win = false;
        if (cls < bestClass) {
            win = true;
        } else if (cls == bestClass) {
            const bool closer = rankDist < bestDist - 1e-9;
            const bool tieWin = std::abs(rankDist - bestDist) <= 1e-9 && pr < bestPrio;
            win = closer || tieWin;
        }
        if (win) {
            bestClass = cls; bestDist = rankDist; bestPrio = pr;
            best.hit = true; best.point = p; best.type = type;
            best.entity = candId;
        }
    };
    auto considerAt = [&](const Point3& p, SnapType type, double rankDist) {
        considerCls(p, type, rankDist, 1);                // deferred
    };
    auto consider = [&](const Point3& p, SnapType type) {
        considerCls(p, type, std::hypot(p.x - cursor.x, p.y - cursor.y), 0);   // ponto
    };

    // 1) Pontos notáveis de cada entidade (Endpoint/Mid/Center/Quadrant).
    std::vector<SnapPoint> pts;
    for (const EntityId id : ids) {
        const Entity* e = doc.getEntity(id);
        if (!e || !e->visible()) continue;
        pts.clear();
        e->appendSnapPoints(pts);
        candId = id;
        for (const SnapPoint& sp : pts) consider(sp.point, sp.type);
    }

    // 1b) Perpendicular ("deferred", à la AutoCAD): pairar sobre a ENTIDADE já
    //     captura o pé da perpendicular de `from` — o rank usa a distância do
    //     cursor à entidade, não ao pé (que pode estar longe do cursor).
    if (from) {
        // Pé da perpendicular de `from` ao SEGMENTO A-B; o rank usa a distância
        // do cursor ao segmento (+0.001·dPonto p/ desempatar deferred).
        auto considerPerpSeg = [&](const Point3& A, const Point3& B) {
            const double ax = A.x, ay = A.y;
            const double dx = B.x - ax, dy = B.y - ay;
            const double len2 = dx * dx + dy * dy;
            if (len2 <= 1e-18) return;
            const double t = ((from->x - ax) * dx + (from->y - ay) * dy) / len2;
            if (t < 0.0 || t > 1.0) return;       // pé fora do segmento
            double tc = ((cursor.x - ax) * dx + (cursor.y - ay) * dy) / len2;
            tc = std::max(0.0, std::min(1.0, tc));
            const double qx = ax + tc * dx, qy = ay + tc * dy;
            const double dLine = std::hypot(cursor.x - qx, cursor.y - qy);
            const Point3 foot{ax + t * dx, ay + t * dy, 0.0};
            const double dPt = std::hypot(cursor.x - foot.x, cursor.y - foot.y);
            considerAt(foot, SnapType::Perpendicular, dLine + 1e-3 * dPt);
        };
        for (const EntityId id : ids) {
            const Entity* e = doc.getEntity(id);
            if (!e || !e->visible()) continue;
            candId = id;
            if (const auto* l = dynamic_cast<const Line*>(e)) {
                considerPerpSeg(l->start(), l->end());
            } else if (const auto* pl = dynamic_cast<const Polyline*>(e)) {
                // Polilinha: cada trecho RETO é um segmento candidato (cobre
                // retângulos/paredes — o caso mais comum de perpendicular).
                const auto& v = pl->vertices();
                if (v.size() >= 2) {
                    const std::size_t n = v.size();
                    const std::size_t segs = (pl->closed() && n >= 3) ? n : n - 1;
                    for (std::size_t i = 0; i < segs; ++i) {
                        if (std::fabs(pl->bulgeAt(i)) > 1e-12) continue;   // arco: fora
                        considerPerpSeg(v[i], v[(i + 1) % n]);
                    }
                }
            } else if (const auto* c = dynamic_cast<const Circle*>(e)) {
                const double dx = from->x - c->center().x, dy = from->y - c->center().y;
                const double d = std::hypot(dx, dy);
                if (d > 1e-9) {
                    const double r = c->radius();
                    const double dCirc = std::abs(
                        std::hypot(cursor.x - c->center().x, cursor.y - c->center().y) - r);
                    const Point3 foot{c->center().x + dx / d * r,
                                      c->center().y + dy / d * r, 0.0};
                    const double dPt = std::hypot(cursor.x - foot.x, cursor.y - foot.y);
                    considerAt(foot, SnapType::Perpendicular, dCirc + 1e-3 * dPt);
                }
            }
        }
    }

    // 1c) Tangent: pontos de tangência de `from` (externo) a círculos vizinhos.
    //     Dado d=|from-C|, com d>R, há 2 pontos onde a reta from->ponto é
    //     perpendicular ao raio. Em coordenadas polares a partir de C:
    //     direção base beta = atan2(from-C); abertura alpha = acos(R/d);
    //     pontos = C + R*(cos(beta±alpha), sin(beta±alpha)).
    //     Para from dentro/sobre o círculo (d<=R) não existe tangente.
    if (from) {
        for (const EntityId id : ids) {
            const Entity* e = doc.getEntity(id);
            if (!e || !e->visible()) continue;
            candId = id;
            if (const auto* c = dynamic_cast<const Circle*>(e)) {
                const double dx = from->x - c->center().x, dy = from->y - c->center().y;
                const double d = std::hypot(dx, dy);
                const double r = c->radius();
                if (d > r + 1e-9) {            // from estritamente externo: tangentes existem
                    const double alpha = std::acos(r / d);
                    const double beta  = std::atan2(dy, dx);
                    // "Deferred": pairar sobre o CÍRCULO basta; entre as duas
                    // tangentes vence a mais próxima do cursor (epsilon no rank).
                    const double dCirc = std::abs(
                        std::hypot(cursor.x - c->center().x, cursor.y - c->center().y) - r);
                    const Point3 t1{c->center().x + r * std::cos(beta + alpha),
                                    c->center().y + r * std::sin(beta + alpha), 0.0};
                    const Point3 t2{c->center().x + r * std::cos(beta - alpha),
                                    c->center().y + r * std::sin(beta - alpha), 0.0};
                    const double d1 = std::hypot(cursor.x - t1.x, cursor.y - t1.y);
                    const double d2 = std::hypot(cursor.x - t2.x, cursor.y - t2.y);
                    considerAt(d1 <= d2 ? t1 : t2, SnapType::Tangent,
                               dCirc + 1e-3 * std::min(d1, d2));
                }
            }
        }
    }

    // 1d) Extension e Parallel ("deferred", classe 1). Ambos usam uma sonda
    //     AMPLIADA (kExtRange×tol) porque o segmento-fonte fica longe do cursor.
    if (typeMask & (snapBit(SnapType::Extension) | snapBit(SnapType::Parallel))) {
        const double range = kExtRange * tolWorld;
        const AABB probeExt = AABB::fromCenterHalf(cursor, Vec3{range, range, range});
        const std::vector<EntityId> extIds = doc.query(probeExt);

        // Extension: prolongamento COLINEAR do segmento A-B além dos extremos.
        // Aceita o pé da projeção do cursor sobre a reta-suporte quando ele cai
        // FORA do segmento (t<0 ou t>1), a até kExtRange×tol do extremo.
        auto considerExtSeg = [&](const Point3& A, const Point3& B) {
            if (!(typeMask & snapBit(SnapType::Extension))) return;
            const double dx = B.x - A.x, dy = B.y - A.y;
            const double len2 = dx * dx + dy * dy;
            if (len2 <= 1e-18) return;
            const double t = ((cursor.x - A.x) * dx + (cursor.y - A.y) * dy) / len2;
            if (t >= 0.0 && t <= 1.0) return;             // sobre o segmento: não é extensão
            const Point3 foot{A.x + t * dx, A.y + t * dy, 0.0};
            const double dPerp = std::hypot(cursor.x - foot.x, cursor.y - foot.y);
            const Point3& endNear = (t < 0.0) ? A : B;    // extremo do lado prolongado
            const double dEnd = std::hypot(foot.x - endNear.x, foot.y - endNear.y);
            if (dEnd > kExtRange * tolWorld) return;      // longe demais do extremo
            considerAt(foot, SnapType::Extension, dPerp + 1e-3 * dEnd);
        };
        // Parallel: reta por `from` PARALELA ao segmento A-B; gruda no pé da
        // projeção do cursor sobre essa reta quando ele passa perto dela.
        auto considerParSeg = [&](const Point3& A, const Point3& B) {
            if (!(typeMask & snapBit(SnapType::Parallel)) || !from) return;
            const double dx = B.x - A.x, dy = B.y - A.y;
            const double len = std::hypot(dx, dy);
            if (len <= 1e-9) return;
            const double ux = dx / len, uy = dy / len;
            const double t = (cursor.x - from->x) * ux + (cursor.y - from->y) * uy;
            if (std::abs(t) <= 2.0 * tolWorld) return;    // perto demais da base: ruído
            const Point3 foot{from->x + t * ux, from->y + t * uy, 0.0};
            const double dPerp = std::hypot(cursor.x - foot.x, cursor.y - foot.y);
            considerAt(foot, SnapType::Parallel, dPerp);
        };
        for (const EntityId id : extIds) {
            const Entity* e = doc.getEntity(id);
            if (!e || !e->visible()) continue;
            candId = id;
            if (const auto* l = dynamic_cast<const Line*>(e)) {
                considerExtSeg(l->start(), l->end());
                considerParSeg(l->start(), l->end());
            } else if (const auto* pl = dynamic_cast<const Polyline*>(e)) {
                const auto& v = pl->vertices();
                if (v.size() >= 2) {
                    const std::size_t n = v.size();
                    const std::size_t segs = (pl->closed() && n >= 3) ? n : n - 1;
                    for (std::size_t i = 0; i < segs; ++i) {
                        if (std::fabs(pl->bulgeAt(i)) > 1e-12) continue;   // arco: fora
                        considerExtSeg(v[i], v[(i + 1) % n]);
                        considerParSeg(v[i], v[(i + 1) % n]);
                    }
                }
            }
        }
    }

    // 2) Interseções entre pares de entidades vizinhas.
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const Entity* a = doc.getEntity(ids[i]);
        if (!a || !a->visible()) continue;
        candId = ids[i];
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            const Entity* b = doc.getEntity(ids[j]);
            if (!b || !b->visible()) continue;
            for (const Point3& ip : intersectEntities(*a, *b))
                consider(ip, SnapType::Intersection);
        }
    }

    // 2b) Interseção APARENTE (linha×linha): onde as retas-suporte cruzariam
    //     se estendidas. A interseção REAL (acima) vence por prioridade; aqui
    //     só entra o cruzamento FORA de pelo menos um dos segmentos.
    if (typeMask & snapBit(SnapType::AppInt)) {
        const double range = kExtRange * tolWorld;
        const AABB probeAi = AABB::fromCenterHalf(cursor, Vec3{range, range, range});
        const std::vector<EntityId> aiIds = doc.query(probeAi);
        std::vector<const Line*> lines;
        std::vector<EntityId> lineIds;
        for (const EntityId id : aiIds) {
            const Entity* e = doc.getEntity(id);
            if (!e || !e->visible()) continue;
            if (const auto* l = dynamic_cast<const Line*>(e)) {
                lines.push_back(l);
                lineIds.push_back(id);
            }
        }
        for (std::size_t i = 0; i < lines.size(); ++i) {
            for (std::size_t j = i + 1; j < lines.size(); ++j) {
                const Point3 &a = lines[i]->start(), &b = lines[i]->end();
                const Point3 &c = lines[j]->start(), &d = lines[j]->end();
                const double den = (b.x - a.x) * (d.y - c.y) - (b.y - a.y) * (d.x - c.x);
                if (std::abs(den) < 1e-12) continue;      // paralelas
                const double t = ((c.x - a.x) * (d.y - c.y) - (c.y - a.y) * (d.x - c.x)) / den;
                const double u = ((c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x)) / den;
                if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0) continue;   // real
                candId = lineIds[i];
                consider(Point3{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), 0.0},
                         SnapType::AppInt);
            }
        }
    }

    // 3) Nearest (fallback): se nenhum ponto notável foi capturado, gruda no
    //    ponto mais próximo SOBRE a geometria (projeção via hitTest).
    if (!best.hit && (typeMask & snapBit(SnapType::Nearest))) {
        double nd = tolWorld;
        for (const EntityId id : ids) {
            const Entity* e = doc.getEntity(id);
            if (!e || !e->visible()) continue;
            Ray r; r.origin = cursor;
            const HitResult h = e->hitTest(r, tolWorld);
            if (h.hit && h.distance < nd) {
                nd = h.distance;
                best.hit = true; best.point = h.point; best.type = SnapType::Nearest;
                best.entity = id;
            }
        }
    }

    return best;
}

} // namespace cad
