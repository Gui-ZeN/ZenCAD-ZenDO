// src/app/LayersPanel.hpp
#pragma once
#include <QWidget>
#include <QVector>
#include <QString>
#include <QColor>

class QVBoxLayout;

namespace cad {

// Painel de camadas estilo AutoCAD. É um widget pronto para ser colocado dentro
// de um QDockWidget (o dock é criado por quem instancia). Cada linha mostra um
// quadradinho de cor clicável, um checkbox de visibilidade e o nome; a camada
// corrente recebe destaque visual. Não conhece o documento nem o MainWindow —
// quem instancia conecta os sinais (ex.: abrir QColorDialog em colorClicked).
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    // Descrição de uma camada exibida na lista.
    struct LayerInfo {
        QString name;
        QColor  color;
        bool    visible{true};            // ON/OFF (lâmpada)
        bool    frozen{false};            // congelada (floco)
        bool    current{false};
        QString linetype{"CONTINUOUS"};   // CONTINUOUS/DASHED/CENTER/HIDDEN
        bool    locked{false};            // travada: visível mas não editável
        double  lineweight{-1.0};         // mm; -1 = padrão (fino)
    };

    explicit LayersPanel(QWidget* parent = nullptr);

    // Reconstrói a lista inteira a partir das camadas informadas.
    void setLayers(const QVector<LayerInfo>& layers);

signals:
    // A camada corrente mudou (clique simples numa linha).
    void currentLayerChanged(const QString& name);
    // O checkbox de visibilidade (ON/OFF) de uma camada foi alternado.
    void visibilityToggled(const QString& name, bool visible);
    // O floco (congelar/descongelar) de uma camada foi alternado.
    void freezeToggled(const QString& name, bool frozen);
    // Pedido de isolar (ligar só esta camada, desligar as outras).
    void isolateRequested(const QString& name);
    // Pedido de religar todas as camadas (reverter isolamento).
    void showAllRequested();
    // O quadradinho de cor foi clicado; o chamador abre o QColorDialog.
    void colorClicked(const QString& name);
    // O tipo de linha da camada foi alterado (valor canônico: CONTINUOUS/DASHED/...).
    void linetypeChanged(const QString& name, const QString& linetype);
    // O cadeado (travar/destravar) da camada foi alternado.
    void lockToggled(const QString& name, bool locked);
    // A espessura (lineweight, em mm; -1 = padrão) da camada foi alterada.
    void lineweightChanged(const QString& name, double mm);
    // O botão "Nova camada" foi confirmado com um nome.
    void newLayerRequested(const QString& name);
    // Renomear pelo menu de contexto da linha (novo nome já perguntado aqui).
    void renameRequested(const QString& oldName, const QString& newName);
    // Excluir pelo menu de contexto da linha.
    void deleteRequested(const QString& name);
    // Botão "Limpar não usadas" (purge de camadas vazias).
    void purgeRequested();
    // Transparência da camada (0..90%), pedida pelo menu de contexto.
    void transparencyRequested(const QString& name);
    // Botão "Estados de camada..." (salvar/aplicar snapshots nomeados).
    void layerStatesRequested();

private:
    void onNewLayer();
    void rebuildRows();             // reaplica o filtro de busca sobre m_all

    QVBoxLayout* m_rows{nullptr};   // container das linhas de camada
    QVector<LayerInfo> m_all;       // última lista completa (fonte do filtro)
    QString m_filter;               // busca (substring, case-insensitive)
};

} // namespace cad
