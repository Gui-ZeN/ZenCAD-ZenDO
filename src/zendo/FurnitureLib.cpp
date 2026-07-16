// src/zendo/FurnitureLib.cpp
// R14: os móveis do Zendo — caixas axis-aligned compostas, cores da paleta
// Sumi & Washi. Cada caixa nasce com 8 vértices próprios (cascas desconexas
// na mesma malha: manifold por casca, checkIntegrity aprova).
#include "FurnitureLib.hpp"

#include <cmath>

namespace moblib {
namespace {

constexpr double kPi = 3.14159265358979323846;

using cad::HalfEdgeMesh;
using Idx = HalfEdgeMesh::Idx;
using Cor = std::array<float, 3>;

// paleta (mesma família das Tintas do app)
constexpr Cor kMadeira{0.478f, 0.329f, 0.208f};
constexpr Cor kNogueira{0.376f, 0.259f, 0.173f};
constexpr Cor kPinho{0.769f, 0.659f, 0.486f};
constexpr Cor kSumi{0.180f, 0.165f, 0.141f};
constexpr Cor kGrafite{0.298f, 0.298f, 0.298f};
constexpr Cor kWashi{0.918f, 0.894f, 0.839f};
constexpr Cor kLinho{0.886f, 0.851f, 0.769f};
constexpr Cor kSalvia{0.439f, 0.561f, 0.498f};
constexpr Cor kMusgo{0.333f, 0.416f, 0.278f};
constexpr Cor kLatao{0.761f, 0.627f, 0.388f};
constexpr Cor kTerracota{0.631f, 0.400f, 0.310f};
constexpr Cor kVidro{0.639f, 0.741f, 0.800f};
constexpr Cor kCal{0.961f, 0.949f, 0.922f};

struct B {                                  // bancada de montagem de um móvel
    Movel mv;
    explicit B(const char* nome) { mv.nome = QString::fromUtf8(nome); }
    // caixa [x0..x1]×[y0..y1]×[z0..z1], faces CCW vistas de FORA
    void cx(double x0, double y0, double z0, double x1, double y1, double z1,
            const Cor& c) {
        HalfEdgeMesh& m = mv.mesh;
        const Idx a = m.addVertex({x0, y0, z0}), b = m.addVertex({x1, y0, z0});
        const Idx cc = m.addVertex({x1, y1, z0}), d = m.addVertex({x0, y1, z0});
        const Idx e = m.addVertex({x0, y0, z1}), f = m.addVertex({x1, y0, z1});
        const Idx g = m.addVertex({x1, y1, z1}), h = m.addVertex({x0, y1, z1});
        const std::vector<Idx> loops[6] = {
            {d, cc, b, a},                   // baixo  (-z)
            {e, f, g, h},                    // topo   (+z)
            {a, b, f, e},                    // frente (-y)
            {cc, d, h, g},                   // trás   (+y)
            {b, cc, g, f},                   // direita(+x)
            {d, a, e, h},                    // esquerda(-x)
        };
        for (const auto& loop : loops) mv.cores[m.addFace(loop)] = c;
    }
    // 4 pernas quadradas nos cantos de [x0..x1]×[y0..y1], seção s, altura h
    void pernas(double x0, double y0, double x1, double y1, double s,
                double h, const Cor& c) {
        cx(x0, y0, 0, x0 + s, y0 + s, h, c);
        cx(x1 - s, y0, 0, x1, y0 + s, h, c);
        cx(x0, y1 - s, 0, x0 + s, y1, h, c);
        cx(x1 - s, y1 - s, 0, x1, y1, h, c);
    }
};

Movel porta() {
    B b("Porta");
    const double L = 0.90, E = 0.14, H = 2.14;   // batente por fora
    b.cx(0, 0, 0, 0.05, E, H, kWashi);            // ombreira esq
    b.cx(L - 0.05, 0, 0, L, E, H, kWashi);        // ombreira dir
    b.cx(0.05, 0, H - 0.05, L - 0.05, E, H, kWashi);   // verga
    b.cx(0.05, 0.05, 0, L - 0.05, 0.09, H - 0.05, kMadeira);  // folha
    b.cx(L - 0.14, 0.02, 1.00, L - 0.08, 0.05, 1.06, kLatao); // maçaneta
    return b.mv;
}

Movel janela() {
    B b("Janela");
    const double L = 1.20, E = 0.10, H = 1.20;
    b.cx(0, 0, 0, 0.05, E, H, kWashi);
    b.cx(L - 0.05, 0, 0, L, E, H, kWashi);
    b.cx(0.05, 0, 0, L - 0.05, E, 0.05, kWashi);
    b.cx(0.05, 0, H - 0.05, L - 0.05, E, H, kWashi);
    b.cx(L / 2 - 0.02, 0, 0.05, L / 2 + 0.02, E, H - 0.05, kWashi);
    b.cx(0.05, 0.04, 0.05, L - 0.05, 0.06, H - 0.05, kVidro);  // vidro
    return b.mv;
}

Movel mesa() {
    B b("Mesa de jantar");
    const double L = 1.60, P = 0.90, H = 0.75;
    b.cx(0, 0, H - 0.04, L, P, H, kMadeira);              // tampo
    b.pernas(0.05, 0.05, L - 0.05, P - 0.05, 0.06, H - 0.04, kSumi);
    return b.mv;
}

Movel cadeira() {
    B b("Cadeira");
    const double L = 0.44, P = 0.44;
    b.cx(0, 0, 0.42, L, P, 0.46, kMadeira);               // assento
    b.pernas(0.02, 0.02, L - 0.02, P - 0.02, 0.04, 0.42, kSumi);
    b.cx(0, P - 0.04, 0.46, L, P, 0.88, kMadeira);        // encosto
    return b.mv;
}

Movel sofa() {
    B b("Sofá 3 lugares");
    const double L = 2.10, P = 0.90;
    b.cx(0, 0, 0.06, L, P, 0.30, kGrafite);               // base
    b.pernas(0.03, 0.03, L - 0.03, P - 0.03, 0.05, 0.06, kSumi);
    b.cx(0.16, 0, 0.30, 0.16 + 0.59, P - 0.14, 0.44, kLinho);  // almofadas
    b.cx(0.16 + 0.605, 0, 0.30, 0.16 + 1.195, P - 0.14, 0.44, kLinho);
    b.cx(0.16 + 1.21, 0, 0.30, L - 0.16, P - 0.14, 0.44, kLinho);
    b.cx(0.16, P - 0.20, 0.30, L - 0.16, P, 0.80, kGrafite);   // encosto
    b.cx(0, 0, 0.30, 0.16, P, 0.62, kGrafite);            // braço esq
    b.cx(L - 0.16, 0, 0.30, L, P, 0.62, kGrafite);        // braço dir
    return b.mv;
}

Movel poltrona() {
    B b("Poltrona");
    const double L = 0.85, P = 0.85;
    b.cx(0, 0, 0.06, L, P, 0.28, kSumi);
    b.pernas(0.03, 0.03, L - 0.03, P - 0.03, 0.05, 0.06, kSumi);
    b.cx(0.14, 0, 0.28, L - 0.14, P - 0.14, 0.42, kSalvia);
    b.cx(0.14, P - 0.18, 0.28, L - 0.14, P, 0.78, kSalvia);
    b.cx(0, 0, 0.28, 0.14, P, 0.58, kSumi);
    b.cx(L - 0.14, 0, 0.28, L, P, 0.58, kSumi);
    return b.mv;
}

Movel cama() {
    B b("Cama de casal");
    const double L = 1.60, C = 2.00;
    b.cx(0, 0, 0.08, L, C, 0.30, kNogueira);              // estrado
    b.pernas(0.02, 0.02, L - 0.02, C - 0.02, 0.06, 0.08, kSumi);
    b.cx(0.02, 0.02, 0.30, L - 0.02, C - 0.02, 0.50, kWashi);  // colchão
    b.cx(0, C - 0.06, 0.08, L, C, 1.05, kNogueira);       // cabeceira
    b.cx(0.12, C - 0.50, 0.50, 0.74, C - 0.14, 0.61, kCal);   // travesseiros
    b.cx(0.86, C - 0.50, 0.50, 1.48, C - 0.14, 0.61, kCal);
    b.cx(0.02, 0.02, 0.44, L - 0.02, C * 0.55, 0.53, kSalvia);  // manta
    return b.mv;
}

Movel mesinha() {
    B b("Mesa de centro");
    const double L = 1.00, P = 0.60, H = 0.40;
    b.cx(0, 0, H - 0.04, L, P, H, kPinho);
    b.pernas(0.04, 0.04, L - 0.04, P - 0.04, 0.05, H - 0.04, kSumi);
    b.cx(0.06, 0.06, 0.12, L - 0.06, P - 0.06, 0.16, kPinho);  // prateleira
    return b.mv;
}

Movel estante() {
    B b("Estante");
    const double L = 0.90, P = 0.35, H = 1.80;
    b.cx(0, 0, 0, 0.02, P, H, kNogueira);                 // lateral esq
    b.cx(L - 0.02, 0, 0, L, P, H, kNogueira);             // lateral dir
    b.cx(0.02, P - 0.02, 0, L - 0.02, P, H, kNogueira);   // fundo
    for (int i = 0; i < 5; ++i) {                         // prateleiras
        const double z = 0.02 + i * (H - 0.06) / 4.0;
        b.cx(0.02, 0, z, L - 0.02, P - 0.02, z + 0.025, kNogueira);
    }
    return b.mv;
}

Movel luminaria() {
    B b("Luminária de chão");
    b.cx(-0.15, -0.15, 0, 0.15, 0.15, 0.03, kSumi);       // base
    b.cx(-0.015, -0.015, 0.03, 0.015, 0.015, 1.40, kSumi);  // haste
    b.cx(-0.18, -0.18, 1.40, 0.18, 0.18, 1.66, kLinho);   // cúpula
    return b.mv;
}

Movel vaso() {
    B b("Vaso com planta");
    b.cx(-0.14, -0.14, 0, 0.14, 0.14, 0.32, kTerracota);  // vaso
    b.cx(-0.11, -0.11, 0.32, 0.11, 0.11, 0.34, kSumi);    // terra
    b.cx(-0.03, -0.03, 0.34, 0.03, 0.03, 0.95, kMusgo);   // caule
    b.cx(-0.22, -0.05, 0.62, 0.02, 0.05, 0.72, kSalvia);  // folhas
    b.cx(-0.02, -0.06, 0.78, 0.24, 0.04, 0.88, kSalvia);
    b.cx(-0.09, -0.20, 0.70, 0.01, 0.02, 0.80, kMusgo);
    b.cx(-0.05, -0.02, 0.88, 0.05, 0.20, 0.98, kMusgo);
    return b.mv;
}

Movel tapete() {
    B b("Tapete");
    b.cx(0, 0, 0, 2.00, 1.40, 0.015, kLinho);
    b.cx(0.10, 0.10, 0.015, 1.90, 1.30, 0.02, kSalvia);
    return b.mv;
}

// R44: ÁRVORE orgânica low-poly — tronco hexagonal afunilado + copas em
// BLOB irregular (anéis com jitter determinístico), cascas fechadas.
// Substitui de vez a estética de árvore-caixote das entregas.
Movel arvore(const char* nome, double s, unsigned seed) {
    B b(nome);
    HalfEdgeMesh& m = b.mv.mesh;
    auto frand = [&seed]() {
        seed = seed * 1103515245u + 12345u;
        return double((seed >> 16) & 0x7fff) / 32767.0;
    };
    const Cor tronco{0.361f, 0.251f, 0.165f};
    const auto anel = [&](double cx, double cy, double z, double r,
                          double a0, bool jit) {
        std::array<Idx, 6> v{};
        for (int k = 0; k < 6; ++k) {
            const double a = a0 + k * kPi / 3.0;
            const double rr = jit ? r * (0.80 + 0.38 * frand()) : r;
            v[std::size_t(k)] =
                m.addVertex({cx + rr * std::cos(a), cy + rr * std::sin(a), z});
        }
        return v;
    };
    // tronco: frustum hexagonal fechado (12 v, 8 f)
    {
        const auto lo = anel(0, 0, 0.0, 0.15 * s, 0.26, false);
        const auto hi = anel(0, 0, 1.55 * s, 0.10 * s, 0.26, false);
        std::vector<Idx> base(lo.rbegin(), lo.rend());
        b.mv.cores[m.addFace(base)] = tronco;
        b.mv.cores[m.addFace({hi.begin(), hi.end()})] = tronco;
        for (int k = 0; k < 6; ++k) {
            const int k2 = (k + 1) % 6;
            b.mv.cores[m.addFace({lo[std::size_t(k)], lo[std::size_t(k2)],
                                  hi[std::size_t(k2)], hi[std::size_t(k)]})] =
                tronco;
        }
    }
    // copa: blob = polo + 2 anéis com jitter + polo (14 v, 18 f por blob)
    const auto blob = [&](double cx, double cy, double cz, double r,
                          const Cor& c) {
        const double a0 = frand() * kPi;
        const Idx polo0 = m.addVertex({cx, cy, cz - 0.92 * r});
        const auto r1 = anel(cx, cy, cz - 0.36 * r, 0.94 * r, a0, true);
        const auto r2 = anel(cx, cy, cz + 0.38 * r, 0.86 * r, a0 + 0.5, true);
        const Idx poloT = m.addVertex({cx, cy, cz + 0.90 * r});
        for (int k = 0; k < 6; ++k) {
            const int k2 = (k + 1) % 6;
            b.mv.cores[m.addFace({polo0, r1[std::size_t(k2)],
                                  r1[std::size_t(k)]})] = c;
            b.mv.cores[m.addFace({r1[std::size_t(k)], r1[std::size_t(k2)],
                                  r2[std::size_t(k2)], r2[std::size_t(k)]})] =
                c;
            b.mv.cores[m.addFace({poloT, r2[std::size_t(k)],
                                  r2[std::size_t(k2)]})] = c;
        }
    };
    blob(0.0, 0.0, 2.15 * s, 1.00 * s, {0.333f, 0.416f, 0.278f});
    blob(0.48 * s, 0.30 * s, 2.72 * s, 0.60 * s, {0.408f, 0.502f, 0.322f});
    blob(-0.52 * s, -0.26 * s, 2.55 * s, 0.54 * s, {0.290f, 0.447f, 0.298f});
    return b.mv;
}

} // namespace

QVector<Movel> gerarTodos() {
    return {porta(),   janela(),   sofa(),     poltrona(),
            mesa(),    cadeira(),  mesinha(),  cama(),
            estante(), luminaria(), vaso(),    tapete(),
            arvore("Árvore", 1.0, 7u),
            arvore("Árvore alta", 1.4, 23u)};
}

} // namespace moblib
