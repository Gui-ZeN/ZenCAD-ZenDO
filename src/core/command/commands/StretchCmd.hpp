// src/core/command/commands/StretchCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/math/AABB.hpp"
#include "core/math/Matrix4.hpp"
#include <memory>
#include <vector>

namespace cad {

// Stretch (estica): move por `delta` apenas os VÉRTICES que caem dentro da janela
// `window` (estilo Crossing do AutoCAD). Vértices fora ficam parados — assim uma
// linha/polilínha "estica". Entidades de outros tipos totalmente dentro da janela
// são transladadas inteiras; parcialmente dentro de tipo não suportado: ignoradas.
class StretchCmd final : public Command {
public:
    StretchCmd(std::vector<EntityId> ids, AABB window, Vec3 delta)
        : m_ids(std::move(ids)), m_win(window), m_delta(delta) {}

    void execute(DrawingManager& doc) override {
        if (!m_done) {
            for (const EntityId id : m_ids) {
                const Entity* e = doc.getEntity(id);
                if (!e) continue;
                EntityPtr nw = stretched(*e);
                if (!nw) continue;
                m_ids2.push_back(id);
                m_before.push_back(e->clone());
                m_after.push_back(nw->clone());
                doc.replaceEntity(id, std::move(nw));
            }
            m_done = true;
        } else {                                 // redo
            for (std::size_t i = 0; i < m_ids2.size(); ++i)
                doc.replaceEntity(m_ids2[i], m_after[i]->clone());
        }
    }

    void undo(DrawingManager& doc) override {
        for (std::size_t i = 0; i < m_ids2.size(); ++i)
            doc.replaceEntity(m_ids2[i], m_before[i]->clone());
    }

    std::string label() const override { return "STRETCH"; }

private:
    Point3 mv(const Point3& p) const {
        return m_win.contains(p) ? Point3{p.x + m_delta.x, p.y + m_delta.y, p.z + m_delta.z} : p;
    }

    std::unique_ptr<Entity> stretched(const Entity& e) const {
        if (const auto* l = dynamic_cast<const Line*>(&e))
            return std::make_unique<Line>(mv(l->start()), mv(l->end()));
        if (const auto* pl = dynamic_cast<const Polyline*>(&e)) {
            std::vector<Point3> v = pl->vertices();
            for (Point3& p : v) p = mv(p);
            return std::make_unique<Polyline>(std::move(v), pl->closed());
        }
        // Outros tipos: se totalmente contidos na janela, transladam inteiros.
        const AABB bb = e.boundingBox();
        if (m_win.contains(bb.min) && m_win.contains(bb.max)) {
            EntityPtr c = e.clone();
            c->transform(Matrix4::translation(m_delta));
            return c;
        }
        return nullptr;
    }

    std::vector<EntityId>  m_ids;
    AABB                   m_win;
    Vec3                   m_delta;
    std::vector<EntityId>  m_ids2;            // ids efetivamente alterados
    std::vector<EntityPtr> m_before, m_after; // mementos por id
    bool                   m_done{false};
};

} // namespace cad
