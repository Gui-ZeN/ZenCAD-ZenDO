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
    // R55: natureza não se repete — cada inserção gira e muda de porte.
    // Só pra vegetação: sofá em fila torto seria defeito, árvore em fila
    // idêntica é que é. Custo: instância jitterada não recebe redefinição
    // (redefineComponent compara dims e a mantém como está) — aceito, é o
    // mesmo contrato de qualquer instância que o usuário girou na mão.
    bool organico{false};
};

QVector<Movel> gerarTodos();                // ordem curada (a da paleta)

} // namespace moblib
