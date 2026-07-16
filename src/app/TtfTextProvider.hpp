// src/app/TtfTextProvider.hpp
// Provider Qt para a fonte TTF do kernel (core/text/TtfFont.hpp): converte uma
// linha de texto nos CONTORNOS dos glifos via QPainterPath e devolve segmentos
// (pares de Point3) já posicionados/escalados/rotacionados no mundo. A escala
// é ancorada na ALTURA DE MAIÚSCULA (capHeight), casando com a StrokeFont.
#pragma once
#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QPolygonF>
#include <QString>
#include <cmath>

#include "core/text/TtfFont.hpp"

namespace cad {

inline void installQtTtfProvider() {
    // Tesselação em pixel-size alto (256) e reescala: contornos suaves em
    // qualquer zoom sem depender do tamanho do device.
    constexpr int kPx = 256;

    auto capHeightOf = [](const QFont& f) {
        const QFontMetricsF fm(f);
        const double cap = fm.capHeight();
        return cap > 0.0 ? cap : fm.ascent();   // fontes sem capHeight: ascent
    };

    ttfTessellator() = [capHeightOf](const TtfLine& L) {
        std::vector<Point3> out;
        QFont f(QString::fromStdString(L.font));
        f.setPixelSize(kPx);
        f.setBold(L.bold);
        f.setItalic(L.italic);
        const double s = L.height / capHeightOf(f);
        QPainterPath path;
        path.addText(0.0, 0.0, f, QString::fromUtf8(L.text.c_str()));
        const double cs = std::cos(L.rotRad), sn = std::sin(L.rotRad);
        auto map = [&](const QPointF& p) {
            const double lx = p.x() * s;
            const double ly = -p.y() * s;    // Y de tela (baixo) -> Y de mundo (cima)
            return Point3{L.pos.x + lx * cs - ly * sn,
                          L.pos.y + lx * sn + ly * cs, L.pos.z};
        };
        for (const QPolygonF& poly : path.toSubpathPolygons()) {
            const int n = poly.size();
            if (n < 2) continue;
            for (int i = 0; i < n; ++i) {          // fecha o contorno ((i+1)%n)
                out.push_back(map(poly[i]));
                out.push_back(map(poly[(i + 1) % n]));
            }
        }
        return out;
    };

    ttfMeasurer() = [capHeightOf](const TtfLine& L) {
        QFont f(QString::fromStdString(L.font));
        f.setPixelSize(kPx);
        f.setBold(L.bold);
        f.setItalic(L.italic);
        const QFontMetricsF fm(f);
        return fm.horizontalAdvance(QString::fromUtf8(L.text.c_str()))
             * (L.height / capHeightOf(f));
    };
}

} // namespace cad
