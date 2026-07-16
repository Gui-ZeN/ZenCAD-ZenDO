// src/app/LayersPanel.cpp
#include "app/LayersPanel.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QFrame>
#include <QScrollArea>
#include <QComboBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>

#include "core/geometry/LinePattern.hpp"

namespace cad {

LayersPanel::LayersPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(250);   // espaço p/ swatch + visibilidade + nome + tipo de linha
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(6);

    auto* newBtn = new QPushButton("+  Nova camada", this);
    newBtn->setCursor(Qt::PointingHandCursor);
    newBtn->setStyleSheet(
        "QPushButton{padding:6px; font-weight:bold; background:#1a1e27; color:#e8eaed;"
        " border:1px solid rgba(255,255,255,0.06); border-radius:6px;}"
        "QPushButton:hover{border:1px solid #c2a063;}");
    connect(newBtn, &QPushButton::clicked, this, &LayersPanel::onNewLayer);
    outer->addWidget(newBtn);

    auto* showAllBtn = new QPushButton("Mostrar todas as camadas", this);
    showAllBtn->setCursor(Qt::PointingHandCursor);
    showAllBtn->setToolTip("Religa e descongela todas as camadas (reverte o isolar)");
    showAllBtn->setStyleSheet(
        "QPushButton{padding:5px; background:#12151c; color:#c8c8c8;"
        " border:1px solid rgba(255,255,255,0.06); border-radius:6px;}"
        "QPushButton:hover{border:1px solid #c2a063; color:#e8eaed;}");
    connect(showAllBtn, &QPushButton::clicked, this, [this] { emit showAllRequested(); });
    outer->addWidget(showAllBtn);

    auto* purgeBtn = new QPushButton("Limpar não usadas (purge)", this);
    purgeBtn->setCursor(Qt::PointingHandCursor);
    purgeBtn->setToolTip("Remove as camadas sem entidades (exceto a 0 e a corrente)");
    purgeBtn->setStyleSheet(showAllBtn->styleSheet());
    connect(purgeBtn, &QPushButton::clicked, this, [this] { emit purgeRequested(); });
    outer->addWidget(purgeBtn);

    auto* statesBtn = new QPushButton("Estados de camada...", this);
    statesBtn->setCursor(Qt::PointingHandCursor);
    statesBtn->setToolTip("Salvar/aplicar snapshots nomeados do estado de todas as camadas");
    statesBtn->setStyleSheet(showAllBtn->styleSheet());
    connect(statesBtn, &QPushButton::clicked, this, [this] { emit layerStatesRequested(); });
    outer->addWidget(statesBtn);

    // Busca: filtra as linhas por substring do nome (case-insensitive).
    auto* search = new QLineEdit(this);
    search->setPlaceholderText("Buscar camada...");
    search->setClearButtonEnabled(true);
    search->setStyleSheet(
        "QLineEdit{padding:5px; background:#12151c; color:#e8eaed;"
        " border:1px solid rgba(255,255,255,0.06); border-radius:6px;}"
        "QLineEdit:focus{border:1px solid #c2a063;}");
    connect(search, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_filter = t.trimmed();
        rebuildRows();
    });
    outer->addWidget(search);

    // Linhas de camada numa área rolável.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    m_rows = new QVBoxLayout(host);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(2);
    m_rows->addStretch(1);
    scroll->setWidget(host);
    outer->addWidget(scroll, 1);
}

void LayersPanel::setLayers(const QVector<LayerInfo>& layers) {
    m_all = layers;      // fonte p/ o filtro de busca
    rebuildRows();
}

void LayersPanel::rebuildRows() {
    // Remove as linhas antigas (recria a lista inteira).
    while (m_rows->count() > 0) {
        QLayoutItem* it = m_rows->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }

    for (const LayerInfo& L : m_all) {
        if (!m_filter.isEmpty() && !L.name.contains(m_filter, Qt::CaseInsensitive))
            continue;    // fora da busca
        const QString nm = L.name;

        auto* row = new QFrame();
        row->setStyleSheet(L.current
            ? "QFrame{background:rgba(79,140,255,0.15); border:1px solid #c2a063; border-radius:6px;}"
            : "QFrame{background:#12151c; border:1px solid transparent; border-radius:6px;}"
              "QFrame:hover{background:#222836;}");
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(6, 4, 6, 4);
        h->setSpacing(8);

        // Menu de contexto da linha: renomear / excluir (a "0" é intocável).
        if (nm != "0") {
            row->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(row, &QFrame::customContextMenuRequested, this, [this, nm, row](const QPoint& p) {
                QMenu menu(row);
                QAction* aRen = menu.addAction("Renomear...");
                QAction* aDel = menu.addAction("Excluir");
                QAction* aTr  = menu.addAction("Transparência...");
                QAction* sel = menu.exec(row->mapToGlobal(p));
                if (sel == aTr) {
                    emit transparencyRequested(nm);
                } else if (sel == aRen) {
                    bool ok = false;
                    const QString novo = QInputDialog::getText(
                        this, "Renomear camada", QString("Novo nome para \"%1\":").arg(nm),
                        QLineEdit::Normal, nm, &ok);
                    if (ok && !novo.trimmed().isEmpty() && novo.trimmed() != nm)
                        emit renameRequested(nm, novo.trimmed());
                } else if (sel == aDel) {
                    emit deleteRequested(nm);
                }
            });
        }

        // Amostra de cor (clicável → abre seletor de cor).
        auto* sw = new QPushButton(row);
        sw->setFixedSize(20, 20);
        sw->setCursor(Qt::PointingHandCursor);
        sw->setToolTip("Cor da camada");
        sw->setStyleSheet(QString("QPushButton{background:%1; border:1px solid rgba(255,255,255,0.12);"
                                  " border-radius:4px;}").arg(L.color.name()));
        connect(sw, &QPushButton::clicked, this, [this, nm] { emit colorClicked(nm); });
        h->addWidget(sw);

        // Visibilidade (ON/OFF = lâmpada).
        auto* vis = new QPushButton(L.visible ? "💡" : "🌑", row);
        vis->setCheckable(true);
        vis->setChecked(L.visible);
        vis->setFixedSize(24, 22);
        vis->setToolTip("Ligar/desligar (ON/OFF)");
        vis->setCursor(Qt::PointingHandCursor);
        vis->setStyleSheet("QPushButton{background:transparent; border:none;}"
                           "QPushButton:hover{background:#222836; border-radius:4px;}");
        connect(vis, &QPushButton::toggled, this, [this, nm, vis](bool on) {
            vis->setText(on ? "💡" : "🌑");
            emit visibilityToggled(nm, on);
        });
        h->addWidget(vis);

        // Congelar (FREEZE = floco).
        auto* frz = new QPushButton(L.frozen ? "❄" : "○", row);
        frz->setCheckable(true);
        frz->setChecked(L.frozen);
        frz->setFixedSize(24, 22);
        frz->setToolTip("Congelar/descongelar (FREEZE)");
        frz->setCursor(Qt::PointingHandCursor);
        frz->setStyleSheet("QPushButton{background:transparent; border:none; color:#8b92a8;}"
                           "QPushButton:checked{color:#c2a063;}"
                           "QPushButton:hover{background:#222836; border-radius:4px;}");
        connect(frz, &QPushButton::toggled, this, [this, nm, frz](bool on) {
            frz->setText(on ? "❄" : "○");
            emit freezeToggled(nm, on);
        });
        h->addWidget(frz);

        // Isolar (liga só esta camada).
        auto* iso = new QPushButton("◉", row);
        iso->setFixedSize(24, 22);
        iso->setToolTip("Isolar (ligar só esta camada)");
        iso->setCursor(Qt::PointingHandCursor);
        iso->setStyleSheet("QPushButton{background:transparent; border:none; color:#8b92a8;}"
                           "QPushButton:hover{background:#222836; border-radius:4px; color:#e8eaed;}");
        connect(iso, &QPushButton::clicked, this, [this, nm] { emit isolateRequested(nm); });
        h->addWidget(iso);

        // Nome (clicável → torna corrente). Botão plano = clique confiável.
        auto* name = new QPushButton((L.current ? "● " : "") + L.name, row);
        name->setFlat(true);
        name->setCursor(Qt::PointingHandCursor);
        name->setToolTip("Tornar camada corrente");
        name->setStyleSheet(QString(
            "QPushButton{border:none; background:transparent; text-align:left; padding:2px;"
            " font-family:'Consolas','Courier New',monospace; color:%1; %2}"
            "QPushButton:hover{color:#c2a063;}")
            .arg(L.current ? "#c2a063" : "#e8eaed",
                 L.current ? "font-weight:bold;" : ""));
        connect(name, &QPushButton::clicked, this, [this, nm] { emit currentLayerChanged(nm); });
        h->addWidget(name, 1);

        // Tipo de linha (combo). Define o índice ANTES de conectar p/ não disparar.
        auto* lt = new QComboBox(row);
        lt->addItem("Contínua",  "CONTINUOUS");
        lt->addItem("Tracejada", "DASHED");
        lt->addItem("Centro",    "CENTER");
        lt->addItem("Oculta",    "HIDDEN");
        for (const std::string& cn : customLineTypeNames())   // tipos custom do usuário
            lt->addItem(QString::fromStdString(cn), QString::fromStdString(cn));
        lt->setToolTip("Tipo de linha");
        lt->setStyleSheet("QComboBox{background:#1a1e27; color:#e8eaed; border:1px solid rgba(255,255,255,0.06);"
                          " border-radius:6px; padding:1px 4px;}"
                          "QComboBox:hover{border:1px solid rgba(255,255,255,0.12);}"
                          "QComboBox QAbstractItemView{background:#12151c; color:#e8eaed;"
                          " selection-background-color:#c2a063; selection-color:#0b0d10;}");
        int li = lt->findData(L.linetype.isEmpty() ? QString("CONTINUOUS") : L.linetype);
        lt->setCurrentIndex(li < 0 ? 0 : li);
        connect(lt, &QComboBox::currentIndexChanged, this,
                [this, nm, lt](int) { emit linetypeChanged(nm, lt->currentData().toString()); });
        h->addWidget(lt);

        // Espessura (lineweight) por camada.
        auto* lw = new QComboBox(row);
        lw->addItem("Padrão", -1.0);
        for (double mm : {0.15, 0.25, 0.35, 0.50, 0.70, 1.00, 2.00})
            lw->addItem(QString::number(mm, 'f', 2), mm);
        lw->setToolTip("Espessura (mm) — pode digitar um valor custom");
        lw->setStyleSheet(lt->styleSheet());
        lw->setEditable(true);                        // permite mm custom
        lw->setInsertPolicy(QComboBox::NoInsert);
        int wi = lw->findData(L.lineweight);
        if (wi >= 0) lw->setCurrentIndex(wi);
        else if (L.lineweight > 0) lw->setEditText(QString::number(L.lineweight, 'f', 2));
        else lw->setCurrentIndex(0);
        auto emitLw = [this, nm, lw] {                // valor digitado tem prioridade
            bool ok = false; const double v = lw->currentText().toDouble(&ok);
            emit lineweightChanged(nm, (ok && v > 0.0) ? v : lw->currentData().toDouble());
        };
        connect(lw, &QComboBox::currentIndexChanged, this, [emitLw](int) { emitLw(); });
        connect(lw->lineEdit(), &QLineEdit::editingFinished, this, emitLw);
        h->addWidget(lw);

        // Cadeado (travar/destravar): travada = visível mas não editável/selecionável.
        auto* lock = new QPushButton(L.locked ? "🔒" : "🔓", row);
        lock->setCheckable(true);
        lock->setChecked(L.locked);
        lock->setFixedSize(24, 22);
        lock->setCursor(Qt::PointingHandCursor);
        lock->setToolTip("Travar camada");
        lock->setStyleSheet("QPushButton{background:transparent; border:none;}"
                            "QPushButton:hover{background:#222836; border-radius:4px;}");
        connect(lock, &QPushButton::toggled, this, [this, nm, lock](bool on) {
            lock->setText(on ? "🔒" : "🔓");
            emit lockToggled(nm, on);
        });
        h->addWidget(lock);

        m_rows->addWidget(row);
    }
    m_rows->addStretch(1);
}

void LayersPanel::onNewLayer() {
    bool ok = false;
    const QString n = QInputDialog::getText(this, "Nova camada", "Nome da camada:",
                                            QLineEdit::Normal, QString(), &ok);
    if (ok && !n.trimmed().isEmpty()) emit newLayerRequested(n.trimmed());
}

} // namespace cad
