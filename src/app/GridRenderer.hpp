// src/app/GridRenderer.hpp
#pragma once
#include <vector>
#include <cmath>

namespace cad {

// Constrói os vértices de uma grade para desenho com GL_LINES.
//
// Recebe o retângulo de mundo visível [visMin..visMax] (coordenadas de mundo, em
// double) e a origem de rebase usada pelo viewport. Cada vértice retornado já vem
// RELATIVO a essa origem (world - origin), pronto para subir à GPU em float — o
// mesmo esquema que o ViewportWidget usa para a geometria do documento.
//
// O espaçamento é escolhido na sequência "redonda" 1,2,5 × 10^n de modo que, na
// TELA, fique entre ~40 e ~120 px (espaçamento_tela = spacing * pxPerUnit). O
// espaçamento escolhido é devolvido em `outSpacing`.
//
// Como salvaguarda, limitamos o total de linhas (horizontais + verticais): se a
// grade passar de ~400 linhas, aumentamos o espaçamento na sequência 1,2,5 até
// caber. Assim nunca explodimos o número de vértices, por mais que o usuário
// dê zoom out.
//
// Retorna pares de floats (x,y) — dois floats por vértice, dois vértices por
// linha. Vazio se o retângulo for degenerado ou o pxPerUnit não for positivo.
inline std::vector<float> buildGrid(double visMinX, double visMinY,
                                    double visMaxX, double visMaxY,
                                    double originX, double originY,
                                    double pxPerUnit, double& outSpacing) {
    std::vector<float> verts;
    outSpacing = 0.0;

    // Guarda contra entradas inválidas (retângulo degenerado, escala não positiva).
    if (pxPerUnit <= 0.0 || !(visMaxX > visMinX) || !(visMaxY > visMinY)) {
        return verts;
    }

    // Limites de espaçamento na tela (em pixels) e teto de linhas por eixo.
    constexpr double kMinPx   = 40.0;
    constexpr double kMaxPx   = 120.0;
    constexpr int    kMaxLines = 400;  // teto total (H + V) de linhas

    // Espaçamento bruto que daria ~kMinPx na tela; depois "arredondamos" para
    // cima na sequência 1,2,5 × 10^n.
    const double rawSpacing = kMinPx / pxPerUnit;

    // Magnitude (potência de 10) imediatamente abaixo do espaçamento bruto.
    double exponent = std::floor(std::log10(rawSpacing));
    double pow10    = std::pow(10.0, exponent);

    // Sobe pela sequência 1,2,5 até o espaçamento na tela entrar na faixa
    // desejada (>= kMinPx). Itera no mantissa {1,2,5} e "vira a casa" no 10.
    static const double kSteps[] = {1.0, 2.0, 5.0};
    int stepIdx = 0;
    double spacing = pow10 * kSteps[stepIdx];

    auto screenPx = [&](double s) { return s * pxPerUnit; };

    while (screenPx(spacing) < kMinPx) {
        ++stepIdx;
        if (stepIdx >= 3) { stepIdx = 0; pow10 *= 10.0; }
        spacing = pow10 * kSteps[stepIdx];
    }

    // Salvaguarda de densidade: enquanto o nº total de linhas estourar o teto,
    // aumenta o espaçamento na mesma sequência 1,2,5. Também respeita kMaxPx
    // implicitamente (espaçamentos maiores deixam menos linhas).
    auto lineCount = [&](double s) {
        const double nx = std::floor(visMaxX / s) - std::ceil(visMinX / s) + 1.0;
        const double ny = std::floor(visMaxY / s) - std::ceil(visMinY / s) + 1.0;
        return (nx > 0 ? nx : 0.0) + (ny > 0 ? ny : 0.0);
    };

    while (lineCount(spacing) > static_cast<double>(kMaxLines)) {
        ++stepIdx;
        if (stepIdx >= 3) { stepIdx = 0; pow10 *= 10.0; }
        spacing = pow10 * kSteps[stepIdx];
    }

    outSpacing = spacing;

    // Primeira/última linha de grade dentro do retângulo visível, em mundo.
    const double i0 = std::ceil(visMinX / spacing);
    const double i1 = std::floor(visMaxX / spacing);
    const double j0 = std::ceil(visMinY / spacing);
    const double j1 = std::floor(visMaxY / spacing);

    // Reserva aproximada: cada linha = 2 vértices = 4 floats.
    const double approxLines = (i1 - i0 + 1.0) + (j1 - j0 + 1.0);
    if (approxLines > 0.0) {
        verts.reserve(static_cast<size_t>(approxLines) * 4);
    }

    // Extremos do retângulo já rebaseados (constantes nas duas direções).
    const float relMinX = static_cast<float>(visMinX - originX);
    const float relMaxX = static_cast<float>(visMaxX - originX);
    const float relMinY = static_cast<float>(visMinY - originY);
    const float relMaxY = static_cast<float>(visMaxY - originY);

    // Linhas verticais (x constante), de baixo a cima.
    for (double i = i0; i <= i1; i += 1.0) {
        const float x = static_cast<float>(i * spacing - originX);
        verts.push_back(x); verts.push_back(relMinY);
        verts.push_back(x); verts.push_back(relMaxY);
    }

    // Linhas horizontais (y constante), da esquerda à direita.
    for (double j = j0; j <= j1; j += 1.0) {
        const float y = static_cast<float>(j * spacing - originY);
        verts.push_back(relMinX); verts.push_back(y);
        verts.push_back(relMaxX); verts.push_back(y);
    }

    return verts;
}

} // namespace cad
