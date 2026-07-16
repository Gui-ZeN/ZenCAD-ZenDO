// src/core/edit/DivideOps.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/math/Vec.hpp"
#include <vector>

namespace cad {

// DIVIDE/MEASURE: pontos distribuídos ao longo de uma entidade. A entidade é
// amostrada como polilinha (Line/Arc/Circle/Polyline/Spline) e os pontos saem
// por comprimento de arco. Para objetos fechados (Circle, polilinha fechada) o
// DIVIDE gera `segments` pontos; para abertos, `segments - 1` pontos internos.

// Divide a entidade em `segments` partes iguais (>= 2). Vazio se inválido.
std::vector<Point3> dividePoints(const Entity& e, int segments);

// Pontos a cada `spacing` ao longo da entidade, a partir do início.
std::vector<Point3> measurePoints(const Entity& e, double spacing);

// Marca = ponto + ângulo da tangente local (p/ DIVIDE/MEASURE com BLOCO
// alinhado à entidade, como no AutoCAD).
struct DivMark { Point3 p; double angleRad{0.0}; };
std::vector<DivMark> divideMarks(const Entity& e, int segments);
std::vector<DivMark> measureMarks(const Entity& e, double spacing);

} // namespace cad
