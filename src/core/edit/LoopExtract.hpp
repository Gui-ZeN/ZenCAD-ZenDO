// src/core/edit/LoopExtract.hpp
// Extração do CONTORNO FECHADO de uma entidade como polígono ordenado —
// compartilhado pela hachura (pick) e pelo regen ASSOCIATIVO do DrawingManager
// (a fonte mudou -> re-extrai o loop). Vazio = contorno aberto/inválido.
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/RenderBatch.hpp"
#include <cmath>
#include <vector>

namespace cad {

inline std::vector<Point3> extractClosedLoop(const Entity& e) {
    std::vector<Point3> bound;
    if (const auto* pl = dynamic_cast<const Polyline*>(&e)) {
        if (pl->closed()) bound = pl->sampledPoints();   // arcos já tesselados
        return bound;
    }
    // Qualquer contorno FECHADO (círculo, elipse, polígono): o emitTo encadeia
    // os segmentos, então os extremos (índices ímpares) reconstroem a sequência.
    RenderBatch b;
    e.emitTo(b);
    const auto& lv = b.lineVertices;
    if (lv.size() >= 6) {                          // >= 3 segmentos
        bound.push_back(lv[0]);
        for (std::size_t i = 1; i < lv.size(); i += 2) bound.push_back(lv[i]);
        const Point3 a = bound.front(), z = bound.back();
        if (std::hypot(z.x - a.x, z.y - a.y) > 1e-6) bound.clear();   // aberto
        else bound.pop_back();                     // remove o fechamento duplicado
    }
    return bound;
}

} // namespace cad
