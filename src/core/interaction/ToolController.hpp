// src/core/interaction/ToolController.hpp
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "core/geometry/Properties.hpp"   // ColorRef/LineType/LineWeight (props correntes)
#include "core/geometry/Dimension.hpp"    // DimAnchor (associatividade de cota)
#include "core/math/Vec.hpp"
#include "core/math/AABB.hpp"
#include "core/math/Matrix4.hpp"
#include "core/edit/BooleanOps.hpp"   // BoolOp (booleanas de polígono)

namespace cad {

class DrawingManager;
class Entity;
class Polyline;
struct RenderBatch;

enum class ToolKind { None, Line, Circle, Rectangle, Arc3, Ellipse,
                      Move, Copy, Rotate, Scale, Mirror, Offset, Trim, Fillet, Chamfer, Extend,
                      Stretch, Block,
                      Text, DimLinear, DimAligned, DimRadius, DimDiameter, DimAngular,
                      DimContinue, DimBaseline, Hatch,
                      Polyline, Point, Spline,
                      Circle2P, Circle3P, ArcSCE, Polygon, RectChamfer, Divide, Measure,
                      ArcCSE, ArcSER, ArcSEA, ArcSED, RectFillet, CircleTTR, SplineCV,
                      EllipseArc, CircleTTT, XLine, Ray, BreakTool, JoinTool, Lengthen, Inquiry,
                      MLine, Leader, RevCloud, Dist, MatchProps, MLeaderTool, Align, ArrayPath,
                      Wipeout, TableTool, Area, Insert, AttDefTool, DimOrdinate, Door,
                      WallTool, WindowTool };

// Controlador de interação (coordenadas de MUNDO, sem Qt). Suporta os dois
// fluxos do AutoCAD para comandos de edição:
//   Noun-Verb: seleciona, depois aciona o comando (usa a seleção corrente);
//   Verb-Noun: aciona o comando sem seleção -> fase "Selecting" -> confirma
//              (Enter/botão-direito) -> ponto-base -> destino.
class ToolController {
public:
    // Fase corrente de uma ferramenta de edição.
    enum class EditPhase { Idle, Selecting, Base, Target };

    explicit ToolController(DrawingManager& doc) : m_doc(doc) {}

    void      setTool(ToolKind k);
    ToolKind  tool() const { return m_tool; }
    EditPhase phase() const { return m_phase; }
    bool      hasPending() const { return !m_pending.empty(); }
    std::size_t pendingCount() const { return m_pending.size(); }
    void      cancel();

    // True quando cliques devem SELECIONAR (modo None ou edição na fase Selecting).
    bool selectingObjects() const;
    // Confirma a seleção (Verb-Noun): Selecting -> Base.
    void confirmSelection();

    bool onPoint(const Point3& p);
    void buildPreview(const Point3& cursor, RenderBatch& out) const;

    // Copy múltiplo (AutoCAD): após o ponto-base, cada clique cola uma cópia;
    // o loop segue até Enter/Esc. True enquanto esse loop está ativo.
    bool editLoopActive() const {
        return m_tool == ToolKind::Copy && m_phase == EditPhase::Target && !m_pending.empty();
    }

    // Ferramentas multi-clique (Polilinha/Spline): conclui o traço acumulado
    // (Enter/botão-direito). `closed` fecha a polilinha. Retorna true se criou.
    bool finishStroke(bool closed = false);

    // Modo ARCO da Polilinha (segmentos viram arcos tangentes ao trecho anterior).
    void setPolyArc(bool on) { m_polyArc = on; }
    bool polyArc() const { return m_polyArc; }
    bool strokeInProgress() const {
        return (m_tool == ToolKind::Polyline || m_tool == ToolKind::Spline ||
                m_tool == ToolKind::SplineCV || m_tool == ToolKind::MLine ||
                m_tool == ToolKind::Leader || m_tool == ToolKind::MLeaderTool ||
                m_tool == ToolKind::Wipeout || m_tool == ToolKind::WallTool) &&
               m_pending.size() >= 2;
    }
    void setMleaderText(std::string t) { m_mleaderText = std::move(t); }
    // Multileader multi-chamada: há chamadas acumuladas aguardando o Enter de conclusão.
    bool mleaderPending() const { return m_tool == ToolKind::MLeaderTool && !m_mleaderLeaders.empty(); }
    void setOffsetDist(double d) { m_offsetDist = d; }   // 0 = distância pelo clique
    void setMlineWidth(double w) { m_mlineWidth = w; }
    void setPolyWidth(double w)  { m_polyWidth = w; }
    void setLeaderText(std::string t) { m_leaderText = std::move(t); }
    void setRevCloudRadius(double r)  { m_revCloudRadius = r; }

    // Cota de Raio/Diâmetro estilo AutoCAD: o 1º clique pega o CÍRCULO/ARCO (centro
    // e raio reais); o 2º clique só posiciona a cota. `dimNeedsCircle` diz se ainda
    // falta pegar o círculo; `dimCirclePick` define centro/raio a partir da entidade.
    bool dimNeedsCircle() const {
        return (m_tool == ToolKind::DimRadius || m_tool == ToolKind::DimDiameter) && !m_dimHasCircle;
    }
    void dimCirclePick(EntityId id);

    // Associatividade de cota: dica de âncora do PRÓXIMO ponto clicado. A UI
    // chama após resolver o OSNAP (entidade + tipo + ponto grudado); consumida
    // e derivada em Which no onPoint. snapType = SnapType (int p/ não incluir).
    void setNextPointAnchor(EntityId id, int snapType, const Point3& snapped);

    // Ferramentas que pedem um VALOR digitado após os pontos (raio/ângulo do arco).
    bool wantsValue() const {
        return ((m_tool == ToolKind::ArcSER || m_tool == ToolKind::ArcSEA) && m_pending.size() == 2)
            || ((m_tool == ToolKind::Circle || m_tool == ToolKind::Polygon) && m_pending.size() == 1)
            || (m_tool == ToolKind::Ellipse && m_pending.size() == 2);   // eixo menor digitado
    }
    bool onValue(double v);   // valor digitado: raio (Circle/Polygon), raio/graus do arco

    // Retângulo (e variantes) por DUAS medidas digitadas (largura, altura): após o
    // 1º canto, define o canto oposto = canto1 + (w, h).
    bool wantsDimensions() const {
        return (m_tool == ToolKind::Rectangle || m_tool == ToolKind::RectChamfer ||
                m_tool == ToolKind::RectFillet) && m_pending.size() == 1;
    }
    bool onDimensions(double w, double h) {
        if (!wantsDimensions()) return false;
        return onPoint(Point3{m_pending[0].x + w, m_pending[0].y + h, 0.0});
    }

    void setFilletRadius(double r) { m_filletRadius = r; }
    void setChamferDist(double d)  { m_chamferDist = d; }
    double filletRadius() const { return m_filletRadius; }
    double chamferDist()  const { return m_chamferDist; }
    double offsetDist()   const { return m_offsetDist; }

    // --- Seleção & edição ---
    void selectAt(const Point3& p, double tol, bool add);
    void selectInBox(const AABB& box, bool crossing, bool add);
    // QSELECT: seleciona TODAS as entidades que casam tipo e/ou camada
    // (string vazia = qualquer). Pula camadas off/congeladas/travadas.
    // Retorna o nº de entidades selecionadas.
    int  selectByFilter(const std::string& typeName, const std::string& layer);
    void clearSelection() { m_selection.clear(); }
    const std::vector<EntityId>& selection() const { return m_selection; }
    // Seleção AVANÇADA (estilo AutoCAD): polígono Window/Crossing, todas as
    // entidades visíveis, a seleção ANTERIOR (memorizada a cada seleção
    // não-vazia) e a ÚLTIMA entidade criada. selectIds = seleção direta.
    void selectInPolygon(const std::vector<Point3>& poly, bool crossing, bool add);
    int  selectAllVisible();
    int  selectPrevious();
    bool selectLastCreated();
    void selectIds(const std::vector<EntityId>& ids, bool add);
    bool eraseSelected();

    // Ponto de referência ativo (último ponto pendente) para o modo Ortho.
    bool referencePoint(Point3& out) const {
        if (m_pending.empty()) return false;
        out = m_pending.back();
        return true;
    }

    // Trim/Fillet/Extend: o viewport faz o pick e passa a entidade + ponto clicado.
    void trimClick(EntityId picked, const Point3& at);
    void filletClick(EntityId picked, const Point3& at);
    void chamferClick(EntityId picked, const Point3& at);
    void extendClick(EntityId picked, const Point3& at);
    void divideClick(EntityId picked);    // DIVIDE: pontos em N partes
    void measureClick(EntityId picked);   // MEASURE: pontos a cada espaçamento
private:
    void placeDivideMarks(const std::vector<struct DivMark>& marks);  // pontos OU blocos
    Polyline ucsRectFrom2(const Point3& a, const Point3& b) const;    // retângulo no UCS
public:
    void ttrClick(EntityId picked, const Point3& at);  // Circulo TTR: 2 entidades + raio
    void tttClick(EntityId picked, const Point3& at);  // Circulo TTT: 3 entidades (retas)
    void matchPropsClick(EntityId picked);  // Match Properties: fonte -> alvos
    void setEditCopy(bool on) { m_editCopy = on; }   // Rotate/Scale mantém o original
    void setEditRef(bool on)  { m_editRef = on; }    // Rotate/Scale por Referência (3 cliques)
    void breakClick(EntityId picked, const Point3& at);   // Break: linha + 2 pontos
    void joinClick(EntityId picked, const Point3& at);    // Join: 2 linhas colineares
    void lengthenClick(EntityId picked, const Point3& at);// Lengthen: linha (delta configurado)
    void setLengthenDelta(double d) { m_lengthenDelta = d; }
    void setTtrRadius(double r) { m_ttrRadius = r; }
    double ttrRadius() const { return m_ttrRadius; }

    // Parâmetros das novas ferramentas de criação/construção.
    void setPolygon(int sides, bool inscribed) { m_polygonSides = sides; m_polygonInscribed = inscribed; }
    void setDivideCount(int n)      { m_divideN = n; }
    void setMeasureSpacing(double s) { m_measureSpacing = s; }
    // DIVIDE/MEASURE marcando com BLOCO da biblioteca ("" = pontos, padrão);
    // align = rotaciona cada inserção pela tangente local da entidade.
    void setDivideBlock(std::string name, bool align) {
        m_divideBlockName = std::move(name); m_divideAlign = align;
    }

    // --- PAREDE (arquitetura) ----------------------------------------------
    void   setWallThickness(double t) { if (t > 1e-9) m_wallThickness = t; }
    double wallThickness() const { return m_wallThickness; }
    // PORTA: reconhece a PAREDE sob o clique (vira VÃO com folha embutida);
    // fora de parede cai no fluxo antigo (linha + arco soltos, 3 pontos).
    void doorClick(EntityId picked, const Point3& p);
    // JANELA: 2 cliques SOBRE a mesma parede definem o vão de 3 linhas.
    void windowClick(EntityId picked, const Point3& p);
    int  doorStage() const { return m_doorStage; }      // 0..2 (prompts)
    int  windowStage() const { return m_winHas ? 1 : 0; }

    // Ações sobre a seleção corrente.
    bool explodeSelected();
    // JOIN em seleção: encadeia LINHAS conectadas pelos extremos numa única
    // polilinha (fechada se formar loop) — estilo AutoCAD moderno. Retorna
    // false se a seleção não formar UMA corrente conectada de linhas.
    bool joinSelected();
    // OVERKILL: remove duplicatas exatas + une linhas colineares sobrepostas
    // (na seleção; seleção vazia = documento todo). Devolve o que limpou.
    bool overkillRun(double tol, int& duplicates, int& merged);
    // REVERSE: inverte o sentido (Line/Polyline/Spline). Retorna quantas.
    int  reverseSelected();
    // BLEND: spline tangente entre os extremos livres de 2 entidades abertas.
    bool blendSelected();
    bool arrayRectangular(int rows, int cols, double dx, double dy);
    bool arrayPolar(int count, double totalAngleRad);
    bool booleanSelected(BoolOp op);                       // ∪/∩/− de 2 polígonos selecionados
    bool regionFromSelection();                            // converte polígonos selecionados em REGION
    void setTableParams(int rows, int cols, double cw, double rh) {
        m_tableRows = rows; m_tableCols = cols; m_tableCW = cw; m_tableRH = rh; }
    void beginArrayPath(int count, bool align);           // captura a seleção como fonte
    bool arrayPathClick(EntityId pathId);                 // usa a entidade clicada como caminho

    // Blocos: nome pendente para criar (BLOCK) e parâmetros de inserção (INSERT).
    void setPendingBlockName(std::string n) { m_pendingBlockName = std::move(n); }
    void setPendingInsert(std::string name, double scale, double rotDeg) {
        m_insertName = std::move(name); m_insertScale = scale; m_insertRot = rotDeg;
        m_insertValues.clear();
    }
    // Valores de atributo respondidos no diálogo do INSERT (tag -> valor).
    void setPendingInsertValues(std::vector<std::pair<std::string, std::string>> v) {
        m_insertValues = std::move(v);
    }
    const std::string& insertName() const { return m_insertName; }

    // ATTDEF: campos pendentes da próxima definição de atributo (1 clique posiciona).
    void setPendingAttDef(std::string tag, std::string prompt, std::string defValue) {
        m_attdefTag = std::move(tag); m_attdefPrompt = std::move(prompt);
        m_attdefDefault = std::move(defValue);
    }

    // Anotação: Texto (o viewport pede a string) e Hachura (pick de polilinha).
    void addText(const Point3& pos, const std::string& text, double boxWidth = 0.0);
    void hatchPick(EntityId picked);
    void hatchAtPoint(const Point3& inside);   // hachura por ponto interno (traça o contorno)
private:
    void emitHatchFrom(std::vector<std::vector<Point3>> loops,   // cria + gradiente +
                       std::vector<EntityId> srcIds);            // âncoras (associativa)
public:
    // Padrão de hachura (0=Linhas,1=ANSI31,2=ANSI37,3=Grade), ângulo (graus), escala.
    void setHatch(int pattern, double angleDeg, double scale) {
        m_hatchPattern = pattern; m_hatchAngle = angleDeg; m_hatchScale = scale;
    }
    void setHatchGradient(int r1, int g1, int b1, int r2, int g2, int b2) {
        m_grad1[0] = r1; m_grad1[1] = g1; m_grad1[2] = b1;
        m_grad2[0] = r2; m_grad2[1] = g2; m_grad2[2] = b2;
    }
    double annotationHeight() const { return m_annotHeight; }
    const std::string& annotationFont() const { return m_annotFont; }
    void   setAnnotationHeight(double h) { m_annotHeight = h; }
    void   setAnnotationFont(std::string f) { m_annotFont = std::move(f); }  // TTF ("" = traços)
    // ANOTATIVO (estilo corrente): alturas em mm de papel -> convertidas pela
    // escala de anotação ao criar (emitNew) e marcadas na entidade.
    void   setTextAnnotative(bool a) { m_textAnnotative = a; }
    void   setDimAnnotative(bool a)  { m_dimAnnotative = a; }
    void   setAnnotationBold(bool b)   { m_annotBold = b; }
    void   setAnnotationItalic(bool i) { m_annotItalic = i; }
    bool   annotationBold()   const { return m_annotBold; }
    bool   annotationItalic() const { return m_annotItalic; }
    // Estilo de cota aplicado às novas cotas (casas decimais, sufixo, tamanho de
    // seta; arrow < 0 = automático = altura do texto).
    void   setDimStyle(int decimals, std::string suffix, double arrow, int arrowType = 0) {
        m_dimDecimals = decimals; m_dimSuffix = std::move(suffix); m_dimArrow = arrow;
        m_dimArrowType = arrowType;
    }
    // Tolerância dimensional (± quando iguais; +a/-b quando diferentes; 0/0 = off).
    void   setDimTolerance(double plus, double minus) {
        m_dimTolPlus = plus; m_dimTolMinus = minus;
    }
    // Próxima cota de raio sai JOGGED (zigue-zague no leader) — one-shot.
    void   setDimJogged(bool j) { m_dimJogged = j; }

    // QDIM: cadeia de cotas contínuas pelos ENDPOINTS da seleção corrente.
    // `linePos` = clique que define a posição da linha de cota E o eixo
    // (fora do bbox em Y = cadeia horizontal; fora em X = vertical).
    // Devolve o nº de cotas criadas.
    int qdim(const Point3& linePos);

    // Propriedades CORRENTES aplicadas às novas entidades (padrão ByLayer).
    void setCurrentColor(ColorRef c)        { m_curColor = c; }
    void setCurrentLineType(std::string n)  { m_curLineType = std::move(n); }
    void setCurrentLineWeight(double mm)    { m_curLineWeight = mm; }

    // Camada corrente (aplicada às entidades criadas).
    void setCurrentLayer(const std::string& name) { m_currentLayer = name; }
    const std::string& currentLayer() const { return m_currentLayer; }

private:
    void    commitEdit(const Point3& base, const Point3& target);
    Matrix4 previewMatrix(const Point3& base, const Point3& cursor) const;
    void    emitNew(std::unique_ptr<Entity> e);   // cria na camada corrente

    DrawingManager&       m_doc;
    ToolKind              m_tool{ToolKind::None};
    EditPhase             m_phase{EditPhase::Idle};
    std::vector<Point3>   m_pending;
    std::vector<double>   m_polyBulges;   // bulge por trecho da polilinha em curso
    bool                  m_polyArc{false};
    std::vector<EntityId> m_selection;
    AABB                  m_lastBox;   // última caixa de seleção (p/ Stretch)
    EntityId              m_trimCutId{kInvalidId};
    EntityId              m_filletFirstId{kInvalidId};
    Point3                m_filletFirstPick{};
    EntityId              m_chamferFirstId{kInvalidId};
    EntityId              m_extendBoundId{kInvalidId};
    double                m_filletRadius{10.0};
    double                m_chamferDist{5.0};
    EntityId              m_ttrFirstId{kInvalidId};
    double                m_ttrRadius{20.0};
    std::vector<EntityId> m_tttIds;   // entidades acumuladas p/ Circulo TTT
    EntityId              m_breakId{kInvalidId};
    Point3                m_breakP1{};
    bool                  m_breakHasP1{false};
    EntityId              m_joinFirstId{kInvalidId};
    double                m_lengthenDelta{10.0};
    int                   m_polygonSides{6};
    bool                  m_polygonInscribed{true};
    std::vector<EntityId> m_arrayPathSrc;          // fontes capturadas p/ Array Path
    int                   m_arrayPathCount{5};
    bool                  m_arrayPathAlign{false};
    int                   m_tableRows{3}, m_tableCols{3};
    double                m_tableCW{40.0}, m_tableRH{15.0};
    int                   m_divideN{4};
    double                m_measureSpacing{10.0};
    std::string           m_divideBlockName;      // "" = marca com PointEntity
    bool                  m_divideAlign{false};   // bloco girado pela tangente
    double                m_wallThickness{0.15};  // espessura corrente da PAREDE
    int                   m_doorStage{0};         // porta-na-parede: 0..2
    EntityId              m_doorWallId{kInvalidId};
    double                m_doorS0{0.0}, m_doorS1{0.0};
    bool                  m_winHas{false};        // janela: 1º clique dado
    EntityId              m_winWallId{kInvalidId};
    double                m_winS0{0.0};
    double                m_annotHeight{5.0};
    std::string           m_annotFont{};       // fonte TTF corrente do estilo de texto
    bool                  m_textAnnotative{false};   // estilo de texto anotativo
    bool                  m_dimAnnotative{false};    // estilo de cota anotativo
    bool                  m_annotBold{false};
    bool                  m_annotItalic{false};
    int                   m_hatchPattern{0};
    double                m_hatchAngle{45.0};
    double                m_hatchScale{1.0};
    int                   m_grad1[3]{0, 0, 0};         // cores do gradiente (preto->branco)
    int                   m_grad2[3]{255, 255, 255};
    Point3                m_lastDimP1{}, m_lastDimP2{}, m_lastDimP3{};  // última cota linear
    bool                  m_hasLastDim{false};
    int                   m_dimDecimals{2};
    std::string           m_dimSuffix{};
    double                m_dimArrow{-1.0};   // < 0 = automático
    int                   m_dimArrowType{0};  // 0=seta, 1=tique, 2=ponto
    double                m_dimTolPlus{0.0}, m_dimTolMinus{0.0};   // tolerância ±
    bool                  m_dimJogged{false};   // próxima cota de raio = jogged
    double                m_mlineWidth{5.0};
    double                m_polyWidth{0.0};
    std::vector<EntityId> m_prevSelection;     // última seleção NÃO-vazia (Previous)
    Point3                m_cyclePoint{};      // último ponto de seleção (ciclagem)
    bool                  m_hasCycle{false};
    std::size_t           m_cycleIndex{0};
    std::string           m_leaderText{};
    std::string           m_mleaderText{};
    std::vector<std::vector<Point3>> m_mleaderLeaders{};   // chamadas acumuladas (multi)
    double                m_revCloudRadius{5.0};
    Point3                m_dimCenter{};        // centro do círculo/arco cotado (Raio/Diâmetro)
    double                m_dimRadius{0.0};      // raio VERDADEIRO do círculo/arco cotado
    bool                  m_dimHasCircle{false};
    EntityId              m_dimSrcId{kInvalidId};  // círculo/arco cotado (associatividade)
    DimAnchor             m_nextAnchor{};          // dica de âncora pendente (1 clique)
    DimAnchor             m_dimAnch[2]{};          // âncoras dos 2 pontos medidos
    double                m_offsetDist{0.0};   // distância fixa do offset (0 = pelo clique)
    bool                  m_editCopy{false};        // Rotate/Scale: copiar em vez de mover
    bool                  m_editRef{false};         // Rotate/Scale: por referência (3 cliques)
    EntityId              m_matchSrcId{kInvalidId}; // fonte do Match Properties
    std::string           m_pendingBlockName{};     // nome ao criar bloco (BLOCK)
    std::string           m_insertName{};           // bloco a inserir (INSERT)
    double                m_insertScale{1.0};
    double                m_insertRot{0.0};         // rotação de inserção (graus)
    std::vector<std::pair<std::string, std::string>> m_insertValues;  // atributos do INSERT
    std::string           m_attdefTag{"TAG"};       // próxima ATTDEF (tag/prompt/padrão)
    std::string           m_attdefPrompt{};
    std::string           m_attdefDefault{};
    std::string           m_currentLayer{"0"};
    ColorRef              m_curColor{};               // ByLayer por padrão
    std::string           m_curLineType{"ByLayer"};
    double                m_curLineWeight{-1.0};      // -1 = ByLayer
};

} // namespace cad
