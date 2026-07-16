// src/zendo/FurnitureLib.hpp
// R14: O MOBILIÁRIO — componentes paramétricos gerados pelo próprio Zendo
// (porta, janela, mesa, sofá, cama, estante…), no mesmo espírito da
// MaterialLib: sem download, nascem prontos na biblioteca de componentes.
// Cada móvel é UMA HalfEdgeMesh com várias caixas (cascas fechadas
// desconexas — manifold vale por casca) + cores por face da paleta do app.
#pragma once
#include "core/mesh/HalfEdgeMesh.hpp"

#include <QString>
#include <QVector>
#include <array>
#include <map>

namespace moblib {

struct Movel {
    QString nome;
    cad::HalfEdgeMesh mesh;                 // base em z=0, dims reais (m)
    std::map<cad::HalfEdgeMesh::Idx, std::array<float, 3>> cores;
};

QVector<Movel> gerarTodos();                // ordem curada (a da paleta)

} // namespace moblib
