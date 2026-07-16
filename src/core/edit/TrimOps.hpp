// src/core/edit/TrimOps.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/math/Vec.hpp"
#include <memory>
#include <vector>

namespace cad {

// Trim GENÉRICO: apara `target` usando `cutter` como aresta de corte. Calcula as
// interseções reais (IntersectOps), parte o alvo nesses pontos e REMOVE o trecho
// que contém `pick`, devolvendo os pedaços que sobram:
//   * Line     -> 0..N Lines (a parte do clique some; o resto fica);
//   * Circle   -> 1 Arc (o complemento do arco clicado; precisa de >=2 interseções);
//   * Arc      -> 0..N Arcs.
// Retorna vetor VAZIO quando não há o que aparar (sem interseção interna, tipo não
// suportado, etc.) — nesse caso o chamador deve manter o alvo intacto.
// Polyline ainda não suportada como alvo (retorna vazio).
std::vector<std::unique_ptr<Entity>> trimEntity(const Entity& target,
                                                const Entity& cutter,
                                                const Point3& pick);

// Núcleo do Trim/Break: parte `target` nos pontos `cutPoints` (que devem cair
// sobre o alvo) e remove o trecho que contém `pick`, devolvendo os pedaços que
// sobram (Line->pedaços, Circle->Arc, Arc->arcos). É o que o Trim usa por baixo
// (com cutPoints = interseções) e o que o Break usa (cutPoints = 2 cliques).
std::vector<std::unique_ptr<Entity>> splitEntityAt(const Entity& target,
                                                   const std::vector<Point3>& cutPoints,
                                                   const Point3& pick);

} // namespace cad
