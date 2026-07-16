// src/core/geometry/Entity.cpp
#include "core/geometry/Entity.hpp"
#include "core/document/LayerTable.hpp"

namespace cad {

Rgba Entity::resolveColor(const LayerTable& layers) const {
    switch (m_color.mode) {
        case ColorRef::Mode::Explicit:
            return m_color.value;
        case ColorRef::Mode::ByBlock:  // sem blocos no scaffold => trata como ByLayer
        case ColorRef::Mode::ByLayer:
        default: {
            const Layer* l = layers.find(m_layer);
            return l ? l->color : Rgba{255, 255, 255, 255};
        }
    }
}

} // namespace cad
