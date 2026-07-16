// src/core/command/commands/DeleteEntityCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/geometry/Entity.hpp"
#include "core/document/DrawingManager.hpp"
#include <utility>

namespace cad {

// Apaga uma entidade guardando-a para um possível undo (Memento). O reinsert
// preserva o id, então referências externas (seleção) continuam válidas.
class DeleteEntityCmd final : public Command {
public:
    explicit DeleteEntityCmd(EntityId id) : m_id(id) {}

    void execute(DrawingManager& doc) override { m_storage = doc.removeEntity(m_id); }
    void undo(DrawingManager& doc) override {
        if (m_storage) doc.reinsert(std::move(m_storage));
    }
    std::string label() const override { return "ERASE"; }

private:
    EntityId  m_id;
    EntityPtr m_storage;           // dono enquanto a entidade está apagada
};

} // namespace cad
