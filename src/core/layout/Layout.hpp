// src/core/layout/Layout.hpp
// Paper Space / Pranchas: modelo de dados de folhas (Layout) com viewports que
// mostram o Modelo a uma escala 1:N, e a matemática de escala mm<->unidade.
// Header-only e SEM Qt/OpenGL: testável headless (como os demais ops do kernel).
#pragma once
#include "core/math/Vec.hpp"
#include <string>
#include <vector>
#include <cstddef>
#include <cstdio>
#include <cmath>

namespace cad {

// Tamanhos de papel ISO série A (mm), definidos em RETRATO (w < h).
enum class PaperSize { A4, A3, A2, A1, A0 };

struct PaperDims { double wMm; double hMm; };

inline PaperDims paperDims(PaperSize s) {
    switch (s) {
        case PaperSize::A4: return {210.0, 297.0};
        case PaperSize::A3: return {297.0, 420.0};
        case PaperSize::A2: return {420.0, 594.0};
        case PaperSize::A1: return {594.0, 841.0};
        case PaperSize::A0: return {841.0, 1189.0};
    }
    return {297.0, 420.0};
}

// Comprimento em mm de 1 unidade de desenho, por índice de unidade
// (mesma ordem do seletor de Unidades da UI: mm/cm/dm/m/km/pol/pé/—).
inline double unitLengthMm(int unitIndex) {
    switch (unitIndex) {
        case 0: return 1.0;        // mm
        case 1: return 10.0;       // cm
        case 2: return 100.0;      // dm
        case 3: return 1000.0;     // m
        case 4: return 1000000.0;  // km
        case 5: return 25.4;       // polegada
        case 6: return 304.8;      // pé
        default: return 1.0;       // sem unidade -> trata como mm
    }
}

// Fator "mm de papel por unidade de modelo", dado o comprimento (mm) de 1
// unidade de desenho e o denominador N de "1:N".
//   desenho em mm (unitMm=1),  1:50  -> 0.02 mm/unid.
//   desenho em m  (unitMm=1000),1:100 -> 10   mm/unid.
inline double scaleMmPerUnit(double unitMm, double denom) {
    return (denom > 0.0) ? unitMm / denom : unitMm;
}

// Rótulo legível de escala a partir do denominador N de "1:N".
//   N>=1 -> redução "1:N" (ex.: 1:50); N<1 -> ampliação "M:1" (ex.: 2:1).
// Arredonda para inteiro quando bem próximo, senão 2 casas.
inline std::string formatScale(double denom) {
    char buf[48];
    if (denom <= 0.0) return "1:1";
    if (denom >= 1.0) {
        if (std::abs(denom - std::round(denom)) < 1e-6) std::snprintf(buf, sizeof(buf), "1:%.0f", denom);
        else                                            std::snprintf(buf, sizeof(buf), "1:%.2f", denom);
    } else {
        const double up = 1.0 / denom;
        if (std::abs(up - std::round(up)) < 1e-6) std::snprintf(buf, sizeof(buf), "%.0f:1", up);
        else                                      std::snprintf(buf, sizeof(buf), "%.2f:1", up);
    }
    return std::string(buf);
}

// Uma janela retangular na prancha que mostra o Modelo a uma escala.
// Retângulo em mm de papel, origem no canto inferior-esquerdo da folha (Y p/ cima).
struct SheetViewport {
    double xMm{20.0}, yMm{20.0};        // canto inferior-esquerdo
    double wMm{120.0}, hMm{90.0};       // tamanho na folha
    double modelCx{0.0}, modelCy{0.0};  // ponto do modelo no centro do viewport
    double mmPerUnit{1.0};              // escala: mm de papel por unidade de modelo
    double scaleDenom{1.0};             // N de "1:N" (rótulo/edição)
    bool   locked{false};               // travado: MSPACE não altera a vista/escala
    std::vector<std::string> frozenLayers;   // VP-FREEZE: camadas ocultas SÓ aqui

    bool layerFrozenHere(const std::string& n) const {
        for (const std::string& f : frozenLayers)
            if (f == n) return true;
        return false;
    }

    double cxMm() const { return xMm + wMm * 0.5; }
    double cyMm() const { return yMm + hMm * 0.5; }

    // Modelo -> papel(mm): escala em torno de modelCenter, centrado no viewport.
    Point3 toPaper(const Point3& m) const {
        return { cxMm() + (m.x - modelCx) * mmPerUnit,
                 cyMm() + (m.y - modelCy) * mmPerUnit, 0.0 };
    }

    // Papel(mm) -> modelo (inverso de toPaper); útil p/ pan dentro do viewport.
    Point3 toModel(double px, double py) const {
        const double k = (mmPerUnit != 0.0) ? 1.0 / mmPerUnit : 0.0;
        return { modelCx + (px - cxMm()) * k,
                 modelCy + (py - cyMm()) * k, 0.0 };
    }

    // Ponto de papel(mm) dentro do retângulo do viewport?
    bool contains(double px, double py) const {
        return px >= xMm && px <= xMm + wMm && py >= yMm && py <= yMm + hMm;
    }
};

// Uma prancha (folha) com moldura, selo paramétrico embutido e N viewports.
struct Layout {
    std::string name{"Prancha 1"};
    PaperSize   paper{PaperSize::A3};
    bool        landscape{true};
    double      marginMm{10.0};
    // Metadados do carimbo/selo (selo paramétrico embutido).
    std::string title, project, author, date, scaleLabel;
    std::vector<SheetViewport> viewports;

    double widthMm()  const { const PaperDims d = paperDims(paper); return landscape ? d.hMm : d.wMm; }
    double heightMm() const { const PaperDims d = paperDims(paper); return landscape ? d.wMm : d.hMm; }
};

// Tabela ordenada de pranchas (como abas), com uma corrente.
class LayoutTable {
public:
    std::size_t size()  const { return m_layouts.size(); }
    bool        empty() const { return m_layouts.empty(); }

    Layout&       at(std::size_t i)       { return m_layouts[i]; }
    const Layout& at(std::size_t i) const { return m_layouts[i]; }
    std::vector<Layout>&       all()       { return m_layouts; }
    const std::vector<Layout>& all() const { return m_layouts; }

    Layout& add(const Layout& l) { m_layouts.push_back(l); return m_layouts.back(); }
    Layout& addDefault(const std::string& name) {
        Layout l; l.name = name; return add(l);
    }
    void remove(std::size_t i) {
        if (i >= m_layouts.size()) return;
        m_layouts.erase(m_layouts.begin() + static_cast<std::ptrdiff_t>(i));
        if (!m_layouts.empty() && m_current >= m_layouts.size())
            m_current = m_layouts.size() - 1;
    }

    std::size_t current() const { return m_current; }
    void        setCurrent(std::size_t i) { if (i < m_layouts.size()) m_current = i; }
    Layout*       currentLayout()       { return m_layouts.empty() ? nullptr : &m_layouts[m_current]; }
    const Layout* currentLayout() const { return m_layouts.empty() ? nullptr : &m_layouts[m_current]; }

private:
    std::vector<Layout> m_layouts;
    std::size_t         m_current{0};
};

} // namespace cad
