// src/core/command/commands/MirrorCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/math/Matrix4.hpp"
#include <utility>
#include <vector>

namespace cad {

// Espelha um conjunto de entidades criando CÓPIAS refletidas (padrão do AutoCAD:
// mantém os originais). A reflexão já vem pronta como matriz.
class MirrorCmd final : public Command {
public:
    MirrorCmd(std::vector<EntityId> ids, Matrix4 reflect)
        : m_srcIds(std::move(ids)), m_reflect(reflect) {}

    void execute(DrawingManager& doc) override {
        if (!m_made) {
            for (const EntityId id : m_srcIds)
                if (const Entity* e = doc.getEntity(id)) {
                    EntityPtr c = e->clone();
                    c->transform(m_reflect);
                    m_newIds.push_back(doc.addEntity(std::move(c)));
                }
            m_made = true;
        } else {
            for (auto& c : m_clones) if (c) doc.reinsert(std::move(c));
            m_clones.clear();
        }
    }
    void undo(DrawingManager& doc) override {
        m_clones.clear();
        for (const EntityId id : m_newIds) m_clones.push_back(doc.removeEntity(id));
    }
    std::string label() const override { return "MIRROR"; }

private:
    std::vector<EntityId>  m_srcIds;
    Matrix4                m_reflect;
    std::vector<EntityPtr> m_clones;
    std::vector<EntityId>  m_newIds;
    bool                   m_made{false};
};

} // namespace cad
