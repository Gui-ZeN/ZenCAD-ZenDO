// src/core/spatial/ISpatialIndex.hpp
#pragma once
#include "core/Types.hpp"
#include "core/math/AABB.hpp"
#include <vector>

namespace cad {

// Contrato de indexação espacial. Quadtree (2D) e Octree (3D) o implementam,
// permitindo trocar a estrutura sem tocar no DrawingManager.
class ISpatialIndex {
public:
    virtual ~ISpatialIndex() = default;

    virtual void insert(EntityId id, const AABB& box) = 0;
    virtual bool remove(EntityId id) = 0;
    virtual std::vector<EntityId> query(const AABB& region) const = 0;
    virtual EntityId nearest(const Point3& p) const = 0;
};

} // namespace cad
