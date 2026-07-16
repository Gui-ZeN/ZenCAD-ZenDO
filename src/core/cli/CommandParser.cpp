// src/core/cli/CommandParser.cpp
#include "core/cli/CommandParser.hpp"

#include <optional>
#include <stdexcept>

namespace cad {

namespace {

// Remove espaços em branco no início e no fim.
std::string trim(const std::string& s) {
    const char* ws = " \t\r\n\f\v";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

// Converte para MAIÚSCULAS (ASCII).
std::string toUpper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return s;
}

// Tenta converter uma string (com trim) em double. Aceita vírgula como
// separador decimal, normalizando-a para ponto. Devolve nullopt se a string
// estiver vazia, mal formada ou tiver lixo sobrando após o número.
std::optional<double> parseNumber(const std::string& token) {
    std::string t = trim(token);
    if (t.empty()) return std::nullopt;

    // O separador de campos já é a vírgula; aqui um '.' é o ponto decimal.
    // std::stod usa locale "C", então não precisamos converter ',' decimais.
    try {
        std::size_t consumed = 0;
        const double v = std::stod(t, &consumed);
        // Garante que TODO o token foi consumido (sem caracteres restantes).
        if (consumed != t.size()) return std::nullopt;
        return v;
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

// Mapeia um token (já em MAIÚSCULAS) para o comando canônico, ou nullopt se
// não for um alias conhecido.
std::optional<std::string> aliasToCommand(const std::string& up) {
    struct Entry { const char* alias; const char* canonical; };
    static const Entry kAliases[] = {
        {"L", "LINE"},        {"LINE", "LINE"},
        {"C", "CIRCLE"},      {"CIRCLE", "CIRCLE"},
        {"REC", "RECTANGLE"}, {"RECTANG", "RECTANGLE"}, {"RECTANGLE", "RECTANGLE"},
        {"A", "ARC"},         {"ARC", "ARC"},
        {"M", "MOVE"},        {"MOVE", "MOVE"},
        {"CO", "COPY"},       {"COPY", "COPY"},
        {"RO", "ROTATE"},     {"ROTATE", "ROTATE"},
        {"SC", "SCALE"},      {"SCALE", "SCALE"},
        {"MI", "MIRROR"},     {"MIRROR", "MIRROR"},
        {"O", "OFFSET"},      {"OFFSET", "OFFSET"},
        {"E", "ERASE"},       {"ERASE", "ERASE"},      {"DEL", "ERASE"},
        {"SE", "SELECT"},     {"SELECT", "SELECT"},
    };
    for (const Entry& e : kAliases) {
        if (up == e.alias) return std::string(e.canonical);
    }
    return std::nullopt;
}

// Constante de conversão graus -> radianos.
constexpr double kPi = 3.14159265358979323846;

} // namespace

ParsedInput parseCommandLine(const std::string& raw, const Point3& lastPoint) {
    ParsedInput out;

    const std::string s = trim(raw);
    if (s.empty()) {
        out.kind = ParsedInput::Kind::Empty;
        return out;
    }

    const std::string up = toUpper(s);

    // --- Coordenada relativa: começa com '@' ---
    if (s.front() == '@') {
        const std::string body = trim(s.substr(1));

        // Forma polar: dist<ang.
        const auto lt = body.find('<');
        if (lt != std::string::npos) {
            const auto dist = parseNumber(body.substr(0, lt));
            const auto ang  = parseNumber(body.substr(lt + 1));
            if (dist && ang) {
                const double rad = *ang * kPi / 180.0;
                out.kind = ParsedInput::Kind::Point;
                out.relative = true;
                out.point = lastPoint + Point3{*dist * std::cos(rad),
                                               *dist * std::sin(rad),
                                               0.0};
                return out;
            }
            // Polar mal formado -> cai no tratamento de comando abaixo.
        } else {
            // Forma cartesiana relativa: dx,dy.
            const auto comma = body.find(',');
            if (comma != std::string::npos) {
                const auto dx = parseNumber(body.substr(0, comma));
                const auto dy = parseNumber(body.substr(comma + 1));
                if (dx && dy) {
                    out.kind = ParsedInput::Kind::Point;
                    out.relative = true;
                    out.point = lastPoint + Point3{*dx, *dy, 0.0};
                    return out;
                }
            }
            // Cartesiano relativo mal formado -> tratamento de comando abaixo.
        }
        // '@' presente mas conteúdo inválido: trata como comando desconhecido.
        out.kind = ParsedInput::Kind::Command;
        out.command = up;
        return out;
    }

    // --- Coordenada absoluta: x,y ---
    {
        const auto comma = s.find(',');
        if (comma != std::string::npos) {
            const auto x = parseNumber(s.substr(0, comma));
            const auto y = parseNumber(s.substr(comma + 1));
            if (x && y) {
                out.kind = ParsedInput::Kind::Point;
                out.relative = false;
                out.point = Point3{*x, *y, 0.0};
                return out;
            }
            // Tem vírgula mas não são dois números -> comando desconhecido.
            out.kind = ParsedInput::Kind::Command;
            out.command = up;
            return out;
        }
    }

    // --- Número solto: distância (tamanho ao longo da direção do cursor) ---
    if (const auto dval = parseNumber(s)) {
        out.kind = ParsedInput::Kind::Distance;
        out.distance = *dval;
        return out;
    }

    // --- Teclas de controle ---
    if (up == "U" || up == "UNDO") {
        out.kind = ParsedInput::Kind::Control;
        out.control = "UNDO";
        return out;
    }
    if (up == "REDO") {
        out.kind = ParsedInput::Kind::Control;
        out.control = "REDO";
        return out;
    }
    if (up == "ESC" || up == "CANCEL") {
        out.kind = ParsedInput::Kind::Control;
        out.control = "CANCEL";
        return out;
    }

    // --- Comando (alias -> canônico, ou desconhecido em MAIÚSCULAS) ---
    out.kind = ParsedInput::Kind::Command;
    if (const auto canonical = aliasToCommand(up)) {
        out.command = *canonical;
    } else {
        out.command = up;
    }
    return out;
}

const std::vector<std::string>& commandKeywords() {
    static const std::vector<std::string> kKeywords = {
        "LINE", "CIRCLE", "RECTANGLE", "ARC", "MOVE", "COPY",
        "ROTATE", "SCALE", "MIRROR", "OFFSET", "ERASE", "SELECT",
    };
    return kKeywords;
}

} // namespace cad
