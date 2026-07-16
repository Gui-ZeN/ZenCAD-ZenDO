// src/core/command/commands/BreakCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include <memory>
#include <utility>
#include <vector>

namespace cad {

// Break: remove a entidade original (memento p/ undo) e adiciona no lugar os
// pedaços que sobraram após remover o trecho entre os 2 pontos (Line->pedaços,
// Circle->Arc, Arc->arcos). Inerte se `parts` vazio.
class BreakCmd final : public Command {
public:
    BreakCmd(EntityId id, std::vector<EntityPtr> parts)
        : m_id(id), m_parts(std::move(parts)) {}

    void execute(DrawingManager& doc) override {
        if (!m_done) {
            if (m_parts.empty()) return;
            m_orig = doc.removeEntity(m_id);
            for (auto& p : m_parts) m_newIds.push_back(doc.addEntity(std::move(p)));
            m_parts.clear();
            m_done = true;
        } else {                                   // redo
            m_orig = doc.removeEntity(m_id);       // remove o original de novo
            for (auto& c : m_clones) if (c) doc.reinsert(std::move(c));
            m_clones.clear();
        }
    }
    void undo(DrawingManager& doc) override {
        if (!m_done) return;
        m_clones.clear();
        for (const EntityId id : m_newIds) m_clones.push_back(doc.removeEntity(id));
        if (m_orig) doc.reinsert(std::move(m_orig));
    }
    std::string label() const override { return "BREAK"; }

private:
    EntityId               m_id;
    std::vector<EntityPtr> m_parts;
    EntityPtr              m_orig;
    std::vector<EntityId>  m_newIds;
    std::vector<EntityPtr> m_clones;
    bool                   m_done{false};
};

} // namespace cad
