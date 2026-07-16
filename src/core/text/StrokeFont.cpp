// src/core/text/StrokeFont.cpp
#include "core/text/StrokeFont.hpp"

#include <cmath>

namespace cad {
namespace {

// ---------------------------------------------------------------------------
// Fonte single-stroke monoespaçada (estilo AutoCAD "txt" / Hershey simples).
//
// Cada glifo é desenhado numa GRADE NORMALIZADA de 4 (largura) x 6 (altura):
//   x ∈ [0..4], y ∈ [0..6], origem no canto inferior-esquerdo da célula.
//
// Um glifo é uma lista de POLILINHAS; cada polilinha é uma sequência de pontos
// (gx, gy) na grade. Para uma polilinha de N pontos emitimos N-1 segmentos
// (pares de pontos consecutivos no vetor de saída).
//
// Avanço por caractere = 6 unidades de grade (4 de largura + 2 de espaço).
// Escala usada na conversão p/ mundo: escala = height / 6, de modo que a
// célula de 6 de altura vira exatamente `height` no mundo.
// ---------------------------------------------------------------------------

// Ponto na grade normalizada (2D; z é sempre 0).
struct GridPt {
    double x;
    double y;
};

using Polyline = std::vector<GridPt>;
using Glyph    = std::vector<Polyline>;

// Constrói a tabela de glifos. Cada entrada associa um caractere ao seu
// conjunto de polilinhas na grade 4x6. Caracteres ausentes => glifo vazio
// (desenha nada, equivalente a espaço em branco).
//
// A tabela é montada UMA vez e mantida estática (função local com static).
const Glyph* glyphFor(char c);

// Tabela construída sob demanda (lazy) na 1ª chamada de glyphFor.
struct GlyphTable {
    Glyph table[256]; // indexado pelo byte (0..255) — inclui Latin-1 ° (0xB0) e Ø (0xD8)

    GlyphTable() { build(); }

    // Atalho de escrita: define o glifo de `ch` a partir de polilinhas.
    void set(char ch, std::initializer_list<Polyline> polys) {
        const unsigned idx = static_cast<unsigned char>(ch);
        if (idx < 256) {
            table[idx] = Glyph(polys);
        }
    }

    void build();
};

void GlyphTable::build() {
    // -------------------------------------------------------------------
    // LETRAS A–Z (maiúsculas). Coordenadas na grade 4x6.
    // -------------------------------------------------------------------
    set('A', {{{0, 0}, {2, 6}, {4, 0}}, {{0.6, 1.8}, {3.4, 1.8}}});
    set('B', {{{0, 0}, {0, 6}, {3, 6}, {4, 5}, {3, 3}, {0, 3}},
              {{3, 3}, {4, 1}, {3, 0}, {0, 0}}});
    set('C', {{{4, 5}, {3, 6}, {1, 6}, {0, 5}, {0, 1}, {1, 0}, {3, 0}, {4, 1}}});
    set('D', {{{0, 0}, {0, 6}, {3, 6}, {4, 5}, {4, 1}, {3, 0}, {0, 0}}});
    set('E', {{{4, 6}, {0, 6}, {0, 0}, {4, 0}}, {{0, 3}, {3, 3}}});
    set('F', {{{4, 6}, {0, 6}, {0, 0}}, {{0, 3}, {3, 3}}});
    set('G', {{{4, 5}, {3, 6}, {1, 6}, {0, 5}, {0, 1}, {1, 0}, {3, 0}, {4, 1},
               {4, 3}, {2, 3}}});
    set('H', {{{0, 0}, {0, 6}}, {{4, 0}, {4, 6}}, {{0, 3}, {4, 3}}});
    set('I', {{{1, 0}, {3, 0}}, {{2, 0}, {2, 6}}, {{1, 6}, {3, 6}}});
    set('J', {{{3, 6}, {3, 1}, {2, 0}, {1, 0}, {0, 1}}});
    set('K', {{{0, 0}, {0, 6}}, {{4, 6}, {0, 3}, {4, 0}}});
    set('L', {{{0, 6}, {0, 0}, {4, 0}}});
    set('M', {{{0, 0}, {0, 6}, {2, 3}, {4, 6}, {4, 0}}});
    set('N', {{{0, 0}, {0, 6}, {4, 0}, {4, 6}}});
    set('O', {{{1, 0}, {0, 1}, {0, 5}, {1, 6}, {3, 6}, {4, 5}, {4, 1}, {3, 0},
               {1, 0}}});
    set('P', {{{0, 0}, {0, 6}, {3, 6}, {4, 5}, {4, 4}, {3, 3}, {0, 3}}});
    set('Q', {{{1, 0}, {0, 1}, {0, 5}, {1, 6}, {3, 6}, {4, 5}, {4, 1}, {3, 0},
               {1, 0}},
              {{2.5, 1.5}, {4, 0}}});
    set('R', {{{0, 0}, {0, 6}, {3, 6}, {4, 5}, {4, 4}, {3, 3}, {0, 3}},
              {{2, 3}, {4, 0}}});
    set('S', {{{4, 5}, {3, 6}, {1, 6}, {0, 5}, {0, 4}, {1, 3}, {3, 3}, {4, 2},
               {4, 1}, {3, 0}, {1, 0}, {0, 1}}});
    set('T', {{{0, 6}, {4, 6}}, {{2, 6}, {2, 0}}});
    set('U', {{{0, 6}, {0, 1}, {1, 0}, {3, 0}, {4, 1}, {4, 6}}});
    set('V', {{{0, 6}, {2, 0}, {4, 6}}});
    set('W', {{{0, 6}, {1, 0}, {2, 3}, {3, 0}, {4, 6}}});
    set('X', {{{0, 0}, {4, 6}}, {{0, 6}, {4, 0}}});
    set('Y', {{{0, 6}, {2, 3}, {4, 6}}, {{2, 3}, {2, 0}}});
    set('Z', {{{0, 6}, {4, 6}, {0, 0}, {4, 0}}});

    // -------------------------------------------------------------------
    // DÍGITOS 0–9.
    // -------------------------------------------------------------------
    set('0', {{{1, 0}, {0, 1}, {0, 5}, {1, 6}, {3, 6}, {4, 5}, {4, 1}, {3, 0},
               {1, 0}},
              {{0, 1}, {4, 5}}});
    set('1', {{{1, 4}, {2, 6}, {2, 0}}, {{1, 0}, {3, 0}}});
    set('2', {{{0, 5}, {1, 6}, {3, 6}, {4, 5}, {4, 4}, {0, 0}, {4, 0}}});
    set('3', {{{0, 6}, {4, 6}, {2, 3}, {4, 2}, {4, 1}, {3, 0}, {1, 0}, {0, 1}}});
    set('4', {{{3, 0}, {3, 6}, {0, 2}, {4, 2}}});
    set('5', {{{4, 6}, {0, 6}, {0, 3}, {3, 3}, {4, 2}, {4, 1}, {3, 0}, {1, 0},
               {0, 1}}});
    set('6', {{{4, 5}, {3, 6}, {1, 6}, {0, 5}, {0, 1}, {1, 0}, {3, 0}, {4, 1},
               {4, 2}, {3, 3}, {0, 3}}});
    set('7', {{{0, 6}, {4, 6}, {1, 0}}});
    set('8', {{{1, 3}, {0, 4}, {0, 5}, {1, 6}, {3, 6}, {4, 5}, {4, 4}, {3, 3},
               {1, 3}, {0, 2}, {0, 1}, {1, 0}, {3, 0}, {4, 1}, {4, 2}, {3, 3}}});
    set('9', {{{0, 1}, {1, 0}, {3, 0}, {4, 1}, {4, 5}, {3, 6}, {1, 6}, {0, 5},
               {0, 4}, {1, 3}, {4, 3}}});

    // -------------------------------------------------------------------
    // PONTUAÇÃO E SÍMBOLOS.
    // -------------------------------------------------------------------
    // Espaço: glifo vazio (não definir => fica vazio).

    // Ponto: pequeno tracinho na base.
    set('.', {{{1.8, 0}, {2.2, 0}, {2.2, 0.4}, {1.8, 0.4}, {1.8, 0}}});
    // Vírgula: ponto na base com "rabinho" descendo (aproximado dentro da célula).
    set(',', {{{2.2, 0.6}, {1.8, 0.6}, {1.8, 1}, {2.2, 1}, {2.2, 0.6},
               {1.6, 0}}});
    // Hífen: traço horizontal no meio.
    set('-', {{{1, 3}, {3, 3}}});
    // Mais: cruz centrada.
    set('+', {{{2, 1.5}, {2, 4.5}}, {{0.5, 3}, {3.5, 3}}});
    // Barra: diagonal.
    set('/', {{{0, 0}, {4, 6}}});
    // Dois-pontos: dois tracinhos verticais.
    set(':', {{{2, 1.5}, {2, 2}}, {{2, 4}, {2, 4.5}}});
    // Porcentagem: barra com dois "zerinhos".
    set('%', {{{0, 0}, {4, 6}},
              {{0.5, 4.5}, {1.5, 4.5}, {1.5, 5.5}, {0.5, 5.5}, {0.5, 4.5}},
              {{2.5, 0.5}, {3.5, 0.5}, {3.5, 1.5}, {2.5, 1.5}, {2.5, 0.5}}});
    // Menor que.
    set('<', {{{4, 5}, {0, 3}, {4, 1}}});
    // Maior que.
    set('>', {{{0, 5}, {4, 3}, {0, 1}}});

    // Grau (°): pequeno círculo aproximado por segmentos no topo direito.
    set('\xB0', {{{2.5, 5}, {2, 5.3}, {2, 5.7}, {2.5, 6}, {3, 6}, {3.5, 5.7},
                  {3.5, 5.3}, {3, 5}, {2.5, 5}}});

    // Diâmetro (Ø): "O" cortado por uma barra diagonal.
    set('\xD8', {{{1, 0}, {0, 1}, {0, 5}, {1, 6}, {3, 6}, {4, 5}, {4, 1},
                  {3, 0}, {1, 0}},
                 {{0, 0}, {4, 6}}});
}

const Glyph* glyphFor(char c) {
    static const GlyphTable kTable; // construída uma única vez

    // Minúsculas a–z mapeiam para as maiúsculas correspondentes (aceitável).
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }

    const unsigned idx = static_cast<unsigned char>(c);
    if (idx >= 256) {
        return nullptr; // fora da tabela => branco
    }
    return &kTable.table[idx];
}

} // namespace

std::vector<Point3> strokeText(const std::string& text, const Point3& pos,
                               double height, double rotation) {
    std::vector<Point3> out;

    // Escala da grade (altura 6) para o mundo (altura = `height`).
    const double escala  = height / 6.0;
    const double advance = 6.0 * escala; // 4 de largura + 2 de espaço

    // Rotação CCW em torno da origem local (0,0) antes de transladar por `pos`.
    const double cs = std::cos(rotation);
    const double sn = std::sin(rotation);

    for (std::size_t i = 0; i < text.size(); ++i) {
        const Glyph* glyph = glyphFor(text[i]);
        if (!glyph || glyph->empty()) {
            continue; // espaço / desconhecido: só avança a célula
        }

        const double baseX = static_cast<double>(i) * advance;

        for (const Polyline& poly : *glyph) {
            // Polilinha de N pontos => N-1 segmentos (pares consecutivos).
            for (std::size_t k = 0; k + 1 < poly.size(); ++k) {
                const GridPt& a = poly[k];
                const GridPt& b = poly[k + 1];

                // Para cada ponto: local -> rotação -> translação por `pos`.
                for (const GridPt& g : {a, b}) {
                    const double lx = baseX + g.x * escala;
                    const double ly = g.y * escala;
                    const double rx = lx * cs - ly * sn;
                    const double ry = lx * sn + ly * cs;
                    out.emplace_back(pos.x + rx, pos.y + ry, pos.z);
                }
            }
        }
    }

    return out;
}

double strokeTextWidth(const std::string& text, double height) {
    if (text.empty()) {
        return 0.0;
    }
    const double escala  = height / 6.0;
    const double advance = 6.0 * escala; // mesmo `advance` de strokeText
    return static_cast<double>(text.size()) * advance;
}

} // namespace cad
