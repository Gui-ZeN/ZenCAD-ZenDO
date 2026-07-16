// src/core/geometry/LinePattern.hpp
#pragma once
#include "core/math/Vec.hpp"
#include <string>
#include <vector>

namespace cad {

// Estilos de linha suportados (tipos de traço usados em desenho técnico).
enum class LineStyle {
    Continuous, // linha contínua (sólida)
    Dashed,     // tracejada
    Center,     // linha de centro (longo-curto-longo...)
    Hidden      // linha oculta (tracejado curto)
};

// Converte um nome textual em LineStyle (case-insensitive).
// Mapeamento:
//   "CONTINUOUS"            -> Continuous
//   "DASHED" / "TRACEJADO"  -> Dashed
//   "CENTER" / "CENTRO"     -> Center
//   "HIDDEN" / "OCULTA"     -> Hidden
// Qualquer outro valor (inclusive desconhecido/vazio) -> Continuous.
LineStyle lineStyleFromName(const std::string& name);

// Aplica um padrão de linha a um conjunto de segmentos.
//
// Entrada: vetor de PARES de pontos — cada par consecutivo (i, i+1) é um
// segmento. (Mesma convenção de RenderBatch::lineVertices.)
//
// Saída: novo vetor de pares de pontos representando apenas os TRAÇOS visíveis
// (as lacunas são omitidas). Cada par de saída também é um segmento.
//
// Comportamento por estilo:
//   - Continuous: devolve `segments` inalterado.
//   - Dashed/Center/Hidden: cada segmento é percorrido do início ao fim,
//     acumulando comprimento e alternando traço/lacuna conforme o padrão base
//     (multiplicado por `scale`). A fase do padrão reinicia a cada segmento.
//
// Padrões base (unidades de desenho, antes de `scale`):
//   - Dashed:  traço 4, lacuna 2.
//   - Hidden:  traço 2, lacuna 1.
//   - Center:  traço-longo 12, lacuna 2, traço-curto 2, lacuna 2.
//
// `scale` <= 0 é tratado como 1. Segmentos de comprimento zero são ignorados.
std::vector<Point3> applyLinePattern(const std::vector<Point3>& segments,
                                     LineStyle style,
                                     double scale);

// Aplica um padrão de traços ARBITRÁRIO (índices pares = traço, ímpares = lacuna),
// permitindo tipos de linha definidos pelo usuário. Vazio = contínuo.
std::vector<Point3> applyLinePattern(const std::vector<Point3>& segments,
                                     const std::vector<double>& pattern,
                                     double scale);

// --- Registro de tipos de linha CUSTOM (definidos pelo usuário) -------------
// Registra/atualiza um tipo de linha nomeado com seu padrão de traços.
void registerLineType(const std::string& name, const std::vector<double>& pattern);
// Registra a família PADRÃO de fábrica (DOT/DASHDOT/DIVIDE/BORDER/PHANTOM/
// CENTER2/HIDDEN2/DASHEDX2) — chamar uma vez no startup do app/ferramenta.
void registerStandardLineTypes();
// Nomes dos tipos custom registrados (para popular combos da UI).
std::vector<std::string> customLineTypeNames();
// Padrão de um nome custom, ou nullptr se não registrado.
const std::vector<double>* customLinePattern(const std::string& name);

// Resolve um nome (custom OU embutido) e aplica aos segmentos. Custom tem
// prioridade; senão cai em lineStyleFromName.
std::vector<Point3> applyLineTypeByName(const std::vector<Point3>& segments,
                                        const std::string& name, double scale);

} // namespace cad
