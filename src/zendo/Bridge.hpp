// src/zendo/Bridge.hpp
// A PONTE DE VOLTA (F4): o Zendo projeta o modelo 3D e devolve DESENHO 2D
// pro ZenCAD, como arquivo .zencad de verdade.
//   * ELEVAÇÃO (N/S/L/O): projeção ortográfica com REMOÇÃO DE LINHAS OCULTAS
//     por amostragem — cada aresta é amostrada e cada amostra lança um raio
//     de volta ao observador contra todos os triângulos do modelo; só os
//     trechos desobstruídos viram linhas (camada ARESTAS).
//   * CORTE (plano X ou Y + sentido do olhar): o plano fatia cada sólido —
//     os laços de interseção viram POLILINHAS FECHADAS + HACHURA SÓLIDA
//     (camada CORTE, a convenção de parede cortada) e o que fica ALÉM do
//     plano entra como fundo projetado (mesma visibilidade, ocluidores e
//     arestas recortados no plano).
#pragma once
#include <QString>
#include <vector>

#include "core/mesh/HalfEdgeMesh.hpp"

namespace bridge {

struct Result {
    bool ok{false};
    int  cutLoops{0};       // laços de corte fechados emitidos
    int  edgeSegs{0};       // segmentos de aresta visíveis emitidos
    QString error;
};

// nsew: 'S' 'N' 'L' 'O' (vista DE onde se olha).
Result exportElevation(const std::vector<const cad::HalfEdgeMesh*>& meshes,
                       char nsew, const QString& outPath);

// axis: 'X' ou 'Y' (plano axis = pos). lookSign: +1 olha no sentido positivo
// do eixo (mantém o material além do plano), -1 o contrário.
Result exportSection(const std::vector<const cad::HalfEdgeMesh*>& meshes,
                     char axis, double pos, int lookSign,
                     const QString& outPath);

// R31: corte por plano ARBITRÁRIO (n, d), a mesma convenção do clip visual
// (descarta dot(P,n) > d) — o desenho mostra o material MANTIDO.
Result exportSectionPlane(const std::vector<const cad::HalfEdgeMesh*>& meshes,
                          const cad::Vec3& n, double d,
                          const QString& outPath);

} // namespace bridge
