// src/core/command/commands/TrimCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/edit/TrimOps.hpp"
#include "core/math/Vec.hpp"
#include <memory>
#include <utility>
#include <vector>

namespace cad {

// Apara (trim) a entidade `target` nos pontos de corte `cuts` (interseções com as
// arestas de corte), removendo o trecho que contém `pick`. Line/Circle/Arc:
//   * 1 peça  -> troca IN-PLACE (preserva o id e referências);
//   * 2+ peças -> remove o alvo e adiciona os pedaços (ids novos).
// Os pedaços herdam camada/cor/tipo/espessura do original. Undo/redo seguro.
class TrimCmd final : public Command {
public:
    TrimCmd(EntityId targetId, std::vector<Point3> cuts, Point3 pick)
        : m_targetId(targetId), m_cuts(std::move(cuts)), m_pick(pick) {}

    void execute(DrawingManager& doc) override {
        if (!m_made) {
            const Entity* tgt = doc.getEntity(m_targetId);
            if (!tgt) return;
            std::vector<EntityPtr> pieces = splitEntityAt(*tgt, m_cuts, m_pick);
            if (pieces.empty()) return;                 // nada a aparar: inerte
            for (auto& p : pieces) {                    // herda as propriedades
                p->setLayer(tgt->layer());
                p->setColor(tgt->color());
                p->setLineType(tgt->lineType());
                p->setLineWeight(tgt->lineWeight());
            }
            if (pieces.size() == 1) {                   // troca in-place (mesmo id)
                m_inPlace = true;
                m_before  = tgt->clone();
                m_after   = pieces[0]->clone();
                doc.replaceEntity(m_targetId, std::move(pieces[0]));
            } else {                                    // split: remove + adiciona
                m_inPlace = false;
                m_memento = doc.removeEntity(m_targetId);
                for (auto& p : pieces) m_newIds.push_back(doc.addEntity(std::move(p)));
            }
            m_made = true;
        } else if (m_inPlace) {                          // redo in-place
            doc.replaceEntity(m_targetId, m_after->clone());
        } else {                                         // redo split
            m_memento = doc.removeEntity(m_targetId);
            for (auto& c : m_pieceClones) if (c) doc.reinsert(std::move(c));
            m_pieceClones.clear();
        }
    }

    void undo(DrawingManager& doc) override {
        if (!m_made) return;
        if (m_inPlace) {
            doc.replaceEntity(m_targetId, m_before->clone());
        } else {
            m_pieceClones.clear();
            for (const EntityId id : m_newIds) m_pieceClones.push_back(doc.removeEntity(id));
            if (m_memento) doc.reinsert(std::move(m_memento));
        }
    }

    std::string label() const override { return "TRIM"; }

private:
    EntityId            m_targetId;
    std::vector<Point3> m_cuts;
    Point3              m_pick;
    bool      m_inPlace{false};
    EntityPtr m_before;
    EntityPtr m_after;
    EntityPtr m_memento;
    std::vector<EntityId>  m_newIds;
    std::vector<EntityPtr> m_pieceClones;
    bool      m_made{false};
};

} // namespace cad
