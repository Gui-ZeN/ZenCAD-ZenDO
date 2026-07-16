// src/core/command/commands/EraseCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include <utility>
#include <vector>

namespace cad {

// Apaga um conjunto de entidades, guardando-as para undo (Memento). Preserva
// os ids no reinsert, então a seleção/referências continuam coerentes.
class EraseCmd final : public Command {
public:
    explicit EraseCmd(std::vector<EntityId> ids) : m_ids(std::move(ids)) {}

    void execute(DrawingManager& doc) override {
        m_storage.clear();
        for (const EntityId id : m_ids) m_storage.push_back(doc.removeEntity(id));
        // OOPS: o doc memoriza CLONES do último apagamento — restauráveis
        // depois SEM desfazer o que veio em seguida (como no AutoCAD).
        std::vector<EntityPtr> clones;
        clones.reserve(m_storage.size());
        for (const EntityPtr& e : m_storage) if (e) clones.push_back(e->clone());
        doc.setLastErased(std::move(clones));
    }
    void undo(DrawingManager& doc) override {
        for (auto& e : m_storage) if (e) doc.reinsert(std::move(e));
        m_storage.clear();
    }
    std::string label() const override { return "ERASE"; }

private:
    std::vector<EntityId>  m_ids;
    std::vector<EntityPtr> m_storage;
};

} // namespace cad
