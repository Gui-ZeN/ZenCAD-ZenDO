// src/core/edit/GeometryOps.hpp
#pragma once
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/math/Vec.hpp"
#include <optional>

namespace cad {

// ============================================================================
//  GeometryOps — operações de edição geométrica (offset, trim, fillet).
//  Funções livres, sem estado, todas no plano XY em precisão dupla (double).
//  A coordenada Z dos pontos de entrada é, em geral, ignorada (geometria 2D);
//  os resultados são emitidos com z = 0.
// ============================================================================

// Resultado de uma concordância (fillet) entre duas linhas: o arco tangente e
// as duas linhas aparadas até os respectivos pontos de tangência. Se a operação
// não for possível (linhas paralelas ou raio grande demais), ok = false e os
// demais campos ficam indefinidos.
struct FilletResult {
    Arc  arc;
    Line line1;
    Line line2;
    bool ok{false};
};

// Interseção das DUAS RETAS INFINITAS que passam por a0–a1 e b0–b1 (não dos
// segmentos). Retorna nullopt se as retas forem paralelas (ou degeneradas).
std::optional<Point3> intersectInfiniteLines(const Point3& a0, const Point3& a1,
                                             const Point3& b0, const Point3& b1);

// Desloca a linha src perpendicularmente por uma distância dist, para o lado em
// que o ponto side se encontra. A linha resultante é paralela à original e tem
// o mesmo comprimento.
Line offsetLine(const Line& src, double dist, const Point3& side);

// Desloca o círculo src somando ou subtraindo dist ao raio: se side estiver
// FORA do círculo o raio cresce (+dist); se estiver DENTRO o raio diminui
// (-dist). O centro é preservado. O raio resultante é fixado em zero como piso.
Circle offsetCircle(const Circle& src, double dist, const Point3& side);

// Desloca o arco src de forma concêntrica (mesmo centro e ângulos): se side
// estiver FORA o raio cresce (+dist), se DENTRO diminui (-dist). Piso de raio 0.
Arc offsetArc(const Arc& src, double dist, const Point3& side);

// Desloca a polilinha src paralelamente por dist, para o lado de side. Cada
// vértice é movido pela normal-bissetriz (miter simples); os bulges (arcos) são
// preservados — um offset paralelo mantém o ângulo incluído, logo o bulge.
Polyline offsetPolyline(const Polyline& src, double dist, const Point3& side);

// Corta a linha target na interseção com a reta infinita de corte cutA–cutB,
// removendo a porção do lado do ponto pick e retornando a Line que permanece.
// Retorna nullopt se não houver interseção ou se ela cair fora do alvo (nada a
// cortar). Trim suportado apenas para linha-vs-linha.
std::optional<Line> trimLineAt(const Line& target, const Point3& cutA,
                               const Point3& cutB, const Point3& pick);

// Calcula a concordância (arco tangente) de raio radius entre as linhas l1 e l2.
// Devolve o arco e as duas linhas aparadas até os pontos de tangência. Em caso
// de impossibilidade (linhas paralelas, ou raio grande demais para caber nos
// segmentos), o campo ok do resultado fica false.
FilletResult filletLines(const Line& l1, const Line& l2, double radius);

} // namespace cad
