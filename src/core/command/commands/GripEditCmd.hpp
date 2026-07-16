// src/core/command/commands/GripEditCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/edit/GripOps.hpp"
#include <memory>

namespace cad {

// Edição por grip: move um ponto editável de UMA entidade para `newPos`,
// substituindo sua geometria (preservando o id). Undo restaura a original.
class GripEditCmd final : public Command {
public:
    GripEditCmd(EntityId id, int gripIndex, Point3 newPos)
        : m_id(id), m_grip(gripIndex), m_newPos(newPos) {}

    void execute(DrawingManager& doc) override {
        if (!m_done) {
            const Entity* e = doc.getEntity(m_id);
            if (!e) return;
            EntityPtr moved = withGripMoved(*e, m_grip, m_newPos);
            if (!moved) return;             // tipo sem grips / índice inválido: inerte
            m_before = e->clone();
            m_after  = moved->clone();
            doc.replaceEntity(m_id, std::move(moved));
            m_done = true;
        } else if (m_after) {               // redo
            doc.replaceEntity(m_id, m_after->clone());
        }
    }

    void undo(DrawingManager& doc) override {
        if (m_done && m_before) doc.replaceEntity(m_id, m_before->clone());
    }

    std::string label() const override { return "GRIP"; }

private:
    EntityId  m_id;
    int       m_grip;
    Point3    m_newPos;
    EntityPtr m_before;   // geometria original (p/ undo)
    EntityPtr m_after;    // geometria editada  (p/ redo)
    bool      m_done{false};
};

} // namespace cad
