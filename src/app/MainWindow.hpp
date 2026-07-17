// src/app/MainWindow.hpp
#pragma once
#include <QMainWindow>
#include <QHash>
#include <memory>
#include <vector>

#include "core/document/DrawingManager.hpp"
#include "core/document/Style.hpp"
#include "core/layout/Layout.hpp"
#include "core/document/PlotStyle.hpp"
#include "ui/Theme.hpp"

class QPlainTextEdit;
class QLineEdit;
class QLabel;
class QMenu;
class QComboBox;
class QPushButton;
class QAction;
class QDockWidget;
class QTabWidget;
class QDoubleSpinBox;
class QFormLayout;
class QTabBar;
class QStackedWidget;
class QFontComboBox;
class QToolButton;
class QActionGroup;
class QPdfWriter;
class QPainter;

namespace cad {

class StartPage;

class ViewportWidget;
class LayersPanel;

// Janela principal: viewport OpenGL 2D (central) + dock com linha de comando
// (estilo AutoCAD) e log. Fecha o ciclo CLI -> Command -> documento -> tela.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(std::unique_ptr<DrawingManager> doc, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Modo de verificação: após o primeiro paint, captura o viewport num PNG e
    // encerra o app. Usado para validar o render headless de CI/scripts.
    void requestShot(const QString& path);
    void testDraw();   // roteia uma sequência de desenho (verificação)
    // Abre um .zencad passado na linha de comando (duplo clique/associação);
    // pula a tela inicial e vai direto ao desenho.
    void openProjectAtStartup(const QString& path) {
        m_suppressStart = true;
        openProjectPath(path);
    }
    // PUBLISH: todas as pranchas com viewport num PDF multipágina. Público
    // também para o modo headless (cadapp projeto.zencad --publish out.pdf).
    bool publishAllLayoutsTo(const QString& path);
    // QA headless: renderiza a prancha corrente (CTB aplicado) num PNG.
    bool plotShotTo(const QString& path);

private slots:
    void onCommandEntered();

private:
    void runCommand(const QString& line);   // executa uma linha de comando (CLI ou canvas)
    void log(const QString& msg);
    void seedDemo();
    void refreshLayers();
    void openDimStyleManager();             // gerenciador de estilos de cota (#7)
    void applyCurrentDimStyle();            // aplica o estilo de cota corrente ao viewport
    void openTextStyleManager();            // gerenciador de estilos de texto (#7)
    bool eventFilter(QObject* obj, QEvent* ev) override;   // Esc na linha de comando = cancelar/desmarcar
    void buildMenuBar();
    void rebuildLayoutTabs();               // refaz as abas Modelo/Pranchas (+)
    void onLayoutTabChanged(int index);     // troca Modelo <-> prancha (ou cria via "+")
    void openPaperSetup();                  // diálogo Config. de Página (tamanho/orientação/selo)
    void addViewportToCurrent(double x, double y, double w, double h);  // viewport desenhado -> escala + insere
    void plotCurrentLayout();               // plota a prancha corrente em PDF (escala 1:N verdadeira)
    void publishInteractive();              // PUBLISH com diálogo de arquivo
    // Desenha UMA prancha na "página" corrente do painter (PDF ou imagem) —
    // compartilhado por Plotar / PUBLISH / preview; aplica o CTB ativo.
    void paintLayoutPage(const Layout& L, QPainter& p, double dpi, double pageHeightPx);
    QImage renderLayoutImage(const Layout& L, double targetWidthPx);  // preview
    void previewPlot();                     // diálogo Visualizar plotagem
    void openPlotStyleDialog();             // editor da tabela CTB (por camada)
    // --- Projeto (.zencad) + tela inicial ---
    void newProject();                      // documento em branco (nova aba, ou reusa a vazia)
    void newProjectInPlace();               // limpa a SESSÃO corrente (sem undo)
    void openProjectInteractive();          // diálogo Abrir projeto
    void openProjectPath(const QString& p); // abre um caminho (recentes/tela inicial)
    void saveProjectInteractive(bool saveAs); // Salvar / Salvar como
    void showStartPage();                   // página inicial (marca + novo/abrir/cards de recentes)
    void showDrawing();                     // volta à área de desenho
    void addRecentProject(const QString& p);
    QStringList recentProjects() const;
    void updateWindowTitle();
    void applyProjectSettingsToUi();        // ltscale/unidades/camada -> UI após abrir
    void createBlockInteractive();          // BLOCK: pede nome -> arma a ferramenta de bloco
    void insertBlockInteractive();          // INSERT: escolhe bloco da biblioteca + escala/rotação
    // --- XREF (referências externas a outros .zencad) ---
    void xrefManagerInteractive();          // diálogo anexar/recarregar/desanexar
    bool xrefReload(const std::string& name, QString* err);  // relê o arquivo -> redefine + atualiza inserções
    int  reloadAllXrefs();                  // ao abrir o projeto (avisa ausentes)
    QString xrefAbsolutePath(const QString& stored) const;   // resolve relativo ao projeto
    void blockVisibilityInteractive();      // estados de visibilidade da inserção selecionada
    void quickSelectInteractive();          // QSELECT: seleção rápida por tipo/camada
    void purgeInteractive();                // PURGE: blocos/estilos/grupos/camadas sem uso
    void attDefInteractive();               // ATTDEF: tag/prompt/padrão -> clique posiciona
    void attEditInteractive();              // ATTEDIT: edita valores de atributo do INSERT selecionado
    void findTextInteractive();             // FIND: localizar/substituir texto no desenho
    void updateProperties();                // preenche o painel Propriedades
    void applyTextRibbonToSelection();      // fonte/tam/N/I da ribbon -> MText selecionado(s)

    // --- MULTI-DOCUMENTO (abas de arquivo, estilo AutoCAD) -----------------
    // Cada documento aberto é uma SESSÃO: doc + viewport PRÓPRIO (câmera/
    // seleção/undo preservados ao trocar de aba) + pranchas/estilos/caminho.
    // Os membros de trabalho (m_layouts/m_styles/m_projectPath/...) são
    // trocados (move) ao alternar a sessão corrente.
    struct DocSession {
        std::unique_ptr<DrawingManager> doc;
        ViewportWidget* view{nullptr};
        LayoutTable layouts;
        StyleTable  styles;
        PlotStyleTable plotStyle;
        QString projectPath;
        QString currentLayer{"0"};
        int     unitIndex{0}, unitDecimals{2};
        QString unitSuffix;
    };
    void wireViewport(ViewportWidget* v);   // connects DE um viewport (por instância)
    QToolButton* makeTabCloseButton();      // "✕" no estilo do app (hover latão)
    int  newDocSession();                   // cria doc em branco + aba + troca
    void switchToDoc(int idx);              // stash da corrente + restore da nova
    void closeDoc(int idx);                 // fecha a aba (última vira doc em branco)
    void stashCurrentSession();             // membros de trabalho -> sessão corrente
    void refreshUiForCurrentDoc();          // painéis/abas/título da sessão corrente
    void updateFileTabText(int idx);
    bool currentIsBlank() const;            // sem título, vazio e sem histórico
    void applySelectedProp();               // aplica a edição inline à seleção (ReplaceCmd)
    void applyGeometry();                   // aplica a edição numérica de coordenadas/raio
    void applyTextProp();                   // aplica altura/alinhamento/ângulo ao MTEXT
    void editTextContent();                 // abre o diálogo multilinha p/ editar o conteúdo

    std::vector<std::unique_ptr<DocSession>> m_sessions;   // documentos abertos
    int             m_curSession{-1};
    QTabBar*        m_fileTabs{nullptr};    // abas de ARQUIVO (multi-doc)
    QStackedWidget* m_viewStack{nullptr};   // um ViewportWidget por sessão
    bool            m_fileTabsBusy{false};
    QActionGroup*   m_toolGroup{nullptr};   // grupo dos botões de ferramenta

    DrawingManager* m_doc{nullptr};         // ATALHO: doc da sessão corrente
    ViewportWidget* m_view{nullptr};        // ATALHO: viewport da sessão corrente
    QPlainTextEdit* m_log{nullptr};
    QLineEdit*      m_cli{nullptr};
    LayersPanel*    m_layers{nullptr};
    QLabel*         m_props{nullptr};        // info (tipo/geometria) do painel Propriedades
    QComboBox*      m_propLayer{nullptr};    // edição inline: camada
    QPushButton*    m_propColor{nullptr};    // edição inline: cor (swatch)
    QComboBox*      m_propLtype{nullptr};    // edição inline: tipo de linha
    QComboBox*      m_propLweight{nullptr};  // edição inline: espessura
    QWidget*        m_propEdit{nullptr};     // container dos editores de propriedade
    QWidget*        m_geoEdit{nullptr};      // container dos campos numéricos de geometria
    QFormLayout*    m_geoForm{nullptr};      // layout (rótulo + spin) dos campos de geometria
    QDoubleSpinBox* m_geo[4]{};              // até 4 campos: Line(x1,y1,x2,y2) / Circle(cx,cy,r)
    QLabel*         m_geoLbl[4]{};           // rótulos dos campos de geometria
    QWidget*        m_txtEdit{nullptr};      // container do editor de TEXTO (só p/ MTEXT)
    QDoubleSpinBox* m_txtHeight{nullptr};    // altura do texto
    QDoubleSpinBox* m_txtRot{nullptr};       // ângulo (graus)
    QComboBox*      m_txtJustify{nullptr};   // alinhamento (esq/centro/dir)
    bool            m_propUpdating{false};   // evita loop ao preencher programaticamente
    QLabel*         m_zoom{nullptr};         // indicador de zoom na status bar
    QMenu*          m_fileMenu{nullptr};     // menu "Arquivo" (estendido com DXF/PDF)
    QString         m_currentLayerName{"0"};
    QWidget*        m_sideDock{nullptr};       // janela-ferramenta Propriedades/Camadas (pop-up)
    QTabWidget*     m_sideTabs{nullptr};
    QComboBox*      m_ribbonLayerCombo{nullptr};   // lista suspensa de camadas na ribbon
    // Ribbon de formatação de TEXTO (padrão dos novos + aplica ao selecionado).
    QFontComboBox*  m_txtRibFont{nullptr};
    QDoubleSpinBox* m_txtRibSize{nullptr};
    QToolButton*    m_txtRibBold{nullptr};
    QToolButton*    m_txtRibItalic{nullptr};
    bool            m_txtRibUpdating{false};
    QHash<int, QAction*> m_toolAct;       // ToolKind(int) -> botão canônico do ribbon
    QHash<QAction*, int> m_toolKindOf;    // botão -> ToolKind(int)
    StyleTable      m_styles;             // estilos nomeados de cota/texto (#7)
    LayoutTable     m_layouts;            // pranchas (Paper Space)
    PlotStyleTable  m_plotStyle;          // estilos de plotagem (CTB) do doc corrente
    QString         m_projectPath;        // caminho do projeto .zencad corrente ("" = sem título)
    bool            m_suppressStart{false}; // --shot: não mostra a tela inicial
    QStackedWidget* m_stack{nullptr};       // [0] StartPage · [1] área de desenho
    StartPage*      m_startPage{nullptr};
    QWidget*        m_drawingPage{nullptr};
    QTabBar*        m_layoutTabs{nullptr};// abas Modelo | Prancha… | +
    bool            m_layoutTabsBusy{false}; // evita reentrância ao refazer as abas
    int             m_unitIndex{0};       // unidade de desenho (índice no diálogo)
    int             m_unitDecimals{2};
    QString         m_unitSuffix;         // sufixo de exibição (ex.: " mm")
    double          m_hatchAngle{45.0};   // ângulo/escala de hachura lembrados (fluidez)
    double          m_hatchScale{1.0};
    ThemeMode       m_themeMode{ThemeMode::Dark};
    void            applyTheme(ThemeMode m);
};

} // namespace cad
