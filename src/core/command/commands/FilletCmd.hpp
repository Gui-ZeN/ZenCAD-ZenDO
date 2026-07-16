// src/core/command/commands/FilletCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Line.hpp"
#include "core/edit/FilletOps.hpp"
#include <memory>
#include <utility>
#include <cmath>

namespace cad {

// Concorda (fillet) duas entidades (Line/Arc/Circle, em qualquer combinação) com
// um arco tangente de raio `radius`. Cada entidade é aparada até seu ponto de
// tangência mantendo o lado clicado (in-place, mesmo id); o arco de concordância
// é adicionado. Mementos guardam o original (undo) e o resultado (redo). Inerte
// se a concordância não for possível (raio inválido, sem solução).
class FilletCmd final : public Command {
public:
    FilletCmd(EntityId id1, EntityId id2, double radius, Point3 pick1, Point3 pick2)
        : m_id1(id1), m_id2(id2), m_radius(radius), m_pick1(pick1), m_pick2(pick2) {}

    void execute(DrawingManager& doc) override {
        if (!m_done) {
            const Entity* e1 = doc.getEntity(m_id1);
            const Entity* e2 = doc.getEntity(m_id2);
            if (!e1 || !e2) return;

            EntityPtr n1, n2, arc;
            if (m_radius <= 0.0) {
                // RAIO 0 = canto vivo: apara/estende as 2 retas até a interseção
                // das retas infinitas (sem arco), como o FILLET R0 do AutoCAD.
                const auto* l1 = dynamic_cast<const Line*>(e1);
                const auto* l2 = dynamic_cast<const Line*>(e2);
                Point3 corner;
                if (!l1 || !l2 || !lineInfiniteIntersect(*l1, *l2, corner)) return;
                n1 = trimToTangent(*e1, corner, m_pick1);
                n2 = trimToTangent(*e2, corner, m_pick2);
                if (!n1 || !n2) return;
            } else {
                const FilletGeom g = filletGeometry(*e1, *e2, m_radius, m_pick1, m_pick2);
                if (!g.ok) return;
                n1 = trimToTangent(*e1, g.tan1, m_pick1);
                n2 = trimToTangent(*e2, g.tan2, m_pick2);
                if (!n1 || !n2) return;
                arc = std::make_unique<Arc>(g.center, g.radius, g.startAngle, g.endAngle);
            }
            copyProps(*e1, *n1);
            copyProps(*e2, *n2);
            if (arc) copyProps(*e1, *arc);       // o arco herda da 1ª — ANTES do replace

            m_before1 = e1->clone(); m_before2 = e2->clone();   // e1/e2 ainda válidos aqui
            m_after1  = n1->clone(); m_after2  = n2->clone();
            if (arc) m_arc = arc->clone();

            doc.replaceEntity(m_id1, std::move(n1));            // a partir daqui e1/e2 podem invalidar
            doc.replaceEntity(m_id2, std::move(n2));
            if (arc) m_arcId = doc.addEntity(std::move(arc));
            m_done = true;
        } else {   // redo
            if (m_after1) doc.replaceEntity(m_id1, m_after1->clone());
            if (m_after2) doc.replaceEntity(m_id2, m_after2->clone());
            if (m_arcClone) doc.reinsert(std::move(m_arcClone));
        }
    }

    void undo(DrawingManager& doc) override {
        if (!m_done) return;
        if (m_before1) doc.replaceEntity(m_id1, m_before1->clone());
        if (m_before2) doc.replaceEntity(m_id2, m_before2->clone());
        if (m_arcId != kInvalidId) m_arcClone = doc.removeEntity(m_arcId);
    }

    std::string label() const override { return "FILLET"; }

private:
    // Interseção das RETAS INFINITAS que contêm os 2 segmentos (p/ o canto R0).
    static bool lineInfiniteIntersect(const Line& a, const Line& b, Point3& out) {
        const Point3 p = a.start();
        const double rx = a.end().x - p.x, ry = a.end().y - p.y;
        const Point3 q = b.start();
        const double sx = b.end().x - q.x, sy = b.end().y - q.y;
        const double d = rx * sy - ry * sx;
        if (std::fabs(d) < 1e-12) return false;             // paralelas
        const double t = ((q.x - p.x) * sy - (q.y - p.y) * sx) / d;
        out = Point3{p.x + t * rx, p.y + t * ry, 0.0};
        return true;
    }

    static void copyProps(const Entity& src, Entity& dst) {
        dst.setLayer(src.layer());
        dst.setColor(src.color());
        dst.setLineType(src.lineType());
        dst.setLineWeight(src.lineWeight());
    }

    EntityId  m_id1, m_id2;
    double    m_radius;
    Point3    m_pick1, m_pick2;
    EntityId  m_arcId{kInvalidId};
    EntityPtr m_before1, m_before2;   // originais (undo)
    EntityPtr m_after1, m_after2;     // aparados (redo)
    EntityPtr m_arc;                  // referência do arco resultante
    EntityPtr m_arcClone;            // dono do arco enquanto fora do doc (após undo)
    bool      m_done{false};
};

} // namespace cad
