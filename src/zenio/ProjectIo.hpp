// src/zenio/ProjectIo.hpp
// Formato de PROJETO do ZenCAD (.zencad): um único arquivo JSON com TUDO que
// o documento carrega — entidades (todos os tipos), camadas (com estado),
// biblioteca de blocos (com atributos), estilos de cota/texto, pranchas e
// viewports (Paper Space) e configurações (LTSCALE/unidades/camada corrente).
// O DXF continua existindo para INTEROP; o .zencad é a persistência fiel.
#pragma once
#include <QString>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>

#include "core/document/DrawingManager.hpp"
#include "core/document/Style.hpp"
#include "core/layout/Layout.hpp"
#include "core/document/PlotStyle.hpp"

namespace cad {

// Configurações do projeto que vivem fora do DrawingManager (UI/render).
struct ProjectSettings {
    double  ltScale{1.0};        // escala global dos tipos de linha
    int     unitIndex{0};        // índice da unidade (mm/cm/dm/m/km/pol/pé/—)
    int     unitDecimals{2};     // casas decimais de exibição
    QString unitSuffix;          // sufixo de exibição (ex.: " mm")
    QString currentLayer{"0"};   // camada corrente
    PlotStyleTable plotStyle;    // estilos de plotagem (CTB) do projeto
};

// PlotStyleTable <-> JSON — usado pelo .zencad e pelo import/export .zctb.
QJsonObject     plotStyleToJson(const PlotStyleTable& t);
PlotStyleTable  plotStyleFromJson(const QJsonValue& v);

// Salva o projeto inteiro em `path` (JSON .zencad). `thumbnail` (opcional) é
// embutido em PNG/base64 — usado pelos cards da tela inicial. Retorna false e
// preenche `err` (se fornecido) em caso de falha de E/S.
bool saveProject(const QString& path,
                 const DrawingManager& doc,
                 const LayoutTable& layouts,
                 const StyleTable& styles,
                 const ProjectSettings& settings,
                 QString* err = nullptr,
                 const QImage* thumbnail = nullptr);

// Lê SÓ a miniatura embutida de um .zencad (rápido o bastante p/ a tela
// inicial). Retorna QImage nula se o arquivo não tiver/for inválido.
QImage projectThumbnail(const QString& path);

// Carrega o projeto de `path` SUBSTITUINDO o conteúdo atual: o chamador deve
// tratar o documento como novo após o retorno (o load faz doc.clearAll()).
// Retorna false e preenche `err` em caso de falha de E/S ou formato inválido.
bool loadProject(const QString& path,
                 DrawingManager& doc,
                 LayoutTable& layouts,
                 StyleTable& styles,
                 ProjectSettings& settings,
                 QString* err = nullptr);

} // namespace cad
