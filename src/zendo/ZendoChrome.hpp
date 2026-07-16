// src/zendo/ZendoChrome.hpp — R33 "Ateliê 2.0": a moldura do Zendo.
// Camada de QSS APENSADA ao zenTheme compartilhado (não toca o tema do
// ZenCAD) + titlebar escura via DWM. Só estilo — zero comportamento.
#pragma once
#include <QEvent>
#include <QString>
#include <QWidget>

#ifdef Q_OS_WIN
// windows.h enxuto: sem GDI/macros que colidem com o kernel (Entity etc.).
// Este header deve ser incluído POR ÚLTIMO em quem o usa.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#endif

namespace zendo {

// Titlebar nativa escura (Windows 10 20H1+ / 11). No Win11 vai além: pinta a
// caption NA COR DO CHROME (#17191c) e o texto em washi — titlebar, menubar e
// toolbar viram UMA moldura contínua. Atributos ausentes falham em silêncio.
inline void applyDarkTitleBar(QWidget* w) {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    const BOOL dark = TRUE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (o valor público desde o 20H1)
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    // 35/36 = DWMWA_CAPTION_COLOR / DWMWA_TEXT_COLOR (Windows 11)
    const COLORREF caption = RGB(0x17, 0x19, 0x1c);   // o sumi do chrome
    const COLORREF text = RGB(0xe4, 0xde, 0xd2);      // washi
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
#else
    Q_UNUSED(w);
#endif
}

// R34: TODA janela nova (diálogos incluídos) ganha a caption escura no Show —
// sem isso, QInputDialog/QMessageBox abriam com a faixa branca do Windows.
// (O QFileDialog NATIVO fica de fora: é janela do sistema, segue o tema dele.)
class DarkTitleFilter : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show) {
            QWidget* w = qobject_cast<QWidget*>(o);
            if (w && w->isWindow()) applyDarkTitleBar(w);
        }
        return false;
    }
};

// O chrome próprio do Zendo, por cima do zenTheme(Dark):
//  - coluna de ferramentas em "laca": clusters respirando, ativo em latão;
//  - docks com título-lombada (fio latão à esquerda);
//  - Bandeja em seções de cabeçalho-com-fio (adeus caixas cinza);
//  - sliders com trilho fino e cabo latão;
//  - status bar com dica contextual + chip de Medidas sempre visível.
inline QString zendoChrome() {
    return QStringLiteral(R"QSS(
/* ---- menubar contínua com a titlebar (mesmo sumi #17191c) ---- */
QMenuBar {
  background: #17191c; color: #cfc9bc;
  border: none; border-bottom: 1px solid rgba(255,255,255,0.06);
  padding: 3px 8px;
  font-size: 12px; letter-spacing: 1px;
}
QMenuBar::item { padding: 6px 13px; background: transparent; border-radius: 5px; }
QMenuBar::item:selected { background: rgba(194,160,99,0.14); color: #c2a063; }
QMenuBar::item:pressed { background: rgba(194,160,99,0.20); color: #c2a063; }

/* ---- coluna de ferramentas em DUAS colunas (o "large tool set") ---- */
QToolBar#ferramentas {
  background: #17191c;
  border: none; border-right: 1px solid rgba(255,255,255,0.07);
  padding: 2px 1px;
}
QToolBar#ferramentas QToolButton {
  padding: 4px;
  border: 1px solid transparent; border-radius: 7px;
  background: transparent;
}
QToolBar#ferramentas QToolButton:hover { background: rgba(255,255,255,0.09); }
QToolBar#ferramentas QToolButton:pressed { background: rgba(255,255,255,0.05); }
QToolBar#ferramentas QToolButton:checked {
  background: rgba(194,160,99,0.20);
  border: 1px solid rgba(194,160,99,0.55);
}
QFrame#tbSep { background: rgba(255,255,255,0.10); margin: 4px 8px; }

/* ---- docks: título como lombada de livro, fio de latão ---- */
QDockWidget::title {
  background: #17191c;
  border-bottom: 1px solid rgba(255,255,255,0.07);
  border-left: 3px solid #c2a063;
  padding: 7px 10px; font-size: 11px; letter-spacing: 2px; color: #bfa476;
}

/* ---- Bandeja: seções de cabeçalho-com-fio ---- */
QGroupBox {
  border: none; border-top: 1px solid rgba(194,160,99,0.28);
  margin-top: 16px; padding-top: 8px;
  background: transparent;
  font-size: 11px; letter-spacing: 2px;
}
QGroupBox::title {
  subcontrol-origin: margin; subcontrol-position: top left;
  left: 0px; padding: 0 8px 2px 0;
  color: #c2a063; background: #191b1e;
}

/* ---- sliders: trilho fino, cabo latão ---- */
QSlider::groove:horizontal {
  height: 3px; background: rgba(255,255,255,0.12); border-radius: 1px;
}
QSlider::sub-page:horizontal { background: #c2a063; border-radius: 1px; }
QSlider::handle:horizontal {
  width: 14px; height: 14px; margin: -6px 0; border-radius: 7px;
  background: #c2a063; border: 2px solid #191b1e;
}
QSlider::handle:horizontal:hover { background: #d4b57a; }

/* ---- status bar: dica contextual + chip de medidas ---- */
QStatusBar { padding: 2px 6px; }
QLabel#toolHint { color: #8f897c; }
QLabel#vcbCaption {
  color: #6f6a60; font-size: 10px; letter-spacing: 2px; margin-right: 2px;
}
QLabel#vcbChip {
  background: #202327; color: #c2a063; font-weight: 600;
  border: 1px solid rgba(194,160,99,0.35); border-radius: 4px;
  padding: 2px 10px; min-width: 88px;
}

/* ---- R50: PRIMEIROS PASSOS — a folha do ateliê ----------------------
   Era um retângulo BRANCO com HTML cru e um botão "Close" em inglês, no
   meio de um app todo sumi: gritava "alerta do sistema" onde devia
   sussurrar "instrução do ateliê". Mesma tinta, mesmo latão, mesmo fio
   da Bandeja — o painel agora É o app, não uma visita.                */
QDialog#ppPanel { background: #17191c; }
QWidget#ppSheet { background: #17191c; }
/* a lombada latão dos docks, de novo: amarra o painel à casa */
QFrame#ppSpine { background: #c2a063; }
QLabel#ppCaption {                       /* mesma voz do vcbCaption */
  color: #6f6a60; font-size: 10px; letter-spacing: 3px; font-weight: 600;
}
QLabel#ppTitle {
  color: #e8e4d9; font-size: 25px; font-weight: 300; letter-spacing: 0.3px;
}
QFrame#ppRule { background: rgba(194,160,99,0.45); max-height: 1px; }
QFrame#ppRuleSoft { background: rgba(255,255,255,0.07); max-height: 1px; }
QLabel#ppNum {                           /* o numeral é a hierarquia */
  color: rgba(194,160,99,0.55); font-size: 26px; font-weight: 300;
  letter-spacing: 1px;
}
QLabel#ppHead { color: #e8e4d9; font-size: 14px; font-weight: 600; }
QLabel#ppBody { color: #a9a396; font-size: 13px; }
QLabel#ppKey {                           /* as teclas viram chip (R33) */
  background: #202327; color: #c2a063; font-size: 12px; font-weight: 600;
  border: 1px solid rgba(194,160,99,0.35); border-radius: 4px;
  padding: 3px 9px;
}
QLabel#ppPath { color: #8f897c; font-size: 12px; }
QLabel#ppPathStrong { color: #cfc9bc; font-size: 12px; font-weight: 600; }
QPushButton#ppGo {                       /* CTA: o único latão sólido */
  background: #c2a063; color: #17191c; font-weight: 700; font-size: 13px;
  border: none; border-radius: 5px; padding: 9px 30px; letter-spacing: 0.5px;
}
QPushButton#ppGo:hover { background: #d4b57a; }
QPushButton#ppGo:pressed { background: #b08f52; }
QLabel#ppFoot { color: #5f5a52; font-size: 11px; }
)QSS");
}

} // namespace zendo
