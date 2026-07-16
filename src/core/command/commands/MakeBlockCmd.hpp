// src/core/command/commands/MakeBlockCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/BlockRef.hpp"
#include "core/geometry/AttDef.hpp"
#include "core/math/Matrix4.hpp"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cad {

// Cria um bloco (BlockRef) a partir das entidades selecionadas e um ponto-base:
// remove as originais (guardadas como memento para undo) e adiciona o bloco no
// lugar. Undo restaura as originais e remove o bloco. Espelha o protocolo de
// undo/redo do FilletCmd (vetor de mementos).
class MakeBlockCmd final : public Command {
public:
    MakeBlockCmd(std::vector<EntityId> ids, Point3 base, std::string name = {})
        : m_ids(std::move(ids)), m_base(base), m_name(std::move(name)) {}

    void execute(DrawingManager& doc) override {
        if (!m_done) {
            std::vector<const Entity*> ents;
            for (const EntityId id : m_ids)
                if (const Entity* e = doc.getEntity(id)) ents.push_back(e);
            if (ents.empty()) return;

            auto block = BlockRef::fromEntities(ents, m_base);
            // Registra a definição nomeada na biblioteca (para reinserir depois).
            // A definição persiste mesmo após undo desta criação (como no AutoCAD).
            // Entidades ATTDEF entre os membros viram AttDefSpec da definição
            // (campos preenchíveis por inserção) — não geometria fixa.
            if (!m_name.empty()) {
                BlockDefinition def;
                def.name = m_name;
                def.base = m_base;
                for (const EntityPtr& mem : block->members()) {
                    if (const auto* ad = dynamic_cast<const AttDef*>(mem.get()))
                        def.attdefs.push_back({ad->tag(), ad->prompt(), ad->defValue(),
                                               ad->position(), ad->height()});
                    else
                        def.members.push_back(mem->clone());
                }
                // REDEFINIÇÃO (à la AutoCAD): nome já existente → guarda a
                // definição antiga (p/ undo) e atualiza TODAS as inserções.
                if (const BlockDefinition* prev = doc.blocks().find(m_name)) {
                    m_oldDef = std::make_unique<BlockDefinition>(*prev);
                    m_newDef = std::make_unique<BlockDefinition>(def);
                }
                doc.blocks().add(def);   // cópia profunda p/ a biblioteca
                if (m_oldDef) updateInserts(doc, def);
                // A inserção criada passa a vir DA DEFINIÇÃO (com os valores
                // padrão dos atributos), em vez de renderizar as tags cruas.
                block = BlockRef::fromDefinition(
                    def, Matrix4::translation(Vec3{m_base.x, m_base.y, m_base.z}));
            }
            for (const EntityId id : m_ids)
                if (EntityPtr old = doc.removeEntity(id)) m_originals.push_back(std::move(old));
            m_blockId = doc.addEntity(std::move(block));
            m_done = true;
        } else {                                   // redo
            for (const EntityId id : m_ids)
                if (EntityPtr old = doc.removeEntity(id)) m_originals.push_back(std::move(old));
            if (m_blockClone) doc.reinsert(std::move(m_blockClone));
            if (m_newDef) { doc.blocks().add(*m_newDef); swapUpdatedRefs(doc); }
        }
    }

    void undo(DrawingManager& doc) override {
        if (!m_done) return;
        if (m_blockId != kInvalidId) m_blockClone = doc.removeEntity(m_blockId);
        for (EntityPtr& e : m_originals) doc.reinsert(std::move(e));
        m_originals.clear();
        if (m_oldDef) { doc.blocks().add(*m_oldDef); swapUpdatedRefs(doc); }
    }

    std::string label() const override { return "BLOCK"; }

private:
    // Reconstrói cada inserção existente do bloco a partir da NOVA definição,
    // preservando a transformação e os valores de atributo cujas tags seguem
    // existindo; guarda as antigas em m_updatedRefs (p/ undo por troca).
    void updateInserts(DrawingManager& doc, const BlockDefinition& def) {
        std::vector<EntityId> refIds;
        doc.forEach([&](const Entity& e) {
            if (const auto* br = dynamic_cast<const BlockRef*>(&e))
                if (br->blockName() == m_name) refIds.push_back(e.id());
        });
        for (const EntityId id : refIds) {
            const auto* oldRef = static_cast<const BlockRef*>(doc.getEntity(id));
            auto fresh = BlockRef::fromDefinition(def, oldRef->xform());
            for (const auto& av : oldRef->attValues())
                fresh->setAttValue(av.tag, av.value);   // tags removidas: ignoradas
            m_updatedRefs.emplace_back(id, doc.replaceEntity(id, std::move(fresh)));
        }
    }
    // Troca simétrica: devolve as versões guardadas ao doc e guarda as atuais
    // (funciona igual no undo e no redo).
    void swapUpdatedRefs(DrawingManager& doc) {
        for (auto& [id, kept] : m_updatedRefs)
            if (doc.getEntity(id)) kept = doc.replaceEntity(id, std::move(kept));
    }

    std::vector<EntityId>  m_ids;
    Point3                 m_base;
    std::string            m_name;    // nome da definição (vazio = bloco anônimo)
    EntityId               m_blockId{kInvalidId};
    std::vector<EntityPtr> m_originals;   // originais removidos (p/ undo)
    EntityPtr              m_blockClone;  // bloco fora do doc, após undo (p/ redo)
    // Redefinição: definição antiga/nova + inserções trocadas (mementos).
    std::unique_ptr<BlockDefinition> m_oldDef, m_newDef;
    std::vector<std::pair<EntityId, EntityPtr>> m_updatedRefs;
    bool                   m_done{false};
};

} // namespace cad
