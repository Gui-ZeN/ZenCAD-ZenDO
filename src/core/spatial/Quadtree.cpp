// src/core/spatial/Quadtree.cpp
#include "core/spatial/Quadtree.hpp"
#include <limits>

namespace cad {

Quadtree::Quadtree(const AABB& worldBounds, int maxDepth, int splitThreshold)
    : m_maxDepth(maxDepth), m_splitThreshold(splitThreshold) {
    m_root = std::make_unique<Node>();
    m_root->bounds = worldBounds;
    m_root->depth = 0;
}

Quadtree::~Quadtree() = default;

void Quadtree::insert(EntityId id, const AABB& box) {
    m_boxes[id] = box;
    insertInto(*m_root, Entry{id, box});
}

void Quadtree::insertInto(Node& node, const Entry& e) {
    if (!node.leaf()) {
        for (auto& kid : node.kids) {
            if (kid->bounds.contains(e.box)) { insertInto(*kid, e); return; }
        }
        node.entries.push_back(e);  // straddle: permanece no nó atual
        return;
    }
    node.entries.push_back(e);
    if (static_cast<int>(node.entries.size()) > m_splitThreshold &&
        node.depth < m_maxDepth) {
        subdivide(node);
    }
}

void Quadtree::subdivide(Node& node) {
    const Point3 c  = node.bounds.center();
    const Point3 mn = node.bounds.min;
    const Point3 mx = node.bounds.max;

    // 4 quadrantes no plano XY (z herda do pai).
    const AABB quads[4] = {
        AABB{ {mn.x, mn.y, mn.z}, {c.x,  c.y,  mx.z} },  // SW
        AABB{ {c.x,  mn.y, mn.z}, {mx.x, c.y,  mx.z} },  // SE
        AABB{ {mn.x, c.y,  mn.z}, {c.x,  mx.y, mx.z} },  // NW
        AABB{ {c.x,  c.y,  mn.z}, {mx.x, mx.y, mx.z} }   // NE
    };
    for (int i = 0; i < 4; ++i) {
        node.kids[i] = std::make_unique<Node>();
        node.kids[i]->bounds = quads[i];
        node.kids[i]->depth  = node.depth + 1;
    }

    std::vector<Entry> moving = std::move(node.entries);
    node.entries.clear();
    for (const Entry& e : moving) {
        bool placed = false;
        for (auto& kid : node.kids) {
            if (kid->bounds.contains(e.box)) { insertInto(*kid, e); placed = true; break; }
        }
        if (!placed) node.entries.push_back(e);  // straddle: volta ao pai
    }
}

bool Quadtree::remove(EntityId id) {
    auto it = m_boxes.find(id);
    if (it == m_boxes.end()) return false;
    const AABB box = it->second;
    m_boxes.erase(it);
    return removeFrom(*m_root, id, box);
}

bool Quadtree::removeFrom(Node& node, EntityId id, const AABB& box) {
    for (std::size_t i = 0; i < node.entries.size(); ++i) {
        if (node.entries[i].id == id) {
            node.entries[i] = node.entries.back();  // swap-and-pop
            node.entries.pop_back();
            return true;
        }
    }
    if (!node.leaf()) {
        for (auto& kid : node.kids) {
            if (kid->bounds.intersects(box) && removeFrom(*kid, id, box)) return true;
        }
    }
    return false;
}

std::vector<EntityId> Quadtree::query(const AABB& region) const {
    std::vector<EntityId> out;
    queryInto(*m_root, region, out);
    return out;
}

void Quadtree::queryInto(const Node& node, const AABB& region,
                         std::vector<EntityId>& out) const {
    if (!node.bounds.intersects(region)) return;
    for (const Entry& e : node.entries)
        if (e.box.intersects(region)) out.push_back(e.id);
    if (!node.leaf())
        for (const auto& kid : node.kids)
            queryInto(*kid, region, out);
}

EntityId Quadtree::nearest(const Point3& p) const {
    // Broad-phase simples por centro de bbox (suficiente p/ o scaffold).
    EntityId best   = kInvalidId;
    double   bestSq = std::numeric_limits<double>::max();
    for (const auto& kv : m_boxes) {
        const Point3 c = kv.second.center();
        const double dx = c.x - p.x, dy = c.y - p.y;
        const double dSq = dx * dx + dy * dy;
        if (dSq < bestSq) { bestSq = dSq; best = kv.first; }
    }
    return best;
}

} // namespace cad
