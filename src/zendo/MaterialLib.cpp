// src/zendo/MaterialLib.cpp
// R9: gerador procedural das texturas da biblioteca. Tudo determinístico
// (hash inteiro, sem QRandomGenerator) e PERIÓDICO — o ruído embrulha no
// mesmo período do padrão, então cada 512×512 azuleja sem emenda.
#include "MaterialLib.hpp"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <functional>

namespace matlib {
namespace {

constexpr int S = 512;                    // 512 px = 1 m no mundo

// ---- ruído inteiro determinístico ------------------------------------------
quint32 hash2(int x, int y, quint32 seed) {
    quint32 h = seed + quint32(x) * 0x9E3779B9u;
    h = (h << 13) | (h >> 19);
    h += quint32(y) * 0x85EBCA6Bu;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}
double rnd(int x, int y, quint32 seed) {
    return hash2(x, y, seed) / 4294967295.0;
}
// value noise PERIÓDICO (lattice embrulha em px/py — a chave do seamless)
double vnoise(double x, double y, int px, int py, quint32 seed) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const double fx = x - xi, fy = y - yi;
    auto sm = [](double t) { return t * t * (3.0 - 2.0 * t); };
    auto at = [&](int ix, int iy) {
        return rnd(((ix % px) + px) % px, ((iy % py) + py) % py, seed);
    };
    const double a = at(xi, yi), b = at(xi + 1, yi);
    const double c = at(xi, yi + 1), d = at(xi + 1, yi + 1);
    return a + (b - a) * sm(fx) + (c - a) * sm(fy) +
           (a - b - c + d) * sm(fx) * sm(fy);
}
double fbm(double x, double y, int px, int py, quint32 seed, int oct = 4) {
    double v = 0.0, amp = 0.5;
    for (int o = 0; o < oct; ++o) {
        v += amp * vnoise(x, y, px, py, seed + quint32(o) * 101u);
        x *= 2.0; y *= 2.0; px *= 2; py *= 2; amp *= 0.5;
    }
    return v;                             // ~0..1 (média 0.5)
}

struct RGB { double r, g, b; };
int q8(double v) { return std::clamp(int(v + 0.5), 0, 255); }
void put(QImage& im, int x, int y, RGB c, double f = 1.0) {
    im.setPixel(x, y, qRgb(q8(c.r * f), q8(c.g * f), q8(c.b * f)));
}
RGB mixc(RGB a, RGB b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t};
}

// ---- padrões ---------------------------------------------------------------
// Tijolo em amarração corrida: 8 fiadas, 4 tijolos por fiada, junta de 5 px.
QImage tijolo(RGB base, RGB junta, quint32 seed) {
    QImage im(S, S, QImage::Format_RGB32);
    const int rh = 64, bw = 128, m = 5;
    for (int y = 0; y < S; ++y) {
        const int row = y / rh, yin = y % rh;
        for (int x = 0; x < S; ++x) {
            const int xx = (x + (row % 2) * (bw / 2)) % S;
            const int col = xx / bw, xin = xx % bw;
            if (yin < m || xin < m) {          // argamassa
                const double g = 1.0 + (rnd(x, y, seed + 7) - 0.5) * 0.06;
                put(im, x, y, junta, g);
                continue;
            }
            const double tint =
                1.0 + (rnd(col * 31 + row, row, seed) - 0.5) * 0.26;
            const double grao =
                (fbm(x * 24.0 / S, y * 24.0 / S, 24, 24, seed + 3) - 0.5) *
                0.14;
            // borda do tijolo levemente queimada (relevo barato)
            const int db = std::min({xin - m, bw - 1 - xin, yin - m,
                                     rh - 1 - yin});
            const double edge = db < 6 ? 0.92 + 0.08 * db / 6.0 : 1.0;
            put(im, x, y, base, (tint + grao) * edge);
        }
    }
    return im;
}

// Tábuas com veio: 4 tábuas de 128 px (verticais ou horizontais), gap 3 px.
QImage madeira(RGB clara, RGB escura, quint32 seed, bool vertical) {
    QImage im(S, S, QImage::Format_RGB32);
    const int pw = 128, gap = 3;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const int a = vertical ? x : y;     // eixo transversal às tábuas
            const int b = vertical ? y : x;     // eixo ao longo da tábua
            const int plank = a / pw, ain = a % pw;
            if (ain < gap) { put(im, x, y, {56, 42, 30}); continue; }
            const double tint =
                1.0 + (rnd(plank, 17, seed) - 0.5) * 0.16;
            // veio: ruído esticado ao longo da tábua, afiado em anéis
            const double v = fbm((a + plank * 37) * 20.0 / S, b * 3.0 / S,
                                 20, 3, seed + quint32(plank) * 13u);
            double t = 0.5 + 0.5 * std::sin(v * 18.0 + ain * 0.05);
            t = t * t;
            RGB c = mixc(clara, escura, 0.25 + 0.55 * t);
            put(im, x, y, c, tint);
        }
    }
    return im;
}

// Concreto: manchas fbm + salpico fino + furos de forma (2×2 por metro).
QImage concreto(RGB base, double mancha, quint32 seed, bool furos) {
    QImage im(S, S, QImage::Format_RGB32);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const double v =
                (fbm(x * 6.0 / S, y * 6.0 / S, 6, 6, seed) - 0.5) * mancha;
            const double sal = (rnd(x, y, seed + 9) - 0.5) * 0.05;
            double f = 1.0 + v + sal;
            if (furos)
                for (int cy = 128; cy < S; cy += 256)
                    for (int cx = 128; cx < S; cx += 256) {
                        const double d = std::hypot(x - cx, y - cy);
                        if (d < 9.0) f *= 0.86 + 0.14 * d / 9.0;
                    }
            put(im, x, y, base, f);
        }
    return im;
}

// Alvenaria de pedra: 4 fiadas de 128 px, larguras variadas, junta funda.
QImage pedra(quint32 seed) {
    QImage im(S, S, QImage::Format_RGB32);
    static const int kW[4][4] = {{128, 160, 96, 128},
                                 {160, 128, 128, 96},
                                 {96, 128, 160, 128},
                                 {128, 96, 128, 160}};
    const RGB junta{150, 142, 130}, base{158, 151, 140};
    const int rh = 128, m = 8;
    for (int y = 0; y < S; ++y) {
        const int row = y / rh, yin = y % rh;
        for (int x = 0; x < S; ++x) {
            int xin = x, id = 0, w = kW[row][0];
            while (xin >= w) { xin -= w; id = (id + 1) % 4; w = kW[row][id]; }
            if (yin < m || xin < m) {
                put(im, x, y, junta,
                    0.82 + (rnd(x, y, seed + 5) - 0.5) * 0.08);
                continue;
            }
            const double tint =
                1.0 + (rnd(id * 7 + row * 3, row, seed) - 0.5) * 0.24;
            const double gr =
                (fbm(x * 12.0 / S, y * 12.0 / S, 12, 12,
                     seed + quint32(id) * 29u) - 0.5) * 0.18;
            const int db = std::min({xin - m, w - 1 - xin, yin - m,
                                     rh - 1 - yin});
            const double edge = db < 12 ? 0.84 + 0.16 * db / 12.0 : 1.0;
            put(im, x, y, base, (tint + gr) * edge);
        }
    }
    return im;
}

// Ardósia: 8 fiadas de 64 px sobrepostas, sombra na beira inferior.
QImage ardosia(quint32 seed) {
    QImage im(S, S, QImage::Format_RGB32);
    const RGB base{82, 88, 96};
    const int rh = 64, sw = 128;
    for (int y = 0; y < S; ++y) {
        const int row = y / rh, yin = y % rh;
        const int off = int(hash2(row, 0, seed) % sw);
        for (int x = 0; x < S; ++x) {
            const int xx = (x + off) % S;
            const int col = xx / sw, xin = xx % sw;
            const double tint =
                1.0 + (rnd(col, row, seed + 2) - 0.5) * 0.22;
            const double gr = (fbm(x * 10.0 / S, y * 10.0 / S, 10, 10,
                                   seed + 4) - 0.5) * 0.10;
            double f = tint + gr;
            if (xin < 3) f *= 0.78;                    // junta vertical
            if (yin > rh - 10) f *= 0.62 + 0.38 * (rh - 1 - yin) / 10.0;
            put(im, x, y, base, f);
        }
    }
    return im;
}

// Telha cerâmica capa-e-canal: colunas de 64 px em meia-cana, fiadas 128 px.
QImage telha(quint32 seed) {
    QImage im(S, S, QImage::Format_RGB32);
    const RGB base{171, 98, 66};
    const int cw = 64, rh = 128;
    for (int y = 0; y < S; ++y) {
        const int row = y / rh, yin = y % rh;
        for (int x = 0; x < S; ++x) {
            const int col = x / cw, xin = x % cw;
            const double barrel =
                std::cos((xin / double(cw) - 0.5) * 3.14159265);
            const double tint =
                1.0 + (rnd(col, row, seed) - 0.5) * 0.20;
            double f = (0.70 + 0.34 * barrel) * tint;
            if (yin > rh - 14)                          // beira da fiada
                f *= 0.60 + 0.40 * (rh - 1 - yin) / 14.0;
            else if (yin < 6) f *= 1.06;                // topo pega sol
            put(im, x, y, base, f);
        }
    }
    return im;
}

// Piso de placas: grelha com rejunte, leve variação por placa + veio suave.
QImage piso(RGB placa, RGB rejunte, int tile, double veio, quint32 seed) {
    QImage im(S, S, QImage::Format_RGB32);
    const int m = 4;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            if (x % tile < m || y % tile < m) {
                put(im, x, y, rejunte);
                continue;
            }
            const double tint = 1.0 + (rnd(x / tile, y / tile, seed) - 0.5) *
                                          0.05;
            const double v =
                (fbm(x * 5.0 / S, y * 5.0 / S, 5, 5, seed + 8) - 0.5) * veio;
            put(im, x, y, placa, tint + v);
        }
    return im;
}

// Mármore/porcelanato: veios finos por ruído dobrado (ridged).
QImage marmore(RGB base, RGB veia, double forca, quint32 seed) {
    QImage im(S, S, QImage::Format_RGB32);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const double warp =
                fbm(x * 4.0 / S, y * 4.0 / S, 4, 4, seed + 11);
            const double v = fbm(x * 3.0 / S + warp * 1.6,
                                 y * 3.0 / S, 3, 3, seed);
            double vein = 1.0 - std::abs(2.0 * v - 1.0);
            vein = std::pow(vein, 14.0) * forca;
            const double gr =
                (fbm(x * 9.0 / S, y * 9.0 / S, 9, 9, seed + 6) - 0.5) * 0.05;
            put(im, x, y, mixc(base, veia, std::min(1.0, vein)), 1.0 + gr);
        }
    return im;
}

// Grama: manchas + milhares de "folhas" (risquinhos verticais).
QImage grama(quint32 seed) {
    QImage im(S, S, QImage::Format_RGB32);
    const RGB base{106, 132, 72};
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const double v =
                (fbm(x * 8.0 / S, y * 8.0 / S, 8, 8, seed) - 0.5) * 0.30;
            const double sal = (rnd(x, y, seed + 3) - 0.5) * 0.10;
            put(im, x, y, base, 1.0 + v + sal);
        }
    for (int k = 0; k < 9000; ++k) {                    // folhas
        const int x = int(hash2(k, 1, seed) % S);
        const int y0 = int(hash2(k, 2, seed) % S);
        const double f = 0.72 + rnd(k, 3, seed) * 0.62;
        for (int d = 0; d < 4; ++d) {
            const int y = (y0 + d) % S;
            const QRgb p = im.pixel(x, y);
            im.setPixel(x, y, qRgb(q8(qRed(p) * f), q8(qGreen(p) * f),
                                   q8(qBlue(p) * f)));
        }
    }
    return im;
}

// Areia: grão fino + pontinhos escuros esparsos.
QImage areia(quint32 seed) {
    QImage im(S, S, QImage::Format_RGB32);
    const RGB base{209, 191, 154};
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const double v =
                (fbm(x * 10.0 / S, y * 10.0 / S, 10, 10, seed) - 0.5) * 0.10;
            const double g = (rnd(x, y, seed + 1) - 0.5) * 0.09;
            double f = 1.0 + v + g;
            if (hash2(x, y, seed + 12) % 997 == 0) f *= 0.72;
            put(im, x, y, base, f);
        }
    return im;
}

// Aço escovado: estrias horizontais contínuas.
QImage aco(quint32 seed) {
    QImage im(S, S, QImage::Format_RGB32);
    const RGB base{176, 180, 184};
    for (int y = 0; y < S; ++y) {
        const double linha =
            (fbm(0.0, y * 90.0 / S, 3, 90, seed) - 0.5) * 0.18;
        for (int x = 0; x < S; ++x) {
            const double v =
                (fbm(x * 3.0 / S, y * 90.0 / S, 3, 90, seed + 5) - 0.5) *
                0.10;
            put(im, x, y, base, 1.0 + linha + v);
        }
    }
    return im;
}

// Reboco: grão fino e quente.
QImage reboco(quint32 seed) {
    QImage im(S, S, QImage::Format_RGB32);
    const RGB base{231, 224, 210};
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const double v =
                (fbm(x * 14.0 / S, y * 14.0 / S, 14, 14, seed) - 0.5) * 0.07;
            const double g = (rnd(x, y, seed + 2) - 0.5) * 0.04;
            put(im, x, y, base, 1.0 + v + g);
        }
    return im;
}

} // namespace

QVector<QPair<QString, QString>> ensureLibrary() {
    struct Def {
        const char* nome;
        std::function<QImage()> gen;
    };
    const QVector<Def> defs = {
        {"Tijolo aparente",
         [] { return tijolo({152, 76, 58}, {186, 176, 162}, 100); }},
        {"Tijolo branco",
         [] { return tijolo({224, 217, 206}, {201, 196, 187}, 200); }},
        {"Madeira clara",
         [] { return madeira({201, 172, 128}, {148, 116, 78}, 300, true); }},
        {"Madeira escura",
         [] { return madeira({116, 82, 54}, {66, 44, 30}, 400, true); }},
        {"Deck",
         [] { return madeira({164, 128, 88}, {110, 80, 52}, 500, false); }},
        {"Concreto liso",
         [] { return concreto({178, 176, 170}, 0.16, 600, true); }},
        {"Concreto queimado",
         [] { return concreto({141, 139, 134}, 0.30, 700, false); }},
        {"Reboco", [] { return reboco(800); }},
        {"Pedra", [] { return pedra(900); }},
        {"Ardósia", [] { return ardosia(1000); }},
        {"Telha cerâmica", [] { return telha(1100); }},
        {"Piso branco",
         [] { return piso({241, 239, 233}, {199, 195, 187}, 256, 0.03,
                          1200); }},
        {"Porcelanato",
         [] { return marmore({190, 190, 188}, {148, 148, 148}, 0.9, 1300); }},
        {"Mármore",
         [] { return marmore({240, 238, 233}, {168, 168, 172}, 1.4, 1400); }},
        {"Grama", [] { return grama(1500); }},
        {"Areia", [] { return areia(1600); }},
        {"Aço escovado", [] { return aco(1700); }},
    };

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/materiais");
    QDir().mkpath(dir + QStringLiteral("/thumbs"));
    QVector<QPair<QString, QString>> out;
    for (const Def& d : defs) {
        const QString nome = QString::fromUtf8(d.nome);
        const QString arq = dir + QStringLiteral("/") + nome +
                            QStringLiteral(".png");
        const QString thumb = dir + QStringLiteral("/thumbs/") + nome +
                              QStringLiteral(".png");
        if (!QFile::exists(arq)) {
            const QImage img = d.gen();
            img.save(arq);
            img.scaled(64, 64).save(thumb);    // R11: paleta preguiçosa
        } else if (!QFile::exists(thumb)) {
            QImage(arq).scaled(64, 64).save(thumb);
        }
        out.append({nome, arq});
    }
    return out;
}

} // namespace matlib
