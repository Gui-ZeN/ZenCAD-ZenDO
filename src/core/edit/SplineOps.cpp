// src/core/edit/SplineOps.cpp
#include "core/edit/SplineOps.hpp"

#include <algorithm>

namespace cad {
namespace {

// Constrói um knot vector "clamped uniforme" para n+1 pontos de controle e
// grau `degree`.
//
// "Clamped" = as duas pontas têm o nó repetido degree+1 vezes. Essa
// multiplicidade na borda faz a B-spline interpolar (passar por) o 1º e o
// último ponto de controle, em vez de apenas se aproximar deles.
//
// O vetor tem m+1 = (n+1) + degree + 1 nós, distribuídos assim:
//   [0, 0, ..., 0,  u_1, u_2, ..., u_{interior},  1, 1, ..., 1]
//    \---degree+1--/  \---internos uniformes---/  \--degree+1--/
//
// Aqui usamos a parametrização normalizada em [0, 1].
std::vector<double> clampedUniformKnots(int n, int degree) {
    const int m = n + degree + 1;          // último índice válido do vetor de nós
    std::vector<double> knots(m + 1, 0.0);

    // Pontas presas (clamped): degree+1 zeros no início, degree+1 uns no fim.
    for (int i = 0; i <= degree; ++i) {
        knots[i]     = 0.0;
        knots[m - i] = 1.0;
    }

    // Nós internos uniformemente espaçados.
    // Quantidade de nós internos = m - 2*(degree+1) + 1 = n - degree.
    const int interior = n - degree;       // pode ser 0 (sem nós internos)
    for (int j = 1; j <= interior; ++j) {
        knots[degree + j] = static_cast<double>(j) / static_cast<double>(interior + 1);
    }

    return knots;
}

// Localiza o span de nós que contém o parâmetro `u`, i.e. devolve o índice k
// tal que knots[k] <= u < knots[k+1], respeitando os limites válidos
// [degree, n]. Tratamos u == 1.0 (extremo direito) como pertencente ao último
// span válido para que o ponto final seja avaliado corretamente.
int findSpan(int n, int degree, double u, const std::vector<double>& knots) {
    if (u >= knots[n + 1]) {
        return n;                          // caso de borda: u no extremo direito
    }
    if (u <= knots[degree]) {
        return degree;                     // caso de borda: u no extremo esquerdo
    }
    // Busca binária no intervalo [degree, n+1].
    int low = degree;
    int high = n + 1;
    int mid = (low + high) / 2;
    while (u < knots[mid] || u >= knots[mid + 1]) {
        if (u < knots[mid]) high = mid;
        else                low = mid;
        mid = (low + high) / 2;
    }
    return mid;
}

// Avalia a B-spline no parâmetro `u` pelo algoritmo de de Boor.
//
// de Boor é a contraparte do de Casteljau para B-splines: parte dos pontos de
// controle que influenciam o span atual (são degree+1 deles) e os combina
// linearmente, em `degree` rodadas de interpolação, até restar um único ponto —
// o ponto da curva em `u`. É estável porque só faz interpolações lineares
// (combinações convexas), sem subtrações que ampliem erro.
Point3 deBoor(int span, double u, int degree,
              const std::vector<Point3>& ctrl,
              const std::vector<double>& knots) {
    // d[j] = pontos de controle relevantes para este span (cópia de trabalho).
    std::vector<Point3> d(degree + 1);
    for (int j = 0; j <= degree; ++j) {
        d[j] = ctrl[span - degree + j];
    }

    for (int r = 1; r <= degree; ++r) {
        for (int j = degree; j >= r; --j) {
            const int i = span - degree + j;
            const double denom = knots[i + degree - r + 1] - knots[i];
            const double alpha = (denom > 0.0)
                                     ? (u - knots[i]) / denom
                                     : 0.0;
            // Interpolação linear: d[j] = (1-alpha)*d[j-1] + alpha*d[j].
            d[j] = d[j - 1] * (1.0 - alpha) + d[j] * alpha;
        }
    }
    return d[degree];
}

} // namespace

std::vector<Point3> bsplinePoints(const std::vector<Point3>& ctrl,
                                  int degree,
                                  int samplesPerSpan) {
    // Caso de borda: menos de 2 pontos não definem curva alguma.
    if (ctrl.size() < 2) {
        return ctrl;
    }

    // Grau efetivo: nunca pode exceder (nº de pontos - 1). Assim 2 pontos viram
    // reta (grau 1), 3 pontos grau 2, etc.
    int deg = std::min(degree, static_cast<int>(ctrl.size()) - 1);
    if (deg < 1) deg = 1;

    if (samplesPerSpan < 1) samplesPerSpan = 1;

    const int n = static_cast<int>(ctrl.size()) - 1;   // índice do último ponto
    const std::vector<double> knots = clampedUniformKnots(n, deg);

    // Nº de vãos (spans) entre nós internos = n - deg + 1 (sempre >= 1).
    const int spans = n - deg + 1;
    const int totalSamples = spans * samplesPerSpan;    // intervalos a percorrer

    std::vector<Point3> out;
    out.reserve(totalSamples + 1);

    // Amostragem uniforme em u ∈ [0, 1], inclusive os dois extremos.
    for (int s = 0; s <= totalSamples; ++s) {
        const double u = static_cast<double>(s) / static_cast<double>(totalSamples);
        const int span = findSpan(n, deg, u, knots);
        Point3 p = deBoor(span, u, deg, ctrl, knots);
        p.z = 0.0;                                       // resultado no plano XY
        out.push_back(p);
    }

    return out;
}

} // namespace cad
