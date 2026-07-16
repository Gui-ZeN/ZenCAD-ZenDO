// src/core/edit/ChamferOps.hpp
#pragma once
#include "core/geometry/Line.hpp"
#include "core/math/Vec.hpp"

namespace cad {

// ============================================================================
//  ChamferOps — chanfro (chamfer) entre duas linhas.
//  Função livre, sem estado, no plano XY em precisão dupla (double). A
//  coordenada Z dos pontos de entrada é ignorada; os resultados saem com z = 0.
//  Análogo ao fillet, porém o canto é substituído por um segmento reto (o
//  chanfro) em vez de um arco tangente.
// ============================================================================

// Resultado de um chanfro entre duas linhas: as duas linhas aparadas até os
// respectivos pontos de tangência (T1 e T2) e o segmento de chanfro que os liga.
// Se a operação não for possível (linhas paralelas, ou recuos d1/d2 maiores que
// o comprimento disponível em cada linha), ok = false e os demais campos ficam
// indefinidos.
struct ChamferResult {
    Line line1;       // l1 aparada, terminando em T1
    Line line2;       // l2 aparada, terminando em T2
    Line bevel;       // segmento de chanfro T1–T2
    bool ok{false};
};

// Calcula o chanfro entre as linhas l1 e l2.
//
// Seja V o vértice (interseção das DUAS RETAS INFINITAS de l1 e l2). A partir de
// V, recua-se l1 por d1 na direção do extremo MAIS DISTANTE de V (esse é o lado
// que permanece), obtendo o ponto de tangência T1; idem l2 por d2, obtendo T2.
//   line1 = l1 aparada, do extremo distante até T1 (extremo distante preservado);
//   line2 = l2 aparada, do extremo distante até T2 (extremo distante preservado);
//   bevel = segmento reto T1–T2.
//
// Retorna ok = false se as linhas forem paralelas/degeneradas, ou se d1 (d2) for
// maior que o comprimento disponível da linha 1 (linha 2) a partir de V.
ChamferResult chamferLines(const Line& l1, const Line& l2, double d1, double d2);

} // namespace cad
