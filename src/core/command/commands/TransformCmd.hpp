// src/core/command/commands/TransformCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/math/Matrix4.hpp"
#include <utility>
#include <vector>

namespace cad {

// Transforma um conjunto de entidades por uma matriz afim. O chamador fornece a
// matriz direta e a inversa (para undo) — serve a Rotate, Scale, Move etc.
class TransformCmd final : public Command {
public:
    TransformCmd(std::vector<EntityId> ids, Matrix4 fwd, Matrix4 inv)
        : m_ids(std::move(ids)), m_fwd(fwd), m_inv(inv) {}

    void execute(DrawingManager& doc) override { apply(doc, m_fwd); }
    void undo(DrawingManager& doc) override { apply(doc, m_inv); }
    std::string label() const override { return "MODIFY"; }

private:
    void apply(DrawingManager& doc, const Matrix4& m) {
        for (const EntityId id : m_ids)
            if (Entity* e = doc.getEntity(id)) { e->transform(m); doc.markDirty(id); }
    }
    std::vector<EntityId> m_ids;
    Matrix4               m_fwd, m_inv;
};

} // namespace cad
