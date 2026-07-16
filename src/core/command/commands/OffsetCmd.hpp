// src/core/command/commands/OffsetCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/edit/GeometryOps.hpp"
#include <memory>
#include <utility>

namespace cad {

// Cria uma cópia deslocada (offset) de uma Line ou Circle. A entidade original
// é preservada; gera-se uma nova entidade paralela/concêntrica do lado do ponto
// side, à distância dist. Undo remove a nova entidade (guardando o clone para
// redo); redo a reinsere preservando o id.
class OffsetCmd final : public Command {
public:
    OffsetCmd(EntityId src, double dist, Point3 side)
        : m_src(src), m_dist(dist), m_side(side) {}

    void execute(DrawingManager& doc) override {
        if (!m_made) {
            EntityPtr created;
            if (const Entity* e = doc.getEntity(m_src)) {
                if (const auto* line = dynamic_cast<const Line*>(e))
                    created = std::make_unique<Line>(
                        offsetLine(*line, m_dist, m_side));
                else if (const auto* circle = dynamic_cast<const Circle*>(e))
                    created = std::make_unique<Circle>(
                        offsetCircle(*circle, m_dist, m_side));
                else if (const auto* arc = dynamic_cast<const Arc*>(e))
                    created = std::make_unique<Arc>(
                        offsetArc(*arc, m_dist, m_side));
                else if (const auto* pl = dynamic_cast<const Polyline*>(e))
                    created = std::make_unique<Polyline>(
                        offsetPolyline(*pl, m_dist, m_side));
                if (created) {   // herda layer/cor/tipo/espessura da LINHA ORIGINAL
                    created->setLayer(e->layer());
                    created->setColor(e->color());
                    created->setLineType(e->lineType());
                    created->setLineWeight(e->lineWeight());
                }
            }
            if (created) m_newId = doc.addEntity(std::move(created));
            m_made = true;
        } else if (m_clone) {  // redo: reinsere o clone guardado
            doc.reinsert(std::move(m_clone));
        }
    }

    void undo(DrawingManager& doc) override {
        if (m_newId != kInvalidId) m_clone = doc.removeEntity(m_newId);
    }

    std::string label() const override { return "OFFSET"; }

private:
    EntityId    m_src;
    double      m_dist;
    Point3      m_side;
    EntityId  m_newId{kInvalidId};
    EntityPtr m_clone;          // dono enquanto FORA do documento (após undo)
    bool      m_made{false};
};

} // namespace cad
