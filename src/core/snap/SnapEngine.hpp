// src/core/snap/SnapEngine.hpp
#pragma once
#include "core/geometry/SnapPoint.hpp"
#include "core/geometry/Entity.hpp"
#include "core/math/Vec.hpp"

namespace cad {

class DrawingManager;

struct SnapResult {
    bool     hit{false};
    Point3   point{};
    SnapType type{SnapType::None};
    EntityId entity{kInvalidId};   // entidade que forneceu o ponto (associatividade)
    explicit operator bool() const { return hit; }
};

// Motor de captura (OSNAP). Dado o cursor em coordenadas de mundo e um raio de
// atração (tolerância em unidades de mundo), encontra o ponto de interesse mais
// próximo entre as entidades vizinhas (via índice espacial). Sem Qt: testável.
class SnapEngine {
public:
    // `from` (opcional) = ponto-base do desenho em curso; quando dado, habilita
    // o snap Perpendicular (pé da perpendicular de `from` à entidade).
    // `typeMask` (bits via snapBit): liga/desliga OSNAP por tipo; padrão = todos.
    SnapResult resolve(const Point3& cursor, double tolWorld, const DrawingManager& doc,
                       const Point3* from = nullptr, unsigned typeMask = kAllSnaps) const;
};

} // namespace cad
