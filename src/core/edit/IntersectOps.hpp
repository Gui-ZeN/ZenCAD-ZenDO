// src/core/edit/IntersectOps.hpp
#pragma once
#include <vector>

#include "core/geometry/Entity.hpp"
#include "core/math/Vec.hpp"

namespace cad {

// ============================================================================
//  IntersectOps — cálculo dos PONTOS DE INTERSEÇÃO reais entre duas entidades.
//
//  Por "reais" entende-se: pontos que efetivamente caem SOBRE as duas
//  entidades, respeitando suas fronteiras finitas:
//    * Line  — apenas dentro do segmento finito [start, end];
//    * Arc   — apenas dentro da abertura angular (varredura CCW de start a end);
//    * Polyline — cada aresta tratada como um segmento Line independente
//      (inclui a aresta de fechamento quando closed()).
//  Circle é a única primitiva sem fronteira (curva completa).
//
//  Tolerância numérica de ~1e-9 para testes de paralelismo, tangência e
//  pertinência a segmento/arco.
// ============================================================================

// Retorna os pontos de interseção entre 'a' e 'b' que caem sobre ambas as
// entidades. A ordem dos argumentos é irrelevante (trata a,b e b,a). Retorna
// vetor vazio se não houver interseção ou se o par de tipos não for suportado.
//
// Pares suportados:
//   Line     × Line
//   Line     × Circle
//   Line     × Arc
//   Circle   × Circle
//   Circle   × Arc
//   Arc      × Arc
//   Polyline × (qualquer dos acima) — via decomposição em arestas Line.
std::vector<Point3> intersectEntities(const Entity& a, const Entity& b);

} // namespace cad
