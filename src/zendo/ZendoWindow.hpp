// src/zendo/ZendoWindow.hpp
// Janela do Zendo: abre um projeto .zencad do ZenCAD e o exibe em 3D, com
// seleção de faces, retângulo/linha na face, push/pull, pintura e o ESTUDO
// como documento (.zendo — F3).
#pragma once
#include <QMainWindow>
#include <memory>

class QLockFile;

#include "core/document/DrawingManager.hpp"
#include "core/document/Style.hpp"
#include "core/layout/Layout.hpp"
#include "zenio/ProjectIo.hpp"

class Viewport3D;
class ZendoStartPage;
class QLabel;
class QStackedWidget;

class ZendoWindow : public QMainWindow {
    Q_OBJECT
public:
    ZendoWindow();
    // R48: definido no .cpp — o unique_ptr<QLockFile> precisa do
    // tipo completo pra destruir (QLockFile é só forward aqui).
    ~ZendoWindow() override;

    bool openFile(const QString& path);     // .zencad (2D -> paredes 3D)
    bool openStudy(const QString& path);    // .zendo (estudo salvo)
    bool saveStudy(const QString& path);
    void showSpace();                       // viewport 3D
    void showStart();                       // tela inicial (recentes)
    // QA headless: espera o 1º frame, grava PNG e sai (0 = ok).
    void shootAndQuit(const QString& pngPath);
    void setCameraPose(float yawDeg, float pitchDeg, float distFactor);
    // QA: cliques sintéticos (coords normalizadas) antes do --shot.
    void setQaPick(double nx, double ny) { m_qaPickX = nx; m_qaPickY = ny; }
    void setQaPull(double d) { m_qaPull = d; }
    void setQaRect(double x1, double y1, double x2, double y2) {
        m_qaRect[0] = x1; m_qaRect[1] = y1; m_qaRect[2] = x2; m_qaRect[3] = y2;
    }
    void setQaLine(double x1, double y1, double x2, double y2) {
        m_qaLine[0] = x1; m_qaLine[1] = y1; m_qaLine[2] = x2; m_qaLine[3] = y2;
    }
    void setQaPaint(const QString& rgb) { m_qaPaint = rgb; }   // "r,g,b"
    void setQaSaveAs(const QString& p) { m_qaSaveAs = p; }
    void setQaUndo(int n) { m_qaUndo = n; }   // nº de Ctrl+Z após as edições
    void setQaElev(const QString& s) { m_qaElev = s; }   // "S,arquivo.zencad"
    void setQaCut(const QString& s) { m_qaCut = s; }     // "Y,5.4,+,arq.zencad"
    void setQaDel(bool b) { m_qaDel = b; }               // apaga a seleção
    void setQaGlue(bool b) { m_qaGlue = b; }             // gruda a seleção
    void setQaMoveZ(double d) { m_qaMoveZ = d; }         // sobe/desce (m)
    void setQaRoof(const QString& s) { m_qaRoof = s; }   // "h[,beiral[,4]]"
    void setQaTerrain(bool b) { m_qaTerrain = b; }       // platô
    void setQaVcb(const QString& s) { m_qaVcb = s; }     // "4;3" pós-gesto
    void setQaRot(double a) { m_qaRot = a; }
    void setQaScale(double f) { m_qaScale = f; }
    void setQaOffset(double d) { m_qaOffset = d; }
    void setQaPencil(const QString& s) { m_qaPencil = s; }
    void setQaMkComp(const QString& s) { m_qaMkComp = s; }
    void setQaInsComp(const QString& s) { m_qaInsComp = s; }
    void setQaRedef(bool b) { m_qaRedef = b; }
    void setQaMkScene(const QString& s) { m_qaMkScene = s; }
    void setQaGoScene(const QString& s) { m_qaGoScene = s; }
    void setQaSun(const QString& s) { m_qaSun = s; }
    void setQaTex(const QString& s) { m_qaTex = s; }
    void setQaStyle(int s) { m_qaStyle = s; }
    void setQaClip(const QString& s) { m_qaClip = s; }
    void setQaObj(const QString& s) { m_qaObj = s; }
    void setQaGltf(const QString& s) { m_qaGltf = s; }
    void setQaMove(const QString& s, bool copy) {        // "x1,y1,x2,y2"
        m_qaMove = s;
        m_qaMoveCopy = copy;
    }
    void setQaCircle(const QString& s) { m_qaCircle = s; }   // "cx,cy,rx,ry"
    void setQaHover(const QString& s) { m_qaHover = s; }     // G1 "nx,ny;…"
    void setQaBalde(const QString& s) { m_qaBalde = s; }     // R55 "nx,ny"
    void setQaCtrlS(bool b) { m_qaCtrlS = b; }               // R55: Ctrl+S
    void setQaRedo(int n) { m_qaRedo = n; }                  // G2: Ctrl+Y
    void setQaErase(const QString& s) { m_qaErase = s; }     // G2 "nx,ny"
    void setQaVMove(const QString& s) { m_qaVMove = s; }     // G2 5 números
    void setQaSketch3d(const QString& s) { m_qaSketch3d = s; } // G2 mundo
    void setQaSelBox(const QString& s) { m_qaSelBox = s; }     // G3 caixa
    void setQaSides(int n);                                    // G4 polígono
    void setQaTape(const QString& s) { m_qaTape = s; }         // G4 fita
    void setQaFollow(bool b) { m_qaFollow = b; }               // G4 Follow Me
    void setQaArray(const QString& s) { m_qaArray = s; }       // G4 "x3"/"​/3"
    void setQaMkGroup(const QString& s) { m_qaMkGroup = s; }   // G5
    void setQaCtxAt(const QString& s) { m_qaCtxAt = s; }       // G5 "nx,ny"
    void setQaTagSet(const QString& s) { m_qaTagSet = s; }     // G5
    void setQaTagVis(const QString& s) { m_qaTagVis = s; }     // G5 "tag,0/1"
    void setQaOrtho(bool b) { m_qaOrtho = b; }                 // G6
    void setQaFog(double d) { m_qaFog = d; }                   // G6
    void setQaNewStudy(bool b) { m_qaNewStudy = b; }           // R1
    void setQaRectX(const QString& s) { m_qaRectX = s; }       // R2 exato
    void setQaPencilX(const QString& s) { m_qaPencilX = s; }   // R4
    void setQaMoveM(const QString& s) { m_qaMoveM = s; }       // R4 pós-selbox
    void setQaNight(bool b) { m_qaNight = b; }                 // R5 noite
    void setQaPalette(const QString& s) { m_qaPalette = s; }   // R5 "r,g,b"
    void setQaBucket(const QString& s) { m_qaBucket = s; }     // R6 arma
    void setQaPaintAt(const QString& s) { m_qaPaintAt = s; }   // R6 cliques
    void setQaArcQ(const QString& s) { m_qaArcQ = s; }         // R7
    void setQaProtr(const QString& s) { m_qaProtr = s; }       // R7
    void setQaScaleQ(const QString& s) { m_qaScaleQ = s; }     // R7
    void setQaFmPerim(const QString& s) { m_qaFmPerim = s; }   // R8
    void setQaTexScale(const QString& s) { m_qaTexScale = s; } // R13
    void setQaImpObj(const QString& s) { m_qaImpObj = s; }     // R15
    void setQaInspect(bool b) { m_qaInspect = b; }             // R17
    void setQaFixSolid(bool b) { m_qaFixSolid = b; }           // R17
    void setQaCleanup(bool b) { m_qaCleanup = b; }             // R17
    void setQaMirror(const QString& s) { m_qaMirror = s; }     // R17
    void setQaSubtract(bool b) { m_qaSubtract = b; }           // R21
    void setQaUnite(bool b) { m_qaUnite = b; }                 // R24
    void setQaDim3d(const QString& s) { m_qaDim3d = s; }       // R26
    void setQaWalk(const QString& s) { m_qaWalk = s; }         // R27
    void setQaDimClick(const QString& s) { m_qaDimClick = s; } // R27
    void setQaWalkEnter(bool b) { m_qaWalkEnter = b; }         // R27 (F8)
    void setQaPosCam(const QString& s) { m_qaPosCam = s; }     // R28
    void setQaWalkSim(const QString& s) { m_qaWalkSim = s; }   // R28
    void setQaClipFace(const QString& s) { m_qaClipFace = s; } // R30
    void setQaClipPlane(const QString& s) { m_qaClipPlane = s; } // R30
    void setQaCutPlane(const QString& s) { m_qaCutPlane = s; } // R31
    void setQaClipSlide(const QString& s) { m_qaClipSlide = s; } // R34
    void setQaClipDrag(const QString& s) { m_qaClipDrag = s; }   // R34
    void setQaRender(const QString& s) { m_qaRender = s; }       // R36
    void setQaHdri(bool b) { m_qaHdri = b; }                     // R46
    void setQaEngine(const QString& s) { m_qaEngine = s; }       // R47
    void setQaPreset(int p) { m_qaPreset = p; }                  // R47
    // R48: proteção do trabalho (o main só arma no modo interativo)
    void iniciarProtecao();
    QString fazerAutosave();                 // a função REAL (timer e QA)
    int avaliarRecuperacao(QString& arq, QString& origem) const;
    void limparAutosave();
    void limparOferta();                     // R48: o reservado da oferta
    void limparRecuperacao();
    void setQaAutosave(bool b) { m_qaAutosave = b; }
    void setQaRecovery(bool b) { m_qaRecovery = b; }
    void setQaLimpeza(bool b) { m_qaLimpeza = b; }
    void setQaAjuda(bool b) { m_qaAjuda = b; }
    void setQaProtecao(bool b) { m_qaProtecao = b; }
    void setQaDirtyBase(bool b) { m_qaDirtyBase = b; }
    void setQaI18n(bool b) { m_qaI18n = b; }                    // R52
    void setQaEnquadrar(bool b) { m_qaEnquadrar = b; }          // R52
    void setQaFoto(bool b) { m_qaFoto = b; }                    // R52
    void setQaVista(const QString& s) { m_qaVista = s; }        // R52
    // R48: raiz injetável — o QA jamais toca a recuperação REAL
    static void setPastaRecuperacao(const QString& d);
    void primeirosPassos(bool forcado);      // R48: painel de boas-vindas
    void setQaStair(const QString& s) { m_qaStair = s; }         // R41
    void setQaGuard(const QString& s) { m_qaGuard = s; }         // R43
    void setQaSlabHole(const QString& s) { m_qaSlabHole = s; }   // R43
    void setQaDimAng(const QString& s) { m_qaDimAng = s; }     // R34
    void setQaDblPick(double nx, double ny) {            // seleciona o sólido
        m_qaDblPick[0] = nx;
        m_qaDblPick[1] = ny;
    }

private:
    void openDialog();
    void openStudyDialog();
    void saveStudyQuick();       // R55: Ctrl+S grava; só pede nome se não há
    void saveStudyDialog();      // Salvar como (Ctrl+Shift+S)
    void savePackageDialog();                  // R38: texturas viajam junto
    void heightDialog();
    void pushPullDialog();
    void paintDialog();
    void elevationDialog();
    void sectionDialog();
    void renderDialog();                       // R36/R37: o Fotógrafo
    bool garantirMotor();                      // R47: baixa o Blender se faltar
    static QString pastaRecuperacao();         // R48
    static QString s_recDir;                   // raiz injetada
    static QString arqSentinela();
    void ofertarRecuperacao(const QString& arq, const QString& origem);
    QString colaDeAtalhos() const;             // GERADA das QActions
    static void presetFotografo(int preset, int& meiaHora, int& ceu,
                                bool& interior, int& qual);      // R47
    bool buildRenderJob(const QString& outPng, double elevDeg, double azimDeg,
                        int samples, int resX, int resY, bool interior,
                        bool hdri, bool enquadrar, QString& blender,
                        QStringList& args, QString& err);
    QString bridgeSuggestion(const QString& stem) const;

    std::unique_ptr<cad::DrawingManager> m_doc;
    cad::LayoutTable     m_layouts;
    cad::StyleTable      m_styles;
    cad::ProjectSettings m_settings;

    Viewport3D* m_vp{nullptr};
    ZendoStartPage* m_start{nullptr};
    QStackedWidget* m_stack{nullptr};
    QStringList recents() const;
    void addRecent(const QString& path);
    QLabel*     m_info{nullptr};
    QLabel*     m_selInfo{nullptr};
    void closeEvent(QCloseEvent* e) override;
    bool writeStudyFile(const QString& path);   // sem mexer em título/estado
    void refreshOutliner();
    // CENAS: câmeras nomeadas (persistem no .zendo)
    struct Scene {
        QString name;
        float yaw, pitch, dist, tgt[3];
    };
    std::vector<Scene> m_scenes;
    QJsonArray scenesJson() const;
    void setScenesJson(const QJsonArray& a);
    class QListWidget* m_outliner{nullptr};
    QTimer* m_outlinerDebounce{nullptr};
    bool m_outlinerBusy{false};
    QString m_sourcePath;               // .zencad de origem (absoluto)
    QString m_studyPath;                // .zendo corrente (absoluto)
    QString m_pendingComp;              // componente aguardando o clique
    double m_qaPickX{-1.0}, m_qaPickY{-1.0}, m_qaPull{0.0};
    double m_qaRect[4]{-1.0, -1.0, -1.0, -1.0};
    double m_qaLine[4]{-1.0, -1.0, -1.0, -1.0};
    QString m_qaPaint, m_qaSaveAs, m_qaElev, m_qaCut, m_qaMove, m_qaCircle;
    QString m_lastInfo;                 // última msg de status (dump de QA)
    double m_qaDblPick[2]{-1.0, -1.0};
    double m_qaMoveZ{0.0}, m_qaRot{0.0}, m_qaScale{0.0}, m_qaOffset{0.0};
    QString m_qaRoof, m_qaVcb, m_qaPencil, m_qaMkComp, m_qaInsComp;
    QString m_qaMkScene, m_qaGoScene, m_qaSun, m_qaTex, m_qaClip, m_qaObj,
        m_qaGltf, m_qaHover, m_qaBalde, m_qaErase, m_qaVMove, m_qaSketch3d,
        m_qaSelBox,
        m_qaTape, m_qaArray, m_qaMkGroup, m_qaCtxAt, m_qaTagSet, m_qaTagVis;
    bool m_qaFollow{false};
    bool m_qaCtrlS{false};                   // R55: exercita o Salvar direto
    bool m_qaOrtho{false};
    double m_qaFog{0.0};
    bool m_qaNewStudy{false};
    QString m_qaRectX, m_qaPencilX, m_qaMoveM, m_qaPalette, m_qaBucket,
        m_qaPaintAt, m_qaArcQ, m_qaProtr, m_qaScaleQ, m_qaFmPerim,
        m_qaTexScale, m_qaImpObj, m_qaMirror;
    bool m_qaInspect{false}, m_qaFixSolid{false}, m_qaCleanup{false};
    bool m_qaNight{false};
    void textureDialog();                // R5: textura na seleção
    void refreshMaterials();             // R5: paleta de materiais
    class QListWidget* m_materials{nullptr};
    class QListWidget* m_materialsTex{nullptr};   // R9: aba de texturas
    class QListWidget* m_moveis{nullptr};   // R14: aba do mobiliário
    class QListWidget* m_scenesList{nullptr};   // R16: cenas na Bandeja
    void refreshScenesPanel();                  // R16
    class QComboBox* m_matFam{nullptr};     // R12: filtro de família
    class QComboBox* m_texFam{nullptr};     // R12: filtro de categoria
    class QLineEdit* m_texBusca{nullptr};   // R12: busca de textura
    int m_qaRedo{0};
    int m_qaStyle{-1};
    bool m_qaRedef{false};
    bool   m_qaTerrain{false};
    int    m_qaUndo{0};
    bool   m_qaDel{false}, m_qaMoveCopy{false}, m_qaGlue{false};
    bool   m_qaSubtract{false};                                // R21
    bool   m_qaUnite{false};                                   // R24
    QString m_qaDim3d;                                          // R26
    QString m_qaWalk;                                           // R27
    QString m_qaDimClick;                                       // R27
    bool m_qaWalkEnter{false};                                  // R27
    QString m_qaPosCam;                                         // R28
    QString m_qaWalkSim;                                        // R28
    QString m_qaClipFace, m_qaClipPlane;                        // R30
    QString m_qaCutPlane;                                       // R31
    QString m_qaClipSlide, m_qaDimAng, m_qaClipDrag;            // R34
    QString m_qaRender;                                         // R36
    QString m_qaStair;                                          // R41
    QString m_qaGuard;                                          // R43
    QString m_qaSlabHole;                                       // R43
    bool m_qaHdri{false};                                       // R46
    QString m_qaEngine;                                         // R47
    int m_qaPreset{-1};                                         // R47
    bool m_qaAutosave{false}, m_qaRecovery{false};              // R48
    bool m_qaLimpeza{false}, m_qaAjuda{false};                  // R48
    bool m_qaProtecao{false};                                   // R48
    bool m_qaDirtyBase{false};                                  // R48
    bool m_qaI18n{false};                                       // R52
    bool m_qaEnquadrar{false};                                  // R52
    bool m_qaFoto{false};                                       // R52
    QString m_qaVista;                                          // R52
    double yawFoto() const;                                     // R52
    std::unique_ptr<QLockFile> m_lock;   // R48: dono da sessão
};
