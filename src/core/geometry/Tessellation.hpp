// src/core/geometry/Tessellation.hpp
#pragma once
#include <algorithm>
#include <cmath>

namespace cad {

// Número de segmentos de reta para aproximar um arco de raio `radius` e abertura
// `sweep` (radianos). A tolerância de flecha (sagitta) é RELATIVA ao raio —
// escala-invariante: um desenho em metros (porta r=0.8) e um em milímetros
// (porta r=800) saem igualmente suaves. Tolerância absoluta aqui era o bug do
// "arco reto": r=0.8 com flecha 0.05 dava 3 segmentos num giro de 90°.
inline int segmentsForArc(double radius, double sweep) {
    constexpr double kRelTol = 0.001;   // flecha máx. = 0.1% do raio (~5.1°/seg)
    constexpr int    kMinSeg = 4;
    constexpr int    kMaxSeg = 512;

    sweep = std::abs(sweep);
    if (radius <= 0.0 || sweep <= 0.0) return kMinSeg;

    // dθ tal que sagitta == kRelTol·r:  sagitta = r·(1 - cos(dθ/2))
    const double dTheta = 2.0 * std::acos(1.0 - kRelTol);   // ≈ 0.0894 rad
    const int n = static_cast<int>(std::ceil(sweep / dTheta));
    return std::clamp(n, kMinSeg, kMaxSeg);
}

} // namespace cad
