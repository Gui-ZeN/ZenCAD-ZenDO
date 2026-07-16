// src/core/command/commands/ReplaceCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include <memory>

namespace cad {

// Substitui a geometria de UMA entidade (preservando o id) por outra. Guarda a
// original para undo. Usado por Lengthen (e qualquer edição "trocar geometria").
class ReplaceCmd final : public Command {
public:
    ReplaceCmd(EntityId id, EntityPtr neu) : m_id(id), m_new(std::move(neu)) {}

    void execute(DrawingManager& doc) override {
        if (!m_done) {
            const Entity* e = doc.getEntity(m_id);
            if (!e || !m_new) return;
            m_old = e->clone();
            doc.replaceEntity(m_id, m_new->clone());
            m_done = true;
        } else if (m_new) {
            doc.replaceEntity(m_id, m_new->clone());
        }
    }
    void undo(DrawingManager& doc) override {
        if (m_done && m_old) doc.replaceEntity(m_id, m_old->clone());
    }
    std::string label() const override { return "REPLACE"; }

private:
    EntityId  m_id;
    EntityPtr m_new;
    EntityPtr m_old;
    bool      m_done{false};
};

} // namespace cad
