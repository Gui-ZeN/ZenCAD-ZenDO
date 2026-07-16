// src/app/CommandTable.hpp
#pragma once
#include <QString>
#include <QStringList>
#include <QHash>
#include <vector>

#include "core/interaction/ToolController.hpp"   // ToolKind

namespace cad {

// Tabela ÚNICA de comandos: nome canônico + aliases (estilo AutoCAD) -> ToolKind.
// Usada tanto pelo parser da linha de comando quanto pelo autocomplete.
struct CommandDef {
    QString    canonical;     // nome exibido no autocomplete
    QStringList aliases;      // atalhos de digitação (incluindo o próprio canônico)
    ToolKind   tool;
};

inline const std::vector<CommandDef>& commandTable() {
    static const std::vector<CommandDef> t = {
        // --- Criação ---
        {"LINE",        {"LINE", "L"},            ToolKind::Line},
        {"CIRCLE",      {"CIRCLE", "C"},          ToolKind::Circle},
        {"CIRCLE2P",    {"CIRCLE2P", "C2P"},      ToolKind::Circle2P},
        {"CIRCLE3P",    {"CIRCLE3P", "C3P"},      ToolKind::Circle3P},
        {"CIRCLETTR",   {"CIRCLETTR", "TTR"},     ToolKind::CircleTTR},
        {"CIRCLETTT",   {"CIRCLETTT", "TTT"},     ToolKind::CircleTTT},
        {"ARC",         {"ARC", "A"},             ToolKind::Arc3},
        {"ARCSCE",      {"ARCSCE"},               ToolKind::ArcSCE},
        {"ARCCSE",      {"ARCCSE"},               ToolKind::ArcCSE},
        {"ARCSER",      {"ARCSER"},               ToolKind::ArcSER},
        {"ARCSEA",      {"ARCSEA"},               ToolKind::ArcSEA},
        {"ARCSED",      {"ARCSED"},               ToolKind::ArcSED},
        {"RECTANGLE",   {"RECTANGLE", "REC", "RECT"}, ToolKind::Rectangle},
        {"RECTCHAMFER", {"RECTCHAMFER"},          ToolKind::RectChamfer},
        {"RECTFILLET",  {"RECTFILLET"},           ToolKind::RectFillet},
        {"ELLIPSE",     {"ELLIPSE", "EL"},        ToolKind::Ellipse},
        {"ELLIPSEARC",  {"ELLIPSEARC"},           ToolKind::EllipseArc},
        {"POLYLINE",    {"POLYLINE", "PL"},       ToolKind::Polyline},
        {"POLYGON",     {"POLYGON", "POL"},       ToolKind::Polygon},
        {"POINT",       {"POINT", "PO"},          ToolKind::Point},
        {"SPLINE",      {"SPLINE", "SPL"},        ToolKind::Spline},
        {"SPLINECV",    {"SPLINECV"},             ToolKind::SplineCV},
        {"XLINE",       {"XLINE", "XL"},          ToolKind::XLine},
        {"RAY",         {"RAY"},                  ToolKind::Ray},
        {"MLINE",       {"MLINE"},                ToolKind::MLine},
        {"TEXT",        {"TEXT", "T", "MTEXT", "MT", "DTEXT"}, ToolKind::Text},
        {"HATCH",       {"HATCH", "H", "BHATCH"}, ToolKind::Hatch},
        {"WIPEOUT",     {"WIPEOUT", "WIPE"},      ToolKind::Wipeout},
        {"TABLE",       {"TABLE", "TB"},          ToolKind::TableTool},
        {"REVCLOUD",    {"REVCLOUD", "REVC"},     ToolKind::RevCloud},
        {"PAREDE",      {"PAREDE", "WALL", "PAR"}, ToolKind::WallTool},
        {"JANELA",      {"JANELA", "WINDOW", "JAN"}, ToolKind::WindowTool},
        {"PORTA",       {"PORTA", "DOOR"},        ToolKind::Door},
        {"LEADER",      {"LEADER", "LE"},         ToolKind::Leader},
        {"MLEADER",     {"MLEADER", "MLD", "MLEAD"}, ToolKind::MLeaderTool},
        // --- Edição ---
        {"MOVE",        {"MOVE", "M"},            ToolKind::Move},
        {"COPY",        {"COPY", "CO", "CP"},     ToolKind::Copy},
        {"ROTATE",      {"ROTATE", "RO"},         ToolKind::Rotate},
        {"SCALE",       {"SCALE", "SC"},          ToolKind::Scale},
        {"MIRROR",      {"MIRROR", "MI"},         ToolKind::Mirror},
        {"OFFSET",      {"OFFSET", "O"},          ToolKind::Offset},
        {"TRIM",        {"TRIM", "TR"},           ToolKind::Trim},
        {"EXTEND",      {"EXTEND", "EX"},         ToolKind::Extend},
        {"FILLET",      {"FILLET", "F"},          ToolKind::Fillet},
        {"CHAMFER",     {"CHAMFER", "CHA"},       ToolKind::Chamfer},
        {"STRETCH",     {"STRETCH", "S"},         ToolKind::Stretch},
        {"BREAK",       {"BREAK", "BR"},          ToolKind::BreakTool},
        {"JOIN",        {"JOIN", "J"},            ToolKind::JoinTool},
        {"LENGTHEN",    {"LENGTHEN", "LEN"},      ToolKind::Lengthen},
        {"BLOCK",       {"BLOCK", "B"},           ToolKind::Block},
        {"INSERT",      {"INSERT", "I"},          ToolKind::Insert},
        {"ATTDEF",      {"ATTDEF", "ATT"},        ToolKind::AttDefTool},
        {"DIVIDE",      {"DIVIDE", "DIV"},        ToolKind::Divide},
        {"MEASURE",     {"MEASURE", "ME"},        ToolKind::Measure},
        {"ALIGN",       {"ALIGN", "AL"},          ToolKind::Align},
        {"ARRAYPATH",   {"ARRAYPATH"},            ToolKind::ArrayPath},
        {"MATCHPROP",   {"MATCHPROP", "MA"},      ToolKind::MatchProps},
        {"DIST",        {"DIST", "DI"},           ToolKind::Dist},
        {"INQUIRY",     {"INQUIRY", "LIST", "LI"},ToolKind::Inquiry},
        {"AREA",        {"AREA", "AA"},           ToolKind::Area},
        // --- Cotas ---
        {"DIMLINEAR",   {"DIMLINEAR", "DLI"},     ToolKind::DimLinear},
        {"DIMALIGNED",  {"DIMALIGNED", "DAL"},    ToolKind::DimAligned},
        {"DIMRADIUS",   {"DIMRADIUS", "DRA"},     ToolKind::DimRadius},
        {"DIMDIAMETER", {"DIMDIAMETER", "DDI"},   ToolKind::DimDiameter},
        {"DIMANGULAR",  {"DIMANGULAR", "DAN"},    ToolKind::DimAngular},
        {"DIMCONTINUE", {"DIMCONTINUE", "DCO"},   ToolKind::DimContinue},
        {"DIMBASELINE", {"DIMBASELINE", "DBA"},   ToolKind::DimBaseline},
        {"DIMORDINATE", {"DIMORDINATE", "DIMORD", "DOR"}, ToolKind::DimOrdinate},
    };
    return t;
}

// Resolve um token (nome OU alias, case-insensitive) para ToolKind.
// Retorna true e preenche `out` se reconhecido.
inline bool resolveCommand(const QString& token, ToolKind& out) {
    static const QHash<QString, int> map = [] {
        QHash<QString, int> m;
        const auto& tbl = commandTable();
        for (std::size_t i = 0; i < tbl.size(); ++i)
            for (const QString& a : tbl[i].aliases) m.insert(a.toUpper(), int(i));
        return m;
    }();
    const auto it = map.find(token.toUpper());
    if (it == map.end()) return false;
    out = commandTable()[*it].tool;
    return true;
}

} // namespace cad
