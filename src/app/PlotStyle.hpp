// src/app/PlotStyle.hpp
// Estilos de plotagem nomeados (o "CTB" do ZenCAD, por CAMADA): o que cada
// camada vira no PAPEL — plota ou não, cor de saída e espessura forçada.
// A tabela ativa é aplicada no pipeline de plot (Plotar/PUBLISH/preview),
// embutida no .zencad e importável/exportável como .zctb (JSON) para
// compartilhar entre projetos.
#pragma once
#include <string>
#include <vector>

#include "core/geometry/Properties.hpp"

namespace cad {

struct PlotLayerStyle {
    std::string layer;
    bool   plot{true};          // false = a camada NÃO sai no papel
    int    colorMode{0};        // 0=como desenhado · 1=preto · 2=tons de cinza · 3=cor fixa
    Rgba   color{0, 0, 0, 255}; // usada quando colorMode==3
    double lineWeightMm{-1.0};  // >= 0 força a espessura no papel (herda se < 0)
};

struct PlotStyleTable {
    std::string name{"Padrao"};
    bool active{false};                  // aplicar ao plotar/visualizar
    std::vector<PlotLayerStyle> entries;

    const PlotLayerStyle* find(const std::string& layer) const {
        for (const PlotLayerStyle& e : entries)
            if (e.layer == layer) return &e;
        return nullptr;
    }
};

} // namespace cad
