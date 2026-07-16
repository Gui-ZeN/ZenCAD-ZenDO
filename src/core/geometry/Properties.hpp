// src/core/geometry/Properties.hpp
#pragma once
#include <cstdint>
#include <string>

namespace cad {

struct Rgba { std::uint8_t r{255}, g{255}, b{255}, a{255}; };

// Cor com semântica AutoCAD: explícita, ByLayer ou ByBlock.
struct ColorRef {
    enum class Mode { ByLayer, ByBlock, Explicit } mode{Mode::ByLayer};
    Rgba value{};

    static ColorRef byLayer()             { return {Mode::ByLayer, {}}; }
    static ColorRef explicitColor(Rgba c) { return {Mode::Explicit, c}; }
};

struct LineType   { std::string name{"ByLayer"}; };  // CONTINUOUS, DASHED, ...
struct LineWeight { double mm{-1.0}; };              // -1 => herda da camada

} // namespace cad
