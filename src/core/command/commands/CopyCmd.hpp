// src/core/command/commands/CopyCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/math/Matrix4.hpp"
#include <utility>
#include <vector>

namespace cad {

// Copia um conjunto de entidades com um deslocamento. As cópias são clones
// transformados; undo remove os clones, redo os reinsere.
class CopyCmd final : public Command {
public:
    CopyCmd(std::vector<EntityId> ids, Vec3 delta)
        : m_srcIds(std::move(ids)), m_delta(delta) {}

    void execute(DrawingManager& doc) override {
        const Matrix4 m = Matrix4::translation(m_delta);
        if (!m_made) {
            for (const EntityId id : m_srcIds)
                if (const Entity* e = doc.getEntity(id)) {
                    EntityPtr c = e->clone();
                    c->transform(m);
                    m_newIds.push_back(doc.addEntity(std::move(c)));
                }
            m_made = true;
        } else {  // redo: reinsere os clones guardados
            for (auto& c : m_clones) if (c) doc.reinsert(std::move(c));
            m_clones.clear();
        }
    }
    void undo(DrawingManager& doc) override {
        m_clones.clear();
        for (const EntityId id : m_newIds) m_clones.push_back(doc.removeEntity(id));
    }
    std::string label() const override { return "COPY"; }

private:
    std::vector<EntityId>  m_srcIds;
    Vec3                   m_delta;
    std::vector<EntityPtr> m_clones;
    std::vector<EntityId>  m_newIds;
    bool                   m_made{false};
};

} // namespace cad
