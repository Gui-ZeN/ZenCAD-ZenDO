// src/ui/Theme.hpp
#pragma once
#include <QString>
#include <QColor>

namespace cad {

// ===========================================================================
//  ZenCAD — tema "Sumi & Washi" (tinta nanquim & papel).
//  Paleta calma: tinta quente, papel washi, acento LATÃO (#c2a063) e apoio
//  SÁLVIA (#789b8d). Dois modos: Dark (Sumi) e Light (Washi).
// ===========================================================================
enum class ThemeMode { Dark, Light };

// Cores do CANVAS (GL) — fora do alcance do QSS, lidas pelo ViewportWidget.
struct CanvasColors {
    QColor bg, gridMinor, gridMajor, crosshair, defaultInk, selection, snap, guide;
};

inline CanvasColors canvasColors(ThemeMode m) {
    CanvasColors c;
    if (m == ThemeMode::Dark) {
        c.bg = QColor(0x19, 0x1b, 0x1e);  c.gridMinor = QColor(0x23, 0x26, 0x2a);
        c.gridMajor = QColor(0x2c, 0x30, 0x36);  c.crosshair = QColor(0xb7, 0xb1, 0xa3);
        c.defaultInk = QColor(0xdc, 0xd6, 0xc8);  c.selection = QColor(0xc2, 0xa0, 0x63);
        c.snap = QColor(0x8f, 0xb3, 0xa3);  c.guide = QColor(0x78, 0x9b, 0x8d);
    } else {
        c.bg = QColor(0xe9, 0xe6, 0xde);  c.gridMinor = QColor(0xda, 0xd6, 0xcc);
        c.gridMajor = QColor(0xc9, 0xc3, 0xb6);  c.crosshair = QColor(0x4a, 0x46, 0x3e);
        c.defaultInk = QColor(0x33, 0x30, 0x2a);  c.selection = QColor(0x9c, 0x7a, 0x3c);
        c.snap = QColor(0x4c, 0x6f, 0x60);  c.guide = QColor(0x5f, 0x7d, 0x70);
    }
    return c;
}

// QSS completo do app para um modo. Usa tokens %xxx% substituídos pela paleta.
inline QString zenTheme(ThemeMode m) {
    struct Pal {
        QString base, surf, elev, hover, border, borderHi, text, dim, accent, accentBg, accentBd, term, onAccent;
    } p;
    if (m == ThemeMode::Dark)
        p = {"#191b1e", "#202327", "#282c31", "#30353b", "rgba(255,255,255,0.07)",
             "rgba(255,255,255,0.14)", "#e4ded2", "#938d80", "#c2a063",
             "rgba(194,160,99,0.14)", "rgba(194,160,99,0.32)", "#8fb3a3", "#191b1e"};
    else
        p = {"#eceae3", "#f4f1ea", "#ffffff", "#e3dfd4", "rgba(40,36,30,0.12)",
             "rgba(40,36,30,0.22)", "#2c2925", "#857e72", "#9c7a3c",
             "rgba(156,122,60,0.15)", "rgba(156,122,60,0.40)", "#4c6f60", "#ffffff"};

    QString s = QStringLiteral(R"QSS(
* { font-family: "Segoe UI"; font-size: 12px; }
QMainWindow, QWidget { background: %base%; color: %text%; }
QLabel { color: %text%; background: transparent; }

QMenuBar { background: %surf%; color: %text%; border-bottom: 1px solid %border%; padding: 2px 4px; }
QMenuBar::item { padding: 5px 12px; background: transparent; border-radius: 4px; }
QMenuBar::item:selected { background: %accentBg%; color: %accent%; }
QMenu { background: %surf%; color: %text%; border: 1px solid %border%; border-radius: 5px; padding: 5px; }
QMenu::item { padding: 6px 22px; border-radius: 4px; }
QMenu::item:selected { background: %accentBg%; color: %accent%; }
QMenu::separator { height: 1px; background: %border%; margin: 5px 8px; }

QToolBar { background: %surf%; border: none; border-bottom: 1px solid %border%; spacing: 3px; padding: 4px 6px; }
QToolButton { background: transparent; color: %text%; border: 1px solid transparent; border-radius: 4px; padding: 5px 9px; }
QToolButton:hover { background: %hover%; }
QToolButton:checked { background: %accentBg%; color: %accent%; border: 1px solid %accentBd%; }
QToolButton[popupMode="1"] { padding-right: 14px; }
QToolButton::menu-button { width: 12px; border: none; background: transparent; }
QToolButton::menu-arrow { width: 5px; height: 5px; }

QDockWidget { color: %text%; titlebar-close-icon: none; }
QDockWidget::title { background: %surf%; padding: 8px 12px; border-bottom: 1px solid %border%;
  font-size: 11px; letter-spacing: 1px; }

QStatusBar { background: %surf%; color: %dim%; border-top: 1px solid %border%; }
QStatusBar::item { border: none; }

QPlainTextEdit#cliLog {
  background: %base%; color: %term%; border: none; border-top: 1px solid %border%;
  font-family: "Consolas","Courier New",monospace; font-size: 12px; padding: 6px;
}
QLineEdit#cliInput {
  background: %elev%; color: %text%; border: 1px solid %border%; border-radius: 5px; padding: 6px 10px;
  font-family: "Consolas","Courier New",monospace; font-size: 12px;
  selection-background-color: %accent%; selection-color: %onAccent%;
}
QLineEdit#cliInput:focus { border: 1px solid %accentBd%; }

QTabWidget::pane { background: %surf%; border: none; }
QTabBar::tab {
  background: transparent; color: %dim%;
  padding: 8px 14px; margin-right: 4px; border: none; border-bottom: 2px solid transparent;
  font-size: 11px; letter-spacing: 1px;
}
QTabBar::tab:hover { color: %text%; }
QTabBar::tab:selected { color: %accent%; border-bottom: 2px solid %accent%; }

QLineEdit, QSpinBox, QDoubleSpinBox {
  background: %elev%; color: %text%; border: 1px solid %border%; border-radius: 4px; padding: 4px 8px;
  selection-background-color: %accent%; selection-color: %onAccent%;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid %accentBd%; }

QPushButton { background: %elev%; color: %text%; border: 1px solid %border%; border-radius: 4px; padding: 6px 12px; }
QPushButton:hover { border: 1px solid %accentBd%; color: %accent%; }
QPushButton:pressed { background: %hover%; }

QComboBox { background: %elev%; color: %text%; border: 1px solid %border%; border-radius: 4px; padding: 3px 8px; }
QComboBox:hover { border: 1px solid %borderHi%; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox::down-arrow { image: url(:/icons/arrow_down.png); width: 10px; height: 6px; margin-right: 4px; }
QComboBox::down-arrow:on { image: url(:/icons/arrow_down_hi.png); }
QComboBox QAbstractItemView {
  background: %surf%; color: %text%; border: 1px solid %border%; border-radius: 5px; padding: 4px;
  selection-background-color: %accentBg%; selection-color: %accent%; outline: none;
}

QCheckBox { color: %text%; background: transparent; spacing: 7px; }
QCheckBox::indicator { width: 15px; height: 15px; border-radius: 4px; border: 1px solid %borderHi%; background: %elev%; }
QCheckBox::indicator:hover { border: 1px solid %accentBd%; }
QCheckBox::indicator:checked { background: %accent%; border: 1px solid %accent%; }

QListWidget { background: %surf%; color: %text%; border: none; outline: none; }
QListWidget::item { padding: 5px 8px; border-radius: 4px; }
QListWidget::item:selected { background: %accentBg%; color: %accent%; }

QScrollBar:vertical { background: transparent; width: 9px; margin: 2px; }
QScrollBar::handle:vertical { background: %hover%; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: %borderHi%; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 2px; }
QScrollBar::handle:horizontal { background: %hover%; border-radius: 4px; min-width: 28px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QToolTip { background: %elev%; color: %text%; border: 1px solid %accentBd%; border-radius: 4px; padding: 5px 8px; }

QToolBar#ribbon { background: %surf%; border: none; border-bottom: 1px solid %border%; padding: 2px 4px; spacing: 0; }
QToolBar#ribbon QToolButton { padding: 2px; border-radius: 4px; }
QToolBar#ribbon QToolButton:hover { background: %hover%; }
QToolBar#ribbon QToolButton:checked { background: %accentBg%; }
QToolBar#ribbon QToolButton::menu-button { width: 10px; border: none; background: transparent; }
QLabel#ribbonPanelTitle { color: %dim%; font-size: 10px; letter-spacing: 1px; }
QFrame#ribbonSep { background: %border%; }
)QSS");

    s.replace("%base%", p.base).replace("%surf%", p.surf).replace("%elev%", p.elev)
     .replace("%hover%", p.hover).replace("%borderHi%", p.borderHi).replace("%border%", p.border)
     .replace("%text%", p.text).replace("%dim%", p.dim).replace("%accentBg%", p.accentBg)
     .replace("%accentBd%", p.accentBd).replace("%accent%", p.accent).replace("%term%", p.term)
     .replace("%onAccent%", p.onAccent);
    return s;
}

// Compat: o tema padrão do app agora é o ZenCAD escuro.
inline QString auroraTheme() { return zenTheme(ThemeMode::Dark); }

} // namespace cad
