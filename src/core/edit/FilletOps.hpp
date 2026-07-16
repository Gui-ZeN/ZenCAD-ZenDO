// src/core/edit/FilletOps.hpp
#pragma once
#include "core/math/Vec.hpp"
#include <memory>

namespace cad {

class Entity;  // fwd — Line/Circle/Arc derivam de Entity

// ============================================================================
//  FilletOps — geometria de concordância (fillet) por raio entre duas
//  primitivas (Line, Circle, Arc), em QUALQUER combinação e em qualquer ordem.
//
//  Espelha o ESTILO de GeometryOps::filletLines, mas generaliza para pares com
//  arco/círculo usando o método robusto por LUGARES GEOMÉTRICOS de offset:
//  o centro do arco de concordância fica a distância `radius` de AMBAS as
//  entidades, então é uma interseção dos loci de centros equidistantes.
//
//  Tudo no plano XY (z = 0); precisão dupla.
// ============================================================================

// Resultado do cálculo de concordância. Em caso de falha (raio inválido,
// paralelismo, sem interseção, tangência fora de arco), ok = false e os demais
// campos ficam zerados.
struct FilletGeom {
    bool   ok = false;
    Point3 center{};        // centro do arco de concordância
    double radius = 0.0;    // = raio pedido
    Point3 tan1{};          // ponto de tangência na entidade 1 (e1)
    Point3 tan2{};          // ponto de tangência na entidade 2 (e2)
    // Ângulos (rad, a partir de +X, vistos do center) do arco MENOR que forma o
    // arredondamento — a "boca" do canto. Convenção igual à de Arc: varredura
    // anti-horária (CCW) de startAngle a endAngle, com sweep <= pi. A ordem é
    // escolhida de modo que o arco curto vá de tan1 a tan2 (não o complemento).
    double startAngle = 0;
    double endAngle   = 0;
};

// Calcula a geometria de concordância (fillet) de raio `radius` entre e1 e e2,
// escolhendo a solução no canto indicado por pick1 (sobre/perto de e1) e pick2
// (sobre/perto de e2). Suporta os pares: Line-Line, Line-Circle, Line-Arc,
// Arc-Arc, Arc-Circle, Circle-Circle (ordem irrelevante). Retorna ok=false se
// não houver concordância possível com esse raio.
FilletGeom filletGeometry(const Entity& e1, const Entity& e2, double radius,
                          const Point3& pick1, const Point3& pick2);

// Apara a entidade `e` até o ponto de tangência `tan`, MANTENDO o lado onde está
// `pick` (usado pelo FILLET após calcular a concordância):
//   * Line  -> nova Line do extremo do lado de pick até tan;
//   * Arc   -> sub-arco do lado de pick até tan;
//   * Circle-> mantém o círculo inteiro (AutoCAD não apara círculo no fillet).
// Devolve a nova geometria (sem copiar propriedades) ou nullptr se não aplicável.
std::unique_ptr<Entity> trimToTangent(const Entity& e, const Point3& tan, const Point3& pick);

} // namespace cad
