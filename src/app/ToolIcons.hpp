// src/app/ToolIcons.hpp
#pragma once

// Ícones das ferramentas da barra desenhados em código (sem assets externos).
//
// Cada glifo é pintado com QPainter sobre um QPixmap transparente, em traço
// claro pensado para uma UI escura. A função pública é:
//
//     QIcon cad::toolIcon(const QString& key);
//
// Chaves suportadas (casam com os nomes das ferramentas em MainWindow):
//   Desenho:    "line", "circle", "rect", "arc", "ellipse", "polyline",
//               "point", "spline".
//   Modificar:  "select", "move", "copy", "rotate", "scale", "mirror",
//               "offset", "trim", "fillet", "chamfer", "extend", "erase".
//   Anotação:   "text", "dim", "hatch".
// Para qualquer chave desconhecida devolve um QIcon vazio.

#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QString>
#include <QColor>
#include <QPen>
#include <QPainterPath>
#include <QPolygonF>
#include <QGuiApplication>
#include <cmath>

namespace cad {

namespace detail {

// Lado do glifo em pixels lógicos (~24x24), com uma pequena margem interna.
inline constexpr int kIconSize = 24;
inline constexpr qreal kMargin = 3.5;

// Tom médio quente: contrasta tanto no Sumi (escuro) quanto no Washi (claro),
// já que o ícone é gerado uma vez. A ferramenta ativa fica em latão (acento).
inline QColor toolIconStroke() { return QColor(150, 144, 132); }

// Prepara um QPainter já configurado (antialiasing, caneta/pincel padrão) sobre
// o pixmap dado. A caneta usa pontas/junções arredondadas para um traço macio.
inline void beginGlyph(QPainter& p, qreal width = 1.8) {
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(toolIconStroke());
    pen.setWidthF(width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
}

// --- Glifos individuais -----------------------------------------------------
// Convenção: recebem um QPainter pronto e desenham na área [m, kIconSize-m].

inline void drawLine(QPainter& p) {
    const qreal m = kMargin;
    const qreal s = kIconSize;
    const QPointF a(m, s - m);
    const QPointF b(s - m, m);
    p.drawLine(a, b);
    p.setBrush(toolIconStroke());
    p.drawEllipse(a, 1.6, 1.6);
    p.drawEllipse(b, 1.6, 1.6);
}

inline void drawCircle(QPainter& p) {
    const qreal m = kMargin;
    const qreal s = kIconSize;
    p.drawEllipse(QRectF(m, m, s - 2 * m, s - 2 * m));
}

inline void drawSelect(QPainter& p) {
    // Seta de cursor (ponteiro) preenchida.
    const QPointF pts[] = {
        {6, 4}, {6, 18}, {10, 14}, {12.5, 19}, {14.5, 18}, {12, 13}, {17, 13}
    };
    QPolygonF arrow;
    for (const auto& pt : pts) arrow << pt;
    p.setBrush(toolIconStroke());
    p.drawPolygon(arrow);
}

inline void drawMove(QPainter& p) {
    // Cruz de 4 setas a partir do centro.
    const qreal c = kIconSize / 2.0;
    const qreal r = c - kMargin;     // alcance da haste
    const qreal h = 3.0;             // meia-base da cabeça da seta

    // Hastes horizontal e vertical.
    p.drawLine(QPointF(c - r, c), QPointF(c + r, c));
    p.drawLine(QPointF(c, c - r), QPointF(c, c + r));

    p.setBrush(toolIconStroke());
    // Cabeça esquerda.
    p.drawPolygon(QPolygonF({{c - r, c}, {c - r + h, c - h}, {c - r + h, c + h}}));
    // Cabeça direita.
    p.drawPolygon(QPolygonF({{c + r, c}, {c + r - h, c - h}, {c + r - h, c + h}}));
    // Cabeça superior.
    p.drawPolygon(QPolygonF({{c, c - r}, {c - h, c - r + h}, {c + h, c - r + h}}));
    // Cabeça inferior.
    p.drawPolygon(QPolygonF({{c, c + r}, {c - h, c + r - h}, {c + h, c + r - h}}));
}

inline void drawCopy(QPainter& p) {
    // Dois retângulos sobrepostos, deslocados.
    const qreal w = 11.0, h = 11.0;
    p.drawRect(QRectF(8, 8, w, h));   // da frente
    p.drawRect(QRectF(4, 4, w, h));   // de trás
}

inline void drawRotate(QPainter& p) {
    // Arco (~270°) com ponta de seta na extremidade.
    const qreal c = kIconSize / 2.0;
    const qreal r = c - kMargin;
    const QRectF box(c - r, c - r, 2 * r, 2 * r);
    // Arco começando no topo, varrendo no sentido horário.
    const int startAngle = 90 * 16;
    const int spanAngle  = -270 * 16;
    p.drawArc(box, startAngle, spanAngle);

    // Ponta da seta no fim do arco (ângulo de 90 - 270 = -180°, ou seja, à esquerda).
    const QPointF tip(c - r, c);
    p.setBrush(toolIconStroke());
    p.drawPolygon(QPolygonF({tip, {tip.x() - 2.6, tip.y() - 3.2},
                             {tip.x() + 2.8, tip.y() - 2.4}}));
}

inline void drawScale(QPainter& p) {
    // Retângulo com seta diagonal saindo do canto inferior-direito.
    const qreal m = kMargin;
    const qreal s = kIconSize;
    p.drawRect(QRectF(m, m, (s - 2 * m) * 0.62, (s - 2 * m) * 0.62));

    const QPointF from(s - m - 6, s - m - 6);
    const QPointF to(s - m, s - m);
    p.drawLine(from, to);
    p.setBrush(toolIconStroke());
    // Cabeça da seta apontando para fora (sentido +x,+y).
    p.drawPolygon(QPolygonF({to, {to.x() - 4.2, to.y() - 1.4},
                             {to.x() - 1.4, to.y() - 4.2}}));
}

inline void drawMirror(QPainter& p) {
    // Triângulo sólido à esquerda + eixo vertical tracejado + reflexo à direita.
    const qreal c = kIconSize / 2.0;
    const qreal top = 5.0, bot = 19.0;

    // Eixo tracejado.
    QPen dashed(toolIconStroke());
    dashed.setWidthF(1.2);
    dashed.setStyle(Qt::DashLine);
    dashed.setCapStyle(Qt::FlatCap);
    p.save();
    p.setPen(dashed);
    p.drawLine(QPointF(c, 3), QPointF(c, kIconSize - 3));
    p.restore();

    // Triângulo original (preenchido) à esquerda.
    p.setBrush(toolIconStroke());
    p.drawPolygon(QPolygonF({{c - 2, top}, {c - 2, bot}, {c - 9, (top + bot) / 2}}));

    // Reflexo (apenas contorno) à direita.
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(QPolygonF({{c + 2, top}, {c + 2, bot}, {c + 9, (top + bot) / 2}}));
}

inline void drawErase(QPainter& p) {
    // Um "X".
    const qreal m = kMargin + 1.0;
    const qreal s = kIconSize;
    p.drawLine(QPointF(m, m), QPointF(s - m, s - m));
    p.drawLine(QPointF(s - m, m), QPointF(m, s - m));
}

inline void drawOffset(QPainter& p) {
    // Duas curvas paralelas (em "L" suave) sugerindo deslocamento.
    QPainterPath inner;
    inner.moveTo(6, 18);
    inner.lineTo(6, 9);
    inner.quadTo(6, 6, 9, 6);
    inner.lineTo(18, 6);

    QPainterPath outer;
    outer.moveTo(3, 21);
    outer.lineTo(3, 9);
    outer.quadTo(3, 3, 9, 3);
    outer.lineTo(21, 3);

    p.drawPath(inner);
    p.drawPath(outer);
}

inline void drawTrim(QPainter& p) {
    // Tesoura simplificada: duas lâminas (linhas) cruzadas + dois anéis.
    const QPointF pivot(13.5, 12);
    // Lâminas.
    p.drawLine(pivot, QPointF(21, 6));
    p.drawLine(pivot, QPointF(21, 18));
    // Anéis dos cabos.
    p.drawEllipse(QPointF(6.5, 7.5), 3.0, 3.0);
    p.drawEllipse(QPointF(6.5, 16.5), 3.0, 3.0);
    // Hastes ligando anéis ao pivô.
    p.drawLine(QPointF(9.0, 9.0), pivot);
    p.drawLine(QPointF(9.0, 15.0), pivot);
}

inline void drawFillet(QPainter& p) {
    // Canto em "L" com o vértice arredondado.
    QPainterPath path;
    path.moveTo(5, 21);
    path.lineTo(5, 11);
    path.quadTo(5, 5, 11, 5);   // arredondamento do canto
    path.lineTo(21, 5);
    p.drawPath(path);
}

inline void drawChamfer(QPainter& p) {
    // Canto em "L" com o vértice cortado em 45° (chanfro).
    QPainterPath path;
    path.moveTo(5, 21);
    path.lineTo(5, 10);
    path.lineTo(10, 5);   // corte reto a 45°
    path.lineTo(21, 5);
    p.drawPath(path);
}

inline void drawExtend(QPainter& p) {
    // Aresta-limite (vertical) + linha sendo estendida até ela, com seta.
    const qreal s = kIconSize;
    // Aresta-limite à direita.
    p.drawLine(QPointF(s - 4, 4), QPointF(s - 4, s - 4));
    // Linha horizontal estendida até a aresta.
    p.drawLine(QPointF(4, s / 2.0), QPointF(s - 4, s / 2.0));
    // Cabeça de seta encostando na aresta.
    p.setBrush(toolIconStroke());
    p.drawPolygon(QPolygonF({{s - 4, s / 2.0},
                             {s - 9, s / 2.0 - 3.0},
                             {s - 9, s / 2.0 + 3.0}}));
}

inline void drawRect(QPainter& p) {
    // Retângulo simples (mais largo que alto).
    const qreal m = kMargin;
    const qreal s = kIconSize;
    p.drawRect(QRectF(m, m + 2.5, s - 2 * m, s - 2 * m - 5));
}

inline void drawArc(QPainter& p) {
    // Semi-arco superior com dois nós nas extremidades.
    const qreal c = kIconSize / 2.0;
    const qreal r = c - kMargin;
    const QRectF box(c - r, c - r + 3, 2 * r, 2 * r);
    p.drawArc(box, 0, 180 * 16);   // metade superior
    p.setBrush(toolIconStroke());
    p.drawEllipse(QPointF(c - r, c + 3), 1.5, 1.5);
    p.drawEllipse(QPointF(c + r, c + 3), 1.5, 1.5);
}

inline void drawEllipse(QPainter& p) {
    // Elipse achatada (eixo maior horizontal).
    const qreal m = kMargin;
    const qreal s = kIconSize;
    p.drawEllipse(QRectF(m - 0.5, m + 3.5, s - 2 * m + 1, s - 2 * m - 7));
}

inline void drawPolyline(QPainter& p) {
    // Sequência de segmentos com vértices marcados.
    const QPointF pts[] = {{4, 17}, {9, 8}, {14, 14}, {20, 6}};
    for (int i = 0; i + 1 < 4; ++i) p.drawLine(pts[i], pts[i + 1]);
    p.setBrush(toolIconStroke());
    for (const auto& pt : pts) p.drawEllipse(pt, 1.6, 1.6);
}

inline void drawPoint(QPainter& p) {
    // Marcador "+" com um nó central.
    const qreal c = kIconSize / 2.0;
    const qreal r = c - kMargin - 1.0;
    p.drawLine(QPointF(c - r, c), QPointF(c + r, c));
    p.drawLine(QPointF(c, c - r), QPointF(c, c + r));
    p.setBrush(toolIconStroke());
    p.drawEllipse(QPointF(c, c), 1.8, 1.8);
}

inline void drawSpline(QPainter& p) {
    // Curva suave em "S" (duas curvas cúbicas).
    QPainterPath path;
    path.moveTo(4, 18);
    path.cubicTo(8, 4, 16, 20, 20, 6);
    p.drawPath(path);
}

inline void drawText(QPainter& p) {
    // Letra "A" estilizada (serifa-base) sugerindo texto.
    p.drawLine(QPointF(6, 19), QPointF(12, 5));    // perna esquerda
    p.drawLine(QPointF(12, 5), QPointF(18, 19));   // perna direita
    p.drawLine(QPointF(8.5, 13.5), QPointF(15.5, 13.5)); // travessão
}

inline void drawDim(QPainter& p) {
    // Cota linear: linha de cota com setas nas pontas + linhas de extensão.
    const qreal y = 13.0;
    // Linha de cota horizontal.
    p.drawLine(QPointF(5, y), QPointF(19, y));
    // Linhas de extensão (verticais).
    p.drawLine(QPointF(5, y), QPointF(5, 20));
    p.drawLine(QPointF(19, y), QPointF(19, 20));
    // Setas nas extremidades.
    p.setBrush(toolIconStroke());
    p.drawPolygon(QPolygonF({{5, y}, {8.5, y - 2.2}, {8.5, y + 2.2}}));
    p.drawPolygon(QPolygonF({{19, y}, {15.5, y - 2.2}, {15.5, y + 2.2}}));
}

inline void drawHatch(QPainter& p) {
    // Retângulo preenchido com linhas inclinadas (padrão de hachura).
    const QRectF r(5, 5, 14, 14);
    p.drawRect(r);
    QPen thin(toolIconStroke());
    thin.setWidthF(1.0);
    p.save();
    p.setPen(thin);
    p.setClipRect(r);
    for (qreal x = -10; x < 24; x += 4.0)
        p.drawLine(QPointF(x, 24), QPointF(x + 18, 6));
    p.restore();
}

// --- Glifos adicionais (ribbon densa) ---------------------------------------
inline void drawPolygon(QPainter& p) {
    const qreal c = kIconSize / 2.0, r = c - kMargin;
    constexpr double pi = 3.14159265358979;
    QPolygonF h;
    for (int i = 0; i < 6; ++i) { const double a = pi / 6 + i * pi / 3;
        h << QPointF(c + r * std::cos(a), c + r * std::sin(a)); }
    p.drawPolygon(h);
}
inline void drawXline(QPainter& p) { p.drawLine(QPointF(1, kIconSize - 1), QPointF(kIconSize - 1, 1)); }
inline void drawArray(QPainter& p) {
    const qreal s = 5.0, g = 3.0;
    for (int r = 0; r < 2; ++r) for (int col = 0; col < 2; ++col)
        p.drawRect(QRectF(6 + col * (s + g), 6 + r * (s + g), s, s));
}
inline void drawStretch(QPainter& p) {
    p.drawRect(QRectF(7, 7, 10, 10));
    p.drawLine(QPointF(3, 12), QPointF(7, 12));
    p.drawLine(QPointF(17, 12), QPointF(21, 12));
    p.setBrush(toolIconStroke());
    p.drawPolygon(QPolygonF({{3, 12}, {6, 10}, {6, 14}}));
    p.drawPolygon(QPolygonF({{21, 12}, {18, 10}, {18, 14}}));
}
inline void drawBreak(QPainter& p) {
    p.drawLine(QPointF(4, 12), QPointF(10, 12));
    p.drawLine(QPointF(14, 12), QPointF(20, 12));
    QPen th(toolIconStroke()); th.setWidthF(1.0); p.save(); p.setPen(th);
    p.drawLine(QPointF(11, 8), QPointF(11, 16)); p.drawLine(QPointF(13, 8), QPointF(13, 16)); p.restore();
}
inline void drawJoin(QPainter& p) {
    p.drawLine(QPointF(4, 18), QPointF(12, 12));
    p.drawLine(QPointF(12, 12), QPointF(20, 6));
    p.setBrush(toolIconStroke()); p.drawEllipse(QPointF(12, 12), 2.0, 2.0);
}
inline void drawLengthen(QPainter& p) {
    p.drawLine(QPointF(4, 12), QPointF(16, 12));
    p.setBrush(toolIconStroke()); p.drawPolygon(QPolygonF({{20, 12}, {15, 9}, {15, 15}}));
}
inline void drawArea(QPainter& p) {
    QPolygonF t; t << QPointF(5, 19) << QPointF(19, 19) << QPointF(12, 6); p.drawPolygon(t);
}
inline void drawDistance(QPainter& p) {
    const qreal y = 12;
    p.drawLine(QPointF(4, y), QPointF(20, y));
    p.drawLine(QPointF(4, y - 3), QPointF(4, y + 3));
    p.drawLine(QPointF(20, y - 3), QPointF(20, y + 3));
}
inline void drawMatch(QPainter& p) {
    QPolygonF head; head << QPointF(8, 12) << QPointF(15, 5) << QPointF(19, 9) << QPointF(12, 16);
    p.setBrush(toolIconStroke()); p.drawPolygon(head); p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(8, 12), QPointF(5, 19)); p.drawLine(QPointF(12, 16), QPointF(8, 19));
}
inline void drawBlock(QPainter& p) {
    p.drawRect(QRectF(5, 5, 14, 14));
    QPen th(toolIconStroke()); th.setWidthF(1.0); p.save(); p.setPen(th);
    p.drawLine(QPointF(5, 9), QPointF(19, 9)); p.drawLine(QPointF(9, 5), QPointF(9, 19)); p.restore();
}
inline void drawExplode(QPainter& p) {
    p.drawLine(QPointF(12, 12), QPointF(6, 6)); p.drawLine(QPointF(12, 12), QPointF(18, 6));
    p.drawLine(QPointF(12, 12), QPointF(6, 18)); p.drawLine(QPointF(12, 12), QPointF(18, 18));
    p.setBrush(toolIconStroke());
    for (QPointF c : {QPointF(5, 5), QPointF(19, 5), QPointF(5, 19), QPointF(19, 19)}) p.drawEllipse(c, 1.5, 1.5);
}
inline void drawTable(QPainter& p) {
    const QRectF r(5, 5, 14, 14); p.drawRect(r);
    QPen th(toolIconStroke()); th.setWidthF(1.0); p.save(); p.setPen(th);
    for (int i = 1; i < 3; ++i) {
        p.drawLine(QPointF(5 + i * 14 / 3.0, 5), QPointF(5 + i * 14 / 3.0, 19));
        p.drawLine(QPointF(5, 5 + i * 14 / 3.0), QPointF(19, 5 + i * 14 / 3.0));
    }
    p.restore();
}
inline void drawWipeout(QPainter& p) {
    QPolygonF r; r << QPointF(5, 5) << QPointF(16, 5) << QPointF(19, 8) << QPointF(19, 19) << QPointF(5, 19);
    p.save(); p.setOpacity(0.45); p.setBrush(toolIconStroke()); p.drawPolygon(r); p.restore();
    p.setBrush(Qt::NoBrush); p.drawPolygon(r);
}
inline void drawRegion(QPainter& p) {
    QPainterPath path; path.addRoundedRect(QRectF(5, 6, 14, 12), 5, 5); p.drawPath(path);
}
inline void drawLeader(QPainter& p) {
    p.drawLine(QPointF(5, 18), QPointF(13, 10)); p.drawLine(QPointF(13, 10), QPointF(20, 10));
    p.setBrush(toolIconStroke()); p.drawPolygon(QPolygonF({{5, 18}, {6, 14}, {9, 16}}));
}
inline void drawMleader(QPainter& p) {
    p.drawLine(QPointF(4, 18), QPointF(11, 11)); p.drawLine(QPointF(18, 18), QPointF(11, 11));
    p.drawLine(QPointF(11, 11), QPointF(20, 8));
    p.setBrush(toolIconStroke()); p.drawEllipse(QPointF(11, 11), 1.5, 1.5);
}
inline void drawRevcloud(QPainter& p) {
    p.drawArc(QRectF(4, 7, 7, 7),  90 * 16,  180 * 16);
    p.drawArc(QRectF(8, 4, 8, 8),  30 * 16,  150 * 16);
    p.drawArc(QRectF(13, 7, 7, 7), -90 * 16, 180 * 16);
    p.drawArc(QRectF(7, 11, 10, 7), 180 * 16, 180 * 16);
}
inline void drawInquiry(QPainter& p) {
    p.drawEllipse(QRectF(5, 5, 14, 14));
    p.setBrush(toolIconStroke()); p.drawEllipse(QPointF(12, 9), 1.0, 1.0);
    p.setBrush(Qt::NoBrush); p.drawLine(QPointF(12, 12), QPointF(12, 17));
}
inline void drawLayers(QPainter& p) {
    auto dia = [&](qreal y) { QPolygonF d;
        d << QPointF(12, y) << QPointF(19, y + 3) << QPointF(12, y + 6) << QPointF(5, y + 3);
        p.drawPolygon(d); };
    dia(4); dia(9); dia(14);
}

// --- Zooms / navegação (lupa + variações) ----------------------------------
inline void drawMagnifier(QPainter& p) {   // lupa base: lente + cabo
    const qreal m = kMargin;
    const QPointF c(m + 7.0, m + 7.0);
    const qreal r = 6.0;
    p.drawEllipse(c, r, r);
    p.drawLine(QPointF(c.x() + r * 0.7, c.y() + r * 0.7),
               QPointF(kIconSize - m, kIconSize - m));
}
inline void drawZoomIn(QPainter& p) {
    drawMagnifier(p);
    const QPointF c(kMargin + 7.0, kMargin + 7.0);
    p.drawLine(QPointF(c.x() - 3, c.y()), QPointF(c.x() + 3, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - 3), QPointF(c.x(), c.y() + 3));
}
inline void drawZoomOut(QPainter& p) {
    drawMagnifier(p);
    const QPointF c(kMargin + 7.0, kMargin + 7.0);
    p.drawLine(QPointF(c.x() - 3, c.y()), QPointF(c.x() + 3, c.y()));
}
inline void drawZoomWin(QPainter& p) {   // retângulo pontilhado de "janela"
    const qreal m = kMargin;
    QPen old = p.pen();
    QPen dash = old; dash.setStyle(Qt::DotLine); p.setPen(dash);
    p.drawRect(QRectF(m, m + 1.0, kIconSize - 2 * m, kIconSize - 2 * m - 2.0));
    p.setPen(old);
    const QPointF c(kIconSize * 0.5, kIconSize * 0.5);
    p.drawEllipse(c, 3.2, 3.2);
    p.drawLine(QPointF(c.x() + 2.3, c.y() + 2.3), QPointF(c.x() + 5.0, c.y() + 5.0));
}
inline void drawZoomPrev(QPainter& p) {   // lupa + seta de "voltar"
    drawMagnifier(p);
    const QPointF c(kMargin + 7.0, kMargin + 7.0);
    p.drawLine(QPointF(c.x() + 3, c.y()), QPointF(c.x() - 3, c.y()));   // haste
    p.drawLine(QPointF(c.x() - 3, c.y()), QPointF(c.x() - 0.5, c.y() - 2.2));
    p.drawLine(QPointF(c.x() - 3, c.y()), QPointF(c.x() - 0.5, c.y() + 2.2));
}
inline void drawPan(QPainter& p) {   // mão estilizada (4 setas em cruz)
    const qreal m = kMargin;
    const QPointF c(kIconSize * 0.5, kIconSize * 0.5);
    const qreal a = kIconSize * 0.5 - m;
    p.drawLine(QPointF(c.x(), c.y() - a), QPointF(c.x(), c.y() + a));
    p.drawLine(QPointF(c.x() - a, c.y()), QPointF(c.x() + a, c.y()));
    auto head = [&](QPointF tip, qreal dx, qreal dy) {
        p.drawLine(tip, QPointF(tip.x() + dx, tip.y() + dy));
        p.drawLine(tip, QPointF(tip.x() + dy, tip.y() + dx)); };
    head(QPointF(c.x(), c.y() - a),  2.4,  2.4);   // topo
    head(QPointF(c.x(), c.y() + a), -2.4, -2.4);   // baixo
    head(QPointF(c.x() - a, c.y()),  2.4, -2.4);   // esq
    head(QPointF(c.x() + a, c.y()), -2.4,  2.4);   // dir
}

} // namespace detail

// Devolve o ícone da ferramenta identificada por `key`, desenhado em código.
// Chave desconhecida -> QIcon vazio.
inline QIcon toolIcon(const QString& key) {
    using namespace detail;

    // Respeita o device pixel ratio para nitidez em telas HiDPI.
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(static_cast<int>(kIconSize * dpr), static_cast<int>(kIconSize * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    beginGlyph(p);

    bool known = true;
    if      (key == "line")     drawLine(p);
    else if (key == "circle")   drawCircle(p);
    else if (key == "rect")     drawRect(p);
    else if (key == "arc")      drawArc(p);
    else if (key == "ellipse")  drawEllipse(p);
    else if (key == "polyline") drawPolyline(p);
    else if (key == "point")    drawPoint(p);
    else if (key == "spline")   drawSpline(p);
    else if (key == "select")   drawSelect(p);
    else if (key == "move")     drawMove(p);
    else if (key == "copy")     drawCopy(p);
    else if (key == "rotate")   drawRotate(p);
    else if (key == "scale")    drawScale(p);
    else if (key == "mirror")   drawMirror(p);
    else if (key == "erase")    drawErase(p);
    else if (key == "offset")   drawOffset(p);
    else if (key == "trim")     drawTrim(p);
    else if (key == "fillet")   drawFillet(p);
    else if (key == "chamfer")  drawChamfer(p);
    else if (key == "extend")   drawExtend(p);
    else if (key == "text")     drawText(p);
    else if (key == "dim")      drawDim(p);
    else if (key == "hatch")    drawHatch(p);
    else if (key == "polygon")  drawPolygon(p);
    else if (key == "xline")    drawXline(p);
    else if (key == "array")    drawArray(p);
    else if (key == "stretch")  drawStretch(p);
    else if (key == "break")    drawBreak(p);
    else if (key == "join")     drawJoin(p);
    else if (key == "lengthen") drawLengthen(p);
    else if (key == "area")     drawArea(p);
    else if (key == "distance") drawDistance(p);
    else if (key == "match")    drawMatch(p);
    else if (key == "block")    drawBlock(p);
    else if (key == "explode")  drawExplode(p);
    else if (key == "table")    drawTable(p);
    else if (key == "wipeout")  drawWipeout(p);
    else if (key == "region")   drawRegion(p);
    else if (key == "leader")   drawLeader(p);
    else if (key == "mleader")  drawMleader(p);
    else if (key == "revcloud") drawRevcloud(p);
    else if (key == "inquiry")  drawInquiry(p);
    else if (key == "layers")   drawLayers(p);
    else if (key == "zoomwin")  drawZoomWin(p);
    else if (key == "zoomin")   drawZoomIn(p);
    else if (key == "zoomout")  drawZoomOut(p);
    else if (key == "zoomprev") drawZoomPrev(p);
    else if (key == "zoomext")  drawZoomIn(p);
    else if (key == "pan")      drawPan(p);
    else                        known = false;

    p.end();

    if (!known) return QIcon();
    return QIcon(pm);
}

} // namespace cad
