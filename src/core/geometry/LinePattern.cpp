// src/core/geometry/LinePattern.cpp
#include "core/geometry/LinePattern.hpp"

#include <cmath>
#include <string>
#include <vector>
#include <map>

namespace cad {

namespace {

// Converte uma string para maiúsculas (apenas ASCII) — usado para comparação
// case-insensitive dos nomes de estilo.
std::string toUpperAscii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c >= 'a' && c <= 'z') {
            out.push_back(static_cast<char>(c - ('a' - 'A')));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Padrão de traço como sequência de comprimentos: índices PARES são traços
// (visíveis) e índices ÍMPARES são lacunas (omitidas). Ciclo: traço, lacuna,
// traço, lacuna, ...
//
// Devolve um vetor vazio para Continuous (sem padronização).
std::vector<double> basePattern(LineStyle style) {
    switch (style) {
        case LineStyle::Dashed:
            return {4.0, 2.0}; // traço 4, lacuna 2
        case LineStyle::Hidden:
            return {2.0, 1.0}; // traço 2, lacuna 1
        case LineStyle::Center:
            // longo 12, lacuna 2, curto 2, lacuna 2 (longo-curto-longo...)
            return {12.0, 2.0, 2.0, 2.0};
        case LineStyle::Continuous:
        default:
            return {};
    }
}

} // namespace

LineStyle lineStyleFromName(const std::string& name) {
    const std::string up = toUpperAscii(name);
    if (up == "CONTINUOUS") return LineStyle::Continuous;
    if (up == "DASHED" || up == "TRACEJADO") return LineStyle::Dashed;
    if (up == "CENTER" || up == "CENTRO") return LineStyle::Center;
    if (up == "HIDDEN" || up == "OCULTA") return LineStyle::Hidden;
    return LineStyle::Continuous; // qualquer outro valor
}

std::vector<Point3> applyLinePattern(const std::vector<Point3>& segments,
                                     LineStyle style,
                                     double scale) {
    if (style == LineStyle::Continuous) return segments;
    return applyLinePattern(segments, basePattern(style), scale);   // delega ao overload
}

std::vector<Point3> applyLinePattern(const std::vector<Point3>& segments,
                                     const std::vector<double>& patternIn,
                                     double scale) {
    if (patternIn.empty()) return segments;           // contínuo
    if (scale <= 0.0) scale = 1.0;

    std::vector<double> pattern;
    pattern.reserve(patternIn.size());
    double sum = 0.0;
    for (double d : patternIn) { const double s = d * scale; pattern.push_back(s); sum += std::fabs(s); }
    if (sum <= 1e-9) return segments;                  // padrão degenerado -> contínuo (evita laço)

    std::vector<Point3> out;
    // Estimativa grosseira de capacidade (cada segmento vira vários traços).
    out.reserve(segments.size() * 2);

    // Processa pares consecutivos (a = i, b = i+1). Índice ímpar final solto
    // (vetor com tamanho ímpar) é ignorado por não formar um par.
    for (std::size_t i = 0; i + 1 < segments.size(); i += 2) {
        const Point3& a = segments[i];
        const Point3& b = segments[i + 1];

        const Vec3 delta = b - a;
        const double total = delta.length();
        if (total <= 0.0) {
            continue; // segmento de comprimento zero
        }

        // Direção unitária ao longo do segmento.
        const Vec3 dir = delta * (1.0 / total);

        // Caminha do início ao fim acumulando comprimento, alternando
        // traço/lacuna ciclando pelo padrão. Fase reinicia a cada segmento.
        double pos = 0.0;          // posição atual ao longo do segmento
        std::size_t pi = 0;        // índice no padrão
        while (pos < total) {
            const double segLen = pattern[pi];
            const double end = pos + segLen;
            const double clampedEnd = end < total ? end : total;

            // Índices pares = traço visível; ímpares = lacuna (omitida).
            if ((pi % 2) == 0 && segLen > 0.0) {
                const Point3 p0 = a + dir * pos;
                const Point3 p1 = a + dir * clampedEnd;
                out.push_back(p0);
                out.push_back(p1);
            }

            pos = end;
            pi = (pi + 1) % pattern.size();
        }
    }

    return out;
}

namespace {
// Registro global de tipos de linha custom (nome -> padrão de traços).
std::map<std::string, std::vector<double>>& customReg() {
    static std::map<std::string, std::vector<double>> reg;
    return reg;
}
} // namespace

void registerLineType(const std::string& name, const std::vector<double>& pattern) {
    if (!name.empty()) customReg()[toUpperAscii(name)] = pattern;
}

void registerStandardLineTypes() {
    // Família padrão à la acad.lin (índices pares = traço, ímpares = lacuna),
    // na MESMA base dos embutidos (Dashed = 4/2) — o LTSCALE escala tudo junto.
    // Registradas no MESMO registro dos tipos custom: aparecem em todos os
    // combos e resolvem por nome no render/persistência.
    static const struct { const char* name; std::vector<double> pat; } kStd[] = {
        {"DOT",      {0.15, 2.0}},                               // ······
        {"DASHDOT",  {4.0, 1.5, 0.15, 1.5}},                     // —·—·
        {"DIVIDE",   {4.0, 1.5, 0.15, 1.5, 0.15, 1.5}},          // —··—··
        {"BORDER",   {4.0, 1.5, 4.0, 1.5, 0.15, 1.5}},           // ——·——·
        {"PHANTOM",  {10.0, 1.5, 2.0, 1.5, 2.0, 1.5}},           // ———— — —
        {"CENTER2",  {6.0, 1.0, 1.0, 1.0}},                      // centro curto
        {"HIDDEN2",  {1.0, 0.5}},                                // oculta curta
        {"DASHEDX2", {8.0, 4.0}},                                // tracejada 2x
    };
    for (const auto& s : kStd) registerLineType(s.name, s.pat);
}

std::vector<std::string> customLineTypeNames() {
    std::vector<std::string> names;
    for (const auto& kv : customReg()) names.push_back(kv.first);
    return names;
}

const std::vector<double>* customLinePattern(const std::string& name) {
    const auto it = customReg().find(toUpperAscii(name));
    return it == customReg().end() ? nullptr : &it->second;
}

std::vector<Point3> applyLineTypeByName(const std::vector<Point3>& segments,
                                        const std::string& name, double scale) {
    if (const std::vector<double>* p = customLinePattern(name))   // custom tem prioridade
        return applyLinePattern(segments, *p, scale);
    return applyLinePattern(segments, lineStyleFromName(name), scale);
}

} // namespace cad
