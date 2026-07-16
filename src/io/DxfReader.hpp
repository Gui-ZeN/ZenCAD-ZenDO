// src/io/DxfReader.hpp
#pragma once
#include <string>

namespace cad {

class DrawingManager;  // fwd — o documento que receberá as entidades lidas

// ============================================================================
//  DxfReader — leitor de DXF ASCII mínimo.
//
//  Lê um arquivo DXF no formato texto (par código/valor, uma linha cada) e
//  ADICIONA ao documento as entidades encontradas na seção ENTITIES.
//
//  Entidades suportadas:
//    * LINE       — 10/20 (início), 11/21 (fim)
//    * CIRCLE     — 10/20 (centro), 40 (raio)
//    * ARC        — 10/20 (centro), 40 (raio), 50/51 (ângulos em graus → rad)
//    * LWPOLYLINE — vários 10/20 (vértices), 70 bit 1 = fechada
//    * TEXT       — 10/20 (posição), 40 (altura), 1 (texto), 50 (rotação em graus)
//    * ELLIPSE    — 10/20 (centro), 11/21 (vetor eixo maior), 40 (ratio b/a),
//                   41/42 (intervalo paramétrico t0/t1; completa se t1-t0 == 2π)
//    * DIMENSION  — 10/20=p1, 11/21=p2, 12/22=p3, 40=altura, 70=DimKind
//                   (0=Linear,1=Aligned,2=Radius,3=Diameter,4=Angular)
//    * HATCH      — 70=padrão (0=Lines,1=ANSI31,2=ANSI37,3=Grid,4=Solid),
//                   52=ângulo(graus), 41=escala, 91=nº loops, 92=nº vértices do
//                   loop seguido dos 10/20 de cada vértice
//
//  Estes três (ELLIPSE/DIMENSION/HATCH) espelham EXATAMENTE o DxfWriter, dando
//  round-trip fiel ao reabrir no próprio CadCore.
//
//  Código 8 = nome da camada: a entidade recebe setLayer(nome) e, se a camada
//  ainda não existir no documento, ela é criada.
//
//  Robustez: tipos de entidade desconhecidos e códigos não usados são ignorados;
//  valores numéricos malformados são tratados sem lançar exceção.
//  Ângulos de ARC/TEXT são convertidos de graus para radianos.
// ============================================================================

// Lê o DXF ASCII em `path` e adiciona as entidades ao documento `doc`.
// Retorna o número de entidades adicionadas, ou -1 se o arquivo não puder ser aberto.
int readDxf(const std::string& path, DrawingManager& doc);

} // namespace cad
