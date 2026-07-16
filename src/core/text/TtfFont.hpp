// src/core/text/TtfFont.hpp
// Fonte TTF PLUGÁVEL: o kernel continua headless — a camada de UI (Qt)
// registra aqui um tesselador que converte UMA linha de texto nos SEGMENTOS
// dos contornos dos glifos (pares de Point3) e um medidor de largura.
// Sem provider registrado (ou com font vazia), o MText usa a StrokeFont
// interna de traços — o smoke headless continua exato.
#pragma once
#include "core/math/Vec.hpp"
#include <functional>
#include <string>
#include <vector>

namespace cad {

struct TtfLine {
    std::string text;    // UTF-8, uma linha (sem '\n')
    std::string font;    // família, ex.: "Arial" (vazia = StrokeFont)
    Point3      pos;     // início da linha de base (esquerda)
    double      height;  // altura de maiúscula em unidades de mundo
    double      rotRad;  // rotação CCW no plano XY
    bool        bold{false};
    bool        italic{false};
};

// Pares consecutivos do retorno = um segmento (mesmo contrato de strokeText).
// O medidor recebe a MESMA TtfLine (pos/rotação ignoradas) — negrito/itálico
// mudam a largura, então a medição precisa dos mesmos atributos.
using TtfTessellator = std::function<std::vector<Point3>(const TtfLine&)>;
using TtfMeasurer    = std::function<double(const TtfLine&)>;

inline TtfTessellator& ttfTessellator() { static TtfTessellator f; return f; }
inline TtfMeasurer&    ttfMeasurer()    { static TtfMeasurer f;    return f; }

} // namespace cad
