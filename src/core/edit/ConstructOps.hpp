// src/core/edit/ConstructOps.hpp
#pragma once
#include "core/geometry/Arc.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/math/Vec.hpp"

namespace cad {

// Operações de construção geométrica pura (sem dependência de Qt nem de estado
// de aplicação). Todas trabalham no plano XY (z = 0) em precisão dupla.

// Constrói um retângulo alinhado aos eixos a partir de dois cantos opostos a e c.
// Retorna uma polilinha FECHADA com os 4 cantos, em ordem:
//   (ax,ay) -> (cx,ay) -> (cx,cy) -> (ax,cy). z = 0 em todos.
Polyline rectangleFrom2Points(const Point3& a, const Point3& c);

// Resultado de arcFrom3Points: o arco calculado e um indicador de validade.
struct Arc3Result {
    Arc  arc;
    bool ok{false};
};

// Calcula o arco circular que passa pelos três pontos p1, p2, p3.
// O centro vem da interseção das mediatrizes de p1p2 e p2p3; o raio é a
// distância do centro a p1. O arco vai de p1 a p3 PASSANDO por p2 (varredura CCW).
// Retorna ok = false se os pontos forem colineares (mediatrizes paralelas).
Arc3Result arcFrom3Points(const Point3& p1, const Point3& p2, const Point3& p3);

// Constrói um polígono regular FECHADO com `sides` vértices (>= 3), inscrito num
// círculo de raio `radius` centrado em `center`. O primeiro vértice fica no
// ângulo `rotation` (radianos, a partir de +X) e os demais seguem CCW. z = 0.
// Se sides < 3, retorna uma polilinha vazia.
Polyline regularPolygon(const Point3& center, int sides, double radius, double rotation = 0.0);

// Arco a partir de início, fim e RAIO (Start-End-Radius). Escolhe o centro que
// dá o arco menor (CCW de s a e). ok = false se |r| for pequeno demais (< corda/2).
Arc3Result arcStartEndRadius(const Point3& s, const Point3& e, double r);

// Arco a partir de início, fim e ÂNGULO incluído (Start-End-Angle, em radianos,
// CCW). ok = false se ângulo ~0 ou pontos coincidentes.
Arc3Result arcStartEndAngle(const Point3& s, const Point3& e, double angleRad);

// Arco a partir de início, fim e DIREÇÃO da tangente no início (Start-End-
// Direction). `dirPt` define a tangente em s (d = dirPt - s). ok = false se a
// direção for nula ou apontar exatamente para e (arco degenera em reta).
Arc3Result arcStartEndDirection(const Point3& s, const Point3& e, const Point3& dirPt);

// Círculo cujo DIÂMETRO vai de a a b (centro no meio, raio = |b-a|/2).
Circle circle2Points(const Point3& a, const Point3& b);

// Círculo que passa por 3 pontos (circuncírculo). `ok` = false se colineares.
Circle circle3Points(const Point3& a, const Point3& b, const Point3& c, bool& ok);

// Retângulo (cantos opostos a, c) com os 4 cantos CHANFRADOS por `dist` em cada
// aresta. Retorna polilinha fechada de 8 vértices. `dist` é limitado a quase
// metade do menor lado.
Polyline rectangleChamfer(const Point3& a, const Point3& c, double dist);

// Retângulo (cantos opostos a, c) com os 4 cantos ARREDONDADOS por raio `r`.
// Retorna uma polilinha FECHADA de 8 vértices cujos trechos de canto são arcos
// (bulge = tan(22,5°)). `r` é limitado a quase metade do menor lado.
Polyline rectangleFillet(const Point3& a, const Point3& c, double r);

} // namespace cad
