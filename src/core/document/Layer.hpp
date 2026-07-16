// src/core/document/Layer.hpp
#pragma once
#include "core/geometry/Properties.hpp"
#include <string>

namespace cad {

// Camada: agrupa entidades e fornece os valores herdados via ByLayer.
struct Layer {
    std::string name{"0"};
    Rgba        color{255, 255, 255, 255};
    LineType    lineType{};
    LineWeight  lineWeight{};
    bool        on{true};        // ligada (ON/OFF): desligada não renderiza nem plota
    bool        frozen{false};   // congelada: não renderiza, não seleciona, fora do extents
    bool        locked{false};   // travada: visível (esmaecida) mas não editável
    int         transparency{0}; // 0..90 (%): esmaece o render (0 = opaca)
};

} // namespace cad
