// src/zendo/Viewport3D.hpp
// O ESPAÇO do Zendo: viewport 3D em perspectiva com câmera orbital (estilo
// SketchUp). A cena é MALHA HALF-EDGE (uma por caixa de parede): render e
// picking derivam dela. F1: seleção de faces + EMPURRAR/PUXAR. F2: DESENHAR
// NA FACE (retângulo com inferência) + undo por snapshot. F3: LINHA que
// DIVIDE a face (splitEdge+splitFace nas arestas), PINTAR faces (cor por
// face no VBO) e o estudo vira DOCUMENTO (.zendo): as malhas saem/voltam por
// sopa de polígonos (o keyhole sobrevive ao round-trip), com câmera e cores.
#pragma once
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QImage>
#include <QJsonArray>
#include <QMatrix4x4>
#include <QPoint>
#include <array>
#include <functional>
#include <map>
#include <vector>

#include "core/mesh/HalfEdgeMesh.hpp"
#include <QSet>

#include <set>
#include <tuple>

#include "PlantImport.hpp"   // R3: PlantScene, o pacote neutro planta→3D
class QColor;

class Viewport3D : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT
public:
    explicit Viewport3D(QWidget* parent = nullptr);
    ~Viewport3D() override;

    // Documento 2D a exibir (não assume posse). Reconstrói a cena e recentra.
    // R3: o viewport recebe o pacote NEUTRO do conector (PlantImport) —
    // nenhuma entidade 2D atravessa esta porta.
    void setPlant(const PlantScene& plant, double wallHeight);
    void resetCamera();
    // QA/render por script: pose da câmera (graus; fator multiplica o auto-zoom).
    void setCameraPose(float yawDeg, float pitchDeg, float distFactor);
    // Estado completo da câmera (persistência do .zendo).
    void cameraState(float& yaw, float& pitch, float& dist, float tgt[3]) const;
    void setCameraState(float yaw, float pitch, float dist, const float tgt[3]);

    // SOL (S4): mês 1..12, hora 0..24, latitude em graus (sul = negativo)
    void setSun(bool on, int month, double hour, double latDeg);
    bool sunOn() const { return m_sunOn; }
    // R36: a vista corrente em MUNDO (órbita ou walk) + sol pro Fotógrafo
    void cameraWorld(cad::Point3& eye, cad::Point3& tgt) const;
    bool sunAngles(double& elevDeg, double& azimDeg) const;
    float fovY() const { return m_fov; }
    // R37: sol PURO por mês/hora/lat (mesma conta do computeSunDir, sem
    // tocar o estado) — o diálogo do Fotógrafo escolhe a hora livremente.
    static void sunAnglesFor(int month, double hour, double latDeg,
                             double& elevDeg, double& azimDeg);
    double sunHour() const { return m_sunHour; }
    int sunMonth() const { return m_sunMonth; }
    double sunLat() const { return m_sunLat; }
    bool walkOn() const { return m_walk; }   // R40: interior auto no Fotógrafo
    // TEXTURA (S4b): imagem na face selecionada (ou sólido inteiro)
    bool applyTexture(const QString& imagePath, double scaleM);
    void setStyle(int s);            // 0 normal · 1 mono · 2 raio-x
    // R32: SEÇÃO é entidade — plano (n̂, d) numa lista; até kMaxSections
    // ativas ao mesmo tempo (todas descartam juntas no shader).
    struct Section { cad::Vec3 n; double d; };
    static constexpr int kMaxSections = 4;
    void setClip(bool on, char axis, double pos);   // on=false LIMPA todas
    // R30/R32: seção com plano GERAL (nx,ny,nz,d): descarta dot(P,n) > d.
    // R32: cada chamada ADICIONA uma seção (até kMaxSections ativas juntas).
    void setClipPlane(const cad::Vec3& n, double d);
    void removeLastSection();                       // R32
    void clearSections();                           // R32
    bool clipOn() const { return !m_sections.empty(); }
    QVector4D clipPlane() const {                   // a ÚLTIMA (export/estado)
        if (m_sections.empty()) return {0, 1, 0, 1e9f};
        const Section& s = m_sections.back();
        return {float(s.n.x), float(s.n.y), float(s.n.z), float(s.d)};
    }
    QJsonArray sectionsJson() const;                // R32: persistência
    void setSectionsJson(const QJsonArray& arr);
    void armClipFace();              // arma: o próximo clique corta na face
    void armClipDrag();              // R34: arma DESLIZAR a última seção
    // R41: ESCADA paramétrica — arma 2 cliques no chão (pé + direção)
    void armStair(double width, double height, double run);
    void buildStair(const cad::Point3& origin, const cad::Vec3& dir,
                    double width, double height, double run);
    void qaStair(const QString& s);  // "ox,oy,dx,dy,w,h,run" direto (QA)
    // R43: GUARDA-CORPO — arma 2 cliques (início + fim, no chão ou laje)
    void armGuard(double height, double gap);
    void buildGuard(const cad::Point3& a, const cad::Point3& b,
                    double height, double gap);
    void qaGuard(const QString& s);  // "x1,y1,x2,y2,z,h,gap" direto (QA)
    // R43: LAJE COM ABERTURA — arma 4 cliques (laje A/B + abertura A/B)
    void armSlabHole(double zTop, double thick);
    void buildSlabHole(double x1, double y1, double x2, double y2,
                       double hx1, double hy1, double hx2, double hy2,
                       double zTop, double thick);
    void qaSlabHole(const QString& s);  // "x1,y1,x2,y2,hx1,hy1,hx2,hy2,zt,th"
    void slideLastSection(double delta);   // R34: d += delta (menu/QA)
    void qaClipFace(const QString& s);   // "nx,ny" pick da face → plano (QA)
    void qaClipPlane(const QString& s);  // "a,b,c,d" seta o plano direto (QA)
    void qaClipSlide(double d);          // R34: desliza a última seção (QA)
    void qaClipDrag(const QString& s);   // R34: "x0,y0,x1,y1" gesto REAL de
                                         // arrasto (arm→press→move→release)
    QJsonArray texturesJson(const QString& studyDir) const;
    void setTexturesJson(const QJsonArray& a, const QString& studyDir);
    QStringList missingTextures() const;                 // R38: aviso no load
    std::map<QString, QString> usedTextureFiles() const; // R38: pro pacote
    void retargetTexture(const QString& name, const QString& newFile);
    int exportObj(const QString& path) const;    // S5: passaporte pro Blender
    int exportGltf(const QString& path) const;   // S5 pleno: cores+texturas

    double wallHeight() const { return m_wallHeight; }
    void   setWallHeight(double h);      // REGENERA do 2D (descarta edições)

    std::size_t wallCount() const { return m_wallCount; }
    bool edited() const { return m_edited; }   // houve edição 3D no estudo?

    // --- documento de estudo (.zendo) ---------------------------------------
    QJsonArray studyMeshes() const;      // malhas -> sopa de polígonos + cores
    // Ponte (F4): acesso somente-leitura às malhas p/ projeção 2D.
    std::vector<const cad::HalfEdgeMesh*> meshPointers() const {
        std::vector<const cad::HalfEdgeMesh*> out;
        for (const MeshPart& p : m_meshes) out.push_back(&p.mesh);
        return out;
    }
    // Adota doc 2D (pode ser nullptr/vazio) + malhas vindas do arquivo.
    bool setStudy(const PlantScene& plant, const QJsonArray& meshes,
                  double wallHeightM);

    // --- ferramentas / edição de estudo -------------------------------------
    enum class Tool {
        Select, Rect, Line, Move, Circle, Pull, Erase, Tape, Paint,
        Arc, Rotate, Scale, Offset, Dim
    };
    void setTool(Tool t);
    Tool tool() const { return m_tool; }

    bool hasSelection() const { return m_selMesh >= 0; }
    bool pushPullSelected(double dist);          // extrude a face selecionada
    bool paintSelected(const QColor& c);         // "balde de tinta" da face
    bool undoLast();                             // Ctrl+Z (snapshot da cena)
    void deleteSelected();       // sólido da face OU dissolve a aresta selec.
    void startMoveCopy(bool copy);   // ferramenta Mover/Copiar (2 cliques)
    int  glueSelected();         // funde o sólido selecionado no que ele toca
    bool moveSelectedZ(double dz);   // sobe/desce o sólido (laje sobre paredes)
    bool rotateSelected(double deg); // rotação Z em torno do centro do bbox
    bool scaleSelected(double f);    // escala uniforme em torno do centro
    bool offsetSelectedFace(double d);   // anel interno na face (convexa)
    void markSaved() { m_edited = false; m_dirtyBase = false; }   // pós-save
    // R48: a cena carregada JÁ está suja e não existe em disco nenhum (é um
    // recuperado). Sem isto, o 1º Ctrl+Z zeraria m_edited (undoLast faz
    // m_edited = !m_undo.empty(), certo para arquivo do disco, MENTIRA para
    // recuperado) e o app fecharia sem perguntar, apagando a recuperação.
    void markEdited() { m_edited = true; m_dirtyBase = true; }

    // --- S2: ocultar/isolar + COMPONENTES ------------------------------------
    void hideSelected();
    void isolateSelected();
    void showAll();
    bool makeComponent(const QString& name);     // do sólido selecionado
    bool insertComponent(const QString& name, const cad::Point3& at);
    int  redefineComponent();    // selecionado vira a nova definição; propaga
    QStringList componentNames() const;
    QJsonArray compsJson() const;
    void setCompsJson(const QJsonArray& a);
    void qaInsertComp(const QString& name, double nx, double ny);
    void armInsert(const QString& n) { m_pendingComp = n; }
    // R14: componente de FÁBRICA (mobiliário) — entra em m_compDefs mas
    // NÃO é gravado no .zendo (o app o regenera a cada sessão)
    void addLibraryComponent(
        const QString& name, const cad::HalfEdgeMesh& mesh,
        const std::map<cad::HalfEdgeMesh::Idx, std::array<float, 3>>& cores);
    // R15: importa um .obj como componente DO USUÁRIO (persistido no .zendo);
    // devolve o nome registrado ("" = falhou). yUp converte Blender→Z-up.
    QString importObjComponent(const QString& path, double escala, bool yUp);
    // --- R17: a OFICINA (o kit Solid Inspector / CleanUp / Mirror) ---------
    QString inspectSolids();       // relatório de furos/integridade por sólido
    int fixSelectedSolid();        // tampa os loops de borda (nº de caps)
    QString cleanupModel();        // dissolve coplanares + purge de defs
    void mirrorSelected(int axis); // 0=X 1=Y 2=Z, pivô = centro do conjunto
    // R21: booleana real — subtrai um prisma (o menor dos 2 sólidos na
    // seleção múltipla) do outro, abrindo um túnel de lado a lado.
    QString subtractSelected();
    // R24: booleana real — une 2 sólidos (seleção múltipla) num só, restrito
    // a prismas retos de eixo PARALELO e MESMA extensão axial (a seção some
    // pro problema 2D já resolvido: unionSimple2D + extrusão).
    QString uniteSelected();
    // R18: funde um contorno novo do chão com travesseiros sobrepostos
    std::vector<cad::Point3> mergeGroundFootprint(
        std::vector<cad::Point3> pts);
    // R18: cria o sólido "travesseiro" (2 faces, 1 mm) a partir de um
    // contorno fechado (N vértices, plano horizontal); devolve o índice do
    // sólido novo e a face de topo em *topOut
    int buildGroundPillow(const std::vector<cad::Point3>& pts,
                          cad::HalfEdgeMesh::Idx* topOut);
    // Outliner (S2b): acesso por índice
    int partCount() const { return int(m_meshes.size()); }
    QString partLabel(int i) const;
    bool partHidden(int i) const {
        return i >= 0 && i < int(m_meshes.size()) &&
               m_meshes[std::size_t(i)].hidden;
    }
    void setPartHidden(int i, bool h);
    void selectPart(int i);
    // telhado sobre a seleção: cumeeira + BEIRAL; hip = QUATRO águas (45°)
    bool roofSelected(double ridgeH, double beiral = 0.0, bool hip = false);
    bool addTerrain(double thick, double margin);   // platô c/ TALUDE

    // QA sintético (coords normalizadas 0..1)
    void qaPick(double nx, double ny);
    void qaRect(double nx1, double ny1, double nx2, double ny2);
    void qaLine(double nx1, double ny1, double nx2, double ny2);
    void qaMove(double nx1, double ny1, double nx2, double ny2, bool copy);
    void qaCircle(double nx1, double ny1, double nx2, double ny2);
    void qaPencil(const QVector<double>& nxy);   // pares normalizados
    QJsonArray sketchJson() const;               // persistência do rascunho
    void setSketchJson(const QJsonArray& a);
    void qaDoublePick(double nx, double ny);     // seleciona o sólido inteiro
    void qaHover(const QString& seq);   // G1: "nx,ny;nx,ny…" — infere e acumula
    bool redoLast();                    // G2: refazer (Ctrl+Y)
    void qaErase(double nx, double ny, bool hide = false); // G2/R8: borracha
    void qaVertexMove(const QString& s);             // G2: "nx,ny,dx,dy,dz"
    void qaSketch3d(const QString& s);               // G2: "x,y,z;x,y,z;…"
    void qaSelBox(const QString& s);   // G3: "x1,y1,x2,y2[,del]" (norm.)
    // --- G4 ---
    void setPolySides(int n) { m_polySides = n; }   // 0 = círculo (24)
    void followMe();                   // varre o perfil pelo caminho do lápis
    void qaTape(const QString& s);     // "nx1,ny1,nx2,ny2" fita métrica
    QJsonArray guidesJson() const;
    void setGuidesJson(const QJsonArray& arr);
    // --- R26: cotas 3D persistentes ---
    // 3 cliques: ponto A, ponto B (o que se mede), ponto C (lado/posição da
    // linha de cota). Âncora por POSIÇÃO (não índice — não sobrevive a
    // booleana); re-snap a cada buildRenderArrays, órfã se o vértice sumiu.
    QJsonArray dimsJson() const;
    void setDimsJson(const QJsonArray& arr);
    void qaDim3d(const QString& s);   // "ax,ay,az,bx,by,bz,cx,cy,cz"
    void qaDimAng(const QString& s);  // R34: idem, mas cota ANGULAR (b=vértice)
    void qaDimClick(const QString& s);  // "nx,ny;nx,ny;…" cliques da cota (QA)
    // --- G5: grupos, contexto e tags ---
    void makeGroup(const QString& name);     // agrupa a multi-seleção
    void ungroupSelected();
    void assignTag(const QString& tag);      // tag na seleção/multi
    void setTagVisible(const QString& tag, bool vis);
    QStringList allTags() const;
    void exitContext();
    bool inEditContext() const { return !m_ctxGroup.isEmpty() || m_ctxMesh >= 0; }
    QString partGroup(int i) const;
    void qaCtxAt(double nx, double ny);      // entra no contexto sob o ponto
    // --- G6: câmera e atmosfera ---
    void setOrtho(bool on) { m_ortho = on; update(); }
    bool ortho() const { return m_ortho; }
    // --- R27: WALKTHROUGH (primeira pessoa) ---
    // Câmera no nível do olho (~1,6 m): WASD/setas andam, arrastar olha em
    // volta. Sai voltando pra órbita. Não toca geometria — só a câmera.
    void setWalkthrough(bool on);
    bool walkthrough() const { return m_walk; }
    void qaWalk(const QString& s);   // "ex,ey,ez,yaw,pitch" posiciona e mostra
    void qaWalkSim(const QString& s);   // "W,60" testa o integrador do walkStep
    // R28: arma "Posicionar câmera" — o próximo clique põe o olho (1ª pessoa)
    // 1,6 m acima do ponto clicado e entra no walkthrough.
    void armPositionCamera();
    void qaPosCam(const QString& s);   // "nx,ny" clique de posicionar (QA)
    void setFov(float deg) {
        m_fov = deg < 15.0f ? 15.0f : (deg > 90.0f ? 90.0f : deg);
        update();
    }
    void setFog(bool on, float density) {
        m_fogOn = on;
        m_fogDens = density;
        update();
    }
    void animateCameraTo(float yaw, float pitch, float dist,
                         const float tgt[3]);
    // --- R5: o Ateliê ---
    void setDay(bool on) { m_dayOn = on; update(); }
    bool day() const { return m_dayOn; }
    void paletteApplyColor(float r, float g, float b);   // pinta a seleção
    // --- R6: o BALDE — material ativo + clique pinta contínuo ---
    void setActiveColor(float r, float g, float b);      // arma cor + balde
    void setActiveTexture(const QString& path, double scale);
    void qaPaintAt(const QString& s);    // "nx,ny[,ctrl]" clique do balde
    void addLibraryTexture(const QString& name, const QString& path); // R9
    double textureScaleSelected() const;      // R13: -1 = sem textura
    void setTextureScaleSelected(double m);    // R13: reveste na escala
    // --- R7: as três que faltavam ---
    void qaArc(const QString& s);        // "ax,ay,bx,by,raio"
    void qaProtractor(const QString& s); // "cx,cy,rx,ry,ang[,copy]"
    void qaScaleTo(const QString& s);    // "fator[,x|y|z]" (na seleção)
    void zoomExtents();                  // R8: enquadra tudo, mantém ângulo
    void armFollowPerimeter();           // R8: próximo clique = face-caminho
    void qaFmPerim(const QString& s);    // R8: "px,py,cx,cy" perfil+caminho
    QStringList textureNames() const;
    QString texturePath(const QString& name) const;
    QImage textureImage(const QString& name) const;
    // --- R1/R2 ---
    void addScaleFigure();               // Ensō-san: a escala humana (1,75 m)
    void qaRectExact(const QString& s);  // "nx,ny,a,b" — gesto + medidas
    void qaPencilExact(const QString& s);   // R4: "nx,ny,len" traço exato
    void qaMoveLate(const QString& s, bool copy);   // R4: move pós-selbox
private:
    void liveVcb(const QString& s) {     // Measurements viva (digitar cala)
        if (m_vcbBuf.isEmpty()) emit vcbText(s);
    }
    bool finishGestureExact(const QString& typed);   // Enter NO MEIO do gesto
    bool m_pullSticky{false};            // pull em click-move-click
    double m_lastDu{0.0}, m_lastDv{0.0}; // sinais do rect corrente
    cad::Vec3 m_lastMoveDir{};           // direção corrente do mover/lápis
    bool m_exactClick{false};            // clique sintético sem snap
    Tool m_lastToolUsed{Tool::Select};   // R4: Enter repete a ferramenta
public:
    // VCB (S1): "4;3" refaz o último retângulo com 4×3; "2.8" refaz o pull.
    void vcbApply(const QString& typed);
    QString debugSelState() const;               // vértices da face selec.

signals:
    void pickInfo(const QString& text);          // "" = seleção limpa
    void vcbText(const QString& text);           // buffer de medidas digitadas
    void structureChanged();                     // outliner: relistar sólidos
    void toolChanged(int tool);                  // sincroniza a toolbar
    void entityInfo(const QString& text);        // R16: painel Info vivo
    void walkthroughChanged(bool on);            // R27: sincroniza o menu

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;   // R28: solta as teclas do walk

private:
    using Idx = cad::HalfEdgeMesh::Idx;
    struct MeshPart {
        cad::HalfEdgeMesh mesh;
        int wallNo{0};                           // rótulo "Parede N"
        std::map<Idx, std::array<float, 3>> faceColors;   // pintura por face
        bool hidden{false};                      // ocultar/isolar (S2)
        QString compName;                        // instância de componente
        std::map<Idx, QString> faceTex;          // textura por face (S4b)
        double texScale{1.0};                    // metros por ladrilho
        QString group;                           // G5: grupo nomeado
        QString tag;                             // G5: tag de visibilidade
        // R8: arestas ocultadas pela borracha+Shift (par de chaves 1e-5)
        std::set<std::pair<std::tuple<long long, long long, long long>,
                           std::tuple<long long, long long, long long>>>
            hiddenEdges;
    };

    void rebuildScene();                    // doc 2D -> malhas (paredes) + resto
    void finishScene();                     // chão/grade/bbox + arrays + reset
    void buildRenderArrays();               // malhas -> tris/arestas (CPU)
    void uploadScene();                     // CPU -> VBOs
    void uploadHighlights();                // faces hover/seleção -> VBOs
    void uploadGhost();                     // fantasma + marcador de snap
    void buildBoxMesh(const double l0[2], const double l1[2],
                      const double r0[2], const double r1[2],
                      double z0, double z1, int wallNo);

    bool pickAt(const QPoint& pos, int& meshOut, Idx& faceOut,
                cad::Point3* hitOut = nullptr) const;
    bool rayAt(const QPoint& pos, cad::Point3& orig, cad::Vec3& dir) const;

    // --- G1: MOTOR DE INFERÊNCIA (o sistema nervoso) ------------------------
    // kinds: 0 nada · 1 extremidade · 2 ponto médio · 3 na aresta · 4 na face
    //        5 no chão · 6 origem · 7/8/9 no eixo X/Y/Z · 10 extensão de aresta
    struct Infer {
        int kind{0};
        cad::Point3 p{};
        bool hasRef{false};
        cad::Point3 ref{};      // origem da linha pontilhada (alinhamento)
    };
    Infer inferAt(const QPoint& pos, const cad::Point3* base,
                  bool allowFace, bool allowGround) const;
    bool toScreen(const QMatrix4x4& mvp, const cad::Point3& p,
                  QPointF& out) const;
    void drawInferOverlay(class QPainter& qp);
    mutable Infer m_infer;                         // corrente (overlay)
    mutable std::vector<cad::Point3> m_acquired;   // pontos "a partir de"
    bool m_shiftLock{false};                       // Shift trava a inferência
    int m_lockKind{0};
    cad::Point3 m_lockP{};
    cad::Vec3 m_lockDir{};
    int m_axisDrawLock{0};                  // setas no lápis (1 X · 2 Y · 3 Z)

    // --- G2: geometria viva --------------------------------------------------
    bool eraseAt(const QPoint& pos, bool hide = false);   // borracha; Shift=oculta
    void autofold(cad::HalfEdgeMesh& m, const std::vector<Idx>& moved);
    bool m_eraseGesture{false};             // arrastando a borracha
    bool m_erasedAny{false};
    bool m_eraseHide{false};                // R8: Shift = ocultar aresta
    int m_moveMode{0};                      // 0 sólido · 1 vértice · 2 aresta
    std::vector<Idx> m_moveVerts;           // vértices arrastados (modo 1/2)

    // --- G3: multi-seleção + caixa janela/crossing --------------------------
    std::vector<std::pair<int, Idx>> m_selFacesMulti;   // Ctrl/Shift + clique
    std::vector<int> m_selSolidsMulti;                  // caixa de seleção
    bool m_boxSelecting{false};
    QPoint m_boxStart, m_boxEnd;
    void applyBoxSelect();
    QString selectionSummary() const;

    // --- G4: as mãos que faltam ----------------------------------------------
    int m_polySides{0};                     // círculo vira polígono N (0 = 24)
    bool m_pullLeave{false};                // Ctrl+Pull: empilha volume novo
    std::vector<std::pair<cad::Point3, cad::Point3>> m_guides;   // fita métrica
    int m_tapeStage{0};
    cad::Point3 m_tapeP1{};
    double m_tapeLast{0.0};              // última medida (R8: calibrar)
    void tapeClick(const QPoint& pos);

    // --- R26: cotas 3D persistentes ------------------------------------------
    struct Dim3D {
        cad::Point3 a{}, b{};   // pontos medidos (âncora por posição)
        cad::Point3 c{};        // linear: só deriva o lado/offset;
                                // angular: o 2º braço (TAMBÉM medido)
        bool orphan{false};     // âncora sumiu (booleana/edição) — não regenera
        int kind{0};            // R34: 0 = linear (a↔b), 1 = angular (a-b-c,
                                //      b é o VÉRTICE do ângulo)
    };
    std::vector<Dim3D> m_dims;
    int m_dimStage{0};
    bool m_dimAng{false};       // R34: fluxo de criação da cota angular
    cad::Point3 m_dimA{}, m_dimB{};
    void dimClick(const QPoint& pos, bool ang = false);
    void syncDimAnchors();       // re-snap por posição + marca órfã (choke
                                 // point único: topo de buildRenderArrays)
    bool dimScreenLine(const Dim3D& d, const QMatrix4x4& mvp,
                       QPointF& da, QPointF& db) const;   // linha de cota
                                                          // deslocada, em tela
    void dragDimAnchors(const cad::HalfEdgeMesh& before, const cad::Vec3& d);
                                 // carona: MOVER explícito arrasta âncoras que
                                 // batem com um vértice do sólido movido
    // R29: carona genérica — aplica um transform QUALQUER (rotação/escala) às
    // âncoras que casam com um vértice do sólido ANTES da transformação. Mesma
    // regra rígida da R26: a/b movem se casam; c só se AMBOS casaram.
    // Uma malha (conveniência) e VÁRIAS (gesto multi/grupo: casa contra a UNIÃO
    // dos vértices dos alvos e aplica UMA vez — senão cota entre 2 sólidos
    // perde o offset e cantos coincidentes transformam em dobro).
    void transformDimAnchors(const cad::HalfEdgeMesh& before,
                             const std::function<cad::Point3(const cad::Point3&)>& xf);
    void transformDimAnchors(const std::vector<int>& targets,
                             const std::function<cad::Point3(const cad::Point3&)>& xf);
    // R31: variante restrita a um CONJUNTO de posições (ex.: só os vértices da
    // face puxada no push/pull — âncora no resto do sólido não pode mover).
    void transformDimAnchors(const std::vector<cad::Point3>& pts,
                             const std::function<cad::Point3(const cad::Point3&)>& xf);

    // --- R6: material ativo do balde -----------------------------------------
    struct ActiveMat {
        int kind{0};                     // 0 cor · 1 textura
        float rgb[3]{0.761f, 0.627f, 0.388f};   // latão de fábrica
        QString texPath;
        double texScale{1.0};
    };
    ActiveMat m_mat;
    void paintClickAt(const QPoint& pos, bool wholeSolid, bool sample,
                      int match = 0);   // R8: 1=iguais no MODELO 2=no objeto

    // --- R7: arco (A→B→curvar) · transferidor · escala viva ------------------
    int m_arcStage{0};
    cad::Point3 m_arcA{}, m_arcB{};
    void arcClick(const QPoint& pos);
    void arcHover(const QPoint& pos);
    void commitArc(double sagitta);      // + no lado corrente do mouse
    double m_arcSag{0.0};                // sagitta corrente (sinal = lado)
    int m_rotStage{0};
    cad::Point3 m_rotC{};
    cad::Vec3 m_rotRef{};
    double m_rotAng{0.0};                // ângulo corrente (graus)
    std::vector<int> gestureTargets() const;   // multi/grupo ou selecionado
    void rotClick(const QPoint& pos, bool copy);
    bool rotPlanePoint(const QPoint& pos, cad::Point3& out) const;
    void rotHover(const QPoint& pos);
    void commitRotate(double deg, bool copy);
    int m_scStage{0};
    cad::Point3 m_scCenter{};
    double m_scBase{1.0};
    double m_scF{1.0};
    int m_scAxis{0};                     // R8: 0 uniforme · 1 X · 2 Y · 3 Z
    cad::Vec3 m_rotN{0, 0, 1};           // R8: plano do transferidor
    cad::Vec3 m_rotU{1, 0, 0}, m_rotV{0, 1, 0};
    int m_offStage{0};                   // R8: offset interativo
    int m_offMesh{-1};
    Idx m_offFace{cad::HalfEdgeMesh::kNone};
    cad::Point3 m_offP0{};
    double m_offD{0.0};
    void offsetClick(const QPoint& pos);
    void offsetHover(const QPoint& pos);
    bool m_fmPerimArm{false};            // R8: Follow Me pelo perímetro
    void followMePerimeter(int mi, Idx f);
    void scaleClick(const QPoint& pos);
    void scaleHover(const QPoint& pos);
    void commitScale(double f);

    // --- G5: contexto de edição (fora esmaece e não responde) ---------------
    QString m_ctxGroup;
    int m_ctxMesh{-1};
    // --- G6 ---
    bool m_ortho{false};
    // --- R27: walkthrough (primeira pessoa) ---
    bool m_walk{false};
    QElapsedTimer m_walkClock;   // R49: dt MEDIDO, não presumido
    float m_walkEye[3]{0.0f, 0.0f, 1.6f};   // posição do olho (m)
    float m_orbYaw{0.0f}, m_orbPitch{0.0f}; // órbita salva ao entrar no walk
    // R28: movimento CONTÍNUO — timer + conjunto de teclas pressionadas
    QSet<int> m_walkKeys;
    class QTimer* m_walkTimer{nullptr};
    void walkStep();          // mede o dt real (timer)
    void walkStep(double dt);  // dt injetado (QA determinístico)
    bool m_armPositionCam{false};           // R28: próximo clique posiciona câmera
    float m_fov{42.0f};
    bool m_fogOn{false};
    float m_fogDens{0.03f};
    // --- R5: Amanhecer (padrão) ⇄ Noite + tinta do rascunho + grade infinita
    bool m_dayOn{true};
    QVector3D bgTop() const;
    QVector3D bgBottom() const;
    QVector4D gridInk() const;
    QVector4D groundInkC() const;
    QVector4D sketchInkC() const;
    std::vector<float> m_sketchInk;      // rascunho+guias, tinta própria
    QOpenGLVertexArrayObject m_vaoSketch;
    QOpenGLBuffer m_vboSketch;
    int m_nSketch{0};
    QOpenGLShaderProgram m_progGrid;     // grade PROCEDURAL infinita
    QOpenGLVertexArrayObject m_vaoGridQ;
    QOpenGLBuffer m_vboGridQ;
    class QVariantAnimation* m_camAnim{nullptr};
    bool inCtx(int i) const {
        if (m_ctxGroup.isEmpty() && m_ctxMesh < 0) return true;
        if (m_ctxMesh >= 0) return i == m_ctxMesh;
        return i >= 0 && i < int(m_meshes.size()) &&
               m_meshes[std::size_t(i)].group == m_ctxGroup;
    }
    float ctxDim(const MeshPart& p) const {
        if (m_ctxGroup.isEmpty() && m_ctxMesh < 0) return 1.0f;
        if (m_ctxMesh >= 0)
            return &p == &m_meshes[std::size_t(m_ctxMesh)] ? 1.0f : 0.30f;
        return p.group == m_ctxGroup ? 1.0f : 0.30f;
    }
    void selectAt(const QPoint& pos);       // clique: seleciona/limpa + info
    QString faceInfo(int meshIdx, Idx face) const;

    // --- ferramenta Retângulo (desenhar na face) ----------------------------
    void rectClick(const QPoint& pos);
    void rectHover(const QPoint& pos);      // fantasma + inferência
    void cancelRectStage();
    bool rectPointAt(const QPoint& pos, cad::Point3& out, int& snapKind) const;
    void applySnap(int meshIdx, Idx face, cad::Point3& p, int& kind) const;
    bool cornersInsideFace(int meshIdx, Idx face,
                           const std::vector<cad::Point3>& corners,
                           const cad::Point3& origin, const cad::Vec3& U,
                           const cad::Vec3& V) const;
    // R19: como insetFace, mas se o novo polígono sobrepõe um vão coplanar
    // JÁ existente na mesma face-anel (ainda não puxado), FUNDE em vez de
    // falhar. kNone = nem cabe (mesmo erro de antes).
    Idx insetOrMergeOnFace(int meshIdx, Idx ring, std::vector<cad::Point3> poly,
                           const cad::Point3& origin, const cad::Vec3& U,
                           const cad::Vec3& V);

    // --- ferramenta Linha (divide a face) ------------------------------------
    // Clique resolvido contra a BORDA da face: vértice existente, ponto médio
    // ou ponto sobre a aresta (os dois últimos viram splitEdge na hora do 2º
    // clique). kind: 0 nada · 1 vértice · 2 ponto médio · 3 na aresta.
    struct BoundaryPick {
        int kind{0};
        Idx vertex{cad::HalfEdgeMesh::kNone};   // kind 1
        Idx he{cad::HalfEdgeMesh::kNone};       // kind 2/3
        cad::Point3 p{};
    };
    BoundaryPick boundaryPickAt(int meshIdx, Idx face,
                                const cad::Point3& hit) const;
    void lineClick(const QPoint& pos);
    void lineHover(const QPoint& pos);
    void cancelLineStage();

    void pushUndo();

    // --- VCB: última operação re-executável com medidas exatas -------------
    struct LastOp {
        int kind{0};                    // 0 nada · 1 rect · 2 círculo · 3 pull
        int mesh{-1};                   // -2 = chão
        Idx face{cad::HalfEdgeMesh::kNone};
        cad::Point3 p1{};
        cad::Vec3 U{}, V{};
        double a{0.0}, b{0.0};          // du,dv (rect) · raio (circ) · dist (pull)
    };
    LastOp m_lastOp;
    QString m_vcbBuf;
    void redoRect(double w, double h);
    void redoCircle(double r);
    void redoPull(double d);

    // --- cena (CPU) ---------------------------------------------------------
    std::vector<MeshPart> m_meshes;         // uma malha manifold por caixa
    std::vector<float> m_tris;      // pos3 + normal3 + cor3 (das malhas)
    std::vector<float> m_edges;     // pos3, pares (cada aresta UMA vez)
    std::vector<float> m_ground;    // pos3, pares (linework 2D no chão)
    std::vector<float> m_grid;      // pos3, pares (grade do terreno)
    float m_center[3]{0, 0, 0};
    float m_radius{10.0f};
    std::size_t m_wallCount{0};
    bool m_sceneDirty{true};
    bool m_edited{false};
    bool m_dirtyBase{false};   // R48: a base carregada já é suja (recuperado)

    // --- seleção / hover ----------------------------------------------------
    int m_selMesh{-1};
    Idx m_selFace{cad::HalfEdgeMesh::kNone};
    bool m_selWhole{false};                 // duplo clique: o SÓLIDO inteiro
    int m_hovMesh{-1};
    Idx m_hovFace{cad::HalfEdgeMesh::kNone};
    bool m_hlDirty{false};

    // --- retângulo na face --------------------------------------------------
    Tool m_tool{Tool::Select};
    int  m_rectStage{0};                    // 0: 1º canto · 1: canto oposto
    int  m_rectMesh{-1};
    Idx  m_rectFace{cad::HalfEdgeMesh::kNone};
    cad::Point3 m_rectP1{};                 // 1º canto (snapado)
    cad::Vec3   m_rectU{}, m_rectV{}, m_rectN{};   // base da face

    // --- linha na face ------------------------------------------------------
    int  m_lineStage{0};
    int  m_lineMesh{-1};
    Idx  m_lineFace{cad::HalfEdgeMesh::kNone};
    BoundaryPick m_lineP1;

    // --- círculo na face / no chão (M2: vira cilindro com o P) ---------------
    int  m_circStage{0};
    int  m_circMesh{-1};                    // -2 = chão
    Idx  m_circFace{cad::HalfEdgeMesh::kNone};
    cad::Point3 m_circC{};
    cad::Vec3   m_circU{}, m_circV{}, m_circN{};
    void circleClick(const QPoint& pos);
    void circleHover(const QPoint& pos);
    void cancelCircleStage();

    // --- PUSH/PULL arrastando (S1.2) -----------------------------------------
    bool m_pullDrag{false};
    int  m_pullMesh{-1};
    Idx  m_pullFace{cad::HalfEdgeMesh::kNone};
    cad::Point3 m_pullC{};
    cad::Vec3   m_pullN{};
    double m_pullT{0.0};
    std::vector<MeshPart> m_pullBase;       // cena antes do arrasto

    // --- mover/copiar sólido ------------------------------------------------
    int  m_moveStage{0};
    bool m_moveCopy{false};
    int  m_moveMesh{-1};                    // sólido em movimento
    int  m_moveAxisLock{0};                 // 0 livre · 1 X · 2 Y · 3 Z (setas)
    cad::Point3 m_moveBase{};
    void moveClick(const QPoint& pos);
    void moveHover(const QPoint& pos);
    void cancelMoveStage();
    bool movePointAt(const QPoint& pos, cad::Point3& out) const;

    // --- seleção de ARESTA (exclusiva com a de face) --------------------------
    int  m_selEdgeMesh{-1};
    Idx  m_selEdge{cad::HalfEdgeMesh::kNone};
    bool pickEdgeAt(const QPoint& pos, int& meshOut, Idx& heOut) const;

    // --- órbita pivotada ------------------------------------------------------
    bool  m_hasPivot{false};
    float m_pivot[3]{0, 0, 0};

    std::vector<float> m_ghost, m_snapMark; // fantasma + marcador (CPU)
    std::vector<float> m_axes;              // eixos globais X/Y/Z na origem
    bool m_ghostDirty{false};

    // --- LÁPIS: arestas soltas + face-healing (S1) ---------------------------
    std::vector<std::pair<cad::Point3, cad::Point3>> m_sketch;
    cad::Point3 m_chainPt{};
    bool m_chainActive{false};
    bool pencilPointAt(const QPoint& pos, cad::Point3& out) const;
    void pencilClick(cad::Point3 p);   // por valor: o fechamento tolerante
                                       // pode grudar o ponto (R5.1)
    bool tryHealFace(const cad::Point3& a, const cad::Point3& b);

    // --- undo de estudo (snapshot: malhas + rascunho do lápis + cotas) ------
    struct SceneSnap {
        std::vector<MeshPart> meshes;
        std::vector<std::pair<cad::Point3, cad::Point3>> sketch;
        std::vector<Dim3D> dims;             // R29: cota volta com o Ctrl+Z
    };
    std::vector<SceneSnap> m_undo;
    std::vector<SceneSnap> m_redo;          // G2: espelho do undo (Ctrl+Y)
    std::map<QString, MeshPart> m_compDefs;      // biblioteca de componentes
    QSet<QString> m_libComps;                    // R14: os de fábrica (não salva)
    QString m_pendingComp;                       // inserção aguardando clique

    // --- GL -----------------------------------------------------------------
    QOpenGLShaderProgram m_progMesh, m_progLine, m_progBg;
    QOpenGLVertexArrayObject m_vaoMesh, m_vaoEdge, m_vaoGround, m_vaoGrid,
                             m_vaoBg, m_vaoSel, m_vaoHov, m_vaoGhost, m_vaoSnap,
                             m_vaoAxes;
    QOpenGLBuffer m_vboMesh, m_vboEdge, m_vboGround, m_vboGrid, m_vboBg,
                  m_vboSel, m_vboHov, m_vboGhost, m_vboSnap, m_vboAxes;
    int m_nMesh{0}, m_nEdge{0}, m_nGround{0}, m_nGrid{0}, m_nSel{0}, m_nHov{0},
        m_nGhost{0}, m_nSnap{0}, m_nAxes{0};
    bool m_selIsEdge{false};        // VBO de seleção contém LINHA, não tris
    bool m_glReady{false};

    // --- sol e sombras (shadow map) ------------------------------------------
    bool m_sunOn{false};
    int m_sunMonth{9};
    double m_sunHour{15.0};
    double m_sunLat{-3.73};                 // Fortaleza-CE
    QVector3D m_sunDirW{0.42f, 0.30f, 0.86f};   // PARA o sol (luz)
    unsigned m_shadowFbo{0}, m_shadowTex{0};
    static constexpr int kShadowRes = 2048;
    QOpenGLShaderProgram m_progDepth;
    QMatrix4x4 m_lightMvp;
    void computeSunDir();
    void renderShadowPass();

    // --- texturas / estilo / seção (S4b) --------------------------------------
    // R11: `img` pode ficar VAZIA (biblioteca grande é preguiçosa) — a cheia
    // é lida do disco só quando o material é usado; a paleta usa `thumb`.
    struct TexEntry { QString file; QImage img; QImage thumb;
                      unsigned glId{0}; };
    std::map<QString, TexEntry> m_texLib;        // nome -> entrada
    struct TexBatch {
        QString tex;
        std::vector<float> data;                 // pos3+nrm3+uv2
        QOpenGLVertexArrayObject* vao{nullptr};
        QOpenGLBuffer* vbo{nullptr};
        int count{0};
    };
    std::vector<TexBatch> m_texBatches;
    QOpenGLShaderProgram m_progTex;
    int m_style{0};
    std::vector<Section> m_sections;   // R32: as seções ativas (0..kMax)
    void setClipUniforms(QOpenGLShaderProgram& p);   // uClips[]/uClipCount
    bool addSection(const cad::Vec3& nUnit, double d);   // false = lotado
    bool m_armClipFace{false};       // R30: próximo clique corta na face
    bool m_armClipDrag{false};       // R34: próximo arrasto desliza a seção
    bool m_armStair{false};          // R41: 2 cliques no chão fazem a escada
    quint64 m_ctxCompHash{0};        // R42: geometria ao ENTRAR no contexto
    int m_stairStage{0};
    cad::Point3 m_stairOrigin{};
    double m_stairW{0.9}, m_stairH{3.0}, m_stairRun{0.27};
    bool m_armGuard{false};          // R43: 2 cliques fazem o guarda-corpo
    int m_guardStage{0};
    cad::Point3 m_guardA{};
    double m_guardH{1.10}, m_guardGap{1.50};
    bool m_armSlab{false};           // R43: 4 cliques fazem a laje furada
    int m_slabStage{0};
    double m_slabPts[6]{};           // x1,y1,x2,y2,hx1,hy1 acumulados
    double m_slabZ{2.89}, m_slabTh{0.15};
    bool m_clipDragging{false};      // R34: arrasto de seção em curso
    double m_clipT0{0}, m_clipD0{0}; // R34: âncora do arrasto (t inicial, d0)
    cad::Point3 m_clipP0{};          // R34: ponto-base do eixo (n̂·d0)
    void disarmGestures();           // R34: lição R30 — 3ª arma → helper único

    // --- câmera orbital (Z para cima) ---------------------------------------
    float m_yaw{-125.0f};     // graus, em torno de Z
    float m_pitch{30.0f};     // graus acima do horizonte
    float m_dist{30.0f};
    float m_target[3]{0, 0, 0};
    enum class Drag { None, Orbit, Pan };
    Drag   m_drag{Drag::None};
    QPoint m_lastPos, m_pressPos;

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projMatrix() const;

    PlantScene m_plant;                  // pacote neutro do conector (R3)
    double m_wallHeight{3.10};    // pé-direito do estudo (m)
};
