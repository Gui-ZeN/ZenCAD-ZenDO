// src/core/command/commands/JoinCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include <memory>
#include <utility>

namespace cad {

// Join: remove duas entidades (mementos p/ undo) e adiciona a entidade unida
// (uma Line, se colineares, ou uma Polyline, se apenas se tocam num extremo).
class JoinCmd final : public Command {
public:
    JoinCmd(EntityId a, EntityId b, EntityPtr joined)
        : m_a(a), m_b(b), m_joined(std::move(joined)) {}

    void execute(DrawingManager& doc) override {
        if (!m_joined) return;
        if (!m_done) {
            m_origA = doc.removeEntity(m_a);
            m_origB = doc.removeEntity(m_b);
            m_joinedId = doc.addEntity(m_joined->clone());
            m_done = true;
        } else {                                   // redo
            m_origA = doc.removeEntity(m_a);
            m_origB = doc.removeEntity(m_b);
            if (m_joinedClone) doc.reinsert(std::move(m_joinedClone));
        }
    }
    void undo(DrawingManager& doc) override {
        if (!m_done) return;
        if (m_joinedId != kInvalidId) m_joinedClone = doc.removeEntity(m_joinedId);
        if (m_origA) doc.reinsert(std::move(m_origA));
        if (m_origB) doc.reinsert(std::move(m_origB));
    }
    std::string label() const override { return "JOIN"; }

private:
    EntityId  m_a, m_b;
    EntityPtr m_joined;
    EntityId  m_joinedId{kInvalidId};
    EntityPtr m_origA, m_origB, m_joinedClone;
    bool      m_done{false};
};

} // namespace cad
