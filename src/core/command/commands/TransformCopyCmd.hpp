// src/core/command/commands/TransformCopyCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/math/Matrix4.hpp"
#include <utility>
#include <vector>

namespace cad {

// Copia um conjunto de entidades aplicando uma TRANSFORMAÇÃO qualquer (rotação,
// escala, etc.) — preserva os originais. Espelha CopyCmd, mas com Matrix4.
// Usado pelas variantes "Rotacionar+Copiar" / "Escalar+Copiar".
class TransformCopyCmd final : public Command {
public:
    TransformCopyCmd(std::vector<EntityId> ids, Matrix4 m)
        : m_srcIds(std::move(ids)), m_m(m) {}

    void execute(DrawingManager& doc) override {
        if (!m_made) {
            for (const EntityId id : m_srcIds)
                if (const Entity* e = doc.getEntity(id)) {
                    EntityPtr c = e->clone();
                    c->transform(m_m);
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
    Matrix4                m_m;
    std::vector<EntityPtr> m_clones;
    std::vector<EntityId>  m_newIds;
    bool                   m_made{false};
};

} // namespace cad
