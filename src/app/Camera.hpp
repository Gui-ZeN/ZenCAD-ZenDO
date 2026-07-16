// src/app/Camera.hpp
#pragma once
#include <algorithm>

namespace cad {

// Câmera ortográfica 2D. Mantém o centro da vista em coordenadas de mundo
// (double) e a escala em pixels por unidade. A projeção é montada relativa a uma
// origem próxima dos dados (rebase), feito no ViewportWidget — aqui só expomos o
// retângulo visível em coordenadas de mundo.
class Camera2D {
public:
    void setViewport(int w, int h) { m_w = (w > 0 ? w : 1); m_h = (h > 0 ? h : 1); }
    int  width()  const { return m_w; }
    int  height() const { return m_h; }
    double scale() const { return m_pxPerUnit; }

    // Arrastar: desloca a vista por um delta em pixels de tela.
    void panPixels(double dxPix, double dyPix) {
        m_cx -= dxPix / m_pxPerUnit;
        m_cy += dyPix / m_pxPerUnit;   // y de tela cresce para baixo
    }

    // Zoom mantendo o ponto de mundo sob (mx,my) fixo na tela.
    void zoomAt(double factor, double mxPix, double myPix) {
        double wx, wy; screenToWorld(mxPix, myPix, wx, wy);
        m_pxPerUnit = std::clamp(m_pxPerUnit * factor, 1e-6, 1e9);
        double nx, ny; screenToWorld(mxPix, myPix, nx, ny);
        m_cx += wx - nx;
        m_cy += wy - ny;
    }

    void screenToWorld(double px, double py, double& wx, double& wy) const {
        wx = m_cx + (px - m_w * 0.5) / m_pxPerUnit;
        wy = m_cy - (py - m_h * 0.5) / m_pxPerUnit;
    }

    // Inverso de screenToWorld (posicionar widgets sobre pontos do mundo).
    void worldToScreen(double wx, double wy, double& px, double& py) const {
        px = m_w * 0.5 + (wx - m_cx) * m_pxPerUnit;
        py = m_h * 0.5 - (wy - m_cy) * m_pxPerUnit;
    }

    // Enquadra a caixa [minx,miny]-[maxx,maxy] com uma folga de 10%.
    void fit(double minx, double miny, double maxx, double maxy) {
        m_cx = (minx + maxx) * 0.5;
        m_cy = (miny + maxy) * 0.5;
        const double dx = std::max(maxx - minx, 1e-6);
        const double dy = std::max(maxy - miny, 1e-6);
        m_pxPerUnit = std::min(m_w / (dx * 1.1), m_h / (dy * 1.1));
        if (m_pxPerUnit <= 0.0) m_pxPerUnit = 1.0;
    }

    // Estado da câmera (para histórico de vista / Zoom Anterior).
    struct State { double cx, cy, pxPerUnit; };
    State state() const { return {m_cx, m_cy, m_pxPerUnit}; }
    void  setState(const State& s) { m_cx = s.cx; m_cy = s.cy; m_pxPerUnit = s.pxPerUnit; }

    // Retângulo de mundo atualmente visível.
    void visibleRect(double& minx, double& miny, double& maxx, double& maxy) const {
        const double hw = (m_w * 0.5) / m_pxPerUnit;
        const double hh = (m_h * 0.5) / m_pxPerUnit;
        minx = m_cx - hw; maxx = m_cx + hw;
        miny = m_cy - hh; maxy = m_cy + hh;
    }

private:
    int    m_w{1}, m_h{1};
    double m_cx{0.0}, m_cy{0.0};
    double m_pxPerUnit{1.0};
};

} // namespace cad
