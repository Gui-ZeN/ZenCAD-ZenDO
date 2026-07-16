// src/core/text/StrokeFont.hpp
#pragma once
#include "core/math/Vec.hpp"
#include <string>
#include <vector>

namespace cad {

// Fonte de traços (single-stroke) embutida: converte um texto em segmentos de
// reta para desenho com GL_LINES — assim o texto entra no MESMO pipeline da
// geometria (emitTo -> RenderBatch). Sem dependência de Qt/fonte do sistema.
//
// Os pares CONSECUTIVOS do vetor retornado formam um segmento (a,b),(c,d),...
// A origem do texto é o canto inferior-esquerdo, em `pos`. `height` é a altura
// das maiúsculas; `rotation` em radianos (CCW). Espaçamento monoespaçado.
std::vector<Point3> strokeText(const std::string& text, const Point3& pos,
                               double height, double rotation = 0.0);

// Largura total ocupada pelo texto (para alinhamento e bounding box).
double strokeTextWidth(const std::string& text, double height);

} // namespace cad
