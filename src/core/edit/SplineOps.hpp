// src/core/edit/SplineOps.hpp
#pragma once
#include <vector>
#include "core/math/Vec.hpp"

namespace cad {

// Amostra uma B-spline definida pelos pontos de CONTROLE `ctrl`.
//
// É o modo "CV" (control vertices) do comando SPLINE do AutoCAD: a curva NÃO
// passa pelos pontos de controle — eles funcionam como "ímãs" que puxam a
// curva, que fica contida no fecho convexo do polígono de controle.
//
// Características da implementação:
//  * Knot vector "clamped uniforme": as pontas têm multiplicidade degree+1, o
//    que força a curva a COMEÇAR exatamente no 1º ponto de controle e TERMINAR
//    exatamente no último. Os nós internos são igualmente espaçados.
//  * Avaliação por algoritmo de de Boor (numericamente estável — é a forma
//    recursiva análoga ao de Casteljau, porém para B-splines).
//  * `degree`: grau da curva (3 = cúbica, padrão).
//  * `samplesPerSpan`: nº de amostras por vão (span) entre nós internos; o total
//    de pontos retornados é aproximadamente samplesPerSpan * (nº de vãos).
//
// Casos de borda:
//  * ctrl.size() < 2            -> devolve `ctrl` inalterado.
//  * ctrl.size() <= degree      -> reduz o grau efetivo para ctrl.size()-1
//                                  (2 pontos viram uma reta, 3 pontos grau 2 etc.).
//
// Todos os pontos retornados estão no plano XY (z = 0). Precisão dupla.
std::vector<Point3> bsplinePoints(const std::vector<Point3>& ctrl,
                                  int degree = 3,
                                  int samplesPerSpan = 16);

} // namespace cad
