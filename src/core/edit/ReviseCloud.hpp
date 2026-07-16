// src/core/edit/ReviseCloud.hpp
#pragma once
#include "core/geometry/Polyline.hpp"
#include "core/math/Vec.hpp"

#include <vector>

namespace cad {

// Geração de NUVEM DE REVISÃO (revision cloud): sequência de arcos convexos
// ("bolhas") distribuídos ao longo de um caminho, todos apontando para o MESMO
// lado (para FORA). Operações de construção geométrica pura — sem Qt, sem estado
// de aplicação, plano XY (z = 0), precisão dupla.
//
// Modelo do arco: cada bolha é um trecho de Polyline com bulge fixo. Optou-se por
// SEMICÍRCULOS (bulge de módulo 1.0): bulge = tan(ângulo_incluído / 4); um
// semicírculo tem ângulo incluído de 180°, logo bulge = tan(45°) = 1.0. Assim a
// flecha (sagitta) de cada bolha é igual ao raio da sub-corda, dando bolhas bem
// marcadas e independentes do raio escolhido. O comprimento de cada bolha (corda)
// é ~= 2*arcRadius, de modo que o raio da bolha ~= arcRadius.

// Gera a nuvem de revisão a partir de um caminho de vértices.
//   - Cada trecho do caminho é subdividido em N sub-cordas de comprimento
//     ~= 2*arcRadius (N >= 1 por trecho). Cada sub-corda vira um arco convexo
//     (semicírculo) abaulado para FORA do caminho.
//   - `closed` controla se o último vértice liga de volta ao primeiro (e define
//     "fora" pela orientação do polígono; ver .cpp).
// Retorna uma Polyline com os vértices nas junções das bolhas e m_bulges
// preenchido. Se o caminho tiver < 2 vértices ou arcRadius <= 0, retorna a
// polilinha trivial (sem arcos).
Polyline revisionCloudFromPath(const std::vector<Point3>& path, double arcRadius, bool closed);

// Gera a nuvem de revisão fechada a partir de um retângulo dado por dois cantos
// opostos a e c. Monta o caminho dos 4 cantos em ordem CCW e delega a
// revisionCloudFromPath(path, arcRadius, /*closed=*/true). As bolhas ficam para
// FORA do retângulo.
Polyline revisionCloudRect(const Point3& a, const Point3& c, double arcRadius);

} // namespace cad
