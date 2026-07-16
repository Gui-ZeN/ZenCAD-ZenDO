// src/core/spatial/Quadtree.hpp
#pragma once
#include "core/spatial/ISpatialIndex.hpp"
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace cad {

// Quadtree adaptativa no plano XY. Entidades cujo bbox não cabe inteiramente
// num quadrante-filho ficam retidas no nó pai ("straddle"), o que evita
// duplicação e mantém remove() correto. Um mapa autoritativo id->bbox dá
// suporte O(1) ao remove e ao nearest.
class Quadtree final : public ISpatialIndex {
public:
    Quadtree(const AABB& worldBounds, int maxDepth, int splitThreshold);
    ~Quadtree() override;

    void insert(EntityId id, const AABB& box) override;
    bool remove(EntityId id) override;
    std::vector<EntityId> query(const AABB& region) const override;
    EntityId nearest(const Point3& p) const override;

    std::size_t size() const { return m_boxes.size(); }

private:
    struct Entry { EntityId id; AABB box; };
    struct Node {
        AABB bounds;
        int  depth{0};
        std::vector<Entry> entries;
        std::array<std::unique_ptr<Node>, 4> kids{};
        bool leaf() const { return kids[0] == nullptr; }
    };

    void insertInto(Node& node, const Entry& e);
    void subdivide(Node& node);
    bool removeFrom(Node& node, EntityId id, const AABB& box);
    void queryInto(const Node& node, const AABB& region, std::vector<EntityId>& out) const;

    std::unique_ptr<Node> m_root;
    int m_maxDepth;
    int m_splitThreshold;
    std::unordered_map<EntityId, AABB> m_boxes;  // autoritativo p/ remove/nearest
};

} // namespace cad
