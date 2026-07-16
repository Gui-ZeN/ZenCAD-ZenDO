// src/core/command/commands/AddEntityCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/geometry/Entity.hpp"
#include "core/document/DrawingManager.hpp"
#include <utility>

namespace cad {

// Insere uma entidade. Owner alterna entre o comando (fora do doc) e o doc:
//  - 1ª execução: addEntity() atribui um id novo;
//  - redo:        reinsert() preserva o id original.
class AddEntityCmd final : public Command {
public:
    explicit AddEntityCmd(EntityPtr entity) : m_entity(std::move(entity)) {}

    void execute(DrawingManager& doc) override {
        if (m_id == kInvalidId) m_id = doc.addEntity(std::move(m_entity));
        else                    doc.reinsert(std::move(m_entity));
    }
    void undo(DrawingManager& doc) override { m_entity = doc.removeEntity(m_id); }
    std::string label() const override { return "ADD"; }

private:
    EntityPtr m_entity;            // dono enquanto FORA do documento
    EntityId  m_id{kInvalidId};
};

} // namespace cad
