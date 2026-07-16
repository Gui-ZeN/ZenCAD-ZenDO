// src/core/command/commands/ExplodeCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/BlockRef.hpp"
#include "core/geometry/Bulge.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace cad {

// Explode (explode) uma Polyline em Lines independentes: uma Line por aresta
// (vértices consecutivos) mais o fechamento se closed(). A polilinha original é
// removida (guardada como memento) e cada Line é adicionada à base, guardando-se
// os novos ids. undo remove as Lines novas e reinsere a polilinha original;
// redo reconstrói as Lines guardadas. Para tipos diferentes de Polyline o
// comando é inerte — execute/undo são no-ops seguros.
class ExplodeCmd final : public Command {
public:
    explicit ExplodeCmd(EntityId id) : m_srcId(id) {}

    void execute(DrawingManager& doc) override {
        if (!m_done) {                 // primeira execução: explode a entidade
            const Entity* src = doc.getEntity(m_srcId);
            // BlockRef (INSERT): explode nos membros já transformados pela inserção.
            if (const auto* blk = dynamic_cast<const BlockRef*>(src)) {
                std::vector<EntityPtr> parts = blk->explodedClones();
                if (parts.empty()) return;
                m_original = doc.removeEntity(m_srcId);
                for (auto& c : parts) m_newIds.push_back(doc.addEntity(std::move(c)));
                m_done = true;
                return;
            }
            const auto* poly = dynamic_cast<const Polyline*>(src);
            if (!poly) return;         // não é polilinha/bloco: inerte

            const std::vector<Point3>& v = poly->vertices();
            if (v.size() < 2) return;  // sem arestas: nada a explodir

            m_original = doc.removeEntity(m_srcId);  // memento p/ undo

            // Cada trecho vira Line (reto) ou Arc (se tiver bulge); inclui o
            // fechamento se a polilinha for fechada.
            const std::size_t segs = (poly->closed() && v.size() >= 3) ? v.size() : v.size() - 1;
            for (std::size_t i = 0; i < segs; ++i) {
                const Point3& a = v[i];
                const Point3& b = v[(i + 1) % v.size()];
                const double bu = poly->bulgeAt(i);
                if (std::fabs(bu) < 1e-12) {
                    m_newIds.push_back(doc.addEntity(std::make_unique<Line>(a, b)));
                } else {
                    const BulgeArc ba = bulgeToArc(a, b, bu);
                    double a0 = ba.startAng, a1 = ba.startAng + ba.sweep;
                    if (ba.sweep < 0.0) std::swap(a0, a1);   // Arc é sempre CCW
                    m_newIds.push_back(
                        doc.addEntity(std::make_unique<Arc>(ba.center, ba.radius, a0, a1)));
                }
            }

            m_done = true;
        } else {                       // redo: reinsere os clones guardados
            for (auto& c : m_lines) if (c) doc.reinsert(std::move(c));
            m_lines.clear();
        }
    }

    void undo(DrawingManager& doc) override {
        if (!m_done) return;
        m_lines.clear();
        for (const EntityId id : m_newIds)
            m_lines.push_back(doc.removeEntity(id));
        if (m_original) doc.reinsert(std::move(m_original));
    }

    std::string label() const override { return "EXPLODE"; }

private:
    EntityId               m_srcId;
    EntityPtr              m_original;   // polilinha original (p/ undo)
    std::vector<EntityId>  m_newIds;     // ids das Lines criadas
    std::vector<EntityPtr> m_lines;      // Lines removidas no undo (p/ redo)
    bool                   m_done{false};
};

} // namespace cad
