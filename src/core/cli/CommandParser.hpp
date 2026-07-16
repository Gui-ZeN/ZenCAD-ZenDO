// src/core/cli/CommandParser.hpp
#pragma once

#include <string>
#include <vector>

#include "core/math/Vec.hpp"

namespace cad {

// Resultado do parsing de uma linha digitada na barra de comandos (estilo
// AutoCAD). Pode ser um comando, uma coordenada, uma tecla de controle ou
// simplesmente vazio. Quem chama decide o que fazer com cada caso.
struct ParsedInput {
    enum class Kind { Empty, Command, Point, Distance, Control } kind{Kind::Empty};

    // Nome canônico do comando em MAIÚSCULAS (ex.: "LINE"). Válido em Kind::Command.
    std::string command;
    // Ponto resolvido (já absoluto, no sistema de mundo). Válido em Kind::Point.
    Point3      point{};
    // true quando a coordenada foi informada de forma relativa (@dx,dy ou @dist<ang).
    bool        relative{false};
    // Distância (número solto digitado): tamanho ao longo da direção do cursor.
    // Válido em Kind::Distance.
    double      distance{0.0};
    // Ação de controle: "UNDO", "REDO" ou "CANCEL". Válido em Kind::Control.
    std::string control;
};

// Faz o parsing de uma linha de entrada bruta. `lastPoint` é o último ponto
// confirmado, usado como base para coordenadas relativas (@). Comandos são
// case-insensitive e a string sofre trim antes da análise.
ParsedInput parseCommandLine(const std::string& raw, const Point3& lastPoint);

// Lista dos nomes canônicos de comandos (para autocomplete). Ordem estável.
const std::vector<std::string>& commandKeywords();

} // namespace cad
