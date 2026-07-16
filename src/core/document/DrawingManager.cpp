// src/core/document/DrawingManager.cpp
#include "core/document/DrawingManager.hpp"
#include "core/command/Command.hpp"
#include "core/command/commands/AddEntityCmd.hpp"
#include "core/geometry/Dimension.hpp"
#include "core/geometry/Hatch.hpp"
#include "core/geometry/MText.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/PointEntity.hpp"
#include "core/edit/LoopExtract.hpp"

#include <cmath>
#include <limits>

namespace cad {

DrawingManager::DrawingManager(std::unique_ptr<ISpatialIndex> index)
    : m_spatialIndex(std::move(index)) {
    m_layers.ensureDefaultLayer();
}

DrawingManager::~DrawingManager() = default;

void DrawingManager::clearAll() {
    for (const auto& kv : m_entities) m_spatialIndex->remove(kv.first);
    m_entities.clear();
    m_layers = LayerTable{};
    m_layers.ensureDefaultLayer();
    m_blocks = BlockTable{};
    m_groups.clear();
    m_layerStates.clear();
    m_xrefs.clear();
    m_lastErased.clear();
    m_history.clear();
    m_nextId = 1;
    m_dirty.clear();
    m_fullDirty = true;   // o render descarta todo o cache de tesselação
    m_annoMmPerUnit = 1.0;
    m_ucsOrigin = Point3{};
    m_ucsAngle  = 0.0;
}

EntityId DrawingManager::addEntity(EntityPtr entity) {
    const EntityId id = m_nextId++;
    entity->setId(id);
    m_spatialIndex->insert(id, entity->boundingBox());
    m_entities.emplace(id, std::move(entity));
    noteDirty(id);
    return id;
}

EntityPtr DrawingManager::removeEntity(EntityId id) {
    auto it = m_entities.find(id);
    if (it == m_entities.end()) return nullptr;
    m_spatialIndex->remove(id);
    EntityPtr owned = std::move(it->second);
    m_entities.erase(it);
    noteDirty(id);   // o render descarta a entrada de cache deste id
    return owned;  // posse transferida ao chamador (ex.: Command guarda p/ undo)
}

void DrawingManager::reinsert(EntityPtr entity) {
    const EntityId id = entity->id();  // preserva o id original
    m_spatialIndex->insert(id, entity->boundingBox());
    m_entities.emplace(id, std::move(entity));
    noteDirty(id);
}

EntityPtr DrawingManager::replaceEntity(EntityId id, EntityPtr entity) {
    auto it = m_entities.find(id);
    if (it == m_entities.end()) return nullptr;
    m_spatialIndex->remove(id);
    entity->setId(id);                 // mantém o mesmo id
    m_spatialIndex->insert(id, entity->boundingBox());
    EntityPtr old = std::move(it->second);
    it->second = std::move(entity);
    noteDirty(id);
    return old;
}

Entity* DrawingManager::getEntity(EntityId id) {
    auto it = m_entities.find(id);
    return it == m_entities.end() ? nullptr : it->second.get();
}

const Entity* DrawingManager::getEntity(EntityId id) const {
    auto it = m_entities.find(id);
    return it == m_entities.end() ? nullptr : it->second.get();
}

std::vector<EntityId> DrawingManager::query(const AABB& region) const {
    return m_spatialIndex->query(region);
}

EntityId DrawingManager::pick(const Ray& pickRay, double tol) const {
    // 1) broad-phase: candidatos pelo índice (caixa do raio inflada por tol).
    const std::vector<EntityId> candidates = m_spatialIndex->query(AABB::fromRay(pickRay, tol));

    // 2) narrow-phase exato sobre os poucos candidatos.
    EntityId best     = kInvalidId;
    double   bestDist = std::numeric_limits<double>::max();
    for (const EntityId id : candidates) {
        auto it = m_entities.find(id);
        if (it == m_entities.end() || !it->second->visible()) continue;
        HitResult h = it->second->hitTest(pickRay, tol);
        if (h && h.distance < bestDist) {
            bestDist = h.distance;
            best     = id;
        }
    }
    return best;
}

void DrawingManager::markDirty(EntityId id) {
    auto it = m_entities.find(id);
    if (it == m_entities.end()) return;
    m_spatialIndex->remove(id);
    m_spatialIndex->insert(id, it->second->boundingBox());
    noteDirty(id);
}

std::size_t DrawingManager::layerUsage(const std::string& name) const {
    std::size_t n = 0;
    for (const auto& kv : m_entities)
        if (kv.second->layer() == name) ++n;
    return n;
}

bool DrawingManager::renameLayer(const std::string& oldName, const std::string& newName) {
    if (!m_layers.rename(oldName, newName)) return false;
    for (auto& kv : m_entities)
        if (kv.second->layer() == oldName) {
            kv.second->setLayer(newName);
            noteDirty(kv.first);
        }
    // Membros das definições de bloco também referenciam camadas por nome.
    for (const std::string& bn : m_blocks.names())
        if (BlockDefinition* def = m_blocks.find(bn))
            for (const auto& m : def->members)
                if (m->layer() == oldName) m->setLayer(newName);
    m_fullDirty = true;   // cor/estado ByLayer é resolvido no rebuild
    return true;
}

bool DrawingManager::removeLayer(const std::string& name, bool moveToDefault) {
    if (name == "0" || !m_layers.contains(name)) return false;
    if (layerUsage(name) > 0) {
        if (!moveToDefault) return false;
        for (auto& kv : m_entities)
            if (kv.second->layer() == name) {
                kv.second->setLayer("0");
                noteDirty(kv.first);
            }
        m_fullDirty = true;
    }
    return m_layers.remove(name);
}

std::vector<std::string> DrawingManager::purgeLayers(const std::string& keep) {
    std::vector<std::string> removed;
    for (const Layer& l : m_layers.all()) {
        if (l.name == "0" || l.name == keep) continue;
        if (layerUsage(l.name) == 0 && m_layers.remove(l.name))
            removed.push_back(l.name);
    }
    return removed;
}

void DrawingManager::execute(std::unique_ptr<Command> cmd) {
    cmd->execute(*this);
    m_history.push(std::move(cmd));  // push limpa a pilha de redo
    regenAssociativeDims();
}

bool DrawingManager::canUndo() const noexcept { return m_history.canUndo(); }
bool DrawingManager::canRedo() const noexcept { return m_history.canRedo(); }

void DrawingManager::undo() {
    if (Command* cmd = m_history.popForUndo()) { cmd->undo(*this); regenAssociativeDims(); }
}

void DrawingManager::redo() {
    if (Command* cmd = m_history.popForRedo()) { cmd->execute(*this); regenAssociativeDims(); }
}

std::size_t DrawingManager::undoBack() {
    if (!m_history.hasMark()) return 0;
    const std::size_t target = m_history.popMarkDepth();
    std::size_t n = 0;
    while (m_history.undoDepth() > target) {
        Command* cmd = m_history.popForUndo();
        if (!cmd) break;
        cmd->undo(*this);
        ++n;
    }
    if (n > 0) regenAssociativeDims();
    return n;
}

std::size_t DrawingManager::oops() {
    // Re-adiciona os clones do último ERASE como entidades NOVAS (com undo:
    // o AddEntityCmd de cada uma entra no histórico normalmente).
    if (m_lastErased.empty()) return 0;
    std::vector<EntityPtr> batch = std::move(m_lastErased);
    m_lastErased.clear();
    const std::size_t n = batch.size();
    for (EntityPtr& e : batch)
        execute(std::make_unique<AddEntityCmd>(std::move(e)));
    return n;
}

namespace {
// Ponto atual da âncora na geometria-fonte. `oldPt` = ponto anterior da cota
// (dá a direção preservada no Which::OnCurve). false = âncora irresolúvel
// (entidade apagada/tipo trocado) — a cota fica como está.
bool resolveDimAnchor(const DrawingManager& doc, const DimAnchor& a,
                      const Point3& oldPt, Point3& out) {
    if (!a.valid()) return false;
    const Entity* e = doc.getEntity(a.id);
    if (!e) return false;
    using W = DimAnchor::Which;
    auto onCircle = [&](const Point3& c, double r) {
        const double dx = oldPt.x - c.x, dy = oldPt.y - c.y;
        const double d = std::hypot(dx, dy);
        out = (d > 1e-9) ? Point3{c.x + dx / d * r, c.y + dy / d * r, c.z}
                         : Point3{c.x + r, c.y, c.z};
    };
    if (const auto* l = dynamic_cast<const Line*>(e)) {
        if (a.which == W::Start) { out = l->start(); return true; }
        if (a.which == W::End)   { out = l->end();   return true; }
    } else if (const auto* c = dynamic_cast<const Circle*>(e)) {
        if (a.which == W::Center)  { out = c->center(); return true; }
        if (a.which == W::OnCurve) { onCircle(c->center(), c->radius()); return true; }
    } else if (const auto* ar = dynamic_cast<const Arc*>(e)) {
        if (a.which == W::Center)  { out = ar->center(); return true; }
        if (a.which == W::OnCurve) { onCircle(ar->center(), ar->radius()); return true; }
        if (a.which == W::Start || a.which == W::End) {
            const double ang = (a.which == W::Start) ? ar->startAngle() : ar->endAngle();
            out = Point3{ar->center().x + ar->radius() * std::cos(ang),
                         ar->center().y + ar->radius() * std::sin(ang), ar->center().z};
            return true;
        }
    } else if (const auto* pl = dynamic_cast<const Polyline*>(e)) {
        if (a.which == W::Vertex && a.index >= 0 &&
            a.index < static_cast<int>(pl->vertices().size())) {
            out = pl->vertices()[static_cast<std::size_t>(a.index)];
            return true;
        }
    } else if (const auto* pt = dynamic_cast<const PointEntity*>(e)) {
        if (a.which == W::Node) { out = pt->position(); return true; }
    }
    return false;
}
} // namespace

std::size_t DrawingManager::applyAnnotationScale(double newMmPerUnit) {
    if (newMmPerUnit <= 1e-12) return 0;
    const double old = m_annoMmPerUnit;
    m_annoMmPerUnit = newMmPerUnit;
    if (std::abs(old - newMmPerUnit) < 1e-12) return 0;
    // Altura em mm de papel preservada: h_novo = h_antigo * (old / new).
    const double f = old / newMmPerUnit;
    std::vector<EntityId> ids;
    for (const auto& kv : m_entities) {
        if (const auto* t = dynamic_cast<const MText*>(kv.second.get())) {
            if (t->annotative()) ids.push_back(kv.first);
        } else if (const auto* d = dynamic_cast<const Dimension*>(kv.second.get())) {
            if (d->annotative()) ids.push_back(kv.first);
        }
    }
    for (const EntityId id : ids) {
        const Entity* e = getEntity(id);
        auto neu = e->clone();
        if (auto* t = dynamic_cast<MText*>(neu.get())) {
            t->setHeight(t->height() * f);
        } else if (auto* d = dynamic_cast<Dimension*>(neu.get())) {
            d->setTextHeight(d->textHeight() * f);
            if (d->arrowSize() > 0.0) d->setArrowSize(d->arrowSize() * f);
        }
        replaceEntity(id, std::move(neu));
    }
    return ids.size();
}

void DrawingManager::regenAssociativeDims() {
    if (m_dirty.empty() && !m_fullDirty) return;
    // 1ª passada: coleta as atualizações (não muta durante a iteração).
    struct Upd { EntityId id; Point3 p1, p2, p3; };
    std::vector<Upd> upds;
    for (const auto& kv : m_entities) {
        const auto* dim = dynamic_cast<const Dimension*>(kv.second.get());
        if (!dim || !dim->associative()) continue;
        const DimAnchor& A = dim->anchorA();
        const DimAnchor& B = dim->anchorB();
        const bool touched = m_fullDirty ||
            (A.valid() && m_dirty.count(A.id)) || (B.valid() && m_dirty.count(B.id));
        if (!touched) continue;
        Point3 np1 = dim->p1(), np2 = dim->p2(), np3 = dim->p3();
        const bool okA = resolveDimAnchor(*this, A, dim->p1(), np1);
        const bool okB = resolveDimAnchor(*this, B, dim->p2(), np2);
        if (!okA && !okB) continue;
        // A linha de cota acompanha a translação média dos pontos medidos
        // (movimento rígido segue exato; esticar um lado segue pela metade).
        np3.x += ((np1.x - dim->p1().x) + (np2.x - dim->p2().x)) * 0.5;
        np3.y += ((np1.y - dim->p1().y) + (np2.y - dim->p2().y)) * 0.5;
        const auto same = [](const Point3& a, const Point3& b) {
            return std::abs(a.x - b.x) < 1e-12 && std::abs(a.y - b.y) < 1e-12;
        };
        if (same(np1, dim->p1()) && same(np2, dim->p2())) continue;   // nada mudou
        upds.push_back({kv.first, np1, np2, np3});
    }
    // 2ª passada: aplica via replaceEntity (mantém id; render invalida só a cota).
    for (const Upd& u : upds) {
        const auto* dim = static_cast<const Dimension*>(getEntity(u.id));
        auto neu = dim->clone();
        static_cast<Dimension*>(neu.get())->setPoints(u.p1, u.p2, u.p3);
        replaceEntity(u.id, std::move(neu));
    }

    // HACHURAS associativas: a fonte de algum loop mudou -> re-extrai os
    // contornos e substitui os loops (fonte apagada/aberta: hachura fica).
    struct HUpd { EntityId id; std::vector<std::vector<Point3>> loops; };
    std::vector<HUpd> hupds;
    for (const auto& kv : m_entities) {
        const auto* h = dynamic_cast<const Hatch*>(kv.second.get());
        if (!h || !h->associative()) continue;
        bool touched = m_fullDirty;
        for (const EntityId sid : h->srcIds())
            if (m_dirty.count(sid)) { touched = true; break; }
        if (!touched) continue;
        std::vector<std::vector<Point3>> loops;
        bool ok = true;
        for (const EntityId sid : h->srcIds()) {
            const Entity* src = getEntity(sid);
            if (!src) { ok = false; break; }
            std::vector<Point3> loop = extractClosedLoop(*src);
            if (loop.size() < 3) { ok = false; break; }
            loops.push_back(std::move(loop));
        }
        if (ok) hupds.push_back({kv.first, std::move(loops)});
    }
    for (HUpd& u : hupds) {
        const auto* h = static_cast<const Hatch*>(getEntity(u.id));
        auto neu = h->clone();
        static_cast<Hatch*>(neu.get())->setLoops(std::move(u.loops));
        replaceEntity(u.id, std::move(neu));
    }
}

} // namespace cad
