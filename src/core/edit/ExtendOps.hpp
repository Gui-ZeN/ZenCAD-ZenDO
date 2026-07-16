// src/core/edit/ExtendOps.hpp
#pragma once
#include "core/geometry/Line.hpp"
#include "core/geometry/Entity.hpp"
#include "core/math/Vec.hpp"
#include <memory>
#include <optional>

namespace cad {

// ============================================================================
//  ExtendOps — operação de extensão (extend) no plano XY em precisão dupla.
//  Função livre, sem estado. A coordenada Z é ignorada (geometria 2D); o
//  resultado é emitido com z = 0.
// ============================================================================

// Estende a linha target até a RETA INFINITA de contorno que passa por
// boundA–boundB. A extremidade da target mais próxima de pickEnd é movida até o
// ponto de interseção; a outra extremidade é preservada.
//
// Retorna nullopt quando:
//   * a target é degenerada (extremidades coincidentes);
//   * as direções são paralelas (sem interseção única);
//   * a interseção ficaria "atrás" da extremidade escolhida — isto é,
//     encurtaria a linha em vez de estendê-la.
std::optional<Line> extendLineToBoundary(const Line& target,
                                         const Point3& boundA,
                                         const Point3& boundB,
                                         const Point3& pickEnd);

// Extend GENÉRICO: estende a `target` (Line ou Arc) até encostar na `boundary`
// (qualquer entidade), alongando a extremidade mais próxima de `pickEnd` na
// direção de FORA. Usa as interseções reais (IntersectOps) entre o suporte da
// target (reta infinita p/ Line, círculo completo p/ Arc) e a boundary, e move
// a ponta escolhida até o ponto de interseção mais próximo no sentido da extensão.
// Retorna a nova geometria (mesmo tipo) ou nullopt quando não há para onde estender.
std::optional<std::unique_ptr<Entity>> extendEntity(const Entity& target,
                                                    const Entity& boundary,
                                                    const Point3& pickEnd);

} // namespace cad
