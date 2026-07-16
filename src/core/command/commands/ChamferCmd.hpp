// src/core/command/commands/ChamferCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Line.hpp"
#include "core/edit/ChamferOps.hpp"
#include <memory>

namespace cad {

// Chanfra (chamfer) duas Lines, substituindo o canto por um segmento reto. As
// duas linhas são aparadas até os pontos de tangência (replaceEntity, mantendo
// seus ids) e o segmento de chanfro é adicionado como nova entidade. Guarda-se o
// clone das linhas originais para undo e o clone do chanfro para redo. Inerte se
// as entidades não forem 2 linhas ou se o chanfro não for possível (!ok).
// Espelha FilletCmd, trocando o arco tangente pelo segmento de chanfro.
class ChamferCmd final : public Command {
public:
    ChamferCmd(EntityId line1Id, EntityId line2Id, double d1, double d2)
        : m_line1Id(line1Id), m_line2Id(line2Id), m_d1(d1), m_d2(d2) {}

    void execute(DrawingManager& doc) override {
        if (!m_done) {                 // primeira execução: calcula o chanfro
            const auto* l1 = dynamic_cast<const Line*>(doc.getEntity(m_line1Id));
            const auto* l2 = dynamic_cast<const Line*>(doc.getEntity(m_line2Id));
            if (!l1 || !l2) return;    // não são linhas: inerte

            ChamferResult r = chamferLines(*l1, *l2, m_d1, m_d2);
            if (!r.ok) return;         // chanfro impossível: inerte

            // Mementos das linhas originais (p/ undo).
            m_before1 = l1->clone();
            m_before2 = l2->clone();

            // Resultados aparados (p/ redo).
            m_after1 = std::make_unique<Line>(r.line1);
            m_after2 = std::make_unique<Line>(r.line2);
            m_bevel  = std::make_unique<Line>(r.bevel);

            doc.replaceEntity(m_line1Id, std::make_unique<Line>(r.line1));
            doc.replaceEntity(m_line2Id, std::make_unique<Line>(r.line2));
            m_bevelId = doc.addEntity(std::make_unique<Line>(r.bevel));
            m_done = true;
        } else if (m_after1 && m_after2 && m_bevelClone) {  // redo
            doc.replaceEntity(m_line1Id, m_after1->clone());
            doc.replaceEntity(m_line2Id, m_after2->clone());
            doc.reinsert(std::move(m_bevelClone));          // reinsere com mesmo id
        }
    }

    void undo(DrawingManager& doc) override {
        if (!m_done) return;
        if (m_before1) doc.replaceEntity(m_line1Id, m_before1->clone());
        if (m_before2) doc.replaceEntity(m_line2Id, m_before2->clone());
        if (m_bevelId != kInvalidId)
            m_bevelClone = doc.removeEntity(m_bevelId);  // guarda p/ redo
    }

    std::string label() const override { return "CHAMFER"; }

private:
    EntityId  m_line1Id;
    EntityId  m_line2Id;
    double    m_d1;
    double    m_d2;
    EntityId  m_bevelId{kInvalidId};
    EntityPtr m_before1;        // geometria original da linha 1 (p/ undo)
    EntityPtr m_before2;        // geometria original da linha 2 (p/ undo)
    EntityPtr m_after1;         // linha 1 aparada (p/ redo)
    EntityPtr m_after2;         // linha 2 aparada (p/ redo)
    EntityPtr m_bevel;          // segmento de chanfro (referência do resultado)
    EntityPtr m_bevelClone;     // dono do chanfro enquanto FORA do documento (após undo)
    bool      m_done{false};
};

} // namespace cad
