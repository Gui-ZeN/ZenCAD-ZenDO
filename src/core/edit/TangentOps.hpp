// src/core/edit/TangentOps.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/geometry/Circle.hpp"
#include "core/math/Vec.hpp"
#include <optional>

namespace cad {

// ============================================================================
//  TangentOps — construção de círculos tangentes (variação "TTR" do CAD:
//  Tangent–Tangent–Radius). Funções livres, sem estado, no plano XY em
//  precisão dupla (double). A coordenada Z dos pontos de entrada é ignorada
//  e os resultados são emitidos com z = 0.
// ============================================================================

// Calcula um círculo de raio r tangente a DUAS entidades a e b, onde cada
// entidade pode ser uma Line OU um Circle. Entre todas as soluções válidas,
// retorna aquela cujo CENTRO fica mais próximo de nearPt — o que permite ao
// usuário escolher o quadrante/lado clicando perto do resultado desejado.
//
// Estratégia (offset / lugar geométrico do centro):
//   * O centro de um círculo de raio r tangente a uma RETA fica exatamente a
//     r de distância da reta -> está em uma das duas retas paralelas à
//     original, deslocadas por +r e -r.
//   * O centro de um círculo de raio r tangente a um CÍRCULO de raio R fica a
//     r de distância dele -> está em um dos dois círculos concêntricos de
//     raio (R+r) [tangência externa] ou |R-r| [tangência interna].
// O centro procurado é, então, a interseção de um lugar-geométrico-candidato
// de a com um de b. Todos os centros-candidatos são gerados, filtrados por
// tangência efetiva de raio r (tolerância ~1e-7) e o mais próximo de nearPt é
// escolhido.
//
// Tipos suportados em a/b: Line e Circle (qualquer combinação). Para qualquer
// outro tipo concreto, ou se nenhuma solução for válida, retorna nullopt.
std::optional<Circle> circleTanTanRadius(const Entity& a, const Entity& b,
                                         double r, const Point3& nearPt);

} // namespace cad
