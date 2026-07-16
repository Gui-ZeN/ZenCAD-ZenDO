// src/core/edit/InquiryOps.hpp
#pragma once
#include <vector>

#include "core/math/Vec.hpp"

namespace cad {

class Entity;  // fwd

// ============================================================================
//  InquiryOps — funções livres puras de consulta/medição (sem mutação).
//  Trabalham em precisão dupla (double) no plano XY. A componente Z é ignorada
//  nas medições de área/perímetro (são cálculos planos 2D); distance() é 3D.
// ============================================================================

// Distância euclidiana 3D entre dois pontos.
double distance(const Point3& a, const Point3& b);

// Área de um polígono (fórmula do "shoelace"), no plano XY. O resultado é o
// valor ABSOLUTO, independente da orientação (CW/CCW) dos vértices. Assume o
// polígono fechado (a última aresta liga o último vértice ao primeiro).
// Menos de 3 vértices => 0.
double polygonArea(const std::vector<Point3>& pts);

// Perímetro de uma sequência de vértices (plano XY). Soma o comprimento das
// arestas consecutivas; se 'closed' for true, inclui também a aresta de
// fechamento (último -> primeiro). Menos de 2 vértices => 0.
double polygonPerimeter(const std::vector<Point3>& pts, bool closed);

// Comprimento da entidade:
//   * Line      => |end - start|
//   * Circle    => circunferência (kTwoPi * r)
//   * Arc       => r * |sweep|, com sweep CCW de startAngle a endAngle
//                  normalizado para [0, 2*pi)
//   * Polyline  => comprimento de sampledPoints() (já tessela arcos e já inclui
//                  o ponto de fechamento quando closed())
//   * demais    => 0
double entityLength(const Entity& e);

// Área da entidade:
//   * Circle             => kPi * r * r
//   * Polyline FECHADA   => polygonArea(sampledPoints())
//   * demais             => 0
double entityArea(const Entity& e);

} // namespace cad
