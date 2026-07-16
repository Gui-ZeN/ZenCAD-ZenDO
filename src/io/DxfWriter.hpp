// src/io/DxfWriter.hpp
#pragma once
#include <string>

namespace cad {

class DrawingManager;  // fwd — o documento a ser exportado

// ============================================================================
//  DxfWriter — exportador para DXF ASCII mínimo (porém válido).
//
//  Gera um arquivo DXF no formato texto, compatível com AutoCAD/LibreCAD,
//  contendo:
//    * seção TABLES com uma tabela LAYER (uma entrada por camada do documento);
//    * seção ENTITIES com cada entidade suportada do documento.
//
//  Entidades suportadas: Line, Circle, Arc, Polyline (LWPOLYLINE), MText (TEXT),
//  Ellipse, Dimension e Hatch. Todas têm round-trip com o DxfReader (o que o
//  writer escreve, o reader relê e reconstrói).
//
//  Coordenadas em precisão dupla; ângulos convertidos de radianos para graus
//  conforme exige o formato DXF.
// ============================================================================

// Grava o documento em `path` no formato DXF ASCII.
// Retorna true em caso de sucesso, false se o arquivo não pôde ser aberto.
bool writeDxf(const DrawingManager& doc, const std::string& path);

} // namespace cad
