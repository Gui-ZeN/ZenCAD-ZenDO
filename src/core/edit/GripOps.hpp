// src/core/edit/GripOps.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/math/Vec.hpp"
#include <memory>
#include <vector>

namespace cad {

// Operações de "grips" (alças de edição) — pontos azuis que o AutoCAD mostra ao
// selecionar uma entidade e que podem ser arrastados para editar a geometria.

// Pontos editáveis de uma entidade (por tipo): Line {início, fim}; Polyline
// {cada vértice}; Circle {centro, alça de raio}. Outros tipos: vazio.
std::vector<Point3> gripsOf(const Entity& e);

// Retorna uma NOVA entidade com o grip de índice `gripIndex` movido para
// `newPos`. nullptr se o índice for inválido ou o tipo não tiver grips.
std::unique_ptr<Entity> withGripMoved(const Entity& e, int gripIndex, const Point3& newPos);

// PEDIT — edição de vértices de Polyline:
//  withVertexRemoved: remove o vértice `gripIndex` (mantém >= 2 vértices).
//  withVertexInserted: insere um vértice na aresta mais próxima de `at`.
// nullptr se o tipo não suportar ou a operação for inválida.
std::unique_ptr<Entity> withVertexRemoved(const Entity& e, int gripIndex);
std::unique_ptr<Entity> withVertexInserted(const Entity& e, const Point3& at);

} // namespace cad
