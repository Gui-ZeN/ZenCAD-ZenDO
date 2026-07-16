// src/core/command/commands/MoveCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/math/Matrix4.hpp"
#include <utility>
#include <vector>

namespace cad {

// Move um conjunto de entidades por um deslocamento. Undo aplica o inverso
// (translação é exatamente reversível — sem necessidade de snapshot).
class MoveCmd final : public Command {
public:
    MoveCmd(std::vector<EntityId> ids, Vec3 delta)
        : m_ids(std::move(ids)), m_delta(delta) {}

    void execute(DrawingManager& doc) override { apply(doc, m_delta); }
    void undo(DrawingManager& doc) override {
        apply(doc, Vec3{-m_delta.x, -m_delta.y, -m_delta.z});
    }
    std::string label() const override { return "MOVE"; }

private:
    void apply(DrawingManager& doc, const Vec3& d) {
        const Matrix4 m = Matrix4::translation(d);
        for (const EntityId id : m_ids)
            if (Entity* e = doc.getEntity(id)) { e->transform(m); doc.markDirty(id); }
    }
    std::vector<EntityId> m_ids;
    Vec3                  m_delta;
};

} // namespace cad
