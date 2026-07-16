// src/core/edit/Tangent3Ops.hpp
#pragma once
#include "core/geometry/Circle.hpp"
#include "core/geometry/Entity.hpp"
#include "core/math/Vec.hpp"
#include <optional>

namespace cad {

// ============================================================================
//  Tangent3Ops — círculo tangente a TRÊS entidades (problema "Tan-Tan-Tan").
//  Operação livre, sem estado, no plano XY em precisão dupla (double).
//  A coordenada Z dos operandos é ignorada; o resultado é emitido com z = 0.
// ============================================================================

// Constrói o círculo tangente simultaneamente às três entidades a, b e c.
//
// Caso suportado: as TRÊS entidades são Line. As três retas (infinitas) formam
// um triângulo; o círculo tangente às três é o incírculo OU um dos três
// excírculos desse triângulo (até 4 soluções). Entre as soluções válidas,
// retorna aquela cujo centro fica mais próximo de nearPt (serve para o usuário
// escolher qual concordância deseja apenas clicando perto dela).
//
// Estratégia (bissetrizes):
//   * O centro de um círculo tangente a duas retas está sobre uma das DUAS
//     bissetrizes do ângulo entre elas (passam pela interseção das retas).
//   * Para três retas, o centro é a interseção de uma bissetriz do par (a,b)
//     com uma bissetriz do par (a,c) — 2 x 2 = 4 candidatos.
//   * Para cada candidato, r = distância do centro a qualquer uma das 3 retas;
//     as três distâncias devem coincidir (validação com tolerância ~1e-6).
//
// Retorna nullopt se:
//   * algum operando não for Line (tipos não suportados);
//   * algum par de retas for paralelo (não há triângulo); ou
//   * nenhum candidato produzir distâncias coincidentes às três retas.
std::optional<Circle> circleTanTanTan(const Entity& a, const Entity& b,
                                      const Entity& c, const Point3& nearPt);

} // namespace cad
