// src/zendo/PlantImport.hpp
// R3 — o CONECTOR planta→3D. É o ÚNICO lugar do Zendo que entende o mundo
// 2D do ZenCAD (entidades, camadas, paredes); o coração do app recebe um
// pacote NEUTRO. Os irmãos do ecossistema conversam por ARQUIVOS .zencad;
// aqui o cadcore 2D é usado como biblioteca de FORMATO, não como fundação.
#pragma once
#include <vector>

namespace cad { class DrawingManager; }

struct PlantScene {
    struct Box {                        // trecho de parede/verga/peitoril
        double l0[2], l1[2], r0[2], r1[2];
        double z0, z1;
        int wallNo;
    };
    std::vector<Box> boxes;
    std::vector<float> groundLines;     // trincas x,y,z do desenho no chão
    int wallCount{0};
};

// Extruda as paredes (com vãos de porta/janela) e tessela o linework 2D.
PlantScene importPlant(const cad::DrawingManager& doc, double wallHeight);
