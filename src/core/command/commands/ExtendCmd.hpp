// src/core/command/commands/ExtendCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/edit/ExtendOps.hpp"
#include <memory>
#include <optional>
#include <utility>

namespace cad {

// Estende (extend) a Line target até a reta infinita de contorno definida pela
// Line boundary, alongando a extremidade mais próxima de pickEnd. A geometria do
// target é trocada in-place (mantendo o id) via replaceEntity; guarda-se o clone
// da geometria original (m_before) para undo e o resultado (m_after) para redo.
// Extend só vale linha-vs-linha: se algo falhar (não são linhas, paralelas, ou a
// interseção encurtaria em vez de estender), o comando vira inerte — execute e
// undo são no-ops seguros.
class ExtendCmd final : public Command {
public:
    ExtendCmd(EntityId targetId, EntityId boundaryId, Point3 pickEnd)
        : m_targetId(targetId), m_boundaryId(boundaryId), m_pickEnd(pickEnd) {}

    void execute(DrawingManager& doc) override {
        if (!m_done) {                 // primeira execução: calcula a extensão
            const Entity* tgt = doc.getEntity(m_targetId);
            const Entity* bnd = doc.getEntity(m_boundaryId);
            if (!tgt || !bnd) return;

            std::optional<EntityPtr> ext = extendEntity(*tgt, *bnd, m_pickEnd);
            if (!ext || !*ext) return;                 // sem extensão: inerte

            EntityPtr neu = std::move(*ext);
            neu->setLayer(tgt->layer());               // herda propriedades
            neu->setColor(tgt->color());
            neu->setLineType(tgt->lineType());
            neu->setLineWeight(tgt->lineWeight());

            m_before = tgt->clone();                   // memento p/ undo
            m_after  = neu->clone();                   // p/ redo
            doc.replaceEntity(m_targetId, std::move(neu));
            m_done = true;
        } else if (m_after) {          // redo: re-troca pela geometria estendida
            doc.replaceEntity(m_targetId, m_after->clone());
        }
    }

    void undo(DrawingManager& doc) override {
        if (m_done && m_before)
            doc.replaceEntity(m_targetId, m_before->clone());
    }

    std::string label() const override { return "EXTEND"; }

private:
    EntityId  m_targetId;
    EntityId  m_boundaryId;
    Point3    m_pickEnd;
    EntityPtr m_before;        // geometria original do target (p/ undo)
    EntityPtr m_after;         // geometria estendida (p/ redo)
    bool      m_done{false};
};

} // namespace cad
