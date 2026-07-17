// src/app/MainWindow.cpp
#include "app/MainWindow.hpp"
#include "app/ViewportWidget.hpp"
#include "app/StatusBar.hpp"
#include "app/ToolIcons.hpp"
#include "app/LayersPanel.hpp"

#include <QColorDialog>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QFont>
#include <QSettings>
#include <QFileInfo>

#include "zenio/ProjectIo.hpp"
#include "app/StartPage.hpp"
#include <QStackedWidget>
#include <QPixmap>
#include <QIcon>
#include <QPdfWriter>
#include <QShortcut>
#include <QSize>
#include <QVector>
#include <algorithm>

#include "io/DxfWriter.hpp"
#include "io/DxfReader.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/geometry/LinePattern.hpp"
#include "app/CommandTable.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/document/LayerTable.hpp"
#include "core/document/Layer.hpp"
#include "core/math/AABB.hpp"
#include <QMenu>
#include <QMenuBar>
#include <cmath>
#include <cstdint>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QFrame>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QFontComboBox>
#include <QSet>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
#include <QJsonDocument>

#include "core/interaction/ToolController.hpp"
#include "core/command/commands/ReplaceCmd.hpp"
#include "core/geometry/MText.hpp"
#include "core/geometry/Dimension.hpp"
#include "core/command/commands/MacroCmd.hpp"

#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/BlockRef.hpp"
#include "core/spatial/Quadtree.hpp"
#include "app/TtfTextProvider.hpp"
#include "core/command/commands/AddEntityCmd.hpp"
#include "core/cli/CommandParser.hpp"

namespace cad {

namespace {
// Item da ribbon: ação + rótulo CURTO (não corta no botão de 60px).
struct RibBtn { QAction* act; const char* label; };

// Painel da ribbon: fileira de botões rotulados (ícone + texto) + título.
// Registra cada botão em `out` para os flyouts (setinha de variantes) depois.
QWidget* makeRibbonPanel(const QString& title, const QList<RibBtn>& items,
                         QHash<QAction*, QToolButton*>& out) {
    auto* panel = new QFrame;
    auto* v = new QVBoxLayout(panel);
    v->setContentsMargins(9, 5, 9, 3);
    v->setSpacing(3);
    auto* row = new QHBoxLayout;
    row->setSpacing(1);
    for (const RibBtn& it : items) {
        auto* b = new QToolButton;
        b->setDefaultAction(it.act);                     // ícone/tooltip/checked da ação
        b->setText(QString::fromUtf8(it.label));         // rótulo curto sobrepõe o longo
        const bool hasIcon = !it.act->icon().isNull();
        b->setToolButtonStyle(hasIcon ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextOnly);
        b->setIconSize(QSize(22, 22));
        b->setAutoRaise(true);
        b->setFixedSize(60, 54);
        b->setFocusPolicy(Qt::NoFocus);
        out.insert(it.act, b);
        row->addWidget(b);
    }
    v->addLayout(row);
    auto* lbl = new QLabel(title);
    lbl->setObjectName("ribbonPanelTitle");
    lbl->setAlignment(Qt::AlignHCenter);
    v->addWidget(lbl);
    return panel;
}
QWidget* ribbonSeparator() {
    auto* s = new QFrame;
    s->setObjectName("ribbonSep");
    s->setFixedWidth(1);
    s->setFrameShape(QFrame::NoFrame);
    return s;
}
// Quadradinho de cor (swatch) para a lista de camadas.
QIcon layerSwatch(const QColor& c) {
    QPixmap pm(14, 14); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(c); p.setPen(QColor(0, 0, 0, 70));
    p.drawRoundedRect(1, 1, 12, 12, 3, 3); p.end();
    return QIcon(pm);
}
} // namespace

MainWindow::MainWindow(std::unique_ptr<DrawingManager> doc, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("ZenCAD");

    installQtTtfProvider();   // fonte TTF de verdade p/ MText (antes do 1º render)

    // Sessão 0 (multi-doc): o documento recebido vira a primeira aba.
    {
        auto first = std::make_unique<DocSession>();
        first->doc = std::move(doc);
        m_sessions.push_back(std::move(first));
        m_curSession = 0;
        m_doc = m_sessions[0]->doc.get();
    }

    m_view = new ViewportWidget(m_doc, this);
    m_sessions[0]->view = m_view;

    // Abas de ARQUIVO (multi-documento) + pilha de viewports (1 por sessão).
    m_fileTabs = new QTabBar(this);
    m_fileTabs->setObjectName("fileTabs");
    m_fileTabs->setExpanding(false);
    m_fileTabs->setDrawBase(false);
    m_fileTabs->setFocusPolicy(Qt::NoFocus);
    // Aba 0 = "Início" (a tela inicial É uma aba, como no AutoCAD); as sessões
    // de documento começam na aba 1 (tab = sessão + 1). Início não fecha; o
    // "✕" das demais é um botão PRÓPRIO no estilo do app (makeTabCloseButton).
    m_fileTabs->addTab("⌂ Início");
    m_fileTabs->addTab("Sem título");
    m_fileTabs->setTabButton(1, QTabBar::RightSide, makeTabCloseButton());
    m_fileTabs->setCurrentIndex(1);
    connect(m_fileTabs, &QTabBar::currentChanged, this, [this](int i) {
        if (m_fileTabsBusy) return;
        if (i == 0) {
            showStartPage();
        } else {
            switchToDoc(i - 1);
            showDrawing();
        }
    });
    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_view);

    // Abas de espaço (Paper Space): [ Modelo | Prancha… | + ] abaixo do canvas.
    m_layoutTabs = new QTabBar(this);
    m_layoutTabs->setObjectName("layoutTabs");
    m_layoutTabs->setExpanding(false);
    m_layoutTabs->setDrawBase(false);
    m_layoutTabs->setFocusPolicy(Qt::NoFocus);
    // Linha de comando COMPACTA e integrada (histórico de ~2 linhas sempre
    // visível + campo de comando), logo abaixo do canvas — as abas de espaço
    // (Modelo/Prancha) ficam DEPOIS dela, rente à status bar.
    auto* cliPanel = new QWidget(this);
    auto* cliLay = new QVBoxLayout(cliPanel);
    cliLay->setContentsMargins(6, 2, 6, 3);
    cliLay->setSpacing(2);
    m_log = new QPlainTextEdit(cliPanel);
    m_log->setObjectName("cliLog");
    m_log->setReadOnly(true);
    m_log->setFixedHeight(44);                 // ~2 linhas (role p/ ver mais)
    m_log->setFrameShape(QFrame::NoFrame);
    m_log->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_cli = new QLineEdit(cliPanel);
    m_cli->setObjectName("cliInput");
    m_cli->setPlaceholderText("Comando  (L=Linha · C=Círculo · TR=Aparar · F=Fillet · O=Offset…)");
    cliLay->addWidget(m_log);
    cliLay->addWidget(m_cli);

    auto* central = new QWidget(this);
    auto* cv = new QVBoxLayout(central);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
    cv->addWidget(m_viewStack, 1);
    cv->addWidget(cliPanel, 0);
    cv->addWidget(m_layoutTabs, 0);

    // Pilha: [0] página inicial (Start) · [1] área de desenho. A Start abre
    // como PÁGINA cheia (não um pop-up), estilo aba Start do AutoCAD.
    m_stack = new QStackedWidget(this);
    m_startPage = new StartPage(this);
    m_drawingPage = central;
    m_stack->addWidget(m_startPage);
    m_stack->addWidget(m_drawingPage);
    // Casca: abas de ARQUIVO em cima (visíveis também na aba Início) + botão
    // "+" (novo desenho) + pilha Início/desenho embaixo.
    auto* tabsRow = new QWidget(this);
    tabsRow->setObjectName("fileTabsRow");
    tabsRow->setStyleSheet(
        "QToolButton#tabClose{background:transparent; color:#8a8f96; border:none;"
        " border-radius:8px; font-size:13px; font-weight:bold; padding:0;}"
        "QToolButton#tabClose:hover{background:#c2a063; color:#16181c;}"
        "QToolButton#fileTabPlus{background:transparent; color:#8a8f96; border:none;"
        " border-radius:5px; font-size:15px; min-width:24px; min-height:22px; padding:0;}"
        "QToolButton#fileTabPlus:hover{background:#222836; color:#c2a063;}");
    auto* trLay = new QHBoxLayout(tabsRow);
    trLay->setContentsMargins(0, 0, 0, 0);
    trLay->setSpacing(2);
    trLay->addWidget(m_fileTabs, 0);
    auto* btnNewTab = new QToolButton(tabsRow);
    btnNewTab->setObjectName("fileTabPlus");
    btnNewTab->setText("+");
    btnNewTab->setToolTip("Novo desenho (Ctrl+N)");
    btnNewTab->setCursor(Qt::PointingHandCursor);
    connect(btnNewTab, &QToolButton::clicked, this, [this] {
        newDocSession();
        showDrawing();
    });
    trLay->addWidget(btnNewTab, 0, Qt::AlignVCenter);
    trLay->addStretch(1);

    auto* shell = new QWidget(this);
    auto* shellLay = new QVBoxLayout(shell);
    shellLay->setContentsMargins(0, 0, 0, 0);
    shellLay->setSpacing(0);
    shellLay->addWidget(tabsRow, 0);
    shellLay->addWidget(m_stack, 1);
    setCentralWidget(shell);
    connect(m_startPage, &StartPage::newRequested,  this, [this] { newProject(); });
    connect(m_startPage, &StartPage::openRequested, this, [this] { openProjectInteractive(); });
    connect(m_startPage, &StartPage::recentRequested, this,
            [this](const QString& p) { openProjectPath(p); });
    connect(m_startPage, &StartPage::dismissed, this, [this] { showDrawing(); });
    connect(m_layoutTabs, &QTabBar::currentChanged, this, &MainWindow::onLayoutTabChanged);
    // (Os connects que SAEM do viewport são feitos por instância em
    //  wireViewport(), chamado mais abaixo e a cada nova sessão/aba.)
    buildMenuBar();

    // Ferramentas modais num grupo exclusivo (redesign "Aurora").
    //
    // O conjunto COMUM (curado) vive numa barra VERTICAL de ícones à esquerda;
    // as ferramentas ESPECIALIZADAS ficam acessíveis pelos menus superiores
    // (montados mais abaixo). Todas as QAction* e seus connect são criados aqui;
    // o que muda é apenas ONDE cada uma é inserida.
    auto* grp = new QActionGroup(this);
    grp->setExclusive(true);

    // --- Barra vertical à esquerda: só ícones, sem texto -----------------------
    // tbTools só CRIA/possui as QActions de desenho/modificar/anotação; fica
    // oculto — a barra visível agora é a RIBBON (montada abaixo, reusando as ações).
    auto* tbTools = addToolBar("Ferramentas");
    tbTools->setObjectName("tbToolsHidden");
    tbTools->hide();

    // Grupo Desenho (comum).
    auto* aLine = tbTools->addAction(toolIcon("line"),     "Linha");
    auto* aCirc = tbTools->addAction(toolIcon("circle"),   "Círculo");
    auto* aRect = tbTools->addAction(toolIcon("rect"),     "Retângulo");
    auto* aArc  = tbTools->addAction(toolIcon("arc"),      "Arco 3P");
    auto* aElli = tbTools->addAction(toolIcon("ellipse"),  "Elipse");
    auto* aPoly = tbTools->addAction(toolIcon("polyline"), "Polilinha");
    tbTools->addSeparator();

    // Grupo Modificar (comum).
    auto* aSel  = tbTools->addAction(toolIcon("select"),  "Selecionar");
    auto* aMove = tbTools->addAction(toolIcon("move"),    "Mover");
    auto* aCopy = tbTools->addAction(toolIcon("copy"),    "Copiar");
    auto* aRot  = tbTools->addAction(toolIcon("rotate"),  "Rotacionar");
    auto* aScl  = tbTools->addAction(toolIcon("scale"),   "Escalar");
    auto* aMir  = tbTools->addAction(toolIcon("mirror"),  "Espelhar");
    auto* aOff  = tbTools->addAction(toolIcon("offset"),  "Offset");
    auto* aTrim = tbTools->addAction(toolIcon("trim"),    "Trim");
    auto* aFil  = tbTools->addAction(toolIcon("fillet"),  "Fillet");
    auto* aCha  = tbTools->addAction(toolIcon("chamfer"), "Chanfro");
    auto* aExt  = tbTools->addAction(toolIcon("extend"),  "Extend");
    auto* aErase = tbTools->addAction(toolIcon("erase"),  "Apagar");   // ação imediata
    tbTools->addSeparator();

    // Grupo Anotação (comum).
    auto* aText  = tbTools->addAction(toolIcon("text"),  "Texto");
    auto* aDimL  = tbTools->addAction(toolIcon("dim"),   "Cota");
    auto* aHatch = tbTools->addAction(toolIcon("hatch"), "Hachura");
    // (A RIBBON é montada mais abaixo, após todas as ações existirem.)

    // --- Ferramentas especializadas: criadas sem pai-toolbar (vão p/ os menus).
    // Tooltips iguais ao texto facilitam descoberta no menu.
    auto* aPt   = new QAction("Ponto", this);
    auto* aSpl  = new QAction("Spline", this);
    auto* aSCv  = new QAction("Spline CV", this);
    auto* aC2P  = new QAction("Circ 2P", this);
    auto* aC3P  = new QAction("Circ 3P", this);
    auto* aTTR  = new QAction("Circ TTR", this);
    auto* aTTT  = new QAction("Circ TTT", this);
    auto* aEArc = new QAction("Arco Elíptico", this);
    auto* aASCE = new QAction("Arco SCE", this);
    auto* aACSE = new QAction("Arco CSE", this);
    auto* aASER = new QAction("Arco SER", this);
    auto* aASEA = new QAction("Arco SEA", this);
    auto* aASED = new QAction("Arco SED", this);
    auto* aPolg = new QAction("Polígono", this);
    auto* aRChf = new QAction("Ret Chanfro", this);
    auto* aRFil = new QAction("Ret Fillet", this);
    auto* aDiv  = new QAction("Dividir", this);
    auto* aMea  = new QAction("Medir", this);
    auto* aStr  = new QAction("Stretch", this);
    auto* aBlk  = new QAction("Bloco", this);
    auto* aInsert = new QAction("Inserir bloco", this);
    auto* aExpl = new QAction("Explodir", this);          // ação imediata
    auto* aArrR = new QAction("Matriz Ret", this);        // ação (diálogo)
    auto* aArrP = new QAction("Matriz Polar", this);      // ação (diálogo)
    auto* aArrPath = new QAction("Matriz no Caminho", this);
    auto* aBoolU = new QAction("Booleana: União", this);
    auto* aBoolI = new QAction("Booleana: Interseção", this);
    auto* aBoolD = new QAction("Booleana: Diferença (A-B)", this);
    auto* aWipe  = new QAction("Wipeout (máscara)", this);
    auto* aRegion = new QAction("Região (da seleção)", this);
    auto* aTable = new QAction("Inserir Tabela", this);
    auto* aDimA   = new QAction("Cota Alinhada", this);
    auto* aDimR   = new QAction("Cota Raio", this);
    auto* aDimD   = new QAction("Cota Diâmetro", this);
    auto* aDimAng = new QAction("Cota Angular", this);
    auto* aDimC   = new QAction("Cota Contínua", this);
    auto* aDimB   = new QAction("Cota Linha-base", this);
    auto* aXLine  = new QAction("Linha de Construção (XLINE)", this);
    auto* aRay    = new QAction("Raio (RAY)", this);
    auto* aBreak  = new QAction("Break (quebrar)", this);
    auto* aJoin   = new QAction("Join (unir)", this);
    auto* aLeng   = new QAction("Lengthen (alongar)", this);
    auto* aMLine  = new QAction("Multilinha (MLINE)", this);
    auto* aPolyW  = new QAction("Largura da Polilinha...", this);
    auto* aInq    = new QAction("Consultar (área/compr.)", this);
    auto* aDist   = new QAction("Distância (DIST)", this);
    auto* aArea   = new QAction("Área (por pontos)", this);
    auto* aLeader = new QAction("Chamada (LEADER)", this);
    auto* aMLead  = new QAction("Multileader", this);
    auto* aRevCl  = new QAction("Nuvem de Revisão", this);
    auto* aDimSty = new QAction("Estilo de Cota...", this);
    auto* aTxtSty = new QAction("Estilo de Texto...", this);
    auto* aUnits  = new QAction("Unidades...", this);
    auto* aLtype  = new QAction("Tipos de Linha...", this);
    auto* aRotC   = new QAction("Rotacionar + Copiar", this);
    auto* aSclC   = new QAction("Escalar + Copiar", this);
    auto* aRotR   = new QAction("Rotacionar (Referência)", this);
    auto* aSclR   = new QAction("Escalar (Referência)", this);
    auto* aMatch  = new QAction("Match Properties", this);
    auto* aAlign  = new QAction("Alinhar (ALIGN)", this);

    // Tooltip = texto do botão para a barra de ícones (só-ícone) ficar legível.
    for (QAction* a : {aLine, aCirc, aRect, aArc, aElli, aPoly,
                       aSel, aMove, aCopy, aRot, aScl, aMir, aOff, aTrim, aFil,
                       aCha, aExt, aErase, aText, aDimL, aHatch}) {
        a->setToolTip(a->text());
    }

    // Todas as ferramentas modais (comuns + especializadas) entram no grupo
    // exclusivo, para o estado "checked" refletir a ferramenta ativa.
    for (QAction* a : {aSel, aLine, aCirc, aRect, aArc, aElli, aPoly, aPt, aSpl,
                       aC2P, aC3P, aTTR, aTTT, aEArc, aSCv, aASCE, aACSE, aASER, aASEA, aASED, aPolg, aRChf, aRFil, aDiv, aMea,
                       aMove, aCopy, aRot, aScl, aMir, aOff, aTrim, aFil, aCha, aExt, aStr, aBlk,
                       aText, aDimL, aDimA, aDimR, aDimD, aDimAng, aDimC, aDimB, aHatch,
                       aXLine, aRay, aBreak, aJoin, aLeng, aMLine, aInq,
                       aDist, aLeader, aRevCl, aRotC, aSclC, aMatch, aRotR, aSclR, aMLead, aAlign}) {
        a->setCheckable(true);
        grp->addAction(a);
    }
    aSel->setChecked(true);

    // Mapa ferramenta <-> botão: permite sincronizar o ribbon quando a ferramenta
    // muda por fora do clique (Esc, comando digitado). Registra a 1ª como canônica
    // (ex.: Rotate -> aRot, mesmo havendo aRotC/aRotR para a mesma ToolKind).
    auto reg = [this](ToolKind k, QAction* a) {
        m_toolKindOf.insert(a, static_cast<int>(k));
        if (!m_toolAct.contains(static_cast<int>(k))) m_toolAct.insert(static_cast<int>(k), a);
    };
    reg(ToolKind::None, aSel);        reg(ToolKind::Line, aLine);     reg(ToolKind::Circle, aCirc);
    reg(ToolKind::Rectangle, aRect);  reg(ToolKind::Arc3, aArc);      reg(ToolKind::Ellipse, aElli);
    reg(ToolKind::Polyline, aPoly);   reg(ToolKind::Point, aPt);      reg(ToolKind::Spline, aSpl);
    reg(ToolKind::SplineCV, aSCv);    reg(ToolKind::Move, aMove);     reg(ToolKind::Copy, aCopy);
    reg(ToolKind::Rotate, aRot);      reg(ToolKind::Rotate, aRotC);   reg(ToolKind::Rotate, aRotR);
    reg(ToolKind::Scale, aScl);       reg(ToolKind::Scale, aSclC);    reg(ToolKind::Scale, aSclR);
    reg(ToolKind::Mirror, aMir);      reg(ToolKind::Offset, aOff);    reg(ToolKind::Trim, aTrim);
    reg(ToolKind::Fillet, aFil);      reg(ToolKind::Chamfer, aCha);   reg(ToolKind::Extend, aExt);
    reg(ToolKind::Stretch, aStr);     reg(ToolKind::Block, aBlk);     reg(ToolKind::Polygon, aPolg);
    reg(ToolKind::Divide, aDiv);      reg(ToolKind::Measure, aMea);   reg(ToolKind::Hatch, aHatch);
    reg(ToolKind::Text, aText);       reg(ToolKind::Circle2P, aC2P);  reg(ToolKind::Circle3P, aC3P);
    reg(ToolKind::CircleTTR, aTTR);   reg(ToolKind::CircleTTT, aTTT); reg(ToolKind::EllipseArc, aEArc);
    reg(ToolKind::XLine, aXLine);     reg(ToolKind::Ray, aRay);       reg(ToolKind::MLine, aMLine);
    reg(ToolKind::BreakTool, aBreak); reg(ToolKind::JoinTool, aJoin); reg(ToolKind::Lengthen, aLeng);
    reg(ToolKind::Inquiry, aInq);     reg(ToolKind::Dist, aDist);     reg(ToolKind::Leader, aLeader);
    reg(ToolKind::Area, aArea);
    reg(ToolKind::RevCloud, aRevCl);  reg(ToolKind::MatchProps, aMatch);
    reg(ToolKind::Leader, aLeader);   reg(ToolKind::MLeaderTool, aMLead);
    reg(ToolKind::Align, aAlign);

    // O grupo dos botões de ferramenta fica acessível ao wireViewport (o
    // sync do botão ativo é conectado POR viewport, a cada nova aba).
    m_toolGroup = grp;

    // (Os atalhos de letra única foram substituídos por COMANDOS DIGITADOS no
    //  canvas, com autocomplete — ex.: digite "L" + Enter para Linha. As letras
    //  precisam chegar ao viewport, por isso não há mais QAction::setShortcut.)

    connect(aSel,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::None);   log("Selecionar: clique ou caixa (E->D Window azul, D->E Crossing verde). Shift soma. Del apaga."); });
    connect(aLine, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Line);   log("Linha: clique 2 pontos (encadeia). Esc cancela."); });
    connect(aCirc, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Circle); log("Círculo: clique o centro e a borda — ou digite o RAIO + Enter."); });
    connect(aRect, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Rectangle); log("Retângulo: clique 1 canto e o oposto — ou, após o 1º canto, digite LARGURA,ALTURA + Enter."); });
    connect(aArc,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Arc3);   log("Arco: clique 3 pontos (início, meio, fim)."); });
    connect(aElli, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Ellipse); log("Elipse: centro, extremo do eixo maior, depois o eixo menor (clique ou digite o valor + Enter)."); });
    connect(aMove, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Move);   log("Mover: selecione, depois ponto-base e destino."); });
    connect(aCopy, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Copy);   log("Copiar: selecione, ponto-base, e clique para colar várias cópias (Enter/Esc termina)."); });
    connect(aRot,  &QAction::triggered, this, [this] { m_view->setEditCopy(false); m_view->setEditRef(false); m_view->setTool(ToolKind::Rotate); log("Rotacionar: selecione, ponto-base, depois a direção do ângulo."); });
    connect(aScl,  &QAction::triggered, this, [this] { m_view->setEditCopy(false); m_view->setEditRef(false); m_view->setTool(ToolKind::Scale);  log("Escalar: selecione, ponto-base, depois um ponto à distância = fator."); });
    connect(aMir,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Mirror); log("Espelhar: selecione, depois 2 pontos do eixo (cria cópia espelhada)."); });
    connect(aOff,  &QAction::triggered, this, [this] {
        m_view->setTool(ToolKind::Offset);
        const double d = m_view->offsetDist();
        log((d > 0.0 ? QString("Offset (dist %1)").arg(d) : QString("Offset (pelo clique)"))
            + ": clique o objeto e o LADO. Digite um número + Enter p/ fixar a distância. Esc termina.");
    });
    connect(aTrim, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Trim);   log("Trim: clique o trecho a remover (apara contra todas as entidades que cruzam)."); });
    connect(aFil,  &QAction::triggered, this, [this] {
        m_view->setTool(ToolKind::Fillet);
        log(QString("Fillet (raio %1): clique a 1ª e a 2ª linha. Digite um número + Enter p/ mudar (0 = canto vivo).")
            .arg(m_view->filletRadius()));
    });
    connect(aCha,  &QAction::triggered, this, [this] {
        m_view->setTool(ToolKind::Chamfer);
        log(QString("Chanfro (recuo %1): clique a 1ª e a 2ª linha. Digite um número + Enter p/ mudar.")
            .arg(m_view->chamferDist()));
    });
    connect(aACSE, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::ArcCSE); log("Arco: clique centro, início e fim."); });
    connect(aASER, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::ArcSER); log("Arco S-E-Raio: clique início e fim, depois digite o raio + Enter."); });
    connect(aASEA, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::ArcSEA); log("Arco S-E-Ângulo: clique início e fim, depois digite o ângulo (graus) + Enter."); });
    connect(aASED, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::ArcSED); log("Arco S-E-Direção: clique início, fim e um ponto na direção da tangente."); });
    connect(aEArc, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::EllipseArc); log("Arco Elíptico: centro, extremo do eixo maior, eixo menor, ponto inicial e final."); });
    connect(aTTT,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::CircleTTT); log("Círculo TTT: clique 3 retas (incírculo/excírculos)."); });
    connect(aRFil, &QAction::triggered, this, [this] {
        bool ok = false;
        const double r = QInputDialog::getDouble(this, "Retângulo arredondado", "Raio do canto:", 5.0, 0.0, 1e6, 2, &ok);
        if (!ok) return;
        m_view->setFilletRadius(r);
        m_view->setTool(ToolKind::RectFillet);
        log(QString("Retângulo arredondado (raio %1): clique 2 cantos.").arg(r));
    });
    connect(aExt,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Extend); log("Extend: clique o contorno (limite), depois as linhas a estender."); });
    connect(aPoly, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Polyline); log("Polilinha: clique vários pontos; Enter/botão-direito finaliza."); });
    connect(aPt,   &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Point); log("Ponto: clique para inserir nós (marcador +)."); });
    connect(aSpl,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Spline); log("Spline: clique pontos de passagem; Enter finaliza."); });
    connect(aSCv,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::SplineCV); log("Spline CV: clique pontos de controle (a curva não passa neles); Enter finaliza."); });
    connect(aTTR,  &QAction::triggered, this, [this] {
        m_view->setTool(ToolKind::CircleTTR);
        log(QString("Círculo TTR (raio %1): clique 2 entidades. Digite um número + Enter p/ mudar o raio.")
            .arg(m_view->ttrRadius()));
    });
    connect(aStr,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Stretch); log("Stretch: caixa Crossing (D->E), Enter, depois ponto-base e destino."); });
    connect(aBlk,  &QAction::triggered, this, [this] { createBlockInteractive(); });
    connect(aInsert, &QAction::triggered, this, [this] { insertBlockInteractive(); });
    connect(aC2P,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Circle2P); log("Círculo 2P: clique os 2 extremos do diâmetro."); });
    connect(aC3P,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Circle3P); log("Círculo 3P: clique 3 pontos."); });
    connect(aASCE, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::ArcSCE); log("Arco: clique início, centro e fim."); });
    connect(aRChf, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::RectChamfer); log("Retângulo chanfrado (chanfro 5): clique 2 cantos."); });
    connect(aPolg, &QAction::triggered, this, [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, "Polígono", "Número de lados:", 6, 3, 200, 1, &ok);
        if (!ok) return;
        const QStringList modes{"Inscrito", "Circunscrito"};
        const QString m = QInputDialog::getItem(this, "Polígono", "Modo:", modes, 0, false, &ok);
        if (!ok) return;
        m_view->setPolygon(n, m == "Inscrito");
        m_view->setTool(ToolKind::Polygon);
        log("Polígono: clique o centro e o raio — ou digite o RAIO + Enter.");
    });
    // DIVIDE/MEASURE: com biblioteca de blocos, pergunta a marca (ponto ou
    // BLOCO alinhado/não à entidade — estilo AutoCAD). Retorna false = cancelou.
    auto askDivideMarker = [this](const QString& title) -> bool {
        m_view->setDivideBlock("", false);                 // padrão: pontos
        const auto names = m_doc->blocks().names();
        if (names.empty()) return true;
        bool ok = false;
        const QStringList kinds{"Pontos", "Bloco..."};
        const QString k = QInputDialog::getItem(this, title, "Marcar com:", kinds, 0, false, &ok);
        if (!ok) return false;
        if (k == "Pontos") return true;
        QStringList list;
        for (const std::string& n : names) list << QString::fromStdString(n);
        const QString bn = QInputDialog::getItem(this, title, "Bloco:", list, 0, false, &ok);
        if (!ok) return false;
        const bool align = QMessageBox::question(this, title,
            "Alinhar o bloco à entidade (girar pela tangente)?") == QMessageBox::Yes;
        m_view->setDivideBlock(bn.toStdString(), align);
        return true;
    };
    connect(aDiv, &QAction::triggered, this, [this, askDivideMarker] {
        bool ok = false;
        const int n = QInputDialog::getInt(this, "Dividir", "Número de partes:", 4, 2, 1000, 1, &ok);
        if (!ok) return;
        if (!askDivideMarker("Dividir")) return;
        m_view->setDivideCount(n);
        m_view->setTool(ToolKind::Divide);
        log("Dividir: clique a entidade a dividir.");
    });
    connect(aMea, &QAction::triggered, this, [this, askDivideMarker] {
        bool ok = false;
        const double s = QInputDialog::getDouble(this, "Medir", "Espaçamento:", 10.0, 0.001, 1e6, 3, &ok);
        if (!ok) return;
        if (!askDivideMarker("Medir")) return;
        m_view->setMeasureSpacing(s);
        m_view->setTool(ToolKind::Measure);
        log("Medir: clique a entidade.");
    });
    connect(aErase,&QAction::triggered, this, [this] { m_view->eraseSelected(); log("Apagar seleção."); });
    connect(aExpl, &QAction::triggered, this, [this] { m_view->explodeSelected(); log("Explodir seleção (polilinhas -> linhas)."); });
    connect(aArrR, &QAction::triggered, this, [this] {
        bool o1 = false, o2 = false, o3 = false;
        const int rows = QInputDialog::getInt(this, "Matriz Retangular", "Linhas:", 2, 1, 1000, 1, &o1);
        if (!o1) return;
        const int cols = QInputDialog::getInt(this, "Matriz Retangular", "Colunas:", 3, 1, 1000, 1, &o2);
        if (!o2) return;
        const double sp = QInputDialog::getDouble(this, "Matriz Retangular", "Espaçamento:", 20.0, 0.001, 1e6, 2, &o3);
        if (!o3) return;
        m_view->arrayRectangular(rows, cols, sp, sp);
        log("Matriz retangular criada.");
    });
    connect(aArrP, &QAction::triggered, this, [this] {
        bool o1 = false, o2 = false;
        const int count = QInputDialog::getInt(this, "Matriz Polar", "Quantidade total:", 6, 2, 1000, 1, &o1);
        if (!o1) return;
        const double ang = QInputDialog::getDouble(this, "Matriz Polar", "Ângulo total (graus):", 360.0, -3600.0, 3600.0, 1, &o2);
        if (!o2) return;
        m_view->arrayPolar(count, ang);
        log("Matriz polar criada.");
    });
    connect(aArrPath, &QAction::triggered, this, [this] {
        if (m_view->selectedIds().empty()) {
            QMessageBox::information(this, "Matriz no Caminho",
                "Selecione primeiro a(s) entidade(s) a repetir; depois clique o caminho.");
            return;
        }
        bool o1 = false, o2 = false;
        const int count = QInputDialog::getInt(this, "Matriz no Caminho", "Quantidade:", 6, 1, 10000, 1, &o1);
        if (!o1) return;
        const int al = QMessageBox::question(this, "Matriz no Caminho",
                          "Alinhar as cópias à tangente do caminho?",
                          QMessageBox::Yes | QMessageBox::No);
        (void)o2;
        m_view->beginArrayPath(count, al == QMessageBox::Yes);
        m_view->setTool(ToolKind::ArrayPath);
        log("Matriz no Caminho: clique a entidade-caminho (linha/polilinha).");
    });
    auto doBool = [this](BoolOp op, const char* nome) {
        if (m_view->selectedIds().size() != 2) {
            QMessageBox::information(this, "Booleana", "Selecione exatamente 2 polígonos fechados.");
            return;
        }
        if (m_view->booleanSelected(op)) log(QString("Booleana aplicada: %1.").arg(nome));
        else QMessageBox::information(this, "Booleana", "Não foi possível aplicar (precisam ser 2 contornos fechados).");
    };
    connect(aBoolU, &QAction::triggered, this, [doBool] { doBool(BoolOp::Union, "União"); });
    connect(aBoolI, &QAction::triggered, this, [doBool] { doBool(BoolOp::Intersection, "Interseção"); });
    connect(aBoolD, &QAction::triggered, this, [doBool] { doBool(BoolOp::Difference, "Diferença"); });
    connect(aWipe, &QAction::triggered, this, [this] {
        m_view->setTool(ToolKind::Wipeout);
        log("Wipeout: clique os vértices da máscara; Enter fecha e cria.");
    });
    connect(aRegion, &QAction::triggered, this, [this] {
        if (m_view->regionFromSelection()) log("Região criada a partir da seleção.");
        else QMessageBox::information(this, "Região", "Selecione 1+ polígono fechado (o maior vira contorno; os demais, furos).");
    });
    connect(aTable, &QAction::triggered, this, [this] {
        bool o1 = false, o2 = false, o3 = false, o4 = false;
        const int r = QInputDialog::getInt(this, "Tabela", "Linhas:", 3, 1, 1000, 1, &o1); if (!o1) return;
        const int c = QInputDialog::getInt(this, "Tabela", "Colunas:", 3, 1, 1000, 1, &o2); if (!o2) return;
        const double cw = QInputDialog::getDouble(this, "Tabela", "Largura da coluna:", 40.0, 0.1, 1e6, 2, &o3); if (!o3) return;
        const double rh = QInputDialog::getDouble(this, "Tabela", "Altura da linha:", 15.0, 0.1, 1e6, 2, &o4); if (!o4) return;
        m_view->setTableParams(r, c, cw, rh);
        m_view->setTool(ToolKind::TableTool);
        log("Tabela: clique o canto superior-esquerdo para inserir.");
    });

    // Arquivo: exportar/importar DXF (interoperável com AutoCAD/LibreCAD).
    // Ações criadas sem pai-toolbar; entram no menu "Arquivo" (buildToolMenus).
    auto* aSaveDxf = new QAction("Salvar DXF", this);
    auto* aOpenDxf = new QAction("Importar DXF", this);
    connect(aSaveDxf, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, "Salvar DXF", "desenho.dxf",
                                                          "DXF ASCII (*.dxf)");
        if (path.isEmpty()) return;
        if (writeDxf(*m_doc, path.toStdString())) log("DXF salvo: " + path);
        else QMessageBox::warning(this, "Salvar DXF", "Não foi possível gravar o arquivo.");
    });
    connect(aOpenDxf, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Importar DXF", QString(),
                                                          "DXF ASCII (*.dxf)");
        if (path.isEmpty()) return;
        const int n = readDxf(path.toStdString(), *m_doc);
        if (n < 0) { QMessageBox::warning(this, "Importar DXF", "Não foi possível abrir o arquivo."); return; }
        refreshLayers();
        m_view->fitView();
        log(QString("DXF importado: %1 entidades").arg(n));
    });
    auto* aPdf = new QAction("Exportar PDF", this);
    connect(aPdf, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, "Exportar PDF", "plotagem.pdf",
                                                          "PDF (*.pdf)");
        if (path.isEmpty()) return;

        // Bounding box de toda a geometria visível.
        AABB bb;
        const LayerTable& layers = m_doc->layers();
        m_doc->forEach([&](const Entity& e) {
            const Layer* lay = layers.find(e.layer());
            if (lay && (!lay->on || lay->frozen)) return;
            RenderBatch b; e.emitTo(b);
            for (const Point3& v : b.lineVertices) bb.expand(v);
            for (const Point3& v : b.fillVertices) bb.expand(v);   // áreas preenchidas contam no enquadramento
        });
        if (!bb.valid()) { QMessageBox::warning(this, "Exportar PDF", "Nada para plotar."); return; }

        QPdfWriter pdf(path.toStdString().c_str());
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setPageOrientation(QPageLayout::Landscape);
        pdf.setResolution(300);
        QPainter p(&pdf);

        // Mapeamento mundo->página: fit uniforme com margem, Y invertido.
        const double W = pdf.width(), H = pdf.height();
        const double margin = 0.06 * std::min(W, H);
        const double pw = W - 2 * margin, ph = H - 2 * margin;
        const double dx = std::max(bb.max.x - bb.min.x, 1e-9);
        const double dy = std::max(bb.max.y - bb.min.y, 1e-9);
        const double s = std::min(pw / dx, ph / dy);
        const double offx = margin + (pw - s * dx) / 2.0;
        const double offy = margin + (ph - s * dy) / 2.0;
        auto toPage = [&](const Point3& v) {
            return QPointF(offx + (v.x - bb.min.x) * s, H - (offy + (v.y - bb.min.y) * s));
        };

        m_doc->forEach([&](const Entity& e) {
            const Layer* lay = layers.find(e.layer());
            if (lay && (!lay->on || lay->frozen)) return;
            RenderBatch b; e.emitTo(b);
            // Tipo de linha no papel (DASHED/DASHDOT não saem contínuas).
            std::string ltn = e.lineType().name;
            if (ltn == "ByLayer" || ltn.empty())
                ltn = lay ? lay->lineType.name : std::string("CONTINUOUS");
            b.lineVertices = applyLineTypeByName(b.lineVertices, ltn,
                                                 1.5 * m_view->lineTypeScale());
            const Rgba c = e.resolveColor(layers);
            // Branco vira preto no papel (plotagem clássica); demais cores preservadas.
            const QColor qc = (c.r > 200 && c.g > 200 && c.b > 200) ? QColor(0, 0, 0)
                                                                    : QColor(c.r, c.g, c.b);

            // Áreas preenchidas (hachura SOLID / regiões) primeiro, sob os traços.
            if (!b.fillVertices.empty()) {
                p.setPen(Qt::NoPen);
                p.setBrush(qc);
                for (std::size_t i = 0; i + 2 < b.fillVertices.size(); i += 3) {
                    const QPointF tri[3] = { toPage(b.fillVertices[i]),
                                             toPage(b.fillVertices[i + 1]),
                                             toPage(b.fillVertices[i + 2]) };
                    p.drawPolygon(tri, 3);
                }
                p.setBrush(Qt::NoBrush);
            }

            // Espessura de plotagem: mm efetivo (ByLayer = -1 herda da camada;
            // <=0 cai no padrão 0.25 mm) convertido para a resolução do PDF.
            double effMm = e.lineWeight().mm;
            if (effMm < 0.0 && lay) effMm = lay->lineWeight.mm;
            if (effMm <= 0.0) effMm = 0.25;
            QPen pen(qc);
            pen.setWidthF(effMm * pdf.resolution() / 25.4);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            for (std::size_t i = 0; i + 1 < b.lineVertices.size(); i += 2)
                p.drawLine(toPage(b.lineVertices[i]), toPage(b.lineVertices[i + 1]));
        });
        p.end();
        log("PDF exportado: " + path);
    });

    // Anotação: texto/cotas/hachura. As ações já foram criadas acima
    // (aText/aDimL/aHatch na barra esquerda; as demais cotas como especializadas).
    // Aqui ficam apenas os connect.
    connect(aText,   &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Text);       log("Texto: clique o ponto e digite o conteúdo."); });
    connect(aDimL,   &QAction::triggered, this, [this] { m_view->setTool(ToolKind::DimLinear);  log("Cota Linear: clique 2 pontos + a posição da linha de cota."); });
    connect(aDimA,   &QAction::triggered, this, [this] { m_view->setTool(ToolKind::DimAligned); log("Cota Alinhada: clique 2 pontos + a posição da linha."); });
    connect(aDimR,   &QAction::triggered, this, [this] { m_view->setTool(ToolKind::DimRadius);  log("Raio: clique o centro e um ponto no círculo."); });
    connect(aDimD,   &QAction::triggered, this, [this] { m_view->setTool(ToolKind::DimDiameter);log("Diâmetro: clique o centro e um ponto no círculo."); });
    connect(aDimAng, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::DimAngular); log("Angular: clique o vértice e 2 pontos."); });
    connect(aDimC,   &QAction::triggered, this, [this] { m_view->setTool(ToolKind::DimContinue); log("Cota Contínua: clique o próximo ponto (encadeia a última cota linear)."); });
    connect(aDimB,   &QAction::triggered, this, [this] { m_view->setTool(ToolKind::DimBaseline); log("Cota Linha-base: clique o próximo ponto (empilha a partir da base)."); });
    connect(aXLine,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::XLine); log("Linha de Construção: clique 2 pontos (reta infinita)."); });
    connect(aRay,    &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Ray); log("Raio: clique origem e direção (semi-infinito)."); });
    connect(aBreak,  &QAction::triggered, this, [this] { m_view->setTool(ToolKind::BreakTool); log("Break: clique a linha, depois 2 pontos (remove o trecho entre eles)."); });
    connect(aJoin,   &QAction::triggered, this, [this] {
        // Com seleção múltipla: une direto (linhas conectadas -> polilinha).
        if (m_view->selectedIds().size() >= 2) {
            if (m_view->joinSelected()) { log("Join: seleção unida numa polilinha."); return; }
            log("Join: a seleção não forma UMA corrente de linhas conectadas.");
            return;
        }
        m_view->setTool(ToolKind::JoinTool);
        log("Join: clique 2 linhas colineares (ou selecione várias e repita o comando).");
    });
    connect(aLeng,   &QAction::triggered, this, [this] {
        bool ok = false;
        const double d = QInputDialog::getDouble(this, "Lengthen", "Delta (+ alonga / - apara):", 10.0, -1e6, 1e6, 2, &ok);
        if (!ok) return;
        m_view->setLengthenDelta(d);
        m_view->setTool(ToolKind::Lengthen);
        log(QString("Lengthen (%1): clique a linha pela ponta a alterar.").arg(d));
    });
    connect(aMLine, &QAction::triggered, this, [this] {
        bool ok = false;
        const double w = QInputDialog::getDouble(this, "Multilinha", "Largura:", 5.0, 0.001, 1e6, 2, &ok);
        if (!ok) return;
        m_view->setMlineWidth(w);
        m_view->setTool(ToolKind::MLine);
        log(QString("Multilinha (largura %1): clique os pontos do eixo; Enter finaliza.").arg(w));
    });
    connect(aInq, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Inquiry); log("Consultar: clique uma entidade para ver tipo, comprimento e área."); });
    connect(aRotC, &QAction::triggered, this, [this] { m_view->setEditCopy(true); m_view->setEditRef(false); m_view->setTool(ToolKind::Rotate); log("Rotacionar+Copiar: selecione, ponto-base, depois a direção (mantém o original)."); });
    connect(aSclC, &QAction::triggered, this, [this] { m_view->setEditCopy(true); m_view->setEditRef(false); m_view->setTool(ToolKind::Scale);  log("Escalar+Copiar: selecione, ponto-base, depois um ponto (mantém o original)."); });
    connect(aRotR, &QAction::triggered, this, [this] { m_view->setEditCopy(false); m_view->setEditRef(true); m_view->setTool(ToolKind::Rotate); log("Rotacionar por Referência: base, ponto de REFERÊNCIA, depois a nova direção."); });
    connect(aSclR, &QAction::triggered, this, [this] { m_view->setEditCopy(false); m_view->setEditRef(true); m_view->setTool(ToolKind::Scale);  log("Escalar por Referência: base, ponto de REFERÊNCIA (=1), depois o novo comprimento."); });
    connect(aMatch, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::MatchProps); log("Match Properties: clique a fonte, depois os alvos (copia cor/camada/tipo/espessura)."); });
    connect(aAlign, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Align); log("Alinhar: selecione (Enter), depois origem1->destino1 e origem2->destino2 (move+rotaciona+escala)."); });
    connect(aDist, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Dist); log("DIST: clique o primeiro ponto, depois o segundo."); });
    connect(aArea, &QAction::triggered, this, [this] { m_view->setTool(ToolKind::Area); log("AREA: clique os pontos do contorno (a área/perímetro aparecem ao vivo). Enter fecha e reporta; Esc cancela."); });
    connect(aLeader, &QAction::triggered, this, [this] {
        bool ok = false;
        const QString s = QInputDialog::getText(this, "Chamada (Leader)", "Texto (opcional):",
                                                QLineEdit::Normal, QString(), &ok);
        if (!ok) return;
        m_view->setLeaderText(s.toStdString());
        m_view->setTool(ToolKind::Leader);
        log("Leader: clique a ponta (seta), depois os vértices; Enter finaliza.");
    });
    connect(aMLead, &QAction::triggered, this, [this] {
        bool ok = false;
        const QString s = QInputDialog::getText(this, "Multileader", "Texto (opcional):",
                                                QLineEdit::Normal, QString(), &ok);
        if (!ok) return;
        m_view->setMleaderText(s.toStdString());
        m_view->setTool(ToolKind::MLeaderTool);
        log("Multileader: clique a ponta e os vértices de uma chamada + Enter; repita p/ mais chamadas; Enter vazio conclui (texto no fim).");
    });
    connect(aRevCl, &QAction::triggered, this, [this] {
        bool ok = false;
        const double r = QInputDialog::getDouble(this, "Nuvem de Revisão", "Raio do arco:", 5.0, 0.1, 1e6, 2, &ok);
        if (!ok) return;
        m_view->setRevCloudRadius(r);
        m_view->setTool(ToolKind::RevCloud);
        log("Nuvem de Revisão: clique 2 cantos do retângulo.");
    });
    connect(aPolyW, &QAction::triggered, this, [this] {
        bool ok = false;
        const double w = QInputDialog::getDouble(this, "Largura da Polilinha",
                                                 "Largura (0 = fino):", 0.0, 0.0, 1e6, 2, &ok);
        if (!ok) return;
        m_view->setPolyWidth(w);
        log(QString("Largura da polilinha = %1 (aplica-se às próximas polilinhas).").arg(w));
    });
    connect(aDimSty, &QAction::triggered, this, [this] { openDimStyleManager(); });
    connect(aTxtSty, &QAction::triggered, this, [this] { openTextStyleManager(); });
    connect(aUnits, &QAction::triggered, this, [this] {
        const QStringList units{"Milímetro (mm)", "Centímetro (cm)", "Decímetro (dm)",
                                "Metro (m)", "Quilômetro (km)", "Polegada (pol)", "Pé (ft)",
                                "Sem unidade"};
        const QStringList sufs{" mm", " cm", " dm", " m", " km", " pol", " ft", ""};
        bool ok = false;
        const int ui = units.indexOf(QInputDialog::getItem(this, "Unidades",
                          "Unidade de desenho:", units, m_unitIndex, false, &ok));
        if (!ok || ui < 0) return;
        const int dec = QInputDialog::getInt(this, "Unidades", "Casas decimais (exibição):",
                          m_unitDecimals, 0, 8, 1, &ok);
        if (!ok) return;
        m_unitIndex = ui; m_unitDecimals = dec; m_unitSuffix = sufs[ui];
        if (auto* sb = qobject_cast<CadStatusBar*>(statusBar()))
            sb->setUnitFormat(dec, m_unitSuffix);
        // novas cotas passam a exibir a unidade como sufixo
        m_view->setDimStyle(dec, m_unitSuffix.trimmed().isEmpty() ? std::string()
                                  : m_unitSuffix.toStdString(), -1.0);
        log("Unidade de desenho: " + units[ui] + QString(" (%1 casas).").arg(dec));
    });
    connect(aLtype, &QAction::triggered, this, [this] {
        QDialog dlg(this);
        dlg.setWindowTitle("Tipos de Linha (custom)");
        dlg.resize(360, 300);
        auto* lay = new QVBoxLayout(&dlg);
        auto* list = new QListWidget(&dlg);
        lay->addWidget(list);
        auto reload = [&] {
            list->clear();
            for (const std::string& n : customLineTypeNames())
                list->addItem(QString::fromStdString(n));
        };
        reload();
        auto* btnNew = new QPushButton("Novo tipo...", &dlg);
        connect(btnNew, &QPushButton::clicked, &dlg, [&] {
            bool ok = false;
            const QString nm = QInputDialog::getText(&dlg, "Novo tipo de linha", "Nome:",
                                                     QLineEdit::Normal, QString(), &ok);
            if (!ok || nm.trimmed().isEmpty()) return;
            const QString pat = QInputDialog::getText(&dlg, "Novo tipo de linha",
                                  "Padrão traço,lacuna,... (ex.: 12,2,2,2):",
                                  QLineEdit::Normal, "6,3", &ok);
            if (!ok) return;
            std::vector<double> pattern;
            for (const QString& tok : pat.split(',', Qt::SkipEmptyParts)) {
                bool o2 = false; const double v = tok.trimmed().toDouble(&o2);
                if (o2) pattern.push_back(std::fabs(v));
            }
            if (pattern.size() < 2) {
                QMessageBox::information(&dlg, "Tipo de linha", "Informe ao menos 2 números (traço, lacuna).");
                return;
            }
            registerLineType(nm.trimmed().toStdString(), pattern);
            const QString up = nm.trimmed().toUpper();
            if (m_propLtype->findData(up) < 0) m_propLtype->addItem(nm.trimmed(), up);
            refreshLayers();
            reload();
            log("Tipo de linha custom criado: " + nm.trimmed());
        });
        lay->addWidget(btnNew);
        auto* btnClose = new QPushButton("Fechar", &dlg);
        connect(btnClose, &QPushButton::clicked, &dlg, [&] { dlg.accept(); });
        lay->addWidget(btnClose);
        dlg.exec();
    });
    connect(aHatch,  &QAction::triggered, this, [this] {
        const QStringList pats{"Linhas", "ANSI31", "ANSI37", "Grade", "Sólido", "Gradiente",
                               "Tijolo", "Concreto", "Madeira", "Areia"};
        bool ok = false;
        const QString p = QInputDialog::getItem(this, "Hachura", "Padrão:", pats, 0, false, &ok);
        if (!ok) return;
        const int pidx = static_cast<int>(pats.indexOf(p));
        if (pidx == 5) {   // Gradiente: pede as 2 cores
            const QColor c1 = QColorDialog::getColor(QColor(40, 90, 200), this, "Cor inicial do gradiente");
            if (!c1.isValid()) return;
            const QColor c2 = QColorDialog::getColor(QColor(220, 230, 255), this, "Cor final do gradiente");
            if (!c2.isValid()) return;
            m_view->setHatchGradient(c1.red(), c1.green(), c1.blue(), c2.red(), c2.green(), c2.blue());
        }
        // Ângulo/escala usam o último valor (fluido); muda via menu "Hachura: estilo"
        // ou nas Propriedades. Evita 2 popups por hachura. Materiais (Tijolo/Concreto/
        // Madeira/Areia, índice >= 6) têm orientação NATURAL horizontal (ângulo 0).
        const double useAng = (pidx >= 6) ? 0.0 : m_hatchAngle;
        m_view->setHatch(pidx, useAng, m_hatchScale);
        m_view->setTool(ToolKind::Hatch);
        log(QString("Hachura %1 (ang %2, esc %3): clique numa área fechada.")
            .arg(p).arg(m_hatchAngle).arg(m_hatchScale));
    });

    // === RIBBON DENSA (1 botão grande + grade de ícones pequenos), tudo em Início.
    auto* aZoomExt = new QAction("Tudo", this);
    connect(aZoomExt, &QAction::triggered, this, [this] { m_view->zoomExtents(); });
    auto* aZoomWin = new QAction("Janela", this);
    connect(aZoomWin, &QAction::triggered, this, [this] { m_view->beginZoomWindow(); });
    auto* aZoomIn = new QAction("Ampliar", this);
    connect(aZoomIn, &QAction::triggered, this, [this] { m_view->zoomIn(); });
    auto* aZoomOut = new QAction("Reduzir", this);
    connect(aZoomOut, &QAction::triggered, this, [this] { m_view->zoomOut(); });
    auto* aZoomPrev = new QAction("Anterior", this);
    connect(aZoomPrev, &QAction::triggered, this, [this] { m_view->zoomPrevious(); });
    auto* aPan = new QAction("Pan", this);
    connect(aPan, &QAction::triggered, this, [this] { m_view->beginPan(); });
    auto* aThemeTgl = new QAction("Tema", this);
    connect(aThemeTgl, &QAction::triggered, this, [this] {
        applyTheme(m_themeMode == ThemeMode::Dark ? ThemeMode::Light : ThemeMode::Dark); });

    // Ícones nas ações que só viviam nos menus (pra ribbon ser 100% icônica).
    aPolg->setIcon(toolIcon("polygon"));  aXLine->setIcon(toolIcon("xline"));
    aSpl->setIcon(toolIcon("spline"));    aSCv->setIcon(toolIcon("spline"));   aPt->setIcon(toolIcon("point"));
    aArrR->setIcon(toolIcon("array"));    aArrP->setIcon(toolIcon("array"));   aArrPath->setIcon(toolIcon("array"));
    aStr->setIcon(toolIcon("stretch"));   aBreak->setIcon(toolIcon("break"));  aJoin->setIcon(toolIcon("join"));
    aLeng->setIcon(toolIcon("lengthen")); aMea->setIcon(toolIcon("area"));     aDiv->setIcon(toolIcon("point"));
    aDist->setIcon(toolIcon("distance")); aInq->setIcon(toolIcon("inquiry"));  aMatch->setIcon(toolIcon("match"));
    aArea->setIcon(toolIcon("area"));
    aBlk->setIcon(toolIcon("block"));     aExpl->setIcon(toolIcon("explode")); aTable->setIcon(toolIcon("table"));
    aWipe->setIcon(toolIcon("wipeout"));  aRegion->setIcon(toolIcon("region"));
    aLeader->setIcon(toolIcon("leader")); aMLead->setIcon(toolIcon("mleader"));aRevCl->setIcon(toolIcon("revcloud"));
    aBoolU->setIcon(toolIcon("region"));  aBoolI->setIcon(toolIcon("region")); aBoolD->setIcon(toolIcon("region"));
    aDimA->setIcon(toolIcon("dim")); aDimR->setIcon(toolIcon("dim")); aDimD->setIcon(toolIcon("dim"));
    aDimAng->setIcon(toolIcon("dim")); aDimC->setIcon(toolIcon("dim")); aDimB->setIcon(toolIcon("dim"));
    aSel->setIcon(toolIcon("select"));    aThemeTgl->setIcon(toolIcon("hatch"));
    aZoomExt->setIcon(toolIcon("zoomext")); aZoomWin->setIcon(toolIcon("zoomwin"));
    aZoomIn->setIcon(toolIcon("zoomin"));   aZoomOut->setIcon(toolIcon("zoomout"));
    aZoomPrev->setIcon(toolIcon("zoomprev")); aPan->setIcon(toolIcon("pan"));

    QHash<QAction*, QToolButton*> ribBtn;
    // Painel denso: botões GRANDES (principais) + GRADE 3-linhas de ícones (secundários).
    auto densePanel = [&ribBtn](const QString& title, const QList<RibBtn>& large,
                                const QList<QAction*>& small) -> QWidget* {
        auto* panel = new QFrame;
        auto* v = new QVBoxLayout(panel);
        v->setContentsMargins(8, 4, 8, 2); v->setSpacing(2);
        auto* content = new QHBoxLayout; content->setSpacing(3);
        for (const RibBtn& it : large) {
            auto* b = new QToolButton; b->setDefaultAction(it.act);
            b->setText(QString::fromUtf8(it.label));
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setIconSize(QSize(26, 26)); b->setAutoRaise(true);
            b->setFixedSize(54, 58); b->setFocusPolicy(Qt::NoFocus);
            ribBtn.insert(it.act, b); content->addWidget(b);
        }
        if (!small.isEmpty()) {
            auto* grid = new QGridLayout; grid->setSpacing(1); grid->setContentsMargins(2, 0, 0, 0);
            for (int i = 0; i < small.size(); ++i) {
                auto* b = new QToolButton; b->setDefaultAction(small[i]);
                b->setToolButtonStyle(Qt::ToolButtonIconOnly);
                b->setIconSize(QSize(18, 18)); b->setAutoRaise(true);
                b->setFixedSize(24, 24); b->setFocusPolicy(Qt::NoFocus);
                ribBtn.insert(small[i], b);
                grid->addWidget(b, i % 3, i / 3);   // 3 linhas, preenche por coluna
            }
            content->addLayout(grid);
        }
        v->addLayout(content);
        auto* lbl = new QLabel(title); lbl->setObjectName("ribbonPanelTitle");
        lbl->setAlignment(Qt::AlignHCenter); v->addWidget(lbl);
        return panel;
    };

    // Ação que abre/fecha o painel lateral (Propriedades/Camadas) — vai p/ a aba Ver.
    auto* aPanel = new QAction("Painel", this);
    aPanel->setCheckable(true);
    aPanel->setIcon(toolIcon("table"));

    // Painel CAMADAS na ribbon: lista suspensa (camada corrente) + botão gerenciar.
    m_ribbonLayerCombo = new QComboBox;
    m_ribbonLayerCombo->setMinimumWidth(130);
    m_ribbonLayerCombo->setToolTip("Camada corrente");
    for (const Layer& l : m_doc->layers().all())
        m_ribbonLayerCombo->addItem(layerSwatch(QColor(l.color.r, l.color.g, l.color.b)),
                                    QString::fromStdString(l.name));
    m_ribbonLayerCombo->setCurrentText(m_currentLayerName);
    // 'activated' = só na escolha do usuário; refresh DIFERIDO evita re-entrância
    // (limpar o próprio combo dentro do sinal dele crashava o app).
    connect(m_ribbonLayerCombo, &QComboBox::activated, this, [this] {
        const QString n = m_ribbonLayerCombo->currentText();
        if (n.isEmpty()) return;
        m_currentLayerName = n;
        m_view->setCurrentLayer(n.toStdString());
        QTimer::singleShot(0, this, [this] { refreshLayers(); });
    });
    auto* layPanel = new QFrame;
    auto* lv = new QVBoxLayout(layPanel);
    lv->setContentsMargins(8, 4, 8, 2); lv->setSpacing(3);
    auto* lrow = new QHBoxLayout; lrow->setSpacing(3);
    auto* bLayers = new QToolButton;
    bLayers->setIcon(toolIcon("layers")); bLayers->setToolTip("Gerenciar camadas");
    bLayers->setAutoRaise(true); bLayers->setIconSize(QSize(20, 20));
    bLayers->setFixedSize(28, 28); bLayers->setFocusPolicy(Qt::NoFocus);
    connect(bLayers, &QToolButton::clicked, this, [this] {
        if (m_sideTabs) m_sideTabs->setCurrentIndex(1);   // aba Camadas
        if (m_sideDock) { m_sideDock->show(); m_sideDock->raise(); }
    });
    lrow->addWidget(m_ribbonLayerCombo); lrow->addWidget(bLayers);
    lv->addLayout(lrow);
    auto* llbl = new QLabel("Camadas"); llbl->setObjectName("ribbonPanelTitle");
    llbl->setAlignment(Qt::AlignHCenter); lv->addWidget(llbl);

    // Painel PROPRIEDADES correntes (Cor/Tipo/Espessura), aplicadas às novas formas.
    auto* propPanel = new QFrame;
    auto* ppv = new QVBoxLayout(propPanel);
    ppv->setContentsMargins(8, 4, 8, 2); ppv->setSpacing(2);
    auto* cCol = new QComboBox; cCol->setMinimumWidth(116);
    cCol->addItem("ByLayer", QColor());            // QColor inválido = ByLayer
    const struct { const char* n; QColor c; } cols[] = {
        {"Vermelho", QColor(220, 70, 70)}, {"Amarelo", QColor(225, 200, 90)},
        {"Verde", QColor(120, 180, 120)}, {"Ciano", QColor(95, 180, 196)},
        {"Azul", QColor(95, 140, 210)}, {"Branco", QColor(235, 235, 235)}};
    for (const auto& c : cols) cCol->addItem(c.n, c.c);
    connect(cCol, &QComboBox::currentIndexChanged, this, [this, cCol](int) {
        const QColor c = cCol->currentData().value<QColor>();
        if (c.isValid()) m_view->setCurrentColorRgb(c.red(), c.green(), c.blue());
        else m_view->setCurrentColorByLayer();
    });
    auto* cLt = new QComboBox;
    cLt->addItem("ByLayer", "ByLayer"); cLt->addItem("Contínua", "CONTINUOUS");
    cLt->addItem("Tracejada", "DASHED"); cLt->addItem("Centro", "CENTER"); cLt->addItem("Oculta", "HIDDEN");
    for (const std::string& cn : customLineTypeNames())   // padrão de fábrica + custom
        cLt->addItem(QString::fromStdString(cn), QString::fromStdString(cn));
    connect(cLt, &QComboBox::currentIndexChanged, this, [this, cLt](int) {
        m_view->setCurrentLineTypeName(cLt->currentData().toString().toStdString()); });
    auto* cLw = new QComboBox;
    cLw->addItem("ByLayer", -1.0);
    for (double mm : {0.15, 0.25, 0.35, 0.50, 0.70, 1.00}) cLw->addItem(QString::number(mm, 'f', 2) + " mm", mm);
    connect(cLw, &QComboBox::currentIndexChanged, this, [this, cLw](int) {
        m_view->setCurrentLineWeightMm(cLw->currentData().toDouble()); });
    ppv->addWidget(cCol); ppv->addWidget(cLt); ppv->addWidget(cLw);
    auto* plbl = new QLabel("Propriedades"); plbl->setObjectName("ribbonPanelTitle");
    plbl->setAlignment(Qt::AlignHCenter); ppv->addWidget(plbl);

    // Painel TEXTO: formatação corrente (fonte/tamanho/negrito/itálico/cor) —
    // vale para os textos NOVOS e aplica na hora ao MText selecionado.
    auto* txtPanel = new QFrame;
    txtPanel->setObjectName("txtPanel");
    txtPanel->setStyleSheet(
        "QFrame#txtPanel QFontComboBox, QFrame#txtPanel QDoubleSpinBox{"
        " background:#1a1e27; color:#e8eaed; border:1px solid rgba(255,255,255,0.06);"
        " border-radius:6px; padding:1px 6px; min-height:20px;}"
        "QFrame#txtPanel QFontComboBox:hover, QFrame#txtPanel QDoubleSpinBox:hover{"
        " border:1px solid #c2a063;}"
        "QFrame#txtPanel QComboBox QAbstractItemView{background:#12151c; color:#e8eaed;"
        " selection-background-color:#c2a063; selection-color:#0b0d10;}"
        "QFrame#txtPanel QToolButton{background:transparent; color:#c9ccd1;"
        " border:1px solid transparent; border-radius:5px;"
        " min-width:24px; min-height:22px; font-size:12px;}"
        "QFrame#txtPanel QToolButton:hover{background:#222836;}"
        "QFrame#txtPanel QToolButton:checked{background:#c2a063; color:#16181c;}");
    auto* txv = new QVBoxLayout(txtPanel);
    txv->setContentsMargins(8, 4, 8, 2); txv->setSpacing(3);
    m_txtRibFont = new QFontComboBox;
    m_txtRibFont->setFixedWidth(164);
    m_txtRibFont->setToolTip("Fonte do texto — \"(traços)\" = fonte técnica de linhas");
    m_txtRibFont->insertItem(0, "(traços)");
    m_txtRibFont->setCurrentFont(QFont("Arial"));   // padrão: texto de verdade
    txv->addWidget(m_txtRibFont);
    auto* txRow = new QHBoxLayout; txRow->setSpacing(4);
    m_txtRibSize = new QDoubleSpinBox;
    m_txtRibSize->setRange(0.01, 1e6); m_txtRibSize->setDecimals(1);
    m_txtRibSize->setValue(5.0);
    m_txtRibSize->setFixedWidth(52);
    m_txtRibSize->setAlignment(Qt::AlignCenter);
    m_txtRibSize->setButtonSymbols(QAbstractSpinBox::NoButtons);   // campo limpo
    m_txtRibSize->setToolTip("Altura do texto (unidades de desenho)");
    m_txtRibBold = new QToolButton;
    m_txtRibBold->setText("N"); m_txtRibBold->setCheckable(true);
    { QFont bf = m_txtRibBold->font(); bf.setBold(true); m_txtRibBold->setFont(bf); }
    m_txtRibBold->setToolTip("Negrito (fontes TTF)");
    m_txtRibItalic = new QToolButton;
    m_txtRibItalic->setText("I"); m_txtRibItalic->setCheckable(true);
    { QFont itf = m_txtRibItalic->font(); itf.setItalic(true); m_txtRibItalic->setFont(itf); }
    m_txtRibItalic->setToolTip("Itálico (fontes TTF)");
    auto* txtColorBtn = new QPushButton;
    txtColorBtn->setFixedSize(24, 22);
    txtColorBtn->setCursor(Qt::PointingHandCursor);
    txtColorBtn->setToolTip("Cor do texto (aplica ao selecionado)");
    const auto swatchQss = [](const QColor& c) {
        return QString("QPushButton{background:%1; border:1px solid rgba(255,255,255,0.14);"
                       " border-radius:5px;}"
                       "QPushButton:hover{border:1px solid #c2a063;}").arg(c.name());
    };
    txtColorBtn->setStyleSheet(swatchQss(QColor(0xe8, 0xea, 0xed)));
    txRow->addWidget(m_txtRibSize);
    txRow->addStretch(1);
    txRow->addWidget(m_txtRibBold);
    txRow->addWidget(m_txtRibItalic);
    txRow->addWidget(txtColorBtn);
    txv->addLayout(txRow);
    txv->addStretch(1);
    auto* txLbl = new QLabel("Texto"); txLbl->setObjectName("ribbonPanelTitle");
    txLbl->setAlignment(Qt::AlignHCenter); txv->addWidget(txLbl);

    auto onTextFmt = [this] {
        if (m_txtRibUpdating) return;
        const std::string fam = (m_txtRibFont->currentIndex() == 0)
                              ? std::string() : m_txtRibFont->currentText().toStdString();
        m_view->setAnnotationFont(fam);
        m_view->setAnnotationHeight(m_txtRibSize->value());
        m_view->setAnnotationBold(m_txtRibBold->isChecked());
        m_view->setAnnotationItalic(m_txtRibItalic->isChecked());
        TextStyle s = m_styles.currentText();     // o estilo corrente acompanha
        s.font = fam; s.height = m_txtRibSize->value();
        m_styles.addText(s);
        applyTextRibbonToSelection();
    };
    connect(m_txtRibFont, &QFontComboBox::currentIndexChanged, this, [onTextFmt](int) { onTextFmt(); });
    connect(m_txtRibSize, &QDoubleSpinBox::valueChanged, this, [onTextFmt](double) { onTextFmt(); });
    connect(m_txtRibBold, &QToolButton::toggled, this, [onTextFmt](bool) { onTextFmt(); });
    connect(m_txtRibItalic, &QToolButton::toggled, this, [onTextFmt](bool) { onTextFmt(); });
    connect(txtColorBtn, &QPushButton::clicked, this, [this, txtColorBtn, swatchQss] {
        const QColor c = QColorDialog::getColor(Qt::white, this, "Cor do texto");
        if (!c.isValid()) return;
        txtColorBtn->setStyleSheet(swatchQss(c));
        auto macro = std::make_unique<MacroCmd>("TEXTCOLOR");
        bool any = false;
        for (const EntityId id : m_view->selectedIds()) {
            const Entity* e = m_doc->getEntity(id);
            if (!e || !dynamic_cast<const MText*>(e)) continue;
            EntityPtr neu = e->clone();
            neu->setColor(ColorRef::explicitColor(Rgba{std::uint8_t(c.red()),
                                                       std::uint8_t(c.green()),
                                                       std::uint8_t(c.blue()), 255}));
            macro->add(std::make_unique<ReplaceCmd>(id, std::move(neu)));
            any = true;
        }
        if (any) { m_doc->execute(std::move(macro)); m_view->rebuild(); log("Cor do texto aplicada."); }
        else log("Cor do texto: selecione um texto primeiro (a cor de formas novas fica em Propriedades).");
    });

    auto* inicio = new QWidget;
    auto* ih = new QHBoxLayout(inicio);
    ih->setContentsMargins(6, 2, 6, 2); ih->setSpacing(0);
    ih->addWidget(densePanel("Desenhar", {{aLine,"Linha"},{aPoly,"Polil."},{aCirc,"Círculo"},{aArc,"Arco"}},
                             {aRect, aElli, aPolg, aSpl, aPt, aXLine, aHatch}));
    ih->addWidget(ribbonSeparator());
    ih->addWidget(densePanel("Modificar", {{aMove,"Mover"},{aCopy,"Copiar"},{aTrim,"Aparar"}},
                             {aRot, aMir, aScl, aOff, aExt, aFil, aCha, aArrR, aStr, aBreak, aJoin, aLeng, aErase, aSel}));
    ih->addWidget(ribbonSeparator());
    ih->addWidget(densePanel("Anotação", {{aText,"Texto"},{aDimL,"Cota"}},
                             {aLeader, aMLead, aTable, aRevCl}));
    ih->addWidget(ribbonSeparator());
    ih->addWidget(txtPanel);                       // Formatação de texto
    ih->addWidget(ribbonSeparator());
    ih->addWidget(layPanel);                       // Camadas (lista suspensa)
    ih->addWidget(ribbonSeparator());
    ih->addWidget(propPanel);                      // Propriedades correntes
    ih->addWidget(ribbonSeparator());
    ih->addWidget(densePanel("Inserir", {},
                             {aBlk, aInsert, aExpl, aWipe, aRegion, aBoolU, aBoolI, aBoolD, aArrP, aArrPath}));
    ih->addWidget(ribbonSeparator());
    ih->addWidget(densePanel("Medir", {}, {aArea, aMea, aDist, aInq, aMatch}));
    ih->addWidget(ribbonSeparator());
    ih->addWidget(densePanel("Ver", {}, {aZoomExt, aZoomWin, aZoomIn, aZoomOut,
                                         aZoomPrev, aPan, aThemeTgl, aPanel}));
    ih->addStretch(1);

    auto* ribbon = new QToolBar("Ribbon");
    ribbon->setObjectName("ribbon");
    ribbon->setMovable(false);
    ribbon->setFloatable(false);
    ribbon->addWidget(inicio);
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, ribbon);

    // --- Menus superiores por categoria -------------------------------------
    // Todas as ferramentas continuam acessíveis: as comuns também aparecem aqui
    // (espelhando a barra esquerda) e as especializadas vivem SÓ no menu.
    {
        QMenu* mDraw = menuBar()->addMenu("Desenho");
        mDraw->addAction(aLine);
        mDraw->addAction(aCirc);
        mDraw->addAction(aC2P);
        mDraw->addAction(aC3P);
        mDraw->addAction(aTTR);
        mDraw->addAction(aTTT);
        mDraw->addSeparator();
        mDraw->addAction(aArc);     // Arco 3P
        mDraw->addAction(aASCE);
        mDraw->addAction(aACSE);
        mDraw->addAction(aASER);
        mDraw->addAction(aASEA);
        mDraw->addAction(aASED);
        mDraw->addSeparator();
        mDraw->addAction("Parede (eixo + espessura)", this, [this] {
            m_view->setTool(ToolKind::WallTool);
            log(QString("Parede (espessura %1): clique os pontos do EIXO — Enter finaliza, "
                        "C fecha o anel; digite um número + Enter para mudar a espessura.")
                    .arg(m_view->wallThickness()));
        });
        mDraw->addAction("Porta (na parede ou 3 pontos)", this, [this] {
            m_view->setTool(ToolKind::Door);
            log("Porta: clique na PAREDE (dobradiça), a outra borda do vão e o LADO de abertura.");
        });
        mDraw->addAction("Janela (na parede)", this, [this] {
            m_view->setTool(ToolKind::WindowTool);
            log("Janela: clique as 2 bordas do vão SOBRE uma parede.");
        });
        mDraw->addSeparator();
        mDraw->addAction(aRect);
        mDraw->addAction(aRChf);
        mDraw->addAction(aRFil);
        mDraw->addAction(aPolg);
        mDraw->addAction(aElli);
        mDraw->addAction(aEArc);
        mDraw->addSeparator();
        mDraw->addAction(aXLine);
        mDraw->addAction(aRay);
        mDraw->addAction(aMLine);
        mDraw->addAction(aPolyW);
        mDraw->addSeparator();
        mDraw->addAction(aPoly);
        mDraw->addAction(aSpl);
        mDraw->addAction(aSCv);
        mDraw->addAction(aPt);

        // Modificar — organizado em SUBMENUS (evita a coluna gigante de antes).
        QMenu* mMod = menuBar()->addMenu("Modificar");
        mMod->addAction(aSel);
        mMod->addSeparator();
        mMod->addAction(aMove);
        mMod->addAction(aCopy);
        mMod->addAction(aMir);
        mMod->addAction(aOff);
        mMod->addAction(aStr);
        mMod->addAction(aAlign);
        QMenu* smRot = mMod->addMenu("Rotacionar");
        smRot->addAction(aRot); smRot->addAction(aRotC); smRot->addAction(aRotR);
        QMenu* smScl = mMod->addMenu("Escalar");
        smScl->addAction(aScl); smScl->addAction(aSclC); smScl->addAction(aSclR);
        mMod->addSeparator();
        mMod->addAction(aTrim);
        mMod->addAction(aExt);
        mMod->addAction(aFil);
        mMod->addAction(aCha);
        mMod->addAction(aBreak);
        mMod->addAction(aJoin);
        mMod->addAction(aLeng);
        mMod->addSeparator();
        mMod->addAction("Emendar com spline (BLEND)", this, [this] { runCommand("BLEND"); });
        mMod->addAction("Inverter sentido (REVERSE)", this, [this] { runCommand("REVERSE"); });
        mMod->addAction("Limpar duplicatas (OVERKILL)", this, [this] { runCommand("OVERKILL"); });
        mMod->addSeparator();
        QMenu* smArr = mMod->addMenu("Matriz");
        smArr->addAction(aArrR); smArr->addAction(aArrP); smArr->addAction(aArrPath);
        QMenu* smBool = mMod->addMenu("Booleanas / Região");
        smBool->addAction(aBoolU); smBool->addAction(aBoolI); smBool->addAction(aBoolD);
        smBool->addSeparator(); smBool->addAction(aRegion);
        QMenu* smBlk = mMod->addMenu("Bloco");
        smBlk->addAction(aBlk); smBlk->addAction(aInsert); smBlk->addAction(aExpl);
        smBlk->addSeparator();
        smBlk->addAction("Definir atributo (ATTDEF)...", this, [this] { attDefInteractive(); });
        smBlk->addAction("Editar atributos (ATTEDIT)...", this, [this] { attEditInteractive(); });
        smBlk->addSeparator();
        smBlk->addAction("Referência externa (XREF)...", this, [this] { xrefManagerInteractive(); });
        smBlk->addAction("Visibilidade do bloco (estados)...", this, [this] { blockVisibilityInteractive(); });
        QMenu* smInq = mMod->addMenu("Consultar / Propriedades");
        smInq->addAction(aMatch); smInq->addAction(aInq); smInq->addAction(aDist);
        smInq->addSeparator(); smInq->addAction(aDiv); smInq->addAction(aMea);
        mMod->addSeparator();
        mMod->addAction(aErase);

        QMenu* mAnn = menuBar()->addMenu("Anotação");
        mAnn->addAction(aText);
        mAnn->addAction(aHatch);
        mAnn->addSeparator();
        QMenu* smDim = mAnn->addMenu("Cotas");
        smDim->addAction(aDimL); smDim->addAction(aDimA); smDim->addAction(aDimR);
        smDim->addAction(aDimD); smDim->addAction(aDimAng);
        smDim->addSeparator(); smDim->addAction(aDimC); smDim->addAction(aDimB);
        QMenu* smLead = mAnn->addMenu("Líderes");
        smLead->addAction(aLeader); smLead->addAction(aMLead);
        mAnn->addAction(aRevCl);
        mAnn->addSeparator();
        QMenu* smIns = mAnn->addMenu("Inserir");
        smIns->addAction(aWipe); smIns->addAction(aTable);
        mAnn->addSeparator();
        QMenu* smSty = mAnn->addMenu("Estilos e unidades");
        smSty->addAction(aDimSty); smSty->addAction(aTxtSty);
        smSty->addAction("Escala de anotação (ANNOSCALE)...", this,
                         [this] { runCommand("ANNOSCALE"); });
        smSty->addSeparator(); smSty->addAction(aUnits); smSty->addAction(aLtype);
        smSty->addAction("Escala de tipo de linha (LTSCALE)...", this, [this] {
            bool ok = false;
            const double s = QInputDialog::getDouble(this, "LTSCALE",
                "Escala global dos tipos de linha:", m_view->lineTypeScale(),
                0.01, 1000.0, 2, &ok);
            if (ok) { m_view->setLineTypeScale(s); log(QString("LTSCALE = %1").arg(s)); }
        });

        // Estende o menu "Arquivo" (criado em buildMenuBar) com import/export,
        // inseridos no topo (antes de "Novo desenho").
        if (m_fileMenu && !m_fileMenu->actions().isEmpty()) {
            QAction* first = m_fileMenu->actions().constFirst();
            m_fileMenu->insertAction(first, aSaveDxf);
            m_fileMenu->insertAction(first, aOpenDxf);
            m_fileMenu->insertAction(first, aPdf);
            m_fileMenu->insertSeparator(first);
        }
    }

    // --- Flyouts do ribbon: clicar no ícone abre as variantes da forma --------
    // O botão principal mantém o desenho rápido (variante default) e a setinha
    // de menu abre TODAS as variantes do grupo (estilo AutoCAD).
    {
        auto flyout = [&ribBtn](QAction* primary, std::initializer_list<QAction*> variants) {
            QToolButton* btn = ribBtn.value(primary, nullptr);   // botão NA RIBBON
            if (!btn) return;
            auto* menu = new QMenu(btn);
            for (QAction* v : variants) menu->addAction(v);
            btn->setMenu(menu);
            btn->setPopupMode(QToolButton::MenuButtonPopup);
        };
        flyout(aCirc, {aCirc, aC2P, aC3P, aTTR, aTTT});
        flyout(aArc,  {aArc, aASCE, aACSE, aASER, aASEA, aASED});
        flyout(aLine, {aLine, aXLine, aRay, aMLine});
        flyout(aPoly, {aPoly, aSpl, aSCv, aPt});
        flyout(aDimL, {aDimL, aDimA, aDimR, aDimD, aDimAng, aDimC, aDimB});   // variantes de cota
    }

    // Status bar (coordenadas + SNAP/GRID/ORTHO).
    auto* sb = new CadStatusBar(this);
    setStatusBar(sb);
    // Toggles da status bar valem para a sessão CORRENTE (lambdas via m_view).
    connect(sb, &CadStatusBar::snapToggled,  this, [this](bool b) { m_view->setSnapEnabled(b); });
    connect(sb, &CadStatusBar::gridToggled,  this, [this](bool b) { m_view->setGridOn(b); });
    connect(sb, &CadStatusBar::orthoToggled, this, [this](bool b) { m_view->setOrthoOn(b); });
    connect(sb, &CadStatusBar::otrackToggled, this, [this](bool b) { m_view->setOtrackEnabled(b); });
    connect(sb, &CadStatusBar::polarToggled, this, [this](bool b) { m_view->setPolarOn(b); });
    connect(sb, &CadStatusBar::polarIncrementChanged, this,
            [this](double d) { m_view->setPolarIncrement(d); });
    connect(sb, &CadStatusBar::polarIncrementsChanged, this, [this](const QVector<double>& v) {
        m_view->setPolarIncrements(std::vector<double>(v.begin(), v.end()));
        QStringList lbl;
        for (double d : v) lbl << QString::number(d);
        log(v.isEmpty() ? QStringLiteral("Polar: nenhum incremento (só ângulos adicionais).")
                        : QStringLiteral("Polar: incrementos %1°.").arg(lbl.join("°, ")));
    });
    connect(sb, &CadStatusBar::polarCustomizeRequested, this, [this, sb] {
        // POLAR personalizado: incremento livre + ângulos ADICIONAIS absolutos.
        QDialog dlg(this);
        dlg.setWindowTitle("Rastreamento polar");
        auto* form = new QFormLayout(&dlg);
        auto* spInc = new QDoubleSpinBox;
        spInc->setRange(0.0, 180.0); spInc->setDecimals(2); spInc->setSuffix(" graus");
        spInc->setSpecialValueText("0 (só os ângulos adicionais)");   // sem múltiplos
        spInc->setValue(m_view->polarIncrement());
        QString extras;
        for (double a : m_view->polarExtraAngles())
            extras += (extras.isEmpty() ? "" : ", ") + QString::number(a);
        auto* edExtras = new QLineEdit(extras);
        edExtras->setPlaceholderText("ex.: 22.5, 67, 105");
        form->addRow("Incremento:", spInc);
        form->addRow("Ângulos adicionais:", edExtras);
        auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(bbox);
        connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        m_view->setPolarIncrement(spInc->value());
        sb->setPolarIncrementChecked(spInc->value());   // sincroniza o check do menu
        std::vector<double> degs;
        for (const QString& tok : edExtras->text().split(',', Qt::SkipEmptyParts)) {
            bool ok = false;
            const double v = tok.trimmed().toDouble(&ok);   // decimal por PONTO (ex.: 22.5)
            if (ok) degs.push_back(v);
        }
        const std::size_t n = degs.size();
        m_view->setPolarExtraAngles(std::move(degs));
        log(QString("Polar: incremento %1° + %2 ângulo(s) adicional(is).")
                .arg(spInc->value()).arg(n));
    });
    connect(sb, &CadStatusBar::snapMaskChanged, this,
            [this](unsigned m2) { m_view->setSnapMask(m2); });
    connect(sb, &CadStatusBar::gridSnapToggled, this,
            [this](bool b) { m_view->setGridSnapOn(b); });
    m_zoom = new QLabel("100%");
    m_zoom->setStyleSheet("padding:0 10px;");   // cor herda do QSS (adapta ao tema)
    sb->addPermanentWidget(m_zoom);
    // Connects do VIEWPORT (por instância) + sync inicial dos toggles.
    wireViewport(m_view);

    // Atalhos de função (estilo AutoCAD): F7 grade, F8 ortho, F9 snap, F11 otrack.
    connect(new QShortcut(QKeySequence(Qt::Key_F7),  this), &QShortcut::activated, sb, &CadStatusBar::toggleGrid);
    connect(new QShortcut(QKeySequence(Qt::Key_F8),  this), &QShortcut::activated, sb, &CadStatusBar::toggleOrtho);
    connect(new QShortcut(QKeySequence(Qt::Key_F9),  this), &QShortcut::activated, sb, &CadStatusBar::toggleGridSnap);  // F9 = Grid Snap (AutoCAD)
    connect(new QShortcut(QKeySequence(Qt::Key_F3),  this), &QShortcut::activated, sb, &CadStatusBar::toggleSnap);      // F3 = OSNAP (AutoCAD)
    connect(new QShortcut(QKeySequence(Qt::Key_F11), this), &QShortcut::activated, sb, &CadStatusBar::toggleOtrack);
    connect(new QShortcut(QKeySequence(Qt::Key_F10), this), &QShortcut::activated, sb, &CadStatusBar::togglePolar);   // F10 = Polar (AutoCAD)

    // Painel lateral à direita com ABAS (Propriedades / Camadas) — estilo do mockup.
    // Painel como JANELA-FERRAMENTA flutuante (Qt::Tool) — pop-up robusto, sem os
    // bugs de QDockWidget flutuante (que crashavam ao mostrar).
    auto* sideDock = new QWidget(this, Qt::Tool);
    sideDock->setWindowTitle("Painel");
    auto* sideOuter = new QVBoxLayout(sideDock);
    sideOuter->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(sideDock);
    tabs->setDocumentMode(true);

    auto* propTab = new QWidget(tabs);
    auto* pv = new QVBoxLayout(propTab);
    pv->setContentsMargins(12, 12, 12, 12);
    pv->setSpacing(8);
    m_props = new QLabel("Nenhum objeto selecionado", propTab);
    m_props->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_props->setWordWrap(true);
    m_props->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pv->addWidget(m_props);

    // Editores inline (camada/cor/tipo/espessura) — ocultos sem seleção.
    m_propEdit = new QWidget(propTab);
    auto* form = new QFormLayout(m_propEdit);
    form->setContentsMargins(0, 6, 0, 0);
    form->setSpacing(6);
    m_propLayer   = new QComboBox(m_propEdit);
    m_propColor   = new QPushButton("Mudar cor...", m_propEdit);
    m_propLtype   = new QComboBox(m_propEdit);
    m_propLweight = new QComboBox(m_propEdit);
    m_propLtype->addItem("ByLayer", "ByLayer");
    m_propLtype->addItem("Contínua",  "CONTINUOUS");
    m_propLtype->addItem("Tracejada", "DASHED");
    m_propLtype->addItem("Centro",    "CENTER");
    m_propLtype->addItem("Oculta",    "HIDDEN");
    for (const std::string& cn : customLineTypeNames())   // padrão de fábrica + custom
        m_propLtype->addItem(QString::fromStdString(cn), QString::fromStdString(cn));
    m_propLweight->addItem("Padrão", -1.0);
    for (double mm : {0.15, 0.25, 0.35, 0.50, 0.70, 1.00, 2.00})
        m_propLweight->addItem(QString::number(mm, 'f', 2), mm);
    m_propLweight->setEditable(true);              // permite digitar uma espessura custom (mm)
    m_propLweight->setInsertPolicy(QComboBox::NoInsert);
    connect(m_propLweight->lineEdit(), &QLineEdit::editingFinished, this, [this] { applySelectedProp(); });
    form->addRow("Camada:", m_propLayer);
    form->addRow("Cor:", m_propColor);
    form->addRow("Tipo:", m_propLtype);
    form->addRow("Espessura:", m_propLweight);
    pv->addWidget(m_propEdit);

    // Campos numéricos de geometria (coordenadas/raio) — relabel por tipo.
    m_geoEdit = new QWidget(propTab);
    m_geoForm = new QFormLayout(m_geoEdit);
    m_geoForm->setContentsMargins(0, 6, 0, 0);
    m_geoForm->setSpacing(6);
    for (int i = 0; i < 4; ++i) {
        m_geo[i] = new QDoubleSpinBox(m_geoEdit);
        m_geo[i]->setRange(-1e7, 1e7);
        m_geo[i]->setDecimals(3);
        m_geo[i]->setKeyboardTracking(false);   // só dispara ao confirmar/sair
        m_geoLbl[i] = new QLabel(QString(), m_geoEdit);
        m_geoForm->addRow(m_geoLbl[i], m_geo[i]);
        connect(m_geo[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double) { applyGeometry(); });
    }
    pv->addWidget(m_geoEdit);

    // Editor de TEXTO (aparece só quando um MTEXT está selecionado).
    m_txtEdit = new QWidget(propTab);
    auto* txtForm = new QFormLayout(m_txtEdit);
    txtForm->setContentsMargins(0, 6, 0, 0);
    txtForm->setSpacing(6);
    auto* txtBtn = new QPushButton("Editar texto…", m_txtEdit);
    connect(txtBtn, &QPushButton::clicked, this, [this] { editTextContent(); });
    m_txtHeight = new QDoubleSpinBox(m_txtEdit);
    m_txtHeight->setRange(0.01, 1e6); m_txtHeight->setDecimals(2);
    m_txtHeight->setKeyboardTracking(false);
    m_txtRot = new QDoubleSpinBox(m_txtEdit);
    m_txtRot->setRange(-360.0, 360.0); m_txtRot->setDecimals(1); m_txtRot->setSuffix("°");
    m_txtRot->setKeyboardTracking(false);
    m_txtJustify = new QComboBox(m_txtEdit);
    m_txtJustify->addItem("Esquerda", 0); m_txtJustify->addItem("Centro", 1); m_txtJustify->addItem("Direita", 2);
    txtForm->addRow(txtBtn);
    txtForm->addRow("Altura:", m_txtHeight);
    txtForm->addRow("Ângulo:", m_txtRot);
    txtForm->addRow("Alinhar:", m_txtJustify);
    connect(m_txtHeight, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyTextProp(); });
    connect(m_txtRot,    QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { applyTextProp(); });
    connect(m_txtJustify, &QComboBox::currentIndexChanged, this, [this](int) { applyTextProp(); });
    pv->addWidget(m_txtEdit);

    pv->addStretch(1);
    m_propEdit->hide();
    m_geoEdit->hide();
    m_txtEdit->hide();
    tabs->addTab(propTab, "Propriedades");

    // (selectionChanged/editText/editBlockAttrs conectados em wireViewport.)
    connect(m_propLayer,   &QComboBox::currentIndexChanged, this, [this](int) { applySelectedProp(); });
    connect(m_propLtype,   &QComboBox::currentIndexChanged, this, [this](int) { applySelectedProp(); });
    connect(m_propLweight, &QComboBox::currentIndexChanged, this, [this](int) { applySelectedProp(); });
    connect(m_propColor,   &QPushButton::clicked, this, [this] {
        if (m_propUpdating) return;
        const std::vector<EntityId> ids = m_view->selectedIds();
        if (ids.empty()) return;
        const Entity* first = m_doc->getEntity(ids.front());
        const Rgba cur = first ? first->resolveColor(m_doc->layers()) : Rgba{255, 255, 255, 255};
        const QColor picked = QColorDialog::getColor(QColor(cur.r, cur.g, cur.b), this, "Cor do objeto");
        if (!picked.isValid()) return;
        const ColorRef col = ColorRef::explicitColor(Rgba{
            std::uint8_t(picked.red()), std::uint8_t(picked.green()),
            std::uint8_t(picked.blue()), 255});
        auto macro = std::make_unique<MacroCmd>("COR");
        for (const EntityId id : ids)
            if (const Entity* e = m_doc->getEntity(id)) {
                EntityPtr neu = e->clone();
                neu->setColor(col);
                macro->add(std::make_unique<ReplaceCmd>(id, std::move(neu)));
            }
        if (!macro->empty()) m_doc->execute(std::move(macro));
        m_view->rebuild();
        updateProperties();
    });

    m_layers = new LayersPanel(tabs);
    tabs->addTab(m_layers, "Camadas");

    sideOuter->addWidget(tabs);
    sideDock->resize(300, 470);
    m_sideDock = sideDock; m_sideTabs = tabs;
    connect(aPanel, &QAction::toggled, this, [this](bool on) {
        if (!m_sideDock) return;
        if (on) { m_sideDock->show(); m_sideDock->raise(); } else m_sideDock->hide();
    });
    sideDock->hide();   // fechado por padrão — abre por "Painel" (Ver) ou "Camadas"
    connect(m_layers, &LayersPanel::currentLayerChanged, this, [this](const QString& n) {
        m_currentLayerName = n;
        m_view->setCurrentLayer(n.toStdString());
        refreshLayers();
        log("Camada corrente: " + n);
    });
    connect(m_layers, &LayersPanel::visibilityToggled, this, [this](const QString& n, bool vis) {
        if (Layer* l = m_doc->layers().find(n.toStdString())) l->on = vis;   // ON/OFF (lâmpada)
        m_view->rebuild();
    });
    connect(m_layers, &LayersPanel::freezeToggled, this, [this](const QString& n, bool frozen) {
        if (Layer* l = m_doc->layers().find(n.toStdString())) l->frozen = frozen;
        m_view->rebuild();
        log(QString("Camada '%1' %2.").arg(n, frozen ? "congelada" : "descongelada"));
    });
    connect(m_layers, &LayersPanel::isolateRequested, this, [this](const QString& n) {
        // Isolar: liga só a camada escolhida, desliga as outras (LAYISO).
        for (const Layer& L : m_doc->layers().all())
            if (Layer* l = m_doc->layers().find(L.name)) l->on = (l->name == n.toStdString());
        m_view->rebuild();
        refreshLayers();
        log("Camada isolada: " + n + " (use 'Mostrar todas' para reverter).");
    });
    connect(m_layers, &LayersPanel::showAllRequested, this, [this] {
        for (const Layer& L : m_doc->layers().all())
            if (Layer* l = m_doc->layers().find(L.name)) { l->on = true; l->frozen = false; }
        m_view->rebuild();
        refreshLayers();
        log("Todas as camadas ligadas.");
    });
    connect(m_layers, &LayersPanel::colorClicked, this, [this](const QString& n) {
        Layer* l = m_doc->layers().find(n.toStdString());
        if (!l) return;
        const QColor c = QColorDialog::getColor(
            QColor(l->color.r, l->color.g, l->color.b), this, "Cor da camada");
        if (c.isValid()) {
            l->color = Rgba{std::uint8_t(c.red()), std::uint8_t(c.green()),
                            std::uint8_t(c.blue()), 255};
            m_view->rebuild();
            refreshLayers();
        }
    });
    connect(m_layers, &LayersPanel::linetypeChanged, this, [this](const QString& n, const QString& lt) {
        if (Layer* l = m_doc->layers().find(n.toStdString())) l->lineType.name = lt.toStdString();
        m_view->rebuild();
    });
    connect(m_layers, &LayersPanel::lockToggled, this, [this](const QString& n, bool on) {
        if (Layer* l = m_doc->layers().find(n.toStdString())) l->locked = on;
        log(QString("Camada '%1' %2.").arg(n, on ? "travada" : "destravada"));
    });
    connect(m_layers, &LayersPanel::lineweightChanged, this, [this](const QString& n, double mm) {
        if (Layer* l = m_doc->layers().find(n.toStdString())) l->lineWeight.mm = mm;
        m_view->rebuild();
    });
    connect(m_layers, &LayersPanel::newLayerRequested, this, [this](const QString& n) {
        Layer l; l.name = n.toStdString();
        m_doc->layers().add(l);
        refreshLayers();
    });
    connect(m_layers, &LayersPanel::renameRequested, this,
            [this](const QString& oldN, const QString& newN) {
        if (m_doc->layers().contains(newN.toStdString())) {
            QMessageBox::warning(this, "Renomear camada",
                                 "Já existe uma camada chamada \"" + newN + "\".");
            return;
        }
        if (!m_doc->renameLayer(oldN.toStdString(), newN.toStdString())) {
            log("Renomear recusado (camada 0 ou nome inválido).");
            return;
        }
        if (m_currentLayerName == oldN) {
            m_currentLayerName = newN;
            m_view->setCurrentLayer(newN.toStdString());
        }
        m_view->rebuild();
        refreshLayers();
        log(QString("Camada \"%1\" renomeada para \"%2\".").arg(oldN, newN));
    });
    connect(m_layers, &LayersPanel::deleteRequested, this, [this](const QString& n) {
        const std::size_t uso = m_doc->layerUsage(n.toStdString());
        if (uso > 0) {
            const auto r = QMessageBox::question(
                this, "Excluir camada",
                QString("A camada \"%1\" tem %2 entidade(s).\n"
                        "Mover as entidades para a camada 0 e excluir?").arg(n).arg(uso));
            if (r != QMessageBox::Yes) return;
        }
        if (!m_doc->removeLayer(n.toStdString(), true)) {
            log("Excluir recusado (camada 0).");
            return;
        }
        if (m_currentLayerName == n) {
            m_currentLayerName = "0";
            m_view->setCurrentLayer("0");
        }
        m_view->rebuild();
        refreshLayers();
        log(QString("Camada \"%1\" excluída%2.").arg(n)
                .arg(uso > 0 ? " (entidades movidas para a 0)" : ""));
    });
    connect(m_layers, &LayersPanel::transparencyRequested, this, [this](const QString& n) {
        Layer* l = m_doc->layers().find(n.toStdString());
        if (!l) return;
        bool ok = false;
        const int t = QInputDialog::getInt(this, "Transparência da camada",
            QString("Transparência de \"%1\" (0 = opaca, até 90%):").arg(n),
            l->transparency, 0, 90, 5, &ok);
        if (!ok) return;
        l->transparency = t;
        m_doc->invalidateAll();
        m_view->rebuild();
        log(QString("Camada \"%1\": transparência %2%.").arg(n).arg(t));
    });
    connect(m_layers, &LayersPanel::layerStatesRequested, this, [this] {
        QDialog dlg(this);
        dlg.setWindowTitle("Estados de camada");
        dlg.resize(340, 300);
        auto* v = new QVBoxLayout(&dlg);
        auto* list = new QListWidget(&dlg);
        auto reload = [&] {
            list->clear();
            for (const auto& kv : m_doc->layerStates())
                list->addItem(QString::fromStdString(kv.first));
        };
        reload();
        v->addWidget(list, 1);
        auto* row = new QHBoxLayout;
        auto* bSave  = new QPushButton("Salvar atual...");
        auto* bApply = new QPushButton("Aplicar");
        auto* bDel   = new QPushButton("Excluir");
        auto* bClose = new QPushButton("Fechar");
        row->addWidget(bSave); row->addWidget(bApply); row->addWidget(bDel);
        row->addStretch(); row->addWidget(bClose);
        v->addLayout(row);
        connect(bSave, &QPushButton::clicked, &dlg, [&] {
            bool ok = false;
            const QString n = QInputDialog::getText(&dlg, "Salvar estado",
                "Nome do estado:", QLineEdit::Normal,
                QString("Estado%1").arg(int(m_doc->layerStates().size()) + 1), &ok);
            if (ok && !n.trimmed().isEmpty()) {
                m_doc->saveLayerState(n.trimmed().toStdString());
                reload();
            }
        });
        connect(bApply, &QPushButton::clicked, &dlg, [&] {
            if (!list->currentItem()) return;
            if (m_doc->applyLayerState(list->currentItem()->text().toStdString())) {
                m_view->rebuild();
                refreshLayers();
                log("Estado de camada aplicado: " + list->currentItem()->text());
            }
        });
        connect(bDel, &QPushButton::clicked, &dlg, [&] {
            if (!list->currentItem()) return;
            m_doc->removeLayerState(list->currentItem()->text().toStdString());
            reload();
        });
        connect(bClose, &QPushButton::clicked, &dlg, [&] { dlg.accept(); });
        dlg.exec();
    });
    connect(m_layers, &LayersPanel::purgeRequested, this, [this] {
        const auto removed = m_doc->purgeLayers(m_currentLayerName.toStdString());
        refreshLayers();
        if (removed.empty()) { log("Purge: nenhuma camada vazia para remover."); return; }
        QStringList nomes;
        for (const std::string& s : removed) nomes << QString::fromStdString(s);
        log("Purge: removida(s) " + nomes.join(", ") + ".");
    });

    // (Linha de comando agora é integrada ao layout central — criada acima.)
    m_cli->installEventFilter(this);        // Esc no campo = cancela/desmarca no canvas
    connect(m_cli, &QLineEdit::returnPressed, this, &MainWindow::onCommandEntered);

    // (seedDemo() aposentado: com a tela inicial e o multi-doc, o documento de
    //  partida nasce LIMPO — e a sessão em branco é reusada por Novo/Abrir.)
    refreshLayers();
    m_view->rebuild();

    // Semeia uma prancha A3 paisagem e monta as abas (Modelo | Prancha 1 | +).
    m_layouts.all().reserve(16);
    m_layouts.addDefault("Prancha 1");
    rebuildLayoutTabs();

    log("Pronto. Desenhe (Linha/Círculo) ou edite (Selecionar -> Mover/Copiar/Apagar).");
    log("Snap magnético ativo. Pan: botão do meio. Zoom: roda. Undo: U.");

    // Estilo de texto de fábrica (Arial) vale já no primeiro desenho da sessão.
    m_view->setAnnotationFont(m_styles.currentText().font);
    m_view->setAnnotationHeight(m_styles.currentText().height);
    m_view->setTextAnnotative(m_styles.currentText().annotative);

    updateWindowTitle();
    // Página inicial assim que o event loop sobe — pulada no modo --shot
    // (verificação headless), que vai direto ao desenho.
    QTimer::singleShot(0, this, [this] {
        if (m_suppressStart) showDrawing();
        else                 showStartPage();
    });
}

MainWindow::~MainWindow() = default;

// Refaz as abas de espaço: [ Modelo | <pranchas> | + ]. A última aba "+" cria
// uma nova prancha. Guardado por m_layoutTabsBusy p/ não disparar a troca ao
// repovoar programaticamente.
void MainWindow::rebuildLayoutTabs() {
    if (!m_layoutTabs) return;
    m_layoutTabsBusy = true;
    const int prev = m_layoutTabs->currentIndex();
    while (m_layoutTabs->count() > 0) m_layoutTabs->removeTab(0);
    m_layoutTabs->addTab("Modelo");
    for (const Layout& L : m_layouts.all())
        m_layoutTabs->addTab(QString::fromStdString(L.name));
    m_layoutTabs->addTab("+");
    m_layoutTabsBusy = false;
    const int last = m_layoutTabs->count() - 1;   // índice do "+"
    m_layoutTabs->setCurrentIndex((prev >= 0 && prev < last) ? prev : 0);
}

void MainWindow::onLayoutTabChanged(int index) {
    if (m_layoutTabsBusy || !m_layoutTabs) return;
    const int last = m_layoutTabs->count() - 1;   // índice do "+"
    if (index == last) {                          // "+": cria e entra na nova prancha
        m_layouts.addDefault("Prancha " + std::to_string(static_cast<int>(m_layouts.size()) + 1));
        rebuildLayoutTabs();
        m_layoutTabs->setCurrentIndex(static_cast<int>(m_layouts.size()));  // a recém-criada
        return;
    }
    if (index <= 0) {                             // "Modelo"
        m_view->setPaperMode(false, nullptr);
        log("Espaço do Modelo.");
    } else {                                      // prancha (index-1)
        const std::size_t li = static_cast<std::size_t>(index - 1);
        m_layouts.setCurrent(li);
        m_view->setPaperMode(true, &m_layouts.at(li));
        log(QString("Prancha: %1").arg(QString::fromStdString(m_layouts.at(li).name)));
    }
}

// Viewport desenhado na prancha -> pergunta a escala e insere na prancha corrente.
void MainWindow::addViewportToCurrent(double x, double y, double w, double h) {
    Layout* L = m_layouts.currentLayout();
    if (!L) return;
    const QStringList scales{ "1:1", "1:5", "1:10", "1:20", "1:25", "1:50",
                              "1:75", "1:100", "1:200", "Ajustar à janela" };
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, "Escala do viewport",
        "Escala (papel : modelo):", scales, 5 /*1:50*/, false, &ok);
    if (!ok) return;

    // Centro do modelo = centro do bounding-box do desenho visível.
    AABB bb;
    const LayerTable& layers = m_doc->layers();
    m_doc->forEach([&](const Entity& e) {
        const Layer* lay = layers.find(e.layer());
        if (lay && (!lay->on || lay->frozen)) return;
        RenderBatch b; e.emitTo(b);
        for (const Point3& v : b.lineVertices) bb.expand(v);
        for (const Point3& v : b.fillVertices) bb.expand(v);
    });

    SheetViewport vp;
    vp.xMm = x; vp.yMm = y; vp.wMm = w; vp.hMm = h;
    if (bb.valid()) { vp.modelCx = (bb.min.x + bb.max.x) * 0.5; vp.modelCy = (bb.min.y + bb.max.y) * 0.5; }
    const double unitMm = unitLengthMm(m_unitIndex);
    if (choice.startsWith("Ajustar")) {
        if (bb.valid()) {
            const double dx = std::max(bb.max.x - bb.min.x, 1e-9);
            const double dy = std::max(bb.max.y - bb.min.y, 1e-9);
            const double k = std::min((w * 0.9) / dx, (h * 0.9) / dy);   // mm de papel por unidade
            vp.mmPerUnit = k;
            vp.scaleDenom = (k > 0.0) ? unitMm / k : 1.0;
        } else { vp.mmPerUnit = 1.0; vp.scaleDenom = 1.0; }
    } else {
        const double denom = choice.mid(2).toDouble();   // "1:50" -> 50
        vp.scaleDenom = denom;
        vp.mmPerUnit  = scaleMmPerUnit(unitMm, denom);
    }
    L->scaleLabel = formatScale(vp.scaleDenom);   // ESCALA do selo reflete o último viewport
    L->viewports.push_back(vp);
    m_view->refreshPaper();
    log(QString("Viewport criado — escala %1.").arg(QString::fromStdString(formatScale(vp.scaleDenom))));
}

// Config. de Página: tamanho/orientação/margem + campos do selo da prancha corrente.
void MainWindow::openPaperSetup() {
    if (!m_view->paperMode()) { log("Entre numa prancha para configurar a página."); return; }
    Layout* L = m_layouts.currentLayout();
    if (!L) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Configurar página");
    auto* form = new QFormLayout(&dlg);
    auto* cbSize = new QComboBox; cbSize->addItems({ "A4", "A3", "A2", "A1", "A0" });
    cbSize->setCurrentIndex(static_cast<int>(L->paper));
    auto* cbOri = new QComboBox; cbOri->addItems({ "Paisagem", "Retrato" });
    cbOri->setCurrentIndex(L->landscape ? 0 : 1);
    auto* spMargin = new QDoubleSpinBox;
    spMargin->setRange(0.0, 50.0); spMargin->setValue(L->marginMm); spMargin->setSuffix(" mm");
    auto* edTitle  = new QLineEdit(QString::fromStdString(L->title));
    auto* edProj   = new QLineEdit(QString::fromStdString(L->project));
    auto* edAuthor = new QLineEdit(QString::fromStdString(L->author));
    auto* edDate   = new QLineEdit(QString::fromStdString(L->date));
    form->addRow("Tamanho:", cbSize);
    form->addRow("Orientação:", cbOri);
    form->addRow("Margem:", spMargin);
    form->addRow("Título:", edTitle);
    form->addRow("Projeto:", edProj);
    form->addRow("Autor:", edAuthor);
    form->addRow("Data:", edDate);
    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bbox);
    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    L->paper     = static_cast<PaperSize>(cbSize->currentIndex());
    L->landscape = (cbOri->currentIndex() == 0);
    L->marginMm  = spMargin->value();
    L->title     = edTitle->text().toStdString();
    L->project   = edProj->text().toStdString();
    L->author    = edAuthor->text().toStdString();
    L->date      = edDate->text().toStdString();
    m_view->refreshPaper();
    log("Página configurada.");
}

// Plota a prancha corrente em PDF no TAMANHO REAL do papel: moldura + selo em
// 1:1 e cada viewport com o Modelo clipado e escalado (a régua bate no impresso).
// ============================================================================
//  XREF — referências externas a outros .zencad (v1: caminho relativo)
// ============================================================================

QString MainWindow::xrefAbsolutePath(const QString& stored) const {
    const QFileInfo fi(stored);
    if (fi.isAbsolute()) return fi.absoluteFilePath();
    if (m_projectPath.isEmpty()) return stored;
    return QDir(QFileInfo(m_projectPath).absolutePath()).absoluteFilePath(stored);
}

// Relê o arquivo do XREF, redefine o bloco e reconstrói TODAS as inserções
// (preservando transformação, atributos, estados de visibilidade e props).
// false = arquivo ausente/ilegível (o snapshot salvo no projeto continua).
bool MainWindow::xrefReload(const std::string& name, QString* err) {
    const auto it = m_doc->xrefs().find(name);
    if (it == m_doc->xrefs().end()) {
        if (err) *err = QStringLiteral("referência desconhecida.");
        return false;
    }
    const QString abs = xrefAbsolutePath(QString::fromStdString(it->second));
    if (!QFileInfo::exists(abs)) {
        if (err) *err = QStringLiteral("arquivo ausente (%1) — usando o snapshot salvo.").arg(abs);
        return false;
    }
    const AABB world = AABB::fromCenterHalf({0, 0, 0}, {1e6, 1e6, 1e6});
    DrawingManager tmp(std::make_unique<Quadtree>(world, 12, 16));
    LayoutTable tl; StyleTable ts; ProjectSettings s2; QString lerr;
    if (!loadProject(abs, tmp, tl, ts, s2, &lerr)) {
        if (err) *err = lerr;
        return false;
    }
    // Camadas do externo que este doc não tem: cria (cores ByLayer resolvem).
    for (const Layer& l : tmp.layers().all())
        if (!m_doc->layers().contains(l.name)) m_doc->layers().add(l);
    BlockDefinition def;
    def.name = name;
    tmp.forEach([&](const Entity& e) { def.members.push_back(e.clone()); });
    m_doc->blocks().add(std::move(def));
    const BlockDefinition* nd = m_doc->blocks().find(name);
    std::vector<EntityId> refIds;
    m_doc->forEach([&](const Entity& e) {
        if (const auto* br = dynamic_cast<const BlockRef*>(&e))
            if (br->blockName() == name) refIds.push_back(e.id());
    });
    for (const EntityId id : refIds) {
        const auto* oldRef = static_cast<const BlockRef*>(m_doc->getEntity(id));
        auto fresh = BlockRef::fromDefinition(*nd, oldRef->xform());
        for (const auto& av : oldRef->attValues()) fresh->setAttValue(av.tag, av.value);
        fresh->setHiddenLayers(oldRef->hiddenLayers());
        fresh->setLayer(oldRef->layer());
        fresh->setColor(oldRef->color());
        fresh->setLineType(oldRef->lineType());
        fresh->setLineWeight(oldRef->lineWeight());
        m_doc->replaceEntity(id, std::move(fresh));
    }
    m_doc->invalidateAll();
    return true;
}

int MainWindow::reloadAllXrefs() {
    if (m_doc->xrefs().empty()) return 0;
    std::vector<std::string> names;
    for (const auto& kv : m_doc->xrefs()) names.push_back(kv.first);
    int ok = 0;
    for (const std::string& n : names) {
        QString err;
        if (xrefReload(n, &err)) ++ok;
        else log(QString("XREF \"%1\": %2").arg(QString::fromStdString(n), err));
    }
    if (ok > 0)
        log(QString("XREF: %1 referência(s) externa(s) recarregada(s).").arg(ok));
    return ok;
}

void MainWindow::xrefManagerInteractive() {
    QDialog dlg(this);
    dlg.setWindowTitle("Referências externas (XREF)");
    auto* lay = new QVBoxLayout(&dlg);
    auto* tbl = new QTableWidget(0, 3);
    tbl->setHorizontalHeaderLabels({"Nome", "Arquivo", "Status"});
    tbl->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tbl->verticalHeader()->setVisible(false);
    tbl->setSelectionBehavior(QAbstractItemView::SelectRows);
    tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto refresh = [&] {
        tbl->setRowCount(0);
        for (const auto& kv : m_doc->xrefs()) {
            const int r = tbl->rowCount();
            tbl->insertRow(r);
            tbl->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(kv.first)));
            tbl->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(kv.second)));
            const bool ok = QFileInfo::exists(
                xrefAbsolutePath(QString::fromStdString(kv.second)));
            tbl->setItem(r, 2, new QTableWidgetItem(ok ? "OK" : "AUSENTE (snapshot)"));
        }
    };
    refresh();
    lay->addWidget(tbl, 1);
    auto* row = new QHBoxLayout;
    auto* bAtt = new QPushButton("Anexar...");
    auto* bRel = new QPushButton("Recarregar");
    auto* bAll = new QPushButton("Recarregar todas");
    auto* bDet = new QPushButton("Desanexar");
    auto* bOk  = new QPushButton("Fechar");
    row->addWidget(bAtt); row->addWidget(bRel); row->addWidget(bAll);
    row->addWidget(bDet); row->addStretch(1); row->addWidget(bOk);
    lay->addLayout(row);
    auto rowName = [&]() -> std::string {
        const int r = tbl->currentRow();
        return (r >= 0 && tbl->item(r, 0)) ? tbl->item(r, 0)->text().toStdString()
                                           : std::string();
    };
    connect(bAtt, &QPushButton::clicked, &dlg, [&] {
        const QString p = QFileDialog::getOpenFileName(&dlg, "Anexar referência externa",
            QString(), "Projeto ZenCAD (*.zencad)");
        if (p.isEmpty()) return;
        if (!m_projectPath.isEmpty() &&
            QFileInfo(p).absoluteFilePath() == QFileInfo(m_projectPath).absoluteFilePath()) {
            QMessageBox::warning(&dlg, "XREF", "Um projeto não pode referenciar a si mesmo.");
            return;
        }
        std::string name = QFileInfo(p).completeBaseName().toUpper().toStdString();
        while (m_doc->blocks().contains(name) && !m_doc->xrefs().count(name))
            name += "_X";   // não atropela bloco local homônimo
        // Caminho RELATIVO ao projeto (compartilhável); absoluto se sem título.
        const QString stored = m_projectPath.isEmpty()
            ? QFileInfo(p).absoluteFilePath()
            : QDir(QFileInfo(m_projectPath).absolutePath()).relativeFilePath(p);
        m_doc->addXref(name, stored.toStdString());
        QString err;
        if (!xrefReload(name, &err)) {
            m_doc->removeXref(name);
            QMessageBox::warning(&dlg, "XREF", "Falha ao ler a referência:\n" + err);
            return;
        }
        refresh();
        // Arma a inserção: 1 clique posiciona o XREF (escala 1, rotação 0).
        m_view->setPendingInsert(name, 1.0, 0.0);
        m_view->setTool(ToolKind::Insert);
        log(QString("XREF \"%1\" anexado — clique o ponto de inserção.")
                .arg(QString::fromStdString(name)));
        dlg.accept();
    });
    connect(bRel, &QPushButton::clicked, &dlg, [&] {
        const std::string n = rowName();
        if (n.empty()) return;
        QString err;
        log(xrefReload(n, &err)
                ? QString("XREF \"%1\" recarregado.").arg(QString::fromStdString(n))
                : QString("XREF \"%1\": %2").arg(QString::fromStdString(n), err));
        m_view->rebuild();
        refresh();
    });
    connect(bAll, &QPushButton::clicked, &dlg, [&] {
        reloadAllXrefs();
        m_view->rebuild();
        refresh();
    });
    connect(bDet, &QPushButton::clicked, &dlg, [&] {
        const std::string n = rowName();
        if (n.empty()) return;
        m_doc->removeXref(n);
        log(QString("XREF \"%1\" desanexado — o bloco vira LOCAL (última geometria lida).")
                .arg(QString::fromStdString(n)));
        refresh();
    });
    connect(bOk, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.resize(560, 340);
    dlg.exec();
    m_view->rebuild();
}

// Estados de visibilidade da INSERÇÃO selecionada: liga/desliga as camadas
// internas do bloco só nesta inserção (bloco "dinâmico" v1).
void MainWindow::blockVisibilityInteractive() {
    const auto ids = m_view->selectedIds();
    const BlockRef* br = nullptr;
    EntityId bid = kInvalidId;
    for (const EntityId id : ids)
        if (const auto* b = dynamic_cast<const BlockRef*>(m_doc->getEntity(id))) {
            br = b; bid = id; break;
        }
    if (!br) { log("Visibilidade: selecione uma inserção de BLOCO primeiro."); return; }
    std::set<std::string> layers;
    for (const EntityPtr& m : br->members()) layers.insert(m->layer());
    if (layers.size() <= 1) {
        log("Visibilidade: este bloco tem uma única camada interna — desenhe as "
            "variações em camadas para alternar por inserção.");
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle("Visibilidade do bloco (por inserção)");
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel("Camadas internas visíveis NESTA inserção:"));
    auto* lst = new QListWidget;
    for (const std::string& n : layers) {
        auto* it = new QListWidgetItem(QString::fromStdString(n));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(br->hiddenLayers().count(n) ? Qt::Unchecked : Qt::Checked);
        lst->addItem(it);
    }
    lay->addWidget(lst, 1);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    dlg.resize(340, 300);
    if (dlg.exec() != QDialog::Accepted) return;
    std::set<std::string> hidden;
    for (int i = 0; i < lst->count(); ++i)
        if (lst->item(i)->checkState() != Qt::Checked)
            hidden.insert(lst->item(i)->text().toStdString());
    const int nHidden = int(hidden.size());
    auto neu = std::unique_ptr<BlockRef>(static_cast<BlockRef*>(br->clone().release()));
    neu->setHiddenLayers(std::move(hidden));
    m_doc->execute(std::make_unique<ReplaceCmd>(bid, std::move(neu)));   // com undo
    m_view->rebuild();
    log(QString("Visibilidade do bloco atualizada (%1 camada(s) oculta(s) nesta inserção).")
            .arg(nHidden));
}

namespace {
QPageSize::PageSizeId pageSizeIdFor(const Layout& L) {
    static const QPageSize::PageSizeId ids[5] = {
        QPageSize::A4, QPageSize::A3, QPageSize::A2, QPageSize::A1, QPageSize::A0 };
    return ids[static_cast<int>(L.paper)];
}
} // namespace

void MainWindow::plotCurrentLayout() {
    const Layout* L = m_layouts.currentLayout();
    if (!L) { log("Nenhuma prancha para plotar."); return; }
    if (L->viewports.empty()) { log("A prancha não tem viewports — nada a plotar."); return; }
    const QString path = QFileDialog::getSaveFileName(this, "Plotar prancha (PDF)",
        QString::fromStdString(L->name) + ".pdf", "PDF (*.pdf)");
    if (path.isEmpty()) return;

    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(pageSizeIdFor(*L)));
    pdf.setPageOrientation(L->landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    pdf.setResolution(300);
    QPainter p(&pdf);
    paintLayoutPage(*L, p, pdf.resolution(), pdf.height());
    p.end();
    log("Prancha plotada em escala: " + path);
}

// PUBLISH: todas as pranchas COM viewport num único PDF multipágina (uma
// página por prancha, cada uma no seu tamanho/orientação).
bool MainWindow::publishAllLayoutsTo(const QString& path) {
    std::vector<const Layout*> toPlot;
    for (const Layout& L : m_layouts.all())
        if (!L.viewports.empty()) toPlot.push_back(&L);
    if (toPlot.empty()) {
        log("PUBLISH: nenhuma prancha com viewport para plotar.");
        return false;
    }
    QPdfWriter pdf(path);
    pdf.setResolution(300);
    pdf.setPageSize(QPageSize(pageSizeIdFor(*toPlot[0])));
    pdf.setPageOrientation(toPlot[0]->landscape ? QPageLayout::Landscape
                                                : QPageLayout::Portrait);
    QPainter p(&pdf);
    if (!p.isActive()) {
        log(QString("PUBLISH: não foi possível gravar \"%1\".").arg(path));
        return false;
    }
    for (std::size_t i = 0; i < toPlot.size(); ++i) {
        if (i > 0) {   // tamanho/orientação da PRÓXIMA página antes do newPage
            pdf.setPageSize(QPageSize(pageSizeIdFor(*toPlot[i])));
            pdf.setPageOrientation(toPlot[i]->landscape ? QPageLayout::Landscape
                                                        : QPageLayout::Portrait);
            pdf.newPage();
        }
        paintLayoutPage(*toPlot[i], p, pdf.resolution(), pdf.height());
    }
    p.end();
    const int puladas = int(m_layouts.all().size()) - int(toPlot.size());
    log(QString("PUBLISH: %1 prancha(s) num único PDF: %2%3")
            .arg(toPlot.size()).arg(path)
            .arg(puladas > 0 ? QString(" (%1 sem viewport pulada(s))").arg(puladas)
                             : QString()));
    return true;
}

// Editor da tabela de ESTILOS DE PLOTAGEM (CTB por camada): o que cada camada
// vira no papel. Embutida no projeto; import/export .zctb para compartilhar.
void MainWindow::openPlotStyleDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle("Estilos de plotagem (CTB)");
    auto* lay = new QVBoxLayout(&dlg);

    auto* top = new QHBoxLayout;
    auto* name = new QLineEdit(QString::fromStdString(m_plotStyle.name));
    auto* act  = new QCheckBox("Usar ao plotar (tabela ativa)");
    act->setChecked(m_plotStyle.active);
    top->addWidget(new QLabel("Tabela:"));
    top->addWidget(name, 1);
    top->addWidget(act);
    lay->addLayout(top);

    const std::vector<Layer> layers = m_doc->layers().all();
    auto* tbl = new QTableWidget(int(layers.size()), 4);
    tbl->setHorizontalHeaderLabels({"Camada", "Plota", "Cor no papel", "Espessura (mm)"});
    tbl->verticalHeader()->setVisible(false);
    tbl->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tbl->setSelectionMode(QAbstractItemView::NoSelection);
    std::vector<Rgba> customColor(layers.size(), Rgba{0, 0, 0, 255});
    for (int r = 0; r < int(layers.size()); ++r) {
        const std::string& ln = layers[std::size_t(r)].name;
        const PlotLayerStyle* cur = m_plotStyle.find(ln);
        auto* it0 = new QTableWidgetItem(QString::fromStdString(ln));
        it0->setFlags(Qt::ItemIsEnabled);
        tbl->setItem(r, 0, it0);
        auto* chk = new QCheckBox;
        chk->setChecked(!cur || cur->plot);
        auto* chkWrap = new QWidget; auto* chkLay = new QHBoxLayout(chkWrap);
        chkLay->setContentsMargins(0, 0, 0, 0); chkLay->setAlignment(Qt::AlignCenter);
        chkLay->addWidget(chk);
        tbl->setCellWidget(r, 1, chkWrap);
        auto* cor = new QComboBox;
        cor->addItems({"Como desenhado", "Preto", "Tons de cinza", "Cor fixa..."});
        cor->setCurrentIndex(cur ? cur->colorMode : 0);
        if (cur && cur->colorMode == 3) customColor[std::size_t(r)] = cur->color;
        connect(cor, QOverload<int>::of(&QComboBox::activated), &dlg,
                [&customColor, r, cor, &dlg](int idx) {
            if (idx != 3) return;
            const Rgba& c0 = customColor[std::size_t(r)];
            const QColor c = QColorDialog::getColor(QColor(c0.r, c0.g, c0.b), &dlg,
                                                    "Cor de plotagem da camada");
            if (c.isValid())
                customColor[std::size_t(r)] = Rgba{std::uint8_t(c.red()),
                                                   std::uint8_t(c.green()),
                                                   std::uint8_t(c.blue()), 255};
            else cor->setCurrentIndex(0);            // cancelou o seletor
        });
        tbl->setCellWidget(r, 2, cor);
        auto* esp = new QDoubleSpinBox;
        esp->setRange(0.0, 3.0);
        esp->setDecimals(2);
        esp->setSingleStep(0.05);
        esp->setSpecialValueText("(como está)");     // 0.00 = não força
        esp->setValue(cur && cur->lineWeightMm >= 0.0 ? cur->lineWeightMm : 0.0);
        tbl->setCellWidget(r, 3, esp);
    }
    lay->addWidget(tbl, 1);

    auto* row = new QHBoxLayout;
    auto* bImp = new QPushButton("Importar .zctb...");
    auto* bExp = new QPushButton("Exportar .zctb...");
    row->addWidget(bImp); row->addWidget(bExp); row->addStretch(1);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    row->addWidget(bb);
    lay->addLayout(row);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // Lê a grade -> PlotStyleTable (entradas que divergem do padrão).
    auto collect = [&]() {
        PlotStyleTable t;
        t.name   = name->text().trimmed().isEmpty() ? "Padrao"
                                                    : name->text().trimmed().toStdString();
        t.active = act->isChecked();
        for (int r = 0; r < tbl->rowCount(); ++r) {
            const auto* chk = tbl->cellWidget(r, 1)->findChild<QCheckBox*>();
            const auto* cor = qobject_cast<QComboBox*>(tbl->cellWidget(r, 2));
            const auto* esp = qobject_cast<QDoubleSpinBox*>(tbl->cellWidget(r, 3));
            PlotLayerStyle e;
            e.layer        = tbl->item(r, 0)->text().toStdString();
            e.plot         = chk && chk->isChecked();
            e.colorMode    = cor ? cor->currentIndex() : 0;
            e.color        = customColor[std::size_t(r)];
            e.lineWeightMm = (esp && esp->value() > 0.0) ? esp->value() : -1.0;
            const bool padrao = e.plot && e.colorMode == 0 && e.lineWeightMm < 0.0;
            if (!padrao) t.entries.push_back(std::move(e));
        }
        return t;
    };

    connect(bExp, &QPushButton::clicked, &dlg, [this, &collect, &dlg] {
        const QString p = QFileDialog::getSaveFileName(&dlg, "Exportar estilos de plotagem",
            "estilos.zctb", "Estilos de plotagem ZenCAD (*.zctb)");
        if (p.isEmpty()) return;
        QFile f(p);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QJsonDocument(plotStyleToJson(collect())).toJson(QJsonDocument::Indented));
            log("CTB exportado: " + p);
        }
    });
    connect(bImp, &QPushButton::clicked, &dlg, [this, &dlg, name, act, tbl, &customColor] {
        const QString p = QFileDialog::getOpenFileName(&dlg, "Importar estilos de plotagem",
            QString(), "Estilos de plotagem ZenCAD (*.zctb)");
        if (p.isEmpty()) return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) return;
        const PlotStyleTable t = plotStyleFromJson(QJsonDocument::fromJson(f.readAll()).object());
        name->setText(QString::fromStdString(t.name));
        act->setChecked(true);
        for (int r = 0; r < tbl->rowCount(); ++r) {
            const PlotLayerStyle* e = t.find(tbl->item(r, 0)->text().toStdString());
            auto* chk = tbl->cellWidget(r, 1)->findChild<QCheckBox*>();
            auto* cor = qobject_cast<QComboBox*>(tbl->cellWidget(r, 2));
            auto* esp = qobject_cast<QDoubleSpinBox*>(tbl->cellWidget(r, 3));
            if (chk) chk->setChecked(!e || e->plot);
            if (cor) cor->setCurrentIndex(e ? e->colorMode : 0);
            if (e && e->colorMode == 3) customColor[std::size_t(r)] = e->color;
            if (esp) esp->setValue(e && e->lineWeightMm >= 0.0 ? e->lineWeightMm : 0.0);
        }
        log("CTB importado (camadas sem correspondência ficam no padrão): " + p);
    });

    dlg.resize(640, 480);
    if (dlg.exec() != QDialog::Accepted) return;
    m_plotStyle = collect();
    log(QString("Estilos de plotagem \"%1\" %2 (%3 camada(s) com ajuste).")
            .arg(QString::fromStdString(m_plotStyle.name))
            .arg(m_plotStyle.active ? "ATIVOS" : "salvos (inativos)")
            .arg(m_plotStyle.entries.size()));
}

// Renderiza a prancha como IMAGEM (papel branco) — base do preview/QA.
QImage MainWindow::renderLayoutImage(const Layout& L, double targetWidthPx) {
    const double W = L.widthMm(), H = L.heightMm();
    const double dpi = std::clamp(targetWidthPx / (W / 25.4), 30.0, 300.0);
    const int wpx = std::max(1, int(W / 25.4 * dpi));
    const int hpx = std::max(1, int(H / 25.4 * dpi));
    QImage img(wpx, hpx, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    paintLayoutPage(L, p, dpi, hpx);
    p.end();
    return img;
}

bool MainWindow::plotShotTo(const QString& path) {
    const Layout* L = m_layouts.currentLayout();
    if (!L) return false;
    return renderLayoutImage(*L, 1400.0).save(path);
}

// PREVIEW: mostra a prancha corrente como sairá no papel (CTB aplicado).
void MainWindow::previewPlot() {
    const Layout* L = m_layouts.currentLayout();
    if (!L) { log("PREVIEW: nenhuma prancha para visualizar (crie uma em Prancha)."); return; }

    QDialog dlg(this);
    dlg.setWindowTitle(QString("Visualizar plotagem — %1%2")
                           .arg(QString::fromStdString(L->name))
                           .arg(m_plotStyle.active
                                    ? QString(" · CTB \"%1\"").arg(QString::fromStdString(m_plotStyle.name))
                                    : QString()));
    auto* lay = new QVBoxLayout(&dlg);
    auto* pic = new QLabel;
    pic->setAlignment(Qt::AlignCenter);
    // Folha com uma sombra discreta sobre a "mesa" (leitura de papel).
    pic->setStyleSheet("background: #3c4048; padding: 14px;");
    const QImage img = renderLayoutImage(*L, 1400.0);
    pic->setPixmap(QPixmap::fromImage(
        img.scaled(1000, 640, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    lay->addWidget(pic, 1);

    auto* row = new QHBoxLayout;
    auto* bCtb  = new QPushButton("Estilos (CTB)...");
    auto* bPlot = new QPushButton("Plotar PDF...");
    auto* bPub  = new QPushButton("Publicar todas...");
    auto* bOk   = new QPushButton("Fechar");
    row->addWidget(bCtb); row->addStretch(1);
    row->addWidget(bPlot); row->addWidget(bPub); row->addWidget(bOk);
    lay->addLayout(row);
    connect(bCtb, &QPushButton::clicked, &dlg, [this, &dlg, pic, L] {
        openPlotStyleDialog();                       // edita e re-renderiza na hora
        const QImage im2 = renderLayoutImage(*L, 1400.0);
        pic->setPixmap(QPixmap::fromImage(
            im2.scaled(1000, 640, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        dlg.setWindowTitle(QString("Visualizar plotagem — %1%2")
                               .arg(QString::fromStdString(L->name))
                               .arg(m_plotStyle.active
                                        ? QString(" · CTB \"%1\"").arg(QString::fromStdString(m_plotStyle.name))
                                        : QString()));
    });
    connect(bPlot, &QPushButton::clicked, &dlg, [this, &dlg] { dlg.accept(); plotCurrentLayout(); });
    connect(bPub,  &QPushButton::clicked, &dlg, [this, &dlg] { dlg.accept(); publishInteractive(); });
    connect(bOk,   &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.resize(1060, 760);
    dlg.exec();
}

void MainWindow::publishInteractive() {
    QString base = m_projectPath.isEmpty()
                 ? QStringLiteral("pranchas")
                 : QFileInfo(m_projectPath).completeBaseName() + "-pranchas";
    const QString path = QFileDialog::getSaveFileName(
        this, "Publicar todas as pranchas (PDF)", base + ".pdf", "PDF (*.pdf)");
    if (path.isEmpty()) return;
    publishAllLayoutsTo(path);
}

// Desenha UMA prancha (moldura + viewports clipados em escala + selo) na
// "página" corrente do painter — compartilhado por Plotar/PUBLISH/preview.
// `dpi` + `pageHeightPx` desacoplam do dispositivo (QPdfWriter ou QImage).
void MainWindow::paintLayoutPage(const Layout& Lref, QPainter& p,
                                 double dpi, double pageHeightPx) {
    const Layout* L = &Lref;
    const double k = dpi / 25.4;                 // device-px por mm
    const double Hpx = pageHeightPx;             // altura da página em device-px
    const double W = L->widthMm(), H = L->heightMm();
    auto dev = [&](double px, double py) { return QPointF(px * k, Hpx - py * k); };  // mm(Y↑) -> device(Y↓)

    const QColor ink(0, 0, 0);
    const double m = L->marginMm;

    // Moldura de margem (a borda do papel é a própria página).
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(ink, std::max(1.0, 0.35 * k)));
    p.drawRect(QRectF(dev(m, H - m), dev(W - m, m)));

    // Viewports: Modelo clipado (clip do QPainter) + escalado por viewport.
    const LayerTable& layers = m_doc->layers();
    for (const SheetViewport& vp : L->viewports) {
        p.save();
        p.setClipRect(QRectF(dev(vp.xMm, vp.yMm + vp.hMm), QSizeF(vp.wMm * k, vp.hMm * k)));
        m_doc->forEach([&](const Entity& e) {
            const Layer* lay = layers.find(e.layer());
            if (lay && (!lay->on || lay->frozen)) return;
            if (vp.layerFrozenHere(e.layer())) return;   // VP-freeze deste viewport
            // CTB: estilo de plotagem da camada (tabela ativa).
            const PlotLayerStyle* ps = m_plotStyle.active
                                     ? m_plotStyle.find(e.layer()) : nullptr;
            if (ps && !ps->plot) return;                 // camada marcada "não plota"
            // ANOTATIVA: plota com a altura recalculada PARA ESTE viewport
            // (mm de papel constantes, seja a prancha 1:50 ou o detalhe 1:20).
            const Entity* src2 = &e;
            std::unique_ptr<Entity> scaled;
            {
                bool annot = false;
                if (const auto* amt = dynamic_cast<const MText*>(&e)) annot = amt->annotative();
                else if (const auto* adm = dynamic_cast<const Dimension*>(&e)) annot = adm->annotative();
                if (annot) {
                    const double fA = m_doc->annoMmPerUnit() / std::max(vp.mmPerUnit, 1e-12);
                    scaled = e.clone();
                    if (auto* mt = dynamic_cast<MText*>(scaled.get())) {
                        mt->setHeight(mt->height() * fA);
                    } else if (auto* dm = dynamic_cast<Dimension*>(scaled.get())) {
                        dm->setTextHeight(dm->textHeight() * fA);
                        if (dm->arrowSize() > 0.0) dm->setArrowSize(dm->arrowSize() * fA);
                    }
                    src2 = scaled.get();
                }
            }
            RenderBatch b; src2->emitTo(b);
            // TIPO DE LINHA no papel: aplica o tracejado (a tela faz isso no
            // upload do VBO; sem isto, DASHED/DASHDOT plotavam CONTÍNUAS).
            std::string ltn = e.lineType().name;
            if (ltn == "ByLayer" || ltn.empty())
                ltn = lay ? lay->lineType.name : std::string("CONTINUOUS");
            const std::vector<Point3> dashedLv = applyLineTypeByName(
                b.lineVertices, ltn, 1.5 * m_view->lineTypeScale());
            const Rgba c = e.resolveColor(layers);
            QColor qc = (c.r > 200 && c.g > 200 && c.b > 200) ? QColor(0, 0, 0)
                                                              : QColor(c.r, c.g, c.b);
            if (ps) {
                if      (ps->colorMode == 1) qc = QColor(0, 0, 0);
                else if (ps->colorMode == 2) { const int g = qGray(qc.rgb()); qc = QColor(g, g, g); }
                else if (ps->colorMode == 3) qc = QColor(ps->color.r, ps->color.g, ps->color.b);
            }
            auto toDev = [&](const Point3& v) { const Point3 pp = vp.toPaper(v); return dev(pp.x, pp.y); };
            if (!b.fillVertices.empty()) {
                p.setPen(Qt::NoPen); p.setBrush(qc);
                for (std::size_t i = 0; i + 2 < b.fillVertices.size(); i += 3) {
                    const QPointF tri[3] = { toDev(b.fillVertices[i]), toDev(b.fillVertices[i + 1]),
                                             toDev(b.fillVertices[i + 2]) };
                    p.drawPolygon(tri, 3);
                }
                p.setBrush(Qt::NoBrush);
            }
            double effMm = e.lineWeight().mm;
            if (effMm < 0.0 && lay) effMm = lay->lineWeight.mm;
            if (effMm <= 0.0) effMm = 0.25;
            if (ps && ps->lineWeightMm >= 0.0) effMm = ps->lineWeightMm;   // CTB força
            QPen pen(qc); pen.setWidthF(effMm * k);
            pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            for (std::size_t i = 0; i + 1 < dashedLv.size(); i += 2)
                p.drawLine(toDev(dashedLv[i]), toDev(dashedLv[i + 1]));
        });
        p.restore();
    }

    // Selo/carimbo (texto nativo do QPainter — mais limpo no papel que a fonte de traços).
    const double stW = std::min(185.0, (W - 2 * m) * 0.42);
    const double stH = 32.0;
    const double x1 = W - m, x0 = x1 - stW, y0 = m, y1 = y0 + stH;
    const double colX = x0 + stW * 0.26, rowH = stH / 4.0;
    p.setPen(QPen(ink, std::max(1.0, 0.30 * k)));
    p.drawRect(QRectF(dev(x0, y1), dev(x1, y0)));
    for (int i = 1; i < 4; ++i) p.drawLine(dev(x0, y0 + rowH * i), dev(x1, y0 + rowH * i));
    p.drawLine(dev(colX, y0), dev(colX, y1));
    auto cell = [&](double cx0, double cx1, int rowFromTop) {
        const double top = y1 - rowH * rowFromTop, bot = y1 - rowH * (rowFromTop + 1);
        return QRectF(dev(cx0, top), dev(cx1, bot)).adjusted(2.0 * k, 0, -1.0 * k, 0);
    };
    const char* labels[4] = { "TITULO", "PROJETO", "AUTOR", "ESCALA" };
    const std::string vals[4] = { L->title, L->project, L->author, L->scaleLabel };
    p.setPen(ink);
    QFont fLbl; fLbl.setPixelSize(std::max(1, int(1.9 * k)));
    QFont fVal; fVal.setPixelSize(std::max(1, int(2.6 * k)));
    for (int i = 0; i < 4; ++i) {
        p.setFont(fLbl); p.drawText(cell(x0, colX, i), Qt::AlignVCenter | Qt::AlignLeft, labels[i]);
        p.setFont(fVal); p.drawText(cell(colX, x1, i), Qt::AlignVCenter | Qt::AlignLeft,
                                    QString::fromStdString(vals[i]));
    }
    // Data no canto direito da linha ESCALA (base).
    p.setFont(fVal);
    p.drawText(cell(x0 + stW * 0.60, x1, 3), Qt::AlignVCenter | Qt::AlignRight,
               QString::fromStdString(L->date));
}

// BLOCK: pede um nome e arma a ferramenta de bloco (seleciona -> Enter -> ponto-base).
// O bloco criado é registrado na biblioteca do documento com esse nome.
void MainWindow::createBlockInteractive() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Criar bloco",
        "Nome do bloco:", QLineEdit::Normal,
        QString("Bloco%1").arg(static_cast<int>(m_doc->blocks().size()) + 1), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    // Nome já existente = REDEFINIÇÃO (atualiza a definição e TODAS as
    // inserções deste bloco no desenho) — confirma antes, como no AutoCAD.
    if (m_doc->blocks().contains(name.trimmed().toStdString())) {
        const auto r = QMessageBox::question(
            this, "Redefinir bloco",
            QString("O bloco \"%1\" já existe.\nRedefinir? As inserções existentes "
                    "serão atualizadas para a nova geometria.").arg(name.trimmed()));
        if (r != QMessageBox::Yes) return;
    }
    m_view->setPendingBlockName(name.trimmed().toStdString());
    m_view->setTool(ToolKind::Block);
    log("Bloco '" + name.trimmed() + "': selecione os objetos, Enter, depois o ponto-base.");
}

// INSERT: escolhe um bloco da biblioteca (+ escala/rotação) e arma a inserção.
void MainWindow::insertBlockInteractive() {
    const std::vector<std::string> names = m_doc->blocks().names();
    if (names.empty()) {
        log("Nenhum bloco na biblioteca. Crie um com 'Bloco' primeiro.");
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle("Inserir bloco");
    auto* form = new QFormLayout(&dlg);
    auto* cbName = new QComboBox;
    for (const std::string& n : names) cbName->addItem(QString::fromStdString(n));
    auto* spScale = new QDoubleSpinBox;
    spScale->setRange(0.001, 10000.0); spScale->setDecimals(3); spScale->setValue(1.0);
    auto* spRot = new QDoubleSpinBox;
    spRot->setRange(-360.0, 360.0); spRot->setDecimals(1); spRot->setSuffix(" graus"); spRot->setValue(0.0);
    form->addRow("Bloco:", cbName);
    form->addRow("Escala:", spScale);
    form->addRow("Rotação:", spRot);
    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bbox);
    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    m_view->setPendingInsert(cbName->currentText().toStdString(), spScale->value(), spRot->value());

    // Atributos: se a definição tem campos (ATTDEF), pergunta os valores.
    if (const BlockDefinition* def = m_doc->blocks().find(cbName->currentText().toStdString());
        def && !def->attdefs.empty()) {
        QDialog adlg(this);
        adlg.setWindowTitle("Atributos de '" + cbName->currentText() + "'");
        auto* aform = new QFormLayout(&adlg);
        std::vector<QLineEdit*> edits;
        for (const AttDefSpec& a : def->attdefs) {
            auto* ed = new QLineEdit(QString::fromStdString(a.defValue));
            const QString label = a.prompt.empty() ? QString::fromStdString(a.tag)
                                                   : QString::fromStdString(a.prompt);
            aform->addRow(label + ":", ed);
            edits.push_back(ed);
        }
        auto* abox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        aform->addRow(abox);
        connect(abox, &QDialogButtonBox::accepted, &adlg, &QDialog::accept);
        connect(abox, &QDialogButtonBox::rejected, &adlg, &QDialog::reject);
        if (adlg.exec() == QDialog::Accepted) {
            std::vector<std::pair<std::string, std::string>> vals;
            for (std::size_t i = 0; i < def->attdefs.size(); ++i)
                vals.emplace_back(def->attdefs[i].tag, edits[i]->text().toStdString());
            m_view->setPendingInsertValues(std::move(vals));
        }
    }

    m_view->setTool(ToolKind::Insert);
    log("Inserir '" + cbName->currentText() + "': clique o ponto de inserção (repita p/ várias).");
}

// ATTDEF: define um atributo de bloco (tag/prompt/valor padrão); o clique
// seguinte posiciona. Ao criar um bloco que contenha ATTDEFs, eles viram
// campos preenchíveis — cada INSERT pergunta os valores.
void MainWindow::attDefInteractive() {
    QDialog dlg(this);
    dlg.setWindowTitle("Definir atributo (ATTDEF)");
    auto* form = new QFormLayout(&dlg);
    auto* edTag = new QLineEdit("TAG");
    auto* edPrompt = new QLineEdit;
    auto* edDefault = new QLineEdit;
    form->addRow("Tag:", edTag);
    form->addRow("Prompt:", edPrompt);
    form->addRow("Valor padrão:", edDefault);
    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bbox);
    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || edTag->text().trimmed().isEmpty()) return;
    m_view->setPendingAttDef(edTag->text().trimmed().toUpper().toStdString(),
                             edPrompt->text().toStdString(),
                             edDefault->text().toStdString());
    m_view->setTool(ToolKind::AttDefTool);
    log("ATTDEF '" + edTag->text().trimmed().toUpper() +
        "': clique a posição do atributo (será um campo do bloco).");
}

// ATTEDIT: edita os VALORES de atributo de um bloco JÁ inserido (o INSERT
// selecionado). Também acionado por duplo clique no bloco. Undo via ReplaceCmd.
void MainWindow::attEditInteractive() {
    const EntityId id = m_view->selectedId();
    const auto* br = (id != kInvalidId)
        ? dynamic_cast<const BlockRef*>(m_doc->getEntity(id)) : nullptr;
    if (!br) { log("ATTEDIT: selecione um bloco (INSERT) primeiro."); return; }
    if (br->attValues().empty()) {
        log("ATTEDIT: este bloco não tem atributos.");
        return;
    }

    // Prompt de cada campo vem da definição (quando o bloco é nomeado).
    const BlockDefinition* def = br->blockName().empty()
        ? nullptr : m_doc->blocks().find(br->blockName());
    QDialog dlg(this);
    dlg.setWindowTitle(br->blockName().empty()
        ? QStringLiteral("Editar atributos")
        : QStringLiteral("Atributos de \"%1\"").arg(QString::fromStdString(br->blockName())));
    auto* form = new QFormLayout(&dlg);
    std::vector<QLineEdit*> eds;
    for (const auto& av : br->attValues()) {
        QString label = QString::fromStdString(av.tag);
        if (def)
            for (const auto& ad : def->attdefs)
                if (ad.tag == av.tag && !ad.prompt.empty()) {
                    label = QString::fromStdString(ad.prompt);
                    break;
                }
        auto* ed = new QLineEdit(QString::fromStdString(av.value));
        form->addRow(label + ":", ed);
        eds.push_back(ed);
    }
    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bbox);
    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    auto neuE = br->clone();
    auto* neu = static_cast<BlockRef*>(neuE.get());
    std::size_t i = 0;
    for (const auto& av : br->attValues())
        neu->setAttValue(av.tag, eds[i++]->text().toStdString());
    m_doc->execute(std::make_unique<ReplaceCmd>(id, std::move(neuE)));
    m_view->rebuild();
    log("ATTEDIT: valores do bloco atualizados.");
}

// FIND: localizar/substituir texto nos MTEXT do desenho inteiro. "Localizar"
// SELECIONA os textos que contêm o termo; "Substituir tudo" troca com undo.
void MainWindow::findTextInteractive() {
    QDialog dlg(this);
    dlg.setWindowTitle("Localizar e substituir (FIND)");
    auto* form = new QFormLayout(&dlg);
    auto* edFind = new QLineEdit;
    auto* edRepl = new QLineEdit;
    form->addRow("Localizar:", edFind);
    form->addRow("Substituir por:", edRepl);
    auto* row = new QHBoxLayout;
    auto* btFind = new QPushButton("Localizar (seleciona)");
    auto* btRepl = new QPushButton("Substituir tudo");
    auto* btClose = new QPushButton("Fechar");
    row->addWidget(btFind); row->addWidget(btRepl);
    row->addStretch(1); row->addWidget(btClose);
    form->addRow(row);

    auto matches = [&]() {
        std::vector<EntityId> ids;
        const QString termo = edFind->text();
        if (termo.isEmpty()) return ids;
        m_doc->forEach([&](const Entity& e) {
            if (const auto* mt = dynamic_cast<const MText*>(&e))
                if (QString::fromStdString(mt->text())
                        .contains(termo, Qt::CaseInsensitive))
                    ids.push_back(e.id());
        });
        return ids;
    };
    connect(btFind, &QPushButton::clicked, &dlg, [&] {
        const auto ids = matches();
        m_view->selectEntityIds(ids);
        log(QString("FIND: %1 texto(s) contendo \"%2\" selecionado(s).")
                .arg(ids.size()).arg(edFind->text()));
    });
    connect(btRepl, &QPushButton::clicked, &dlg, [&] {
        const auto ids = matches();
        if (ids.empty()) { log("FIND: nada para substituir."); return; }
        auto macro = std::make_unique<MacroCmd>("FIND");
        int n = 0;
        for (const EntityId id : ids) {
            const auto* mt = dynamic_cast<const MText*>(m_doc->getEntity(id));
            if (!mt) continue;
            QString s = QString::fromStdString(mt->text());
            s.replace(edFind->text(), edRepl->text(), Qt::CaseInsensitive);
            auto neu = std::make_unique<MText>(mt->position(), s.toStdString(),
                                               mt->height(), mt->rotation());
            neu->setJustify(mt->justify());
            neu->setFont(mt->font());
            neu->setBold(mt->bold());
            neu->setItalic(mt->italic());
            neu->setBoxWidth(mt->boxWidth());
            neu->setLayer(mt->layer());        neu->setColor(mt->color());
            neu->setLineType(mt->lineType());  neu->setLineWeight(mt->lineWeight());
            macro->add(std::make_unique<ReplaceCmd>(id, std::move(neu)));
            ++n;
        }
        m_doc->execute(std::move(macro));
        m_view->rebuild();
        log(QString("FIND: \"%1\" -> \"%2\" em %3 texto(s) (Ctrl+Z desfaz).")
                .arg(edFind->text(), edRepl->text()).arg(n));
    });
    connect(btClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    edFind->setFocus();
    dlg.exec();
}

// PURGE: lista TUDO que não está em uso — blocos sem inserção (inclusive
// dentro de definições aninhadas), estilos de cota/texto não-correntes,
// grupos cujos membros sumiram e camadas vazias — e remove o que for marcado.
void MainWindow::purgeInteractive() {
    // Blocos USADOS: inserções no desenho + inserções dentro de definições.
    QSet<QString> usedBlocks;
    m_doc->forEach([&](const Entity& e) {
        if (const auto* br = dynamic_cast<const BlockRef*>(&e))
            if (!br->blockName().empty())
                usedBlocks.insert(QString::fromStdString(br->blockName()));
    });
    for (const std::string& bn : m_doc->blocks().names())
        if (const BlockDefinition* def = m_doc->blocks().find(bn))
            for (const EntityPtr& m : def->members)
                if (const auto* br = dynamic_cast<const BlockRef*>(m.get()))
                    if (!br->blockName().empty())
                        usedBlocks.insert(QString::fromStdString(br->blockName()));

    struct Item { int kind; QString name; QString label; };   // 0=bloco 1=cota 2=texto 3=grupo 4=camada
    std::vector<Item> items;
    for (const std::string& bn : m_doc->blocks().names()) {
        const QString n = QString::fromStdString(bn);
        if (!usedBlocks.contains(n))
            items.push_back({0, n, "Bloco:  " + n});
    }
    for (const DimStyle& s : m_styles.allDim()) {
        const QString n = QString::fromStdString(s.name);
        if (s.name != "Standard" && s.name != m_styles.currentDimName())
            items.push_back({1, n, "Estilo de cota:  " + n});
    }
    for (const TextStyle& s : m_styles.allText()) {
        const QString n = QString::fromStdString(s.name);
        if (s.name != "Standard" && s.name != m_styles.currentTextName())
            items.push_back({2, n, "Estilo de texto:  " + n});
    }
    for (const auto& kv : m_doc->groups()) {
        bool alive = false;
        for (const EntityId id : kv.second)
            if (m_doc->getEntity(id)) { alive = true; break; }
        if (!alive)
            items.push_back({3, QString::fromStdString(kv.first),
                             "Grupo (vazio):  " + QString::fromStdString(kv.first)});
    }
    for (const Layer& l : m_doc->layers().all()) {
        const QString n = QString::fromStdString(l.name);
        if (l.name != "0" && n != m_currentLayerName && m_doc->layerUsage(l.name) == 0)
            items.push_back({4, n, "Camada (vazia):  " + n});
    }

    if (items.empty()) {
        log("PURGE: nada sem uso — o documento já está limpo.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Limpar não usados (PURGE)");
    dlg.resize(380, 340);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(QString("%1 item(ns) sem uso — desmarque o que quiser manter:")
                                .arg(items.size())));
    auto* list = new QListWidget(&dlg);
    for (const Item& it : items) {
        auto* li = new QListWidgetItem(it.label, list);
        li->setFlags(li->flags() | Qt::ItemIsUserCheckable);
        li->setCheckState(Qt::Checked);
    }
    v->addWidget(list, 1);
    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    v->addWidget(bbox);
    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    int removed = 0;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->checkState() != Qt::Checked) continue;
        const Item& it = items[std::size_t(i)];
        const std::string n = it.name.toStdString();
        bool ok = false;
        switch (it.kind) {
            case 0: ok = m_doc->blocks().remove(n); break;
            case 1: ok = m_styles.removeDim(n); break;
            case 2: ok = m_styles.removeText(n); break;
            case 3: ok = m_doc->removeGroup(n); break;
            case 4: ok = m_doc->removeLayer(n, false); break;
        }
        if (ok) ++removed;
    }
    refreshLayers();
    log(QString("PURGE: %1 item(ns) removido(s).").arg(removed));
}

// QSELECT: seleção rápida por TIPO e/ou CAMADA (estilo AutoCAD, versão enxuta).
// Lista só os tipos que existem no desenho; "(qualquer)" não filtra.
void MainWindow::quickSelectInteractive() {
    // Tipos distintos presentes no documento.
    QStringList types{"(qualquer)"};
    m_doc->forEach([&](const Entity& e) {
        const QString t = QString::fromUtf8(e.typeName());
        if (!types.contains(t)) types << t;
    });
    QDialog dlg(this);
    dlg.setWindowTitle("Seleção rápida (QSELECT)");
    auto* form = new QFormLayout(&dlg);
    auto* cbType = new QComboBox; cbType->addItems(types);
    auto* cbLayer = new QComboBox; cbLayer->addItem("(qualquer)");
    for (const Layer& l : m_doc->layers().all()) cbLayer->addItem(QString::fromStdString(l.name));
    form->addRow("Tipo:", cbType);
    form->addRow("Camada:", cbLayer);
    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bbox);
    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    const std::string type = (cbType->currentIndex() == 0) ? std::string()
                              : cbType->currentText().toStdString();
    const std::string layer = (cbLayer->currentIndex() == 0) ? std::string()
                              : cbLayer->currentText().toStdString();
    m_view->setTool(ToolKind::None);   // modo seleção
    const int n = m_view->selectByFilter(type, layer);
    log(QString("QSELECT: %1 entidade(s) selecionada(s).").arg(n));
}

// ============================================================================
//  Projeto (.zencad) — Novo/Abrir/Salvar + recentes + tela inicial.
// ============================================================================

QStringList MainWindow::recentProjects() const {
    return QSettings("ZenCAD", "ZenCAD").value("recentProjects").toStringList();
}

void MainWindow::addRecentProject(const QString& p) {
    QSettings st("ZenCAD", "ZenCAD");
    QStringList rec = st.value("recentProjects").toStringList();
    rec.removeAll(p);
    rec.prepend(p);
    while (rec.size() > 8) rec.removeLast();
    st.setValue("recentProjects", rec);
}

void MainWindow::updateWindowTitle() {
    setWindowTitle(m_projectPath.isEmpty()
                       ? QStringLiteral("ZenCAD — Sem título")
                       : QStringLiteral("ZenCAD — %1").arg(QFileInfo(m_projectPath).fileName()));
}

// ============================================================================
//  MULTI-DOCUMENTO (abas de arquivo)
// ============================================================================

// Botão "✕" de fechar aba no idioma visual do app (transparente, hover latão).
// Descobre a PRÓPRIA aba no clique (os índices mudam quando abas fecham).
QToolButton* MainWindow::makeTabCloseButton() {
    auto* b = new QToolButton(m_fileTabs);
    b->setObjectName("tabClose");
    b->setText(QString::fromUtf8("×"));   // × (multiplication sign: em toda fonte)
    b->setFixedSize(16, 16);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    connect(b, &QToolButton::clicked, this, [this, b] {
        for (int i = 1; i < m_fileTabs->count(); ++i)
            if (m_fileTabs->tabButton(i, QTabBar::RightSide) == b) {
                closeDoc(i - 1);
                return;
            }
    });
    return b;
}

// Todos os connects que SAEM de um viewport — feitos por INSTÂNCIA (cada
// sessão tem o seu). Os handlers usam m_view/m_doc (a sessão corrente): só o
// viewport visível recebe input, então v == corrente quando o sinal dispara.
void MainWindow::wireViewport(ViewportWidget* v) {
    connect(v, &ViewportWidget::paperViewportDrawn, this,
            [this](double x, double y, double w, double h) { addViewportToCurrent(x, y, w, h); });
    connect(v, &ViewportWidget::paperViewportScaleRequested, this, [this](int i) {
        Layout* L = m_layouts.currentLayout();
        if (!L || i < 0 || i >= int(L->viewports.size())) return;
        SheetViewport& vp = L->viewports[std::size_t(i)];
        bool ok = false;
        const double d = QInputDialog::getDouble(
            this, "Escala do viewport", "Escala 1:N — informe N:",
            vp.scaleDenom, 0.001, 1e6, 3, &ok);
        if (!ok) return;
        vp.scaleDenom = d;
        vp.mmPerUnit  = scaleMmPerUnit(unitLengthMm(m_unitIndex), d);
        m_view->update();
        log("Viewport em escala " + QString::fromStdString(formatScale(d)) + ".");
    });
    connect(v, &ViewportWidget::paperViewportLayersRequested, this, [this](int i) {
        Layout* L = m_layouts.currentLayout();
        if (!L || i < 0 || i >= int(L->viewports.size())) return;
        SheetViewport& vp = L->viewports[std::size_t(i)];
        // VP-FREEZE: checklist de camadas congeladas SÓ neste viewport.
        QDialog dlg(this);
        dlg.setWindowTitle("Congelar camadas neste viewport");
        dlg.resize(300, 340);
        auto* lay = new QVBoxLayout(&dlg);
        lay->addWidget(new QLabel("Marcadas = OCULTAS neste viewport:"));
        auto* list = new QListWidget(&dlg);
        for (const Layer& l : m_doc->layers().all()) {
            auto* it = new QListWidgetItem(QString::fromStdString(l.name), list);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(vp.layerFrozenHere(l.name) ? Qt::Checked : Qt::Unchecked);
        }
        lay->addWidget(list, 1);
        auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        lay->addWidget(bbox);
        connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        vp.frozenLayers.clear();
        for (int r = 0; r < list->count(); ++r)
            if (list->item(r)->checkState() == Qt::Checked)
                vp.frozenLayers.push_back(list->item(r)->text().toStdString());
        m_view->update();
        log(QString("VP-freeze: %1 camada(s) congelada(s) neste viewport.")
                .arg(vp.frozenLayers.size()));
    });
    connect(v, &ViewportWidget::prompt, this, [this](const QString& s) { log(s); });
    connect(v, &ViewportWidget::commandEntered, this, &MainWindow::runCommand);
    connect(v, &ViewportWidget::selectionChanged, this, &MainWindow::updateProperties);
    connect(v, &ViewportWidget::editTextRequested, this, &MainWindow::editTextContent);
    connect(v, &ViewportWidget::editBlockAttrsRequested, this, &MainWindow::attEditInteractive);
    connect(v, &ViewportWidget::toolChanged, this, [this](int k) {
        if (!m_toolGroup) return;
        QAction* cur = m_toolGroup->checkedAction();
        if (cur && m_toolKindOf.value(cur, -999) == k) return;
        if (QAction* a = m_toolAct.value(k, nullptr)) a->setChecked(true);
        else if (cur) cur->setChecked(false);
    });
    if (auto* sb = qobject_cast<CadStatusBar*>(statusBar())) {
        // X/Y da status bar no FRAME DO UCS (leitura de trabalho, como no AutoCAD).
        connect(v, &ViewportWidget::cursorMoved, sb, [this, sb](double x, double y) {
            if (m_doc && m_doc->ucsActive()) {
                const Point3 u = m_doc->worldToUcs(Point3{x, y, 0.0});
                sb->setCoords(u.x, u.y);
            } else {
                sb->setCoords(x, y);
            }
        });
        connect(v, &ViewportWidget::zoomChanged, this,
                [this](int p) { if (m_zoom) m_zoom->setText(QString::number(p) + "%"); });
        // Estado inicial dos toggles = o que a status bar mostra agora.
        v->setSnapEnabled(sb->snapOn());
        v->setGridOn(sb->gridOn());
        v->setOrthoOn(sb->orthoOn());
        v->setOtrackEnabled(sb->otrackOn());
        v->setPolarOn(sb->polarOn());
        v->setGridSnapOn(sb->gridSnapOn());
    }
}

bool MainWindow::currentIsBlank() const {
    return m_projectPath.isEmpty() && m_doc && m_doc->count() == 0 && !m_doc->canUndo();
}

void MainWindow::stashCurrentSession() {
    if (m_curSession < 0 || m_curSession >= int(m_sessions.size())) return;
    DocSession& s = *m_sessions[std::size_t(m_curSession)];
    // Sai do modo papel ANTES de mover a tabela (o viewport guarda ponteiro).
    if (s.view) s.view->setPaperMode(false, nullptr);
    s.layouts       = std::move(m_layouts);
    s.styles        = std::move(m_styles);
    s.plotStyle     = std::move(m_plotStyle);
    s.projectPath   = m_projectPath;
    s.currentLayer  = m_currentLayerName;
    s.unitIndex     = m_unitIndex;
    s.unitDecimals  = m_unitDecimals;
    s.unitSuffix    = m_unitSuffix;
}

void MainWindow::switchToDoc(int idx) {
    if (idx == m_curSession || idx < 0 || idx >= int(m_sessions.size())) return;
    stashCurrentSession();
    m_curSession = idx;
    DocSession& s = *m_sessions[std::size_t(idx)];
    m_doc  = s.doc.get();
    m_view = s.view;
    m_viewStack->setCurrentWidget(s.view);
    m_layouts          = std::move(s.layouts);
    m_styles           = std::move(s.styles);
    m_plotStyle        = std::move(s.plotStyle);
    m_projectPath      = s.projectPath;
    m_currentLayerName = s.currentLayer;
    m_unitIndex        = s.unitIndex;
    m_unitDecimals     = s.unitDecimals;
    m_unitSuffix       = s.unitSuffix;
    m_fileTabsBusy = true;
    m_fileTabs->setCurrentIndex(idx + 1);   // aba 0 = Início
    m_fileTabsBusy = false;
    refreshUiForCurrentDoc();
    m_view->setFocus();
}

int MainWindow::newDocSession() {
    auto s = std::make_unique<DocSession>();
    const AABB world = AABB::fromCenterHalf({0, 0, 0}, {1e6, 1e6, 1e6});
    s->doc = std::make_unique<DrawingManager>(
        std::make_unique<Quadtree>(world, /*maxDepth*/ 12, /*split*/ 16));
    s->layouts.addDefault("Prancha 1");
    m_sessions.push_back(std::move(s));
    const int idx = int(m_sessions.size()) - 1;
    DocSession& ss = *m_sessions[std::size_t(idx)];
    ss.view = new ViewportWidget(ss.doc.get(), this);
    m_viewStack->addWidget(ss.view);
    wireViewport(ss.view);
    m_fileTabsBusy = true;
    m_fileTabs->insertTab(idx + 1, "Sem título");   // aba 0 = Início
    m_fileTabs->setTabButton(idx + 1, QTabBar::RightSide, makeTabCloseButton());
    m_fileTabsBusy = false;
    switchToDoc(idx);
    m_view->resetView();
    return idx;
}

void MainWindow::closeDoc(int idx) {
    if (idx < 0 || idx >= int(m_sessions.size())) return;
    // Heurística de "alterações não salvas": há histórico de undo.
    {
        DrawingManager* d = (idx == m_curSession) ? m_doc
                            : m_sessions[std::size_t(idx)]->doc.get();
        if (d && d->canUndo()) {
            const auto r = QMessageBox::question(
                this, "Fechar documento",
                "Este documento pode ter alterações não salvas.\nFechar mesmo assim?");
            if (r != QMessageBox::Yes) return;
        }
    }
    // Última aba de documento: vira doc em branco E volta à tela INÍCIO.
    if (m_sessions.size() == 1) {
        newProjectInPlace();
        showStartPage();
        return;
    }
    // Se está fechando a corrente, troca para a vizinha primeiro.
    if (idx == m_curSession)
        switchToDoc(idx > 0 ? idx - 1 : 1);
    // Remoção: como switchToDoc já stashou, a sessão fechada está completa.
    DocSession& dead = *m_sessions[std::size_t(idx)];
    if (dead.view) {
        m_viewStack->removeWidget(dead.view);
        dead.view->deleteLater();
    }
    m_sessions.erase(m_sessions.begin() + idx);
    if (m_curSession > idx) --m_curSession;
    m_fileTabsBusy = true;
    m_fileTabs->removeTab(idx + 1);                      // aba 0 = Início
    m_fileTabs->setCurrentIndex(m_curSession + 1);
    m_fileTabsBusy = false;
    log("Documento fechado.");
}

void MainWindow::updateFileTabText(int idx) {
    if (!m_fileTabs || idx < 0 || idx >= int(m_sessions.size()) ||
        idx + 1 >= m_fileTabs->count())
        return;
    const QString p = (idx == m_curSession) ? m_projectPath
                      : m_sessions[std::size_t(idx)]->projectPath;
    m_fileTabsBusy = true;
    m_fileTabs->setTabText(idx + 1, p.isEmpty() ? QStringLiteral("Sem título")
                                                : QFileInfo(p).completeBaseName());
    m_fileTabsBusy = false;
}

// Painéis/abas/título refletindo a sessão CORRENTE (não mexe na câmera).
void MainWindow::refreshUiForCurrentDoc() {
    if (auto* sb = qobject_cast<CadStatusBar*>(statusBar()))
        sb->setUnitFormat(m_unitDecimals, m_unitSuffix);
    m_view->setCurrentLayer(m_currentLayerName.toStdString());
    applyCurrentDimStyle();
    m_view->setAnnotationHeight(m_styles.currentText().height);
    m_view->setTextAnnotative(m_styles.currentText().annotative);
    m_view->setAnnotationFont(m_styles.currentText().font);
    if (m_txtRibFont) {
        m_txtRibUpdating = true;
        const TextStyle& ts = m_styles.currentText();
        if (ts.font.empty()) m_txtRibFont->setCurrentIndex(0);
        else m_txtRibFont->setCurrentFont(QFont(QString::fromStdString(ts.font)));
        m_txtRibSize->setValue(ts.height);
        m_txtRibUpdating = false;
    }
    refreshLayers();
    rebuildLayoutTabs();
    updateWindowTitle();
    updateFileTabText(m_curSession);
    updateProperties();
}

// Ajusta a UI aos valores carregados (ltscale/unidades/camada corrente/estilos)
// e reenquadra a vista — usado após Novo/Abrir (a troca de aba NÃO reenquadra).
void MainWindow::applyProjectSettingsToUi() {
    refreshUiForCurrentDoc();
    m_view->rebuild();
    if (m_doc->count() == 0) m_view->resetView();   // doc vazio: enquadramento padrão
    else                     m_view->fitView();
}

void MainWindow::newProject() {
    // Multi-doc: uma sessão em branco é reusada; senão, NOVA ABA.
    if (currentIsBlank()) {
        newProjectInPlace();
        return;
    }
    newDocSession();
    showDrawing();
    log("Novo desenho (nova aba).");
}

void MainWindow::newProjectInPlace() {
    m_view->setPaperMode(false, nullptr);
    m_doc->clearAll();
    m_layouts = LayoutTable{};
    m_layouts.addDefault("Prancha 1");
    m_styles = StyleTable{};
    m_plotStyle = PlotStyleTable{};
    m_projectPath.clear();
    m_currentLayerName = "0";
    m_view->setLineTypeScale(1.0);
    applyProjectSettingsToUi();
    showDrawing();
    log("Novo desenho em branco.");
}

void MainWindow::openProjectPath(const QString& p) {
    // Já aberto em outra aba? Só troca.
    for (int i = 0; i < int(m_sessions.size()); ++i) {
        const QString sp = (i == m_curSession) ? m_projectPath
                           : m_sessions[std::size_t(i)]->projectPath;
        if (!sp.isEmpty() && sp == p) {
            switchToDoc(i);
            showDrawing();
            log("Projeto já aberto — aba ativada: " + p);
            return;
        }
    }
    // Abre numa aba nova (ou reusa a sessão corrente se estiver em branco).
    if (!currentIsBlank()) newDocSession();

    ProjectSettings s;
    QString err;
    m_view->setPaperMode(false, nullptr);   // solta ponteiros de prancha antes do load
    LayoutTable layouts;
    StyleTable  styles;
    if (!loadProject(p, *m_doc, layouts, styles, s, &err)) {
        QMessageBox::warning(this, "Abrir projeto",
                             "Não foi possível abrir:\n" + p + "\n\n" + err);
        return;
    }
    m_layouts = std::move(layouts);
    if (m_layouts.empty()) m_layouts.addDefault("Prancha 1");
    m_styles = std::move(styles);
    m_plotStyle = std::move(s.plotStyle);
    m_projectPath = p;
    m_currentLayerName = s.currentLayer;
    m_unitIndex = s.unitIndex;
    m_unitDecimals = s.unitDecimals;
    m_unitSuffix = s.unitSuffix;
    m_view->setLineTypeScale(s.ltScale);
    reloadAllXrefs();   // XREF: relê os arquivos externos (ausente = snapshot)
    applyProjectSettingsToUi();
    addRecentProject(p);
    updateFileTabText(m_curSession);
    showDrawing();
}

void MainWindow::openProjectInteractive() {
    const QString p = QFileDialog::getOpenFileName(this, "Abrir projeto", QString(),
                                                   "Projeto ZenCAD (*.zencad)");
    if (!p.isEmpty()) openProjectPath(p);
}

void MainWindow::saveProjectInteractive(bool saveAs) {
    QString p = m_projectPath;
    if (saveAs || p.isEmpty()) {
        p = QFileDialog::getSaveFileName(this, "Salvar projeto",
                                         p.isEmpty() ? "projeto.zencad" : p,
                                         "Projeto ZenCAD (*.zencad)");
        if (p.isEmpty()) return;
    }
    ProjectSettings s;
    s.ltScale      = m_view->lineTypeScale();
    s.unitIndex    = m_unitIndex;
    s.unitDecimals = m_unitDecimals;
    s.unitSuffix   = m_unitSuffix;
    s.currentLayer = m_currentLayerName;
    s.plotStyle    = m_plotStyle;
    QString err;
    // Miniatura do viewport embutida no .zencad (cards da página inicial).
    const QImage thumb = m_view->grabFramebuffer().scaled(
        640, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!saveProject(p, *m_doc, m_layouts, m_styles, s, &err, &thumb)) {
        QMessageBox::warning(this, "Salvar projeto", "Falha ao salvar:\n" + err);
        return;
    }
    m_projectPath = p;
    addRecentProject(p);
    updateWindowTitle();
    updateFileTabText(m_curSession);
    log("Projeto salvo: " + p);
}

void MainWindow::showStartPage() {
    m_startPage->refresh(recentProjects());
    m_stack->setCurrentWidget(m_startPage);
    if (m_fileTabs) {                       // seleciona a aba "Início"
        m_fileTabsBusy = true;
        m_fileTabs->setCurrentIndex(0);
        m_fileTabsBusy = false;
    }
}

void MainWindow::showDrawing() {
    m_stack->setCurrentWidget(m_drawingPage);
    if (m_fileTabs) {                       // seleciona a aba do doc corrente
        m_fileTabsBusy = true;
        m_fileTabs->setCurrentIndex(m_curSession + 1);
        m_fileTabsBusy = false;
    }
    m_view->setFocus();
}

void MainWindow::requestShot(const QString& path) {
    m_suppressStart = true;   // verificação headless: sem tela inicial
    QTimer::singleShot(700, this, [this, path]() {
        const QImage img = this->grab().toImage();   // janela inteira (barra + viewport)
        const bool ok = img.save(path);
        log(ok ? "Shot salvo." : "Falha ao salvar shot.");
        QCoreApplication::quit();
    });
}

void MainWindow::buildMenuBar() {
    QMenu* mArq = menuBar()->addMenu("Arquivo");
    m_fileMenu = mArq;   // guardado para receber Salvar/Importar DXF e Exportar PDF

    // --- Projeto (.zencad): persistência FIEL (o DXF é interop) -------------
    QAction* aNew = mArq->addAction("Novo desenho", this, [this] { newProject(); });
    aNew->setShortcut(QKeySequence::New);
    QAction* aOpenPrj = mArq->addAction("Abrir projeto...", this,
                                        [this] { openProjectInteractive(); });
    aOpenPrj->setShortcut(QKeySequence::Open);
    QAction* aSavePrj = mArq->addAction("Salvar projeto", this,
                                        [this] { saveProjectInteractive(false); });
    aSavePrj->setShortcut(QKeySequence::Save);
    mArq->addAction("Salvar projeto como...", this,
                    [this] { saveProjectInteractive(true); })
        ->setShortcut(QKeySequence("Ctrl+Shift+S"));
    mArq->addAction("Tela inicial...", this, [this] { showStartPage(); });
    mArq->addAction("Fechar documento", this,
                    [this] { closeDoc(m_curSession); })
        ->setShortcut(QKeySequence("Ctrl+W"));
    mArq->addSeparator();
    mArq->addAction("Sair", this, [this] { close(); });

    QMenu* mEd = menuBar()->addMenu("Editar");
    QAction* aUndo = mEd->addAction("Desfazer", this,
        [this] { m_doc->undo(); m_view->rebuild(); refreshLayers(); m_view->update(); log("Undo."); });
    aUndo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
    aUndo->setShortcutContext(Qt::ApplicationShortcut);
    QAction* aRedo = mEd->addAction("Refazer", this,
        [this] { m_doc->redo(); m_view->rebuild(); refreshLayers(); m_view->update(); log("Redo."); });
    aRedo->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Y")), QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});
    aRedo->setShortcutContext(Qt::ApplicationShortcut);
    mEd->addSeparator();
    mEd->addAction("Seleção rápida (QSELECT)...", this, [this] { quickSelectInteractive(); });
    mEd->addAction("Localizar e substituir (FIND)...", this, [this] { findTextInteractive(); });
    mEd->addSeparator();
    mEd->addAction("Selecionar tudo", this, [this] {
        log(QString("%1 entidade(s) selecionada(s).").arg(m_view->selectAllVisible())); })
        ->setShortcut(QKeySequence("Ctrl+A"));
    mEd->addAction("Seleção anterior", this, [this] { m_view->selectPrevious(); });
    mEd->addAction("Agrupar seleção (GROUP)...", this, [this] { runCommand("GROUP"); });
    mEd->addAction("Desagrupar (UNGROUP)", this, [this] { runCommand("UNGROUP"); });
    mEd->addSeparator();
    mEd->addAction("Apagar seleção", this, [this] { m_view->eraseSelected(); });
    mEd->addAction("Limpar não usados (PURGE)...", this, [this] { purgeInteractive(); });

    QMenu* mView = menuBar()->addMenu("Visualizar");
    mView->addAction("Zoom Tudo", this, [this] { m_view->zoomExtents(); });
    mView->addAction("Zoom Janela", this, [this] { m_view->beginZoomWindow(); });
    mView->addAction("Zoom Anterior", this, [this] { m_view->zoomPrevious(); });
    mView->addAction("Ampliar", this, [this] { m_view->zoomIn(); })->setShortcut(QKeySequence("Ctrl++"));
    mView->addAction("Reduzir", this, [this] { m_view->zoomOut(); })->setShortcut(QKeySequence("Ctrl+-"));
    mView->addAction("Pan (mão)", this, [this] { m_view->beginPan(); });
    mView->addSeparator();
    mView->addAction("UCS — sistema de coordenadas...", this, [this] { runCommand("UCS"); });
    mView->addSeparator();
    QAction* aTheme = mView->addAction("Tema claro / escuro");
    aTheme->setShortcut(QKeySequence("Ctrl+Shift+L"));
    connect(aTheme, &QAction::triggered, this, [this] {
        applyTheme(m_themeMode == ThemeMode::Dark ? ThemeMode::Light : ThemeMode::Dark);
    });

    // Menu "Prancha" (Paper Space): criar/configurar pranchas e viewports.
    QMenu* mSheet = menuBar()->addMenu("Prancha");
    mSheet->addAction("Nova prancha", this, [this] {
        m_layouts.addDefault("Prancha " + std::to_string(static_cast<int>(m_layouts.size()) + 1));
        rebuildLayoutTabs();
        m_layoutTabs->setCurrentIndex(static_cast<int>(m_layouts.size()));  // entra na nova
    });
    mSheet->addAction("Novo viewport", this, [this] {
        if (!m_view->paperMode()) { log("Entre numa prancha para criar um viewport."); return; }
        m_view->beginPaperViewport();
    });
    mSheet->addAction("Configurar página...", this, [this] { openPaperSetup(); });
    mSheet->addAction("Estilos de plotagem (CTB)...", this, [this] { openPlotStyleDialog(); });
    mSheet->addSeparator();
    mSheet->addAction("Visualizar plotagem...", this, [this] { previewPlot(); });
    mSheet->addAction("Plotar prancha (PDF)...", this, [this] { plotCurrentLayout(); });
    mSheet->addAction("Publicar TODAS as pranchas (PDF)...", this, [this] { publishInteractive(); });
    mSheet->addSeparator();
    mSheet->addAction("Excluir prancha", this, [this] {
        if (!m_view->paperMode()) { log("Entre numa prancha para excluir."); return; }
        m_view->setPaperMode(false, nullptr);   // solta o ponteiro ANTES de mexer no vetor
        m_layouts.remove(m_layouts.current());
        rebuildLayoutTabs();
        m_layoutTabs->setCurrentIndex(0);        // volta ao Modelo
        log("Prancha excluída.");
    });
}

void MainWindow::applyTheme(ThemeMode m) {
    m_themeMode = m;
    if (auto* app = qobject_cast<QApplication*>(QApplication::instance()))
        app->setStyleSheet(zenTheme(m));
    m_view->setThemeMode(m);
    refreshLayers();   // re-renderiza swatches/estilos com o novo tema
    log(m == ThemeMode::Dark ? "Tema: Sumi (escuro)." : "Tema: Washi (claro).");
}

void MainWindow::updateProperties() {
    if (!m_props) return;
    const std::vector<EntityId> ids = m_view->selectedIds();
    const Entity* e = ids.empty() ? nullptr : m_doc->getEntity(ids.front());

    // Sincroniza a ribbon de TEXTO com o MText selecionado (sem re-aplicar).
    if (m_txtRibFont) {
        if (const auto* mt = dynamic_cast<const MText*>(e); mt && ids.size() == 1) {
            m_txtRibUpdating = true;
            if (mt->font().empty()) m_txtRibFont->setCurrentIndex(0);
            else m_txtRibFont->setCurrentFont(QFont(QString::fromStdString(mt->font())));
            m_txtRibSize->setValue(mt->height());
            m_txtRibBold->setChecked(mt->bold());
            m_txtRibItalic->setChecked(mt->italic());
            m_txtRibUpdating = false;
        }
    }
    if (!e) {
        m_props->setText("Nenhum objeto selecionado");
        if (m_propEdit) m_propEdit->hide();
        if (m_geoEdit) m_geoEdit->hide();
        return;
    }

    QString s;
    if (ids.size() > 1) {
        // Multi-seleção: resumo; os editores aplicam a TODOS.
        s += QString("%1 objetos selecionados\n").arg(ids.size());
        s += QString("(editar abaixo aplica a todos)\n");
    } else {
        s += QString("Tipo:    %1\n").arg(e->typeName());
        s += QString("Camada:  %1\n\n").arg(QString::fromStdString(e->layer()));
        if (const auto* l = dynamic_cast<const Line*>(e)) {
        const double len = std::hypot(l->end().x - l->start().x, l->end().y - l->start().y);
        s += QString("Início:  %1, %2\n").arg(l->start().x, 0, 'f', 2).arg(l->start().y, 0, 'f', 2);
        s += QString("Fim:     %1, %2\n").arg(l->end().x, 0, 'f', 2).arg(l->end().y, 0, 'f', 2);
        s += QString("Compr.:  %1").arg(len, 0, 'f', 2);
    } else if (const auto* c = dynamic_cast<const Circle*>(e)) {
        s += QString("Centro:  %1, %2\n").arg(c->center().x, 0, 'f', 2).arg(c->center().y, 0, 'f', 2);
        s += QString("Raio:    %1").arg(c->radius(), 0, 'f', 2);
    } else if (const auto* a = dynamic_cast<const Arc*>(e)) {
        s += QString("Centro:  %1, %2\n").arg(a->center().x, 0, 'f', 2).arg(a->center().y, 0, 'f', 2);
        s += QString("Raio:    %1").arg(a->radius(), 0, 'f', 2);
    } else if (const auto* pl = dynamic_cast<const Polyline*>(e)) {
        s += QString("Vértices: %1\n").arg(pl->vertices().size());
        s += QString("Fechada:  %1").arg(pl->closed() ? "sim" : "não");
    } else {
        const AABB bb = e->boundingBox();
        s += QString("BBox: (%1, %2) - (%3, %4)")
                 .arg(bb.min.x, 0, 'f', 1).arg(bb.min.y, 0, 'f', 1)
                 .arg(bb.max.x, 0, 'f', 1).arg(bb.max.y, 0, 'f', 1);
        }
    }
    m_props->setText(s);

    // Preenche os editores inline a partir da entidade (sem disparar apply).
    m_propUpdating = true;
    m_propLayer->clear();
    for (const Layer& L : m_doc->layers().all())
        m_propLayer->addItem(QString::fromStdString(L.name));
    m_propLayer->setCurrentText(QString::fromStdString(e->layer()));
    {
        QString lt = QString::fromStdString(e->lineType().name);
        if (lt.isEmpty()) lt = "ByLayer";
        const int i = m_propLtype->findData(lt);
        m_propLtype->setCurrentIndex(i < 0 ? 0 : i);
    }
    {
        const double mm = e->lineWeight().mm;
        const int i = m_propLweight->findData(mm);
        if (i >= 0) m_propLweight->setCurrentIndex(i);
        else if (mm > 0) m_propLweight->setEditText(QString::number(mm, 'f', 2));  // valor custom
        else m_propLweight->setCurrentIndex(0);
    }
    {
        const Rgba c = e->resolveColor(m_doc->layers());
        const QColor q(c.r, c.g, c.b);
        m_propColor->setStyleSheet(QString("text-align:left; padding:4px; border-radius:4px;"
                                           " background:%1; color:%2;")
                                       .arg(q.name(), (c.r + c.g + c.b) > 380 ? "#000" : "#fff"));
    }
    // Campos numéricos de geometria (só seleção ÚNICA de Line/Circle).
    int nGeo = 0;
    if (ids.size() == 1) {
        if (const auto* l = dynamic_cast<const Line*>(e)) {
            m_geoLbl[0]->setText("X inicial:"); m_geo[0]->setValue(l->start().x);
            m_geoLbl[1]->setText("Y inicial:"); m_geo[1]->setValue(l->start().y);
            m_geoLbl[2]->setText("X final:");   m_geo[2]->setValue(l->end().x);
            m_geoLbl[3]->setText("Y final:");   m_geo[3]->setValue(l->end().y);
            nGeo = 4;
        } else if (const auto* c = dynamic_cast<const Circle*>(e)) {
            m_geoLbl[0]->setText("Centro X:"); m_geo[0]->setValue(c->center().x);
            m_geoLbl[1]->setText("Centro Y:"); m_geo[1]->setValue(c->center().y);
            m_geoLbl[2]->setText("Raio:");     m_geo[2]->setValue(c->radius());
            nGeo = 3;
        } else if (const auto* a = dynamic_cast<const Arc*>(e)) {
            m_geoLbl[0]->setText("Centro X:"); m_geo[0]->setValue(a->center().x);
            m_geoLbl[1]->setText("Centro Y:"); m_geo[1]->setValue(a->center().y);
            m_geoLbl[2]->setText("Raio:");     m_geo[2]->setValue(a->radius());
            nGeo = 3;   // ângulos preservados (edição via grips)
        }
    }
    for (int i = 0; i < 4; ++i) m_geoForm->setRowVisible(i, i < nGeo);
    m_geoEdit->setVisible(nGeo > 0);

    // Editor de TEXTO: só p/ seleção única de MTEXT.
    const MText* mt = (ids.size() == 1) ? dynamic_cast<const MText*>(e) : nullptr;
    if (mt) {
        m_txtHeight->setValue(mt->height());
        m_txtRot->setValue(mt->rotation() * 180.0 / 3.14159265358979);
        m_txtJustify->setCurrentIndex(static_cast<int>(mt->justify()));
    }
    m_txtEdit->setVisible(mt != nullptr);

    m_propUpdating = false;
    m_propEdit->show();
}

void MainWindow::applyGeometry() {
    if (m_propUpdating) return;
    const EntityId id = m_view->selectedId();
    const Entity* e = (id != kInvalidId) ? m_doc->getEntity(id) : nullptr;
    if (!e) return;
    EntityPtr neu;
    if (dynamic_cast<const Line*>(e)) {
        neu = std::make_unique<Line>(Point3{m_geo[0]->value(), m_geo[1]->value(), 0.0},
                                     Point3{m_geo[2]->value(), m_geo[3]->value(), 0.0});
    } else if (dynamic_cast<const Circle*>(e)) {
        const double r = m_geo[2]->value();
        if (r <= 0.0) return;
        neu = std::make_unique<Circle>(Point3{m_geo[0]->value(), m_geo[1]->value(), 0.0}, r);
    } else if (const auto* a = dynamic_cast<const Arc*>(e)) {
        const double r = m_geo[2]->value();
        if (r <= 0.0) return;
        neu = std::make_unique<Arc>(Point3{m_geo[0]->value(), m_geo[1]->value(), 0.0}, r,
                                    a->startAngle(), a->endAngle());
    } else {
        return;
    }
    neu->setLayer(e->layer());          // preserva as propriedades
    neu->setColor(e->color());
    neu->setLineType(e->lineType());
    neu->setLineWeight(e->lineWeight());
    m_doc->execute(std::make_unique<ReplaceCmd>(id, std::move(neu)));
    m_view->rebuild();
}

void MainWindow::applyTextProp() {
    if (m_propUpdating) return;
    const EntityId id = m_view->selectedId();
    const Entity* e = (id != kInvalidId) ? m_doc->getEntity(id) : nullptr;
    const MText* mt = dynamic_cast<const MText*>(e);
    if (!mt) return;
    const double h = m_txtHeight->value();
    if (h <= 0.0) return;
    const double rot = m_txtRot->value() * 3.14159265358979 / 180.0;
    auto neu = std::make_unique<MText>(mt->position(), mt->text(), h, rot);
    neu->setJustify(static_cast<MTextJustify>(m_txtJustify->currentData().toInt()));
    neu->setFont(mt->font());
    neu->setLayer(e->layer());  neu->setColor(e->color());
    neu->setLineType(e->lineType());  neu->setLineWeight(e->lineWeight());
    m_doc->execute(std::make_unique<ReplaceCmd>(id, std::move(neu)));
    m_view->rebuild();
}

void MainWindow::editTextContent() {
    const EntityId id = m_view->selectedId();
    const Entity* e = (id != kInvalidId) ? m_doc->getEntity(id) : nullptr;
    const MText* mt = dynamic_cast<const MText*>(e);
    if (!mt) return;
    bool ok = false;
    const QString s = QInputDialog::getMultiLineText(
        this, "Editar texto", "Conteúdo (Enter = nova linha):",
        QString::fromStdString(mt->text()), &ok);
    if (!ok) return;
    auto neu = std::make_unique<MText>(mt->position(), s.toStdString(), mt->height(), mt->rotation());
    neu->setJustify(mt->justify());
    neu->setFont(mt->font());
    neu->setLayer(e->layer());  neu->setColor(e->color());
    neu->setLineType(e->lineType());  neu->setLineWeight(e->lineWeight());
    m_doc->execute(std::make_unique<ReplaceCmd>(id, std::move(neu)));
    m_view->rebuild();
    updateProperties();
}

// Aplica a formatação corrente da ribbon de texto (fonte/tamanho/negrito/
// itálico) aos MText selecionados — um MacroCmd de ReplaceCmd, com undo.
void MainWindow::applyTextRibbonToSelection() {
    if (!m_txtRibFont) return;
    const std::string fam = (m_txtRibFont->currentIndex() == 0)
                          ? std::string() : m_txtRibFont->currentText().toStdString();
    auto macro = std::make_unique<MacroCmd>("TEXTFMT");
    bool any = false;
    for (const EntityId id : m_view->selectedIds()) {
        const auto* mt = dynamic_cast<const MText*>(m_doc->getEntity(id));
        if (!mt) continue;
        auto neu = std::make_unique<MText>(mt->position(), mt->text(),
                                           m_txtRibSize->value(), mt->rotation());
        neu->setJustify(mt->justify());
        neu->setFont(fam);
        neu->setBold(m_txtRibBold->isChecked());
        neu->setItalic(m_txtRibItalic->isChecked());
        neu->setBoxWidth(mt->boxWidth());
        neu->setLayer(mt->layer());        neu->setColor(mt->color());
        neu->setLineType(mt->lineType());  neu->setLineWeight(mt->lineWeight());
        macro->add(std::make_unique<ReplaceCmd>(id, std::move(neu)));
        any = true;
    }
    if (any) {
        m_doc->execute(std::move(macro));
        m_view->rebuild();
    }
}

void MainWindow::applySelectedProp() {
    if (m_propUpdating) return;
    const std::string layer = m_propLayer->currentText().toStdString();
    const LineType    lt{m_propLtype->currentData().toString().toStdString()};
    // Espessura: aceita um mm digitado (custom); senão usa o item da lista.
    bool lwOk = false;
    const double lwTyped = m_propLweight->currentText().toDouble(&lwOk);
    const LineWeight  lw{(lwOk && lwTyped > 0.0) ? lwTyped : m_propLweight->currentData().toDouble()};
    auto macro = std::make_unique<MacroCmd>("PROPS");
    for (const EntityId id : m_view->selectedIds()) {
        const Entity* e = m_doc->getEntity(id);
        if (!e) continue;
        EntityPtr neu = e->clone();
        neu->setLayer(layer);
        neu->setLineType(lt);
        neu->setLineWeight(lw);
        macro->add(std::make_unique<ReplaceCmd>(id, std::move(neu)));
    }
    if (!macro->empty()) { m_doc->execute(std::move(macro)); m_view->rebuild(); }   // 1 só undo
}

void MainWindow::log(const QString& msg) {
    m_log->appendPlainText(msg);
    m_log->moveCursor(QTextCursor::End);   // cursor NO FIM: a última linha sempre
    m_log->ensureCursorVisible();          // fica visível no histórico curto
}

void MainWindow::testDraw() { m_view->testDraw(); }

// Geometria inicial para a tela não abrir vazia.
void MainWindow::seedDemo() {
    Layer red;  red.name  = "Vermelho"; red.color  = Rgba{230, 70, 70, 255};
    Layer cyan; cyan.name = "Ciano";    cyan.color = Rgba{70, 200, 220, 255};
    Layer ctr;  ctr.name  = "Centro";   ctr.color  = Rgba{230, 210, 90, 255};
    ctr.lineType.name = "CENTER";   // linha de centro (traço-ponto)
    m_doc->layers().add(red);
    m_doc->layers().add(cyan);
    m_doc->layers().add(ctr);

    // Cruz de linha de centro através do círculo (na camada Centro).
    {
        auto hl = std::make_unique<Line>(Point3{-28, 0, 0}, Point3{28, 0, 0});
        hl->setLayer("Centro");
        m_doc->addEntity(std::move(hl));
        auto vl = std::make_unique<Line>(Point3{0, -28, 0}, Point3{0, 28, 0});
        vl->setLayer("Centro");
        m_doc->addEntity(std::move(vl));
    }

    std::vector<Point3> rect = {{-40, -25, 0}, {40, -25, 0}, {40, 25, 0}, {-40, 25, 0}};
    auto poly = std::make_unique<Polyline>(rect, /*closed*/ true);
    poly->setLayer("Ciano");
    m_doc->addEntity(std::move(poly));

    auto circ = std::make_unique<Circle>(Point3{0, 0, 0}, 18.0);
    circ->setLayer("Vermelho");
    m_doc->addEntity(std::move(circ));

    m_doc->addEntity(std::make_unique<Line>(Point3{-40, -25, 0}, Point3{40, 25, 0}));  // camada 0 (branco)
    m_doc->addEntity(std::make_unique<Line>(Point3{-40, 25, 0}, Point3{40, -25, 0}));
}

void MainWindow::refreshLayers() {
    if (!m_layers) return;
    QVector<LayersPanel::LayerInfo> infos;
    for (const Layer& l : m_doc->layers().all()) {
        LayersPanel::LayerInfo info;
        info.name    = QString::fromStdString(l.name);
        info.color   = QColor(l.color.r, l.color.g, l.color.b);
        info.visible = l.on;
        info.frozen  = l.frozen;
        info.current = (info.name == m_currentLayerName);
        QString lt = QString::fromStdString(l.lineType.name);
        if (lt == "ByLayer" || lt.isEmpty()) lt = "CONTINUOUS";
        info.linetype = lt;
        info.locked     = l.locked;
        info.lineweight = l.lineWeight.mm;
        infos.push_back(info);
    }
    m_layers->setLayers(infos);

    // Sincroniza a lista suspensa de camadas da ribbon (sem disparar o handler).
    if (m_ribbonLayerCombo) {
        m_ribbonLayerCombo->blockSignals(true);
        m_ribbonLayerCombo->clear();
        for (const Layer& l : m_doc->layers().all())
            m_ribbonLayerCombo->addItem(layerSwatch(QColor(l.color.r, l.color.g, l.color.b)),
                                        QString::fromStdString(l.name));
        m_ribbonLayerCombo->setCurrentText(m_currentLayerName);
        m_ribbonLayerCombo->blockSignals(false);
    }
}

void MainWindow::onCommandEntered() {
    const QString line = m_cli->text().trimmed();
    m_cli->clear();
    runCommand(line);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    // Esc com o foco na linha de comando = cancela a operação e desmarca no canvas
    // (antes o Esc só limpava o campo; a seleção continuava ativa). O Esc chega
    // primeiro como ShortcutOverride — precisamos aceitá-lo para que o KeyPress
    // seja entregue ao campo (senão o sistema de atalhos "engole" a tecla).
    if (obj == m_cli) {
        if (ev->type() == QEvent::ShortcutOverride) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if (ke->key() == Qt::Key_Escape) { ev->accept(); return true; }
        } else if (ev->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if (ke->key() == Qt::Key_Escape) {
                m_cli->clear();
                if (m_view) m_view->cancelAndDeselect();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::applyCurrentDimStyle() {
    const DimStyle& s = m_styles.currentDim();
    m_view->setDimStyle(s.decimals, s.suffix, s.arrowSize, s.arrowType);
    m_view->setDimTolerance(s.tolPlus, s.tolMinus);
    m_view->setAnnotationHeight(s.textHeight);
    m_view->setDimAnnotative(s.annotative);   // altura/seta em mm de papel
}

void MainWindow::openDimStyleManager() {
    QDialog dlg(this);
    dlg.setWindowTitle("Estilos de Cota");
    dlg.resize(480, 300);
    auto* lay = new QHBoxLayout(&dlg);

    auto* list = new QListWidget(&dlg);
    list->setMaximumWidth(170);
    lay->addWidget(list);

    auto* right = new QVBoxLayout;
    auto* form  = new QFormLayout;
    auto* spH = new QDoubleSpinBox; spH->setRange(0.01, 1e6); spH->setDecimals(3);
    auto* spA = new QDoubleSpinBox; spA->setRange(0.0, 1e6);  spA->setDecimals(3);
    spA->setSpecialValueText("Auto");      // 0 = automático (= altura do texto)
    auto* spD = new QSpinBox; spD->setRange(0, 8);
    auto* edS = new QLineEdit;
    auto* spT = new QComboBox;
    spT->addItems({"Seta", "Tique arquitetônico ( / )", "Ponto"});
    auto* spTolP = new QDoubleSpinBox; spTolP->setRange(0.0, 1e6); spTolP->setDecimals(3);
    spTolP->setSpecialValueText("Sem");    // 0 = sem tolerância
    auto* spTolM = new QDoubleSpinBox; spTolM->setRange(0.0, 1e6); spTolM->setDecimals(3);
    spTolM->setSpecialValueText("= +");    // 0 = usa o mesmo valor do +
    form->addRow("Altura do texto:", spH);
    form->addRow("Tamanho da seta:", spA);
    form->addRow("Tipo de ponta:",   spT);
    form->addRow("Casas decimais:",  spD);
    form->addRow("Sufixo:",          edS);
    form->addRow("Tolerância (+):",  spTolP);
    form->addRow("Tolerância (−):",  spTolM);
    auto* ckAnn = new QCheckBox("Anotativo (alturas em MM DE PAPEL)");
    ckAnn->setToolTip("A cota mantém o tamanho impresso em qualquer escala:\n"
                      "altura/seta valem em mm de papel e seguem a escala de anotação.");
    form->addRow("", ckAnn);
    right->addLayout(form);

    auto* btnNew = new QPushButton("Novo");
    auto* btnDel = new QPushButton("Excluir");
    auto* btnCur = new QPushButton("Tornar corrente");
    auto* btnClose = new QPushButton("Fechar");
    auto* brow = new QHBoxLayout;
    brow->addWidget(btnNew); brow->addWidget(btnDel); brow->addWidget(btnCur);
    brow->addStretch(); brow->addWidget(btnClose);
    right->addLayout(brow);
    right->addStretch();
    lay->addLayout(right, 1);

    bool loading = false;
    auto curName = [&]() -> QString {
        QListWidgetItem* it = list->currentItem();
        return it ? it->data(Qt::UserRole).toString() : QString();
    };
    auto reload = [&](const QString& sel) {
        list->clear();
        const QString cur = QString::fromStdString(m_styles.currentDimName());
        for (const DimStyle& s : m_styles.allDim()) {
            const QString nm = QString::fromStdString(s.name);
            auto* it = new QListWidgetItem((nm == cur ? "● " : "") + nm);
            it->setData(Qt::UserRole, nm);
            list->addItem(it);
            if (nm == sel) list->setCurrentItem(it);
        }
        if (!list->currentItem() && list->count()) list->setCurrentRow(0);
    };
    auto loadFields = [&]() {
        const DimStyle* s = m_styles.findDim(curName().toStdString());
        if (!s) return;
        loading = true;
        spH->setValue(s->textHeight);
        spA->setValue(s->arrowSize < 0.0 ? 0.0 : s->arrowSize);
        spD->setValue(s->decimals);
        edS->setText(QString::fromStdString(s->suffix));
        spT->setCurrentIndex(s->arrowType);
        spTolP->setValue(s->tolPlus);
        spTolM->setValue(s->tolMinus);
        ckAnn->setChecked(s->annotative);
        loading = false;
    };
    auto saveFields = [&]() {
        if (loading) return;
        const QString n = curName();
        if (n.isEmpty()) return;
        DimStyle s;
        s.name       = n.toStdString();
        s.textHeight = spH->value();
        s.arrowSize  = spA->value() <= 0.0 ? -1.0 : spA->value();
        s.decimals   = spD->value();
        s.suffix     = edS->text().toStdString();
        s.arrowType  = spT->currentIndex();
        s.tolPlus    = spTolP->value();
        s.tolMinus   = (spTolM->value() > 0.0) ? spTolM->value() : spTolP->value();
        s.annotative = ckAnn->isChecked();
        m_styles.addDim(s);
        if (n == QString::fromStdString(m_styles.currentDimName())) applyCurrentDimStyle();
    };

    connect(list, &QListWidget::currentItemChanged, &dlg, [&] { loadFields(); });
    connect(spH, &QDoubleSpinBox::valueChanged, &dlg, [&] { saveFields(); });
    connect(spA, &QDoubleSpinBox::valueChanged, &dlg, [&] { saveFields(); });
    connect(spD, &QSpinBox::valueChanged,       &dlg, [&] { saveFields(); });
    connect(edS, &QLineEdit::textEdited,        &dlg, [&] { saveFields(); });
    connect(spT, &QComboBox::currentIndexChanged, &dlg, [&] { saveFields(); });
    connect(spTolP, &QDoubleSpinBox::valueChanged, &dlg, [&] { saveFields(); });
    connect(spTolM, &QDoubleSpinBox::valueChanged, &dlg, [&] { saveFields(); });
    connect(ckAnn, &QCheckBox::toggled, &dlg, [&] { saveFields(); });
    connect(btnNew, &QPushButton::clicked, &dlg, [&] {
        bool ok = false;
        const QString n = QInputDialog::getText(&dlg, "Novo estilo de cota", "Nome:",
                                                QLineEdit::Normal, QString(), &ok);
        if (!ok || n.trimmed().isEmpty()) return;
        DimStyle s = m_styles.currentDim();   // herda do corrente como base
        s.name = n.trimmed().toStdString();
        m_styles.addDim(s);
        reload(n.trimmed());
    });
    connect(btnDel, &QPushButton::clicked, &dlg, [&] {
        const QString n = curName();
        if (n.isEmpty()) return;
        if (!m_styles.removeDim(n.toStdString())) {
            QMessageBox::information(&dlg, "Estilos de Cota", "O estilo 'Standard' não pode ser excluído.");
            return;
        }
        applyCurrentDimStyle();
        reload(QString::fromStdString(m_styles.currentDimName()));
    });
    connect(btnCur, &QPushButton::clicked, &dlg, [&] {
        const QString n = curName();
        if (n.isEmpty()) return;
        m_styles.setCurrentDim(n.toStdString());
        applyCurrentDimStyle();
        reload(n);
        log("Estilo de cota corrente: " + n);
    });
    connect(btnClose, &QPushButton::clicked, &dlg, [&] { dlg.accept(); });

    reload(QString::fromStdString(m_styles.currentDimName()));
    loadFields();
    dlg.exec();
}

void MainWindow::openTextStyleManager() {
    QDialog dlg(this);
    dlg.setWindowTitle("Estilos de Texto");
    dlg.resize(420, 260);
    auto* lay = new QHBoxLayout(&dlg);

    auto* list = new QListWidget(&dlg);
    list->setMaximumWidth(170);
    lay->addWidget(list);

    auto* right = new QVBoxLayout;
    auto* form  = new QFormLayout;
    auto* spH = new QDoubleSpinBox; spH->setRange(0.01, 1e6); spH->setDecimals(3);
    form->addRow("Altura do texto:", spH);
    // Fonte: 1º item = fonte de TRAÇOS do kernel (padrão CAD); demais = TTF.
    auto* cbFont = new QFontComboBox;
    cbFont->setEditable(false);
    cbFont->insertItem(0, "(traços — padrão CAD)");
    form->addRow("Fonte:", cbFont);
    auto* ckAnnT = new QCheckBox("Anotativo (altura em MM DE PAPEL)");
    ckAnnT->setToolTip("O texto mantém o tamanho impresso em qualquer escala:\n"
                       "a altura vale em mm de papel e segue a escala de anotação.");
    form->addRow("", ckAnnT);
    right->addLayout(form);

    auto* btnNew = new QPushButton("Novo");
    auto* btnDel = new QPushButton("Excluir");
    auto* btnCur = new QPushButton("Tornar corrente");
    auto* btnClose = new QPushButton("Fechar");
    auto* brow = new QHBoxLayout;
    brow->addWidget(btnNew); brow->addWidget(btnDel); brow->addWidget(btnCur);
    brow->addStretch(); brow->addWidget(btnClose);
    right->addLayout(brow);
    right->addStretch();
    lay->addLayout(right, 1);

    bool loading = false;
    auto curName = [&]() -> QString {
        QListWidgetItem* it = list->currentItem();
        return it ? it->data(Qt::UserRole).toString() : QString();
    };
    auto reload = [&](const QString& sel) {
        list->clear();
        const QString cur = QString::fromStdString(m_styles.currentTextName());
        for (const TextStyle& s : m_styles.allText()) {
            const QString nm = QString::fromStdString(s.name);
            auto* it = new QListWidgetItem((nm == cur ? "● " : "") + nm);
            it->setData(Qt::UserRole, nm);
            list->addItem(it);
            if (nm == sel) list->setCurrentItem(it);
        }
        if (!list->currentItem() && list->count()) list->setCurrentRow(0);
    };
    auto loadFields = [&]() {
        const TextStyle* s = m_styles.findText(curName().toStdString());
        if (!s) return;
        loading = true;
        spH->setValue(s->height);
        if (s->font.empty()) cbFont->setCurrentIndex(0);
        else cbFont->setCurrentFont(QFont(QString::fromStdString(s->font)));
        ckAnnT->setChecked(s->annotative);
        loading = false;
    };
    auto saveFields = [&]() {
        if (loading) return;
        const QString n = curName();
        if (n.isEmpty()) return;
        TextStyle s; s.name = n.toStdString(); s.height = spH->value();
        s.font = (cbFont->currentIndex() == 0)
               ? std::string() : cbFont->currentText().toStdString();
        s.annotative = ckAnnT->isChecked();
        m_styles.addText(s);
        if (n == QString::fromStdString(m_styles.currentTextName())) {
            m_view->setAnnotationHeight(s.height);
            m_view->setAnnotationFont(s.font);
            m_view->setTextAnnotative(s.annotative);
        }
    };

    connect(list, &QListWidget::currentItemChanged, &dlg, [&] { loadFields(); });
    connect(spH, &QDoubleSpinBox::valueChanged, &dlg, [&] { saveFields(); });
    connect(cbFont, &QFontComboBox::currentIndexChanged, &dlg, [&] { saveFields(); });
    connect(ckAnnT, &QCheckBox::toggled, &dlg, [&] { saveFields(); });
    connect(btnNew, &QPushButton::clicked, &dlg, [&] {
        bool ok = false;
        const QString n = QInputDialog::getText(&dlg, "Novo estilo de texto", "Nome:",
                                                QLineEdit::Normal, QString(), &ok);
        if (!ok || n.trimmed().isEmpty()) return;
        TextStyle s = m_styles.currentText();
        s.name = n.trimmed().toStdString();
        m_styles.addText(s);
        reload(n.trimmed());
    });
    connect(btnDel, &QPushButton::clicked, &dlg, [&] {
        const QString n = curName();
        if (n.isEmpty()) return;
        if (!m_styles.removeText(n.toStdString())) {
            QMessageBox::information(&dlg, "Estilos de Texto", "O estilo 'Standard' não pode ser excluído.");
            return;
        }
        m_view->setAnnotationHeight(m_styles.currentText().height);
        m_view->setTextAnnotative(m_styles.currentText().annotative);
        m_view->setAnnotationFont(m_styles.currentText().font);
        reload(QString::fromStdString(m_styles.currentTextName()));
    });
    connect(btnCur, &QPushButton::clicked, &dlg, [&] {
        const QString n = curName();
        if (n.isEmpty()) return;
        m_styles.setCurrentText(n.toStdString());
        m_view->setAnnotationHeight(m_styles.currentText().height);
        m_view->setTextAnnotative(m_styles.currentText().annotative);
        m_view->setAnnotationFont(m_styles.currentText().font);
        reload(n);
        log("Estilo de texto corrente: " + n);
    });
    connect(btnClose, &QPushButton::clicked, &dlg, [&] { dlg.accept(); });

    reload(QString::fromStdString(m_styles.currentTextName()));
    loadFields();
    dlg.exec();
}

void MainWindow::runCommand(const QString& raw) {
    const QString line = raw.trimmed();
    if (line.isEmpty()) return;

    // Retângulo (e variantes): estilo AutoCAD — o 2º input é o CANTO OPOSTO.
    //   absoluto  "x,y"   -> canto naquela coordenada;
    //   relativo "@dx,dy" -> canto = 1º canto + (dx,dy)  (equivale a largura×altura).
    // "L x A" explícito (com 'x') ainda é aceito como dimensões, por conveniência.
    if (m_view->toolWantsDimensions() &&
        (line.contains('x', Qt::CaseInsensitive) || line.contains(';'))) {
        QString s = line;
        s.replace('x', ',').replace('X', ',').replace(';', ',').replace(' ', ',');
        const QStringList parts = s.split(',', Qt::SkipEmptyParts);
        if (parts.size() == 2) {
            bool ok1 = false, ok2 = false;
            const double w = parts[0].toDouble(&ok1), h = parts[1].toDouble(&ok2);
            if (ok1 && ok2) {
                m_view->inputDimensions(w, h);
                log(QString("Retângulo: %1 x %2").arg(w).arg(h));
                return;
            }
        }
    }

    const ParsedInput in = parseCommandLine(line.toStdString(), m_view->lastPoint());

    if (in.kind == ParsedInput::Kind::Point) {
        // UCS ativo: coordenada digitada é interpretada NO FRAME DO UCS —
        // absoluta = ucs->mundo; relativa (@) = delta ROTACIONADO pelo UCS.
        Point3 p = in.point;
        if (m_doc->ucsActive()) {
            if (in.relative) {
                const Point3 last = m_view->lastPoint();
                const Point3 d = m_doc->ucsDirToWorld(
                    Point3{p.x - last.x, p.y - last.y, 0.0});
                p = Point3{last.x + d.x, last.y + d.y, 0.0};
            } else {
                p = m_doc->ucsToWorld(p);
            }
        }
        m_view->inputPoint(p);                              // coordenada digitada
        log(QString("Ponto: %1, %2%3").arg(in.point.x).arg(in.point.y)
                .arg(m_doc->ucsActive() ? " (UCS)" : ""));
        return;
    }
    if (in.kind == ParsedInput::Kind::Distance) {
        // Um número digitado significa coisas diferentes por ferramenta:
        const ToolKind tk = m_view->tool();
        if (tk == ToolKind::Fillet || tk == ToolKind::RectFillet) {
            m_view->setFilletRadius(in.distance);
            log(QString("Raio do fillet: %1").arg(in.distance));
        } else if (tk == ToolKind::Chamfer || tk == ToolKind::RectChamfer) {
            m_view->setChamferDist(in.distance);
            log(QString("Distância do chanfro: %1").arg(in.distance));
        } else if (tk == ToolKind::Offset) {
            m_view->setOffsetDist(in.distance);
            log(QString("Distância do offset: %1").arg(in.distance));
        } else if (tk == ToolKind::CircleTTR) {
            m_view->setTtrRadius(in.distance);
            log(QString("Raio do TTR: %1").arg(in.distance));
        } else if (tk == ToolKind::WallTool) {
            m_view->setWallThickness(in.distance);
            log(QString("Espessura da parede: %1").arg(in.distance));
        } else {
            m_view->inputDistance(in.distance);             // tamanho na direção do cursor
            log(QString("Distância: %1").arg(in.distance));
        }
        return;
    }
    if (in.kind == ParsedInput::Kind::Control) {
        if (in.control == "UNDO")      { m_doc->undo(); m_view->rebuild(); log("Undo."); }
        else if (in.control == "REDO") { m_doc->redo(); m_view->rebuild(); log("Redo."); }
        return;
    }
    if (in.kind == ParsedInput::Kind::Command) {
        const QString c = QString::fromStdString(in.command).toUpper();
        // Ações que não são "setar ferramenta" (com aliases):
        if (c == "ERASE" || c == "E") { m_view->setTool(ToolKind::None); m_view->eraseSelected(); log("Apagar seleção."); return; }
        if (c == "EXPLODE" || c == "X") { m_view->explodeSelected(); log("Explodir seleção."); return; }
        if (c == "SELECT") { m_view->setTool(ToolKind::None); log("Modo seleção."); return; }
        if (c == "BLOCK"  || c == "B") { createBlockInteractive(); return; }
        if (c == "INSERT" || c == "I") { insertBlockInteractive(); return; }
        if (c == "LTSCALE" || c == "LTS") {
            bool ok = false;
            const double s = QInputDialog::getDouble(this, "LTSCALE",
                "Escala global dos tipos de linha:", m_view->lineTypeScale(),
                0.01, 1000.0, 2, &ok);
            if (ok) { m_view->setLineTypeScale(s); log(QString("LTSCALE = %1").arg(s)); }
            return;
        }
        if (c == "QSELECT" || c == "QSE") { quickSelectInteractive(); return; }
        if (c == "ATTDEF" || c == "ATT") { attDefInteractive(); return; }
        if (c == "ATTEDIT" || c == "ATE") { attEditInteractive(); return; }
        if (c == "FIND" || c == "LOCALIZAR") { findTextInteractive(); return; }
        // Vista (estilo AutoCAD): Z=Janela · ZE=Extensão · ZP=Anterior · PAN.
        if (c == "ZOOM" || c == "Z") {
            m_view->beginZoomWindow();
            log("Zoom Janela: arraste a área (ZE=Extensão, ZP=Anterior).");
            return;
        }
        if (c == "ZE" || c == "ZA") { m_view->zoomExtents(); log("Zoom Extensão."); return; }
        if (c == "ZP") { m_view->zoomPrevious(); log("Zoom Anterior."); return; }
        if (c == "PAN" || c == "P") {
            m_view->beginPan();
            log("Pan: arraste para deslocar a vista (Esc sai).");
            return;
        }
        // Seleção avançada (estilo AutoCAD).
        if (c == "WP") { m_view->beginSelectPolygon(false); return; }
        if (c == "CP") { m_view->beginSelectPolygon(true);  return; }
        if (c == "ALL" || c == "TODOS") {
            log(QString("ALL: %1 entidade(s) selecionada(s).").arg(m_view->selectAllVisible()));
            return;
        }
        if (c == "PREVIOUS" || c == "PRE" || c == "ANTERIOR") {
            log(QString("Previous: %1 entidade(s) reselecionada(s).").arg(m_view->selectPrevious()));
            return;
        }
        if (c == "LAST" || c == "ULTIMO") {
            log(m_view->selectLastCreated() ? "Last: última entidade selecionada."
                                            : "Last: nenhuma entidade recente.");
            return;
        }
        if (c == "GROUP" || c == "AGRUPAR" || c == "G") {
            const auto ids = m_view->selectedIds();
            if (ids.size() < 2) { log("GROUP: selecione 2+ entidades primeiro."); return; }
            bool ok = false;
            const QString n = QInputDialog::getText(this, "Agrupar", "Nome do grupo:",
                QLineEdit::Normal, QString("Grupo%1").arg(int(m_doc->groups().size()) + 1), &ok);
            if (!ok || n.trimmed().isEmpty()) return;
            m_doc->addGroup(n.trimmed().toStdString(), ids);
            log(QString("Grupo \"%1\" criado com %2 entidades (clicar num membro seleciona o grupo).")
                    .arg(n.trimmed()).arg(ids.size()));
            return;
        }
        if (c == "M2P" || c == "MTP") { m_view->beginM2P(); return; }
        if (c == "PURGE" || c == "PU") { purgeInteractive(); return; }
        if (c == "PUBLISH" || c == "PUB") { publishInteractive(); return; }
        if (c == "PREVIEW") { previewPlot(); return; }
        if (c == "PLOTSTYLE" || c == "CTB") { openPlotStyleDialog(); return; }
        if (c == "UCS") {
            bool ok = false;
            const QStringList opts{"Origem + ângulo (2 cliques)", "Só a origem (1 clique)",
                                   "Ângulo numérico...", "Mundo (WCS)"};
            const QString op = QInputDialog::getItem(this, "UCS",
                "Sistema de coordenadas de trabalho:", opts, 0, false, &ok);
            if (!ok) return;
            if (op.startsWith("Mundo")) {
                m_doc->setUcs(Point3{}, 0.0);
                m_view->update();
                log("UCS: de volta ao MUNDO (WCS).");
            } else if (op.startsWith("Ângulo")) {
                const double cur = m_doc->ucsAngleRad() * 57.29577951308232;
                const double a = QInputDialog::getDouble(this, "UCS",
                    "Ângulo do eixo X (graus, CCW):", cur, -360.0, 360.0, 2, &ok);
                if (!ok) return;
                m_doc->setUcs(m_doc->ucsOrigin(), a / 57.29577951308232);
                m_view->update();
                log(QString("UCS: eixo X a %1° (origem mantida).").arg(a));
            } else {
                m_view->beginUcsDefine(op.startsWith("Origem +"));
            }
            return;
        }
        if (c == "XREF" || c == "XR") { xrefManagerInteractive(); return; }
        if (c == "BVIS" || c == "VISBLOCO") { blockVisibilityInteractive(); return; }
        if (c == "ANNOSCALE" || c == "CANNOSCALE" || c == "ESCANOT") {
            bool ok = false;
            const double unitMm = unitLengthMm(m_unitIndex);
            const double curDen = m_doc->annoMmPerUnit() > 0.0
                                ? unitMm / m_doc->annoMmPerUnit() : 1.0;
            const double d = QInputDialog::getDouble(this, "Escala de anotação",
                "Escala 1:N das anotações ANOTATIVAS — informe N:",
                curDen, 0.001, 1e6, 3, &ok);
            if (!ok) return;
            const std::size_t n = m_doc->applyAnnotationScale(scaleMmPerUnit(unitMm, d));
            m_view->rebuild();
            log(QString("Escala de anotação: %1 — %2 anotativa(s) redimensionada(s).")
                    .arg(QString::fromStdString(formatScale(d))).arg(n));
            return;
        }
        if (c == "QDIM") { m_view->beginQdim(); return; }
        if (c == "DIMJOG" || c == "DJO") {
            m_view->setDimJogged(true);
            m_view->setTool(ToolKind::DimRadius);
            log("Cota de raio JOGGED: clique o círculo/arco e posicione.");
            return;
        }
        if (c == "UNGROUP" || c == "DESAGRUPAR") {
            const auto ids = m_view->selectedIds();
            const std::string* gn = ids.empty() ? nullptr : m_doc->groupNameOf(ids.front());
            if (!gn) { log("UNGROUP: selecione um membro de um grupo."); return; }
            const QString nome = QString::fromStdString(*gn);
            m_doc->removeGroup(*gn);
            log(QString("Grupo \"%1\" desfeito (as entidades continuam no desenho).").arg(nome));
            return;
        }
        if (c == "OVERKILL") {
            int dups = 0, merged = 0;
            const bool sel = !m_view->selectedIds().empty();
            const bool did = m_view->overkillSelected(1e-6, dups, merged);
            log(did ? QString("OVERKILL: %1 duplicada(s) removida(s), %2 linha(s) sobreposta(s) unida(s)%3.")
                          .arg(dups).arg(merged)
                          .arg(sel ? "" : " (desenho todo)")
                    : QStringLiteral("OVERKILL: nada para limpar."));
            return;
        }
        if (c == "REVERSE" || c == "REV") {
            const int n = m_view->reverseSelected();
            log(n > 0 ? QString("REVERSE: %1 entidade(s) com o sentido invertido.").arg(n)
                      : QStringLiteral("REVERSE: selecione linhas/polilinhas/splines primeiro."));
            return;
        }
        if (c == "BLEND" || c == "BLE") {
            log(m_view->blendSelected()
                    ? QStringLiteral("BLEND: spline de transição tangente criada.")
                    : QStringLiteral("BLEND: selecione exatamente 2 entidades ABERTAS (linha/arco/polilinha/spline)."));
            return;
        }
        if (c == "JOIN" || c == "J") {
            if (m_view->selectedIds().size() >= 2) {   // seleção múltipla: une direto
                if (m_view->joinSelected()) log("Join: seleção unida numa polilinha.");
                else log("Join: a seleção não forma UMA corrente de linhas conectadas.");
                return;
            }
            m_view->setTool(ToolKind::JoinTool);
            log("Join: clique 2 linhas colineares (ou selecione várias e repita).");
            return;
        }
        if (c == "UNDO" || c == "U") { m_doc->undo(); m_view->rebuild(); log("Undo."); return; }
        if (c == "REDO") { m_doc->redo(); m_view->rebuild(); log("Redo."); return; }
        // Marcas de undo (UNDO Mark/Back) e OOPS.
        if (c == "UM") { m_doc->undoMark(); log("Marca de undo colocada (UB volta até aqui)."); return; }
        if (c == "UB") {
            const std::size_t n = m_doc->undoBack();
            m_view->rebuild();
            refreshLayers();
            log(n > 0 ? QString("Undo Back: %1 comando(s) desfeito(s) até a marca.").arg(n)
                      : QStringLiteral("Undo Back: nenhuma marca (use UM para marcar)."));
            return;
        }
        if (c == "OOPS") {
            const std::size_t n = m_doc->oops();
            m_view->rebuild();
            log(n > 0 ? QString("OOPS: %1 entidade(s) do último apagamento restaurada(s).").arg(n)
                      : QStringLiteral("OOPS: nada apagado recentemente."));
            return;
        }
        // Tabela única de comandos + aliases (estilo AutoCAD).
        ToolKind t;
        if (resolveCommand(c, t)) {
            m_view->setTool(t);
            if (t == ToolKind::Door)
                log("Porta: clique a DOBRADIÇA, o outro lado do VÃO e o LADO de abertura.");
            else
                log("Comando: " + c);
        }
        else log(QString("Comando desconhecido: %1").arg(c));
    }
}

} // namespace cad
