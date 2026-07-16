// src/core/geometry/SnapPoint.hpp
#pragma once
#include "core/math/Vec.hpp"

namespace cad {

// Tipos de ponto de captura (OSNAP) que uma entidade pode oferecer.
// Node = entidade POINT; Extension = prolongamento colinear de um segmento;
// Parallel = direção paralela a um segmento vizinho (a partir do ponto-base);
// Insertion = ponto de inserção de bloco/texto; GeomCenter = centroide de
// contorno fechado; AppInt = interseção APARENTE (onde as extensões cruzariam).
enum class SnapType { None, Endpoint, Midpoint, Center, Quadrant, Intersection,
                      Nearest, Perpendicular, Tangent, Node, Extension, Parallel,
                      Insertion, GeomCenter, AppInt };

// Máscara de OSNAP ativos: 1 bit por SnapType (permite ligar/desligar por tipo).
inline constexpr unsigned snapBit(SnapType t) { return 1u << static_cast<unsigned>(t); }
inline constexpr unsigned kAllSnaps = ~0u;

// Conjunto PADRÃO de OSNAP (à la AutoCAD): os úteis no dia-a-dia ligados;
// Quadrante/Tangente/Próximo/Extensão/Paralela ficam opt-in pelo menu OSNAP.
// Node vem ligado para a entidade PONTO continuar capturável por padrão.
inline constexpr unsigned kDefaultSnaps =
    snapBit(SnapType::Endpoint) | snapBit(SnapType::Midpoint) |
    snapBit(SnapType::Center)   | snapBit(SnapType::Intersection) |
    snapBit(SnapType::Perpendicular) | snapBit(SnapType::Node);

struct SnapPoint {
    Point3   point;
    SnapType type{SnapType::Endpoint};
};

} // namespace cad
