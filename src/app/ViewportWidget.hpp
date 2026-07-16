// src/app/ViewportWidget.hpp
#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QPoint>
#include <QVector4D>
#include <QString>
#include <vector>
#include <unordered_map>

#include "app/Camera.hpp"
#include "app/Theme.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/interaction/ToolController.hpp"
#include "core/snap/SnapEngine.hpp"

class QLabel;
class QListWidget;
class QPlainTextEdit;

namespace cad {

class DrawingManager;
struct Layout;   // Paper Space: definido em core/layout/Layout.hpp

// Viewport OpenGL 2D: renderiza a geometria do documento como linhas, com pan
// (botão do meio ou arrastar no modo Selecionar) e zoom (roda). Desenho
// interativo via ToolController: clique = ponto, preview em tempo real.
class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit ViewportWidget(DrawingManager* doc, QWidget* parent = nullptr);
    ~ViewportWidget() override;

    void     rebuild();              // re-emite a geometria e re-sobe à GPU
    void     fitView();              // reenquadra a câmera em toda a geometria
    void     resetView() {           // enquadramento PADRÃO (documento vazio)
        m_cam.setViewport(width(), height());
        m_cam.fit(-60.0, -40.0, 60.0, 40.0);
        m_baseScale = m_cam.scale();
        m_fitted = true;
        emitZoomPercent();
        update();
    }

    // Paper Space (Pranchas): alterna entre o Modelo e a vista de uma prancha.
    // Em modo papel o canvas desenha a folha (mm 1:1), moldura, selo e os
    // viewports (o Modelo clipado, mostrado à escala de cada viewport).
    // O ponteiro é NÃO-const: seleção/grips movem e redimensionam viewports, e
    // o MSPACE ajusta a vista (modelCx/Cy/mmPerUnit) do viewport ativo.
    void     setPaperMode(bool on, Layout* layout);
    bool     paperMode() const { return m_paperMode; }
    void     refreshPaper() { if (m_paperMode) { fitPaper(); update(); } }  // após editar a prancha
    void     beginPaperViewport();   // arma a criação de viewport (2 cliques na prancha)
    void     cancelPaperViewport();  // cancela a criação em curso

    // Operações de VISTA estilo AutoCAD (não são ferramentas de desenho).
    enum class ViewOp { None, ZoomWindow, Pan, ZoomRealtime };
    void     zoomExtents();          // = Zoom Tudo (reenquadra)
    void     zoomIn();               // amplia no centro
    void     zoomOut();              // reduz no centro
    void     zoomPrevious();         // restaura a vista anterior do histórico
    void     beginZoomWindow();      // próximo arrasto define a janela de zoom (one-shot)
    void     beginPan();             // modo mão: arrastar desloca a vista (Esc sai)
    void     beginZoomRealtime();    // modo zoom tempo-real: arrastar ↑/↓ (Esc sai)
    void     endViewOp();            // encerra a operação de vista corrente
    ViewOp   viewOp() const { return m_viewOp; }
    void     cancelAndDeselect();    // Esc "global": cancela op, desmarca e volta o foco ao canvas
    void     setTool(ToolKind k);
    ToolKind tool() const { return m_tools.tool(); }
    EntityId selectedId() const;     // id da única entidade selecionada, ou kInvalidId
    std::vector<EntityId> selectedIds() const;   // todas as entidades selecionadas
    void     eraseSelected();        // apaga a seleção atual
    void     explodeSelected();      // explode polilinhas selecionadas
    bool     joinSelected() {        // JOIN em seleção: linhas conectadas -> polilinha
        const bool ok = m_tools.joinSelected();
        if (ok) rebuild();
        return ok;
    }
    bool overkillSelected(double tol, int& dups, int& merged) {   // OVERKILL
        const bool ok = m_tools.overkillRun(tol, dups, merged);
        if (ok) rebuild();
        return ok;
    }
    int reverseSelected() {          // REVERSE: inverte o sentido da seleção
        const int n = m_tools.reverseSelected();
        if (n > 0) rebuild();
        return n;
    }
    bool blendSelected() {           // BLEND: spline tangente entre 2 abertas
        const bool ok = m_tools.blendSelected();
        if (ok) rebuild();
        return ok;
    }
    void     arrayRectangular(int rows, int cols, double dx, double dy);
    void     arrayPolar(int count, double totalAngleDeg);
    bool     booleanSelected(BoolOp op) { return m_tools.booleanSelected(op); }   // ∪/∩/− de 2 polígonos
    void     beginArrayPath(int count, bool align) { m_tools.beginArrayPath(count, align); }
    bool     regionFromSelection() { return m_tools.regionFromSelection(); }       // polígonos -> REGION
    void     setTableParams(int r, int c, double cw, double rh) { m_tools.setTableParams(r, c, cw, rh); }
    void     setGridOn(bool on);
    void     setThemeMode(ThemeMode m) { m_theme = m; m_canvas = canvasColors(m); rebuild(); update(); }
    void     setSnapEnabled(bool on);
    void     setSnapMask(unsigned mask) { m_snapMask = mask; update(); }   // 0 = nenhum tipo
    unsigned snapMask() const { return m_snapMask; }
    void     setOrthoOn(bool on);
    void     setOtrackEnabled(bool on);
    void     setGridSnapOn(bool on) { m_gridSnapOn = on; update(); }   // gruda na grade (F9)
    void     setPolarOn(bool on);                 // rastreamento polar (exclui Ortho)
    // INCREMENTOS do polar (multi: ex. {45, 90} = múltiplos de 45 E de 90).
    // Incremento único 0 = SEM múltiplos (usa apenas os ângulos adicionais).
    void     setPolarIncrement(double deg) {
        m_polarIncs.clear();
        if (deg > 0.0) m_polarIncs.push_back(deg);
    }
    void     setPolarIncrements(std::vector<double> degs) { m_polarIncs = std::move(degs); }
    double   polarIncrement() const { return m_polarIncs.empty() ? 0.0 : m_polarIncs.front(); }
    // Ângulos ADICIONAIS do polar (absolutos, ex.: 22.5, 67) além dos múltiplos.
    void     setPolarExtraAngles(std::vector<double> degs) { m_polarExtraDeg = std::move(degs); }
    const std::vector<double>& polarExtraAngles() const { return m_polarExtraDeg; }
    bool     polarOn() const { return m_polarOn; }
    void     inputPoint(const Point3& p);   // ponto digitado na linha de comando
    void     inputDistance(double dist);    // tamanho ao longo da direção do cursor
    void     inputDimensions(double w, double h);   // retângulo por largura x altura digitadas
    bool     toolWantsDimensions() const { return m_tools.wantsDimensions(); }
    void     setCurrentLayer(const std::string& name);
    void     setCurrentColorByLayer()        { m_tools.setCurrentColor(ColorRef::byLayer()); }
    void     setCurrentColorRgb(int r, int g, int b) {
        m_tools.setCurrentColor(ColorRef::explicitColor(Rgba{std::uint8_t(r), std::uint8_t(g), std::uint8_t(b), 255})); }
    void     setCurrentLineTypeName(const std::string& n) { m_tools.setCurrentLineType(n); }
    void     setCurrentLineWeightMm(double mm)            { m_tools.setCurrentLineWeight(mm); }
    void     setPolygon(int sides, bool inscribed) { m_tools.setPolygon(sides, inscribed); }
    void     setDivideCount(int n)        { m_tools.setDivideCount(n); }
    void     setMeasureSpacing(double s)  { m_tools.setMeasureSpacing(s); }
    void     setDivideBlock(const std::string& n, bool align) { m_tools.setDivideBlock(n, align); }
    void     setWallThickness(double t) { m_tools.setWallThickness(t); }
    double   wallThickness() const { return m_tools.wallThickness(); }
    void     setFilletRadius(double r)    { m_tools.setFilletRadius(r); }
    void     setChamferDist(double d)     { m_tools.setChamferDist(d); }
    double   filletRadius() const { return m_tools.filletRadius(); }
    double   chamferDist()  const { return m_tools.chamferDist(); }
    double   offsetDist()   const { return m_tools.offsetDist(); }
    void     setTtrRadius(double r)       { m_tools.setTtrRadius(r); }
    double   ttrRadius() const { return m_tools.ttrRadius(); }
    void     setHatch(int pattern, double angleDeg, double scale) { m_tools.setHatch(pattern, angleDeg, scale); }
    void     setHatchGradient(int r1, int g1, int b1, int r2, int g2, int b2) { m_tools.setHatchGradient(r1, g1, b1, r2, g2, b2); }
    void     setLengthenDelta(double d)   { m_tools.setLengthenDelta(d); }
    void     setDimStyle(int dec, const std::string& suffix, double arrow, int arrowType = 0) { m_tools.setDimStyle(dec, suffix, arrow, arrowType); }
    void     setAnnotationHeight(double h) { m_tools.setAnnotationHeight(h); }
    void     setDimTolerance(double p, double m2) { m_tools.setDimTolerance(p, m2); }
    void     setDimJogged(bool j) { m_tools.setDimJogged(j); }
    // QDIM: próximo clique posiciona a linha da CADEIA de cotas da seleção.
    void beginQdim() {
        if (m_tools.selection().empty()) {
            emit prompt(QStringLiteral("QDIM: selecione as entidades primeiro."));
            return;
        }
        m_qdimActive = true;
        emit prompt(QStringLiteral("QDIM: clique onde fica a linha de cotas "
                                   "(fora do desenho = define o eixo)."));
    }
    void     setAnnotationFont(const std::string& f) { m_tools.setAnnotationFont(f); }
    void     setTextAnnotative(bool a) { m_tools.setTextAnnotative(a); }
    void     setDimAnnotative(bool a)  { m_tools.setDimAnnotative(a); }
    void     setAnnotationBold(bool b)   { m_tools.setAnnotationBold(b); }
    void     setAnnotationItalic(bool i) { m_tools.setAnnotationItalic(i); }
    void     setMlineWidth(double w)      { m_tools.setMlineWidth(w); }
    void     setPolyWidth(double w)       { m_tools.setPolyWidth(w); }
    void     setLeaderText(const std::string& t) { m_tools.setLeaderText(t); }
    void     setMleaderText(const std::string& t) { m_tools.setMleaderText(t); }
    void     setOffsetDist(double d)      { m_tools.setOffsetDist(d); }
    void     setRevCloudRadius(double r)  { m_tools.setRevCloudRadius(r); }
    // LTSCALE: escala global dos tipos de linha (tracejados).
    void     setLineTypeScale(double s)   { m_ltScale = (s > 0.0 ? s : 1.0); rebuild(); update(); }
    double   lineTypeScale() const        { return m_ltScale; }
    void     setEditCopy(bool on)         { m_tools.setEditCopy(on); }
    void     setEditRef(bool on)          { m_tools.setEditRef(on); }
    int      selectByFilter(const std::string& type, const std::string& layer) {
        const int n = m_tools.selectByFilter(type, layer);
        m_selCacheValid = false; updateGrips(); update();
        emit selectionChanged();
        return n;
    }
    void     setPendingBlockName(const std::string& n) { m_tools.setPendingBlockName(n); }
    void     setPendingInsert(const std::string& name, double scale, double rotDeg) {
        m_tools.setPendingInsert(name, scale, rotDeg); }
    void     setPendingInsertValues(std::vector<std::pair<std::string, std::string>> v) {
        m_tools.setPendingInsertValues(std::move(v)); }
    void     setPendingAttDef(const std::string& tag, const std::string& prompt,
                              const std::string& defValue) {
        m_tools.setPendingAttDef(tag, prompt, defValue); }
    Point3   lastPoint() const { return m_lastPoint; }
    void     testDraw();             // verificação: desenha via ToolController e deixa um preview

signals:
    void prompt(const QString& msg);        // mensagem de fase para a linha de comando
    void cursorMoved(double x, double y);   // coordenadas de mundo (para a status bar)
    void commandEntered(const QString& text); // texto digitado no canvas (entrada dinâmica)
    void selectionChanged();                  // a seleção mudou (atualiza Propriedades)
    void editTextRequested();                 // 2x num MTEXT -> abrir edição de conteúdo
    void editBlockAttrsRequested();           // 2x num INSERT com atributos -> ATTEDIT
    void paperViewportDrawn(double x, double y, double w, double h);  // retângulo do viewport (mm de papel)
    void paperViewportScaleRequested(int index);  // menu de contexto: "Escala do viewport..."
    void paperViewportLayersRequested(int index); // "Congelar camadas neste viewport..."
    void zoomChanged(int percent);            // zoom relativo ao enquadramento inicial
    void toolChanged(int toolKind);           // a ferramenta ativa mudou (Esc/comando) -> sync ribbon

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;   // 2x roda = Zoom Tudo
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    void uploadFromDoc();
    void paintPaper();               // desenha a prancha corrente (modo papel)
    void fitPaper();                 // enquadra a folha da prancha no canvas
    void uploadDynamic(const std::vector<float>& verts);   // sobe p/ o VBO dinâmico reusando alocação
    void drawDynamic(const std::vector<float>& verts, const QVector4D& color);  // GL_LINES
    void drawFilled(const std::vector<float>& verts, const QVector4D& color);   // GL_TRIANGLES + blend
    void emitPrompt();               // emite o texto da fase corrente
    void applyOrtho(double& wx, double& wy) const;  // restringe a H/V se Ortho ligado
    bool applyPolar(double& wx, double& wy);         // gruda em múltiplos do incremento (Polar)
    bool applyGridSnap(double& wx, double& wy) const; // gruda na grade incremental (F9)
    void applyFence();                                // aplica Trim/Extend por cerca
    void acquireTrackPoint(const Point3& p);        // memoriza ponto de referência (OTRACK)
    bool applyTracking(double& wx, double& wy);      // alinha a um ref e gera guia tracejada
    void updateGrips();                              // recalcula os grips da seleção
    int  gripUnderCursor(double wx, double wy) const; // índice do grip sob o cursor, ou -1
    void updateAutocomplete();                       // filtra/posiciona a lista de comandos
    double snapTolWorld() const;
    void pushViewHistory();                          // memoriza a vista atual (Zoom Anterior)
    void emitZoomPercent();                          // emite zoomChanged a partir da escala

    DrawingManager* m_doc{nullptr};
    Camera2D        m_cam;
    ToolController  m_tools;
    SnapEngine      m_snap;
    SnapResult      m_snapResult;

    QOpenGLShaderProgram     m_prog;
    QOpenGLBuffer            m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer            m_previewVbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_previewVao;

    // Faixa de mesma cor/alpha/espessura/CAMADA (camada p/ VP-freeze;
    // annot = entidade ANOTATIVA, redimensionada por viewport no papel).
    struct ColorRun { float r, g, b; float a{1.0f}; int start; int count;
                      float width{1.0f}; int layerIdx{-1}; bool annot{false}; };
    struct FillRun { QVector4D color; std::vector<float> verts; bool annot{false}; };
    std::vector<float>    m_cpuVerts;       // (vértice - origem) em float, xy
    std::vector<ColorRun> m_colorRuns;      // grupos por cor (ByLayer)
    std::vector<std::string> m_runLayers;   // nomes de camada indexados pelos runs
    std::vector<FillRun>  m_fills;          // áreas preenchidas por cor
    std::vector<std::vector<float>> m_maskFills;                     // máscaras Wipeout (cor de fundo, pós-linhas)

    // --- Caches de regen/frame (fluidez) -----------------------------------
    std::unordered_map<EntityId, RenderBatch> m_emitCache;  // tessela por entidade (dirty-tracking)
    double  m_ltScale{1.0};                  // LTSCALE global (tracejados)
    int     m_dynVboCapacity{0};             // capacidade corrente do VBO dinâmico (bytes)
    std::vector<EntityId> m_selCacheIds;     // seleção cacheada (destaque)
    std::vector<float>    m_selCacheLines;   // linhas do destaque (rebase na origem)
    std::vector<float>    m_selCacheFills;   // fills do destaque
    bool    m_selCacheValid{false};
    std::vector<std::pair<QVector4D, std::vector<float>>> m_thickCache;  // linhas grossas em triângulos
    double  m_thickCacheScale{-1.0};         // escala da câmera usada no cache (-1 = inválido)
    int     m_vertexCount{0};
    double  m_originX{0.0}, m_originY{0.0};  // origem de rebase
    double  m_bbMinX{0.0}, m_bbMinY{0.0}, m_bbMaxX{0.0}, m_bbMaxY{0.0};
    bool    m_haveData{false};
    bool    m_glReady{false};
    bool    m_fitted{false};
    double  m_baseScale{0.0};   // escala no 1º enquadramento (referência p/ zoom %)
    ViewOp  m_viewOp{ViewOp::None};         // operação de vista corrente
    bool    m_viewDragging{false};          // arrastando durante uma operação de vista
    std::vector<Camera2D::State> m_viewHistory;   // pilha p/ Zoom Anterior
    bool    m_gridOn{true};
    ThemeMode    m_theme{ThemeMode::Dark};
    CanvasColors m_canvas{canvasColors(ThemeMode::Dark)};
    bool    m_snapEnabled{true};
    unsigned m_snapMask{kDefaultSnaps};   // OSNAP ativos por tipo (padrão curado)
    bool    m_orthoOn{false};
    bool    m_otrackEnabled{true};
    bool    m_polarOn{false};
    std::vector<double> m_polarIncs{15.0}; // incrementos ativos do polar (multi, graus)
    std::vector<double> m_polarExtraDeg;   // ângulos adicionais absolutos (POLAR custom)
    bool    m_gridSnapOn{false};   // grid snap (F9) — gruda na grade
    double  m_gridSpacing{0.0};    // passo corrente da grade (atualizado no paintGL)
    bool    m_mouseInside{false};
    QLabel* m_hud{nullptr};                   // rótulo de entrada dinâmica
    QListWidget* m_cmdList{nullptr};           // lista de autocomplete de comandos
    QString m_dynInput;                        // texto digitado no canvas (distância/coord/comando)
    QPoint  m_cursorScreen;                    // posição do cursor em pixels (p/ HUD/lista)
    std::vector<Point3> m_trackPts;           // pontos de referência adquiridos (OTRACK)
    std::vector<Point3> m_trackGuides;        // segmentos das guias de rastreamento
    bool    m_trackLocked{false};             // cursor alinhado a uma guia OTRACK (eixo único)
    Point3  m_trackLockRef{};                 // referência da guia ativa
    Point3  m_trackLockDir{};                 // direção unitária ao longo da guia (p/ valor digitado)
    std::vector<Point3> m_grips;              // alças da entidade única selecionada
    EntityId m_gripEntity{kInvalidId};        // dona dos grips
    int      m_gripDrag{-1};                  // índice do grip em arrasto (-1 = nenhum)
    ToolKind m_lastTool{ToolKind::None};      // último comando (p/ repetir com Enter/dir.)
    Point3  m_lastPoint{};                    // último ponto especificado (coords relativas)
    double  m_curX{0.0}, m_curY{0.0};        // cursor em coordenadas de mundo
    QPoint  m_lastMouse;

    // Caixa de seleção em arrasto (modo Selecionar).
    bool    m_boxActive{false};
    bool    m_boxAdd{false};
    QPoint  m_boxStartScreen;
    double  m_boxStartWX{0.0}, m_boxStartWY{0.0};
    double  m_boxCurWX{0.0}, m_boxCurWY{0.0};

    // Medição DIST (2 pontos).
    Point3  m_distP1{};
    bool    m_distHasP1{false};
    std::vector<Point3> m_areaPts;   // vértices acumulados da ferramenta AREA (por pontos)

    // Trim/Extend por FENCE (tecla F): pontos da linha-cerca a aplicar no Enter.
    bool    m_fenceMode{false};
    std::vector<Point3> m_fencePts;
    bool    m_peditInsert{false};   // PEDIT: aguardando clique p/ inserir vértice (tecla I)

    // Paper Space (modo papel).
    bool            m_paperMode{false};
    Layout*         m_paperLayout{nullptr};
    Camera2D::State m_modelCamSaved{};   // câmera do Modelo, restaurada ao voltar
    bool            m_modelCamValid{false};
    bool            m_paperVpCreate{false};   // criando viewport (2 cliques)
    Point3          m_paperVpP1{};            // 1º canto do viewport (mm de papel)
    bool            m_paperVpHasP1{false};
    // Seleção/grips de viewport na prancha + MSPACE (vista ativa).
    int             m_paperVpSel{-1};         // viewport selecionado (-1 = nenhum)
    int             m_paperVpGrip{-1};        // 0..3 = canto (BL,BR,TR,TL); 4 = mover
    bool            m_paperVpDragging{false};
    double          m_paperDragOffX{0.0}, m_paperDragOffY{0.0};  // clique -> canto (mover)
    int             m_mspaceIdx{-1};          // MSPACE: viewport ativo (-1 = papel)

public:
    // --- Seleção avançada (WP/CP/ALL/Previous/Last) -------------------------
    void beginSelectPolygon(bool crossing) {       // WP/CP: cliques + Enter
        m_selPolyActive = true;
        m_selPolyCrossing = crossing;
        m_selPolyPts.clear();
        m_tools.setTool(ToolKind::None);
        emit prompt(crossing
            ? QStringLiteral("CPolygon: clique os vértices (Enter fecha e seleciona o que TOCA).")
            : QStringLiteral("WPolygon: clique os vértices (Enter fecha e seleciona o que está DENTRO)."));
        update();
    }
    int  selectAllVisible()  { const int n = m_tools.selectAllVisible();  afterSelectionChange(); return n; }
    int  selectPrevious()    { const int n = m_tools.selectPrevious();    afterSelectionChange(); return n; }
    bool selectLastCreated() { const bool ok = m_tools.selectLastCreated(); afterSelectionChange(); return ok; }

    // M2P (mid between 2 points): o PRÓXIMO ponto da ferramenta ativa vira o
    // ponto MÉDIO de dois cliques (com OSNAP em cada um) — one-shot.
    // UCS por cliques: 1 = só a origem; 2 = origem + direção do eixo X.
    void beginUcsDefine(bool withAngle) {
        m_ucsMode = withAngle ? 2 : 1;
        emit prompt(withAngle ? QStringLiteral("UCS: clique a nova ORIGEM (depois a direção do eixo X).")
                              : QStringLiteral("UCS: clique a nova ORIGEM."));
        update();
    }
    void beginM2P() {
        m_m2pActive = true;
        m_m2pHasFirst = false;
        emit prompt(QStringLiteral("M2P: clique o 1º ponto de referência."));
    }
    void selectEntityIds(const std::vector<EntityId>& ids) {   // FIND etc.
        m_tools.selectIds(ids, false);
        afterSelectionChange();
    }

private:
    void afterSelectionChange();                   // grips + sinal + repaint

    bool m_selPolyActive{false};
    bool m_selPolyCrossing{false};
    std::vector<Point3> m_selPolyPts;
    bool   m_m2pActive{false};     // M2P: próximo ponto = médio de 2 cliques
    bool   m_m2pHasFirst{false};
    Point3 m_m2pFirst{};
    int    m_ucsMode{0};           // UCS por cliques: 0=off · 1=origem · 2/3=origem+direção
    Point3 m_ucsP1{};
    bool   m_qdimActive{false};    // QDIM: próximo clique = linha da cadeia

    // --- Caixa de texto IN-PLACE (MTEXT estilo caixa de texto) -------------
    // Ferramenta Texto: arrasta a LARGURA da caixa no canvas e digita DIRETO
    // num QPlainTextEdit sobreposto; duplo clique num MText reabre no lugar.
    void beginTextBoxEdit(const Point3& insert, double boxWidthWorld, EntityId editId);
    void commitTextEdit();
    void cancelTextEdit();
    bool eventFilter(QObject* obj, QEvent* ev) override;   // Esc/Ctrl+Enter/foco do editor

    QPlainTextEdit* m_textBox{nullptr};       // editor in-place (criado sob demanda)
    EntityId m_textEditId{kInvalidId};        // kInvalidId = criando texto novo
    Point3   m_textInsert{};                  // ponto de inserção do MText
    double   m_textBoxW{0.0};                 // largura da caixa em mundo (0 = livre)
    bool     m_textDragActive{false};         // arrastando a caixa da ferramenta Texto
    Point3   m_textDragP1{};
    bool     m_textCommitting{false};         // guarda contra reentrância do focusOut
};

} // namespace cad
