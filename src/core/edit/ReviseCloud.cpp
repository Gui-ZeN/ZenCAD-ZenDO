// src/core/edit/ReviseCloud.cpp
#include "core/edit/ReviseCloud.hpp"

#include "core/math/Constants.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cad {

namespace {

// Tolerância geométrica para comparações com zero.
constexpr double kEps = 1e-9;

// Bulge de um SEMICÍRCULO: ângulo incluído = 180° => bulge = tan(180°/4) = tan(45°) = 1.
// O módulo é fixo; só o SINAL muda para escolher o lado da bolha.
constexpr double kBulgeSemicircle = 1.0;

// Área com sinal (fórmula do cadarço / shoelace) do polígono formado pelos
// vértices em ordem. > 0 => orientação CCW; < 0 => CW. Usada apenas em caminhos
// fechados para decidir de que lado fica "fora".
double signedArea(const std::vector<Point3>& pts) {
    double a = 0.0;
    const std::size_t n = pts.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Point3& p = pts[i];
        const Point3& q = pts[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5;
}

} // namespace

// --------------------------------------------------------------------------
// CONVENÇÃO DE SINAL DO BULGE (como garantimos bolhas para FORA)
// --------------------------------------------------------------------------
// O bulge é relativo ao SENTIDO do trecho (p0 -> p1):
//   bulge > 0  => arco CCW, abaulado para a ESQUERDA do trecho;
//   bulge < 0  => arco CW,  abaulado para a DIREITA do trecho.
//
// Para que TODAS as bolhas apontem para o mesmo lado (fora), basta usar o MESMO
// sinal em todos os trechos — o lado correto depende da orientação do caminho:
//   * Caminho FECHADO percorrido em CCW (área com sinal > 0): "fora" fica à
//     DIREITA de cada trecho  => bulge NEGATIVO (-1).
//   * Caminho FECHADO percorrido em CW  (área com sinal < 0): "fora" fica à
//     ESQUERDA => bulge POSITIVO (+1).
//   * Caminho ABERTO: não há "dentro/fora" definido; adotamos a convenção fixa
//     de bolhas à DIREITA do sentido de percurso (bulge NEGATIVO, -1), com sinal
//     consistente em todos os trechos.
// --------------------------------------------------------------------------

Polyline revisionCloudFromPath(const std::vector<Point3>& path, double arcRadius, bool closed) {
    // Casos degenerados: devolve a polilinha trivial (sem bolhas).
    if (path.size() < 2 || arcRadius <= kEps) {
        return Polyline(path, closed);
    }

    // Define o sinal do bulge de modo que as bolhas fiquem para fora (ver acima).
    double bulgeSign;
    if (closed) {
        bulgeSign = (signedArea(path) >= 0.0) ? -1.0 : +1.0;  // CCW => -1, CW => +1
    } else {
        bulgeSign = -1.0;  // convenção fixa: bolhas à direita do sentido
    }
    const double bulge = bulgeSign * kBulgeSemicircle;

    // Comprimento alvo de cada bolha (corda) ~= 2*arcRadius.
    const double targetChord = 2.0 * arcRadius;

    std::vector<Point3> outVerts;
    std::vector<double> outBulges;

    // Número de trechos do caminho: aberto = n-1; fechado = n (inclui o de volta).
    const std::size_t n = path.size();
    const std::size_t nSeg = closed ? n : (n - 1);

    for (std::size_t s = 0; s < nSeg; ++s) {
        const Point3& p0 = path[s];
        const Point3& p1 = path[(s + 1) % n];

        const double segLen = std::hypot(p1.x - p0.x, p1.y - p0.y);
        if (segLen < kEps) continue;  // trecho nulo: ignora

        // Quantas bolhas cabem no trecho (no mínimo 1). Arredonda para o inteiro
        // mais próximo para que a corda real fique o mais perto possível de 2*r.
        int bumps = static_cast<int>(std::lround(segLen / targetChord));
        if (bumps < 1) bumps = 1;

        // Emite os vértices de início de cada sub-corda deste trecho, cada um com
        // o bulge da bolha. O vértice final do trecho (p1) é o início do próximo
        // (ou o fecho), então NÃO é emitido aqui — evita duplicatas.
        for (int i = 0; i < bumps; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(bumps);
            outVerts.emplace_back(p0.x + (p1.x - p0.x) * t,
                                  p0.y + (p1.y - p0.y) * t,
                                  0.0);
            outBulges.push_back(bulge);
        }
    }

    // Para caminho ABERTO, acrescenta o último vértice (fim do último trecho), que
    // não tem bolha de saída. Para caminho FECHADO, o fecho liga o último vértice
    // emitido de volta ao primeiro, então nada a acrescentar.
    if (!closed) {
        const Point3& last = path.back();
        outVerts.emplace_back(last.x, last.y, 0.0);
        outBulges.push_back(0.0);  // sem arco de saída no vértice final
    }

    return Polyline(std::move(outVerts), std::move(outBulges), closed);
}

Polyline revisionCloudRect(const Point3& a, const Point3& c, double arcRadius) {
    // Monta os 4 cantos do retângulo (cantos opostos a e c) em ordem CCW,
    // independentemente da disposição relativa de a e c.
    const double xmin = std::min(a.x, c.x), xmax = std::max(a.x, c.x);
    const double ymin = std::min(a.y, c.y), ymax = std::max(a.y, c.y);

    std::vector<Point3> path{
        {xmin, ymin, 0.0},   // canto inferior-esquerdo
        {xmax, ymin, 0.0},   // inferior-direito
        {xmax, ymax, 0.0},   // superior-direito
        {xmin, ymax, 0.0}};  // superior-esquerdo  (sentido CCW)

    return revisionCloudFromPath(path, arcRadius, /*closed=*/true);
}

} // namespace cad
