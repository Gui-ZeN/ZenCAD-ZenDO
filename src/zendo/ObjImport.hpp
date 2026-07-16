// src/zendo/ObjImport.hpp
// R15: IMPORT OBJ — o espelho do exportador. Lê v/f (índices negativos,
// quads/polígonos, "i/j/k") + .mtl (Kd por usemtl vira cor de face), SOLDA
// vértices por posição (1e-6 — sopas de triângulo viram malha conectada) e
// tolera winding inconsistente (retenta o laço invertido; conta descartes).
// A malha sai CRUA (sem escala/eixo) — quem chama aplica scaleAbout/rotateAxis.
#pragma once
#include "core/mesh/HalfEdgeMesh.hpp"

#include <QString>
#include <array>
#include <map>

namespace objimp {

struct Resultado {
    cad::HalfEdgeMesh mesh;
    std::map<cad::HalfEdgeMesh::Idx, std::array<float, 3>> cores;
    int faces{0};                       // aceitas
    int descartadas{0};                 // rejeitadas mesmo invertidas
};

bool importar(const QString& path, Resultado& out, QString* erro);

} // namespace objimp
