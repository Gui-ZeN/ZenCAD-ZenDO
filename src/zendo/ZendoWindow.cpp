// src/zendo/ZendoWindow.cpp
#include "ZendoWindow.hpp"
#include "Viewport3D.hpp"
#include "StartPage3D.hpp"
#include "PlantImport.hpp"   // R3: o conector planta→pacote neutro
#include "MaterialLib.hpp"   // R9: biblioteca de materiais procedural
#include "FurnitureLib.hpp"  // R14: o mobiliário paramétrico
#include "BlenderInstaller.hpp"   // R47: o motor que se instala sozinho

#include <QApplication>
#include <QCryptographicHash>
#include <QLockFile>
#include <QTextBrowser>
#include <QTime>
#include <QEventLoop>
#include <QBuffer>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QUrl>
#include <QColorDialog>
#include <QSettings>
#include <QStackedWidget>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QRegularExpression>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QGridLayout>
#include <QFrame>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "Bridge.hpp"
#include "core/spatial/Quadtree.hpp"

using namespace cad;

QIcon movelThumb(const moblib::Movel& mv);   // R14 (definida adiante)

namespace {

// Ícones da toolbar desenhados à mão, no traço Sumi & Washi (latão + washi).
QIcon zenIcon(const char* kind) {
    const int S = 26;
    QPixmap px(S, S);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor latao(0xc2, 0xa0, 0x63);
    const QColor washi(0xdc, 0xd6, 0xc8);
    QPen pen(washi, 1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    const QString k = QString::fromLatin1(kind);
    auto brass = [&] { pen.setColor(latao); p.setPen(pen); };
    if (k == "sel") {                       // seta de seleção
        brass();
        const QPointF pts[3] = {{7, 4}, {7, 20}, {13, 15}};
        p.drawPolygon(pts, 3);
        p.drawLine(QPointF(13, 15), QPointF(19, 22));
    } else if (k == "pencil") {             // lápis
        p.drawLine(6, 20, 17, 9);
        brass();
        p.drawLine(17, 9, 20, 6);
        p.drawLine(5, 21, 8, 20.5);
    } else if (k == "rect") {
        p.drawRect(5, 7, 16, 12);
    } else if (k == "circle") {
        p.drawEllipse(QPointF(13, 13), 8, 8);
    } else if (k == "tape") {               // fita métrica (régua)
        p.save();
        p.translate(13, 13);
        p.rotate(-30);
        p.drawRect(QRectF(-9, -3, 18, 6));
        brass();
        for (int i = -6; i <= 6; i += 3)
            p.drawLine(QPointF(i, -3), QPointF(i, i % 2 ? 0.5 : -0.5));
        p.restore();
    } else if (k == "arc") {                // arco 2pt + curvar
        p.drawArc(QRectF(5, 8, 16, 22), 20 * 16, 140 * 16);
        brass();
        p.drawEllipse(QPointF(6, 18), 1.8, 1.8);
        p.drawEllipse(QPointF(20, 18), 1.8, 1.8);
    } else if (k == "scale") {              // escala: caixa + seta diagonal
        p.drawRect(5, 11, 10, 10);
        brass();
        p.drawLine(QPointF(12, 14), QPointF(21, 5));
        p.drawLine(QPointF(21, 5), QPointF(16, 5));
        p.drawLine(QPointF(21, 5), QPointF(21, 10));
    } else if (k == "tex") {                // textura (xadrez)
        p.setBrush(latao);
        p.setPen(Qt::NoPen);
        p.drawRect(6, 6, 7, 7);
        p.drawRect(13, 13, 7, 7);
        p.setBrush(washi);
        p.drawRect(13, 6, 7, 7);
        p.drawRect(6, 13, 7, 7);
        pen.setColor(washi);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(6, 6, 14, 14);
    } else if (k == "poly") {               // polígono N (hexágono)
        QPolygonF hexa;
        for (int i = 0; i < 6; ++i) {
            const double a = 3.14159265 * (i / 3.0);
            hexa << QPointF(13 + 8 * std::cos(a), 13 + 8 * std::sin(a));
        }
        p.drawPolygon(hexa);
    } else if (k == "follow") {             // follow me: perfil + caminho
        brass();
        p.drawRect(5, 15, 5, 5);
        pen.setColor(washi);
        p.setPen(pen);
        p.drawArc(QRectF(7, 5, 13, 13), 180 * 16, -160 * 16);
        p.drawLine(QPointF(20, 13), QPointF(18, 9));
        p.drawLine(QPointF(20, 13), QPointF(22, 9));
    } else if (k == "grupo") {              // grupo: duas caixas + laço
        p.drawRect(5, 9, 9, 9);
        brass();
        p.drawRect(11, 6, 9, 9);
        pen.setColor(washi);
        pen.setStyle(Qt::DotLine);
        p.setPen(pen);
        p.drawRect(3, 3, 20, 18);
        pen.setStyle(Qt::SolidLine);
    } else if (k == "undo" || k == "redo") {   // setas curvas
        brass();
        const bool u = k == "undo";
        p.drawArc(QRectF(6, 8, 14, 12), u ? 30 * 16 : -210 * 16, 180 * 16);
        const QPointF tip = u ? QPointF(6.5, 12) : QPointF(19.5, 12);
        p.drawLine(tip, tip + QPointF(u ? 4 : -4, -2));
        p.drawLine(tip, tip + QPointF(u ? 3 : -3, 3));
    } else if (k == "erase") {              // borracha inclinada
        p.save();
        p.translate(13, 13);
        p.rotate(-35);
        p.drawRoundedRect(QRectF(-8, -4.5, 16, 9), 2, 2);
        brass();
        p.drawLine(QPointF(-1.5, -4.5), QPointF(-1.5, 4.5));
        p.restore();
    } else if (k == "pull") {               // face + seta pra cima
        p.drawRect(6, 13, 14, 7);
        brass();
        p.drawLine(13, 12, 13, 4);
        p.drawLine(10, 7, 13, 4);
        p.drawLine(16, 7, 13, 4);
    } else if (k == "offset") {             // moldura
        p.drawRect(5, 6, 16, 14);
        brass();
        p.drawRect(9, 10, 8, 6);
    } else if (k == "move") {               // cruz de setas
        brass();
        p.drawLine(13, 5, 13, 21);
        p.drawLine(5, 13, 21, 13);
        p.drawLine(11, 7, 13, 5); p.drawLine(15, 7, 13, 5);
        p.drawLine(11, 19, 13, 21); p.drawLine(15, 19, 13, 21);
        p.drawLine(7, 11, 5, 13); p.drawLine(7, 15, 5, 13);
        p.drawLine(19, 11, 21, 13); p.drawLine(19, 15, 21, 13);
    } else if (k == "copy") {               // dois quadrados
        p.drawRect(5, 9, 11, 11);
        brass();
        p.drawRect(10, 5, 11, 11);
    } else if (k == "rotate") {             // arco com seta
        brass();
        p.drawArc(QRectF(6, 6, 14, 14), 30 * 16, 250 * 16);
        p.drawLine(18, 15, 20, 11);
        p.drawLine(18, 15, 14, 14);
    } else if (k == "paint") {              // gota
        brass();
        QPainterPath path(QPointF(13, 4));
        path.cubicTo(20, 13, 18, 20, 13, 20);
        path.cubicTo(8, 20, 6, 13, 13, 4);
        p.drawPath(path);
    } else if (k == "glue") {               // ímã (U)
        brass();
        p.drawArc(QRectF(7, 4, 12, 14), 180 * 16, 180 * 16);
        p.drawLine(7, 11, 7, 20);
        p.drawLine(19, 11, 19, 20);
        pen.setColor(washi); p.setPen(pen);
        p.drawLine(5, 18, 9, 18);
        p.drawLine(17, 18, 21, 18);
    } else if (k == "hide") {               // olho cortado
        p.drawArc(QRectF(4, 8, 18, 12), 0, 180 * 16);
        p.drawEllipse(QPointF(13, 13), 2.4, 2.4);
        brass();
        p.drawLine(6, 20, 20, 6);
    } else if (k == "roof") {               // telhado
        brass();
        p.drawLine(4, 15, 13, 6);
        p.drawLine(13, 6, 22, 15);
        pen.setColor(washi); p.setPen(pen);
        p.drawLine(7, 15, 7, 21);
        p.drawLine(19, 15, 19, 21);
    } else if (k == "tree") {               // estrutura/outliner
        p.drawLine(8, 6, 8, 20);
        p.drawLine(8, 9, 14, 9);
        p.drawLine(8, 14, 14, 14);
        p.drawLine(8, 19, 14, 19);
        brass();
        p.drawRect(15, 7, 5, 4);
        p.drawRect(15, 17, 5, 4);
    } else if (k == "scene") {              // claquete/câmera
        p.drawRect(5, 10, 16, 10);
        brass();
        p.drawLine(5, 10, 8, 6);
        p.drawLine(11, 10, 14, 6);
        p.drawLine(17, 10, 20, 6);
    }
    return QIcon(px);
}

std::unique_ptr<DrawingManager> makeDoc() {
    const AABB world = AABB::fromCenterHalf({0, 0, 0}, {1e6, 1e6, 1e6});
    return std::make_unique<DrawingManager>(
        std::make_unique<Quadtree>(world, /*maxDepth*/ 12, /*split*/ 16));
}
} // namespace

ZendoWindow::ZendoWindow() {
    // R33: título = só o documento — o Qt apensa "- Zendo" (displayName)
    // sozinho; "Zendo — X" virava "Zendo — X - Zendo" na titlebar.
    setWindowTitle(QString());

    m_vp = new Viewport3D(this);
    m_start = new ZendoStartPage(this);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_start);      // índice 0: a porta de entrada
    m_stack->addWidget(m_vp);         // índice 1: o espaço 3D
    setCentralWidget(m_stack);
    connect(m_start, &ZendoStartPage::newRequested, this, [this] {
        // Novo estudo ZERA o mundo (não apenas mostra o espaço corrente)
        if (m_vp->edited()) {
            const auto r = QMessageBox::question(
                this, QStringLiteral("Zendo"),
                QStringLiteral("Há edições não salvas. Salvar antes de "
                               "começar um estudo novo?"),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save);
            if (r == QMessageBox::Cancel) return;
            if (r == QMessageBox::Save) {
                saveStudyDialog();
                if (m_vp->edited()) return;   // cancelou o salvar
            }
        }
        m_doc = makeDoc();
        m_sourcePath.clear();
        m_studyPath.clear();
        m_scenes.clear();
        refreshScenesPanel();                // R16
        m_vp->setPlant(PlantScene{}, 0.0);   // mundo limpo
        m_vp->setGuidesJson(QJsonArray());
        m_vp->setDimsJson(QJsonArray());
        m_vp->setSectionsJson(QJsonArray());
        m_vp->setCompsJson(QJsonArray());
        showSpace();
        m_vp->addScaleFigure();              // R1: a escala humana
        refreshOutliner();
        setWindowTitle(QStringLiteral("Novo estudo"));
        m_info->setText(QStringLiteral(
            "Estudo novo · Ensō-san (1,75 m) dá a escala"));
    });
    connect(m_start, &ZendoStartPage::openStudyRequested, this,
            &ZendoWindow::openStudyDialog);
    connect(m_start, &ZendoStartPage::openPlanRequested, this,
            &ZendoWindow::openDialog);
    connect(m_start, &ZendoStartPage::dismissed, this,
            [this] { showSpace(); });
    connect(m_start, &ZendoStartPage::recentRequested, this,
            [this](const QString& p) {
                if (!QFileInfo::exists(p)) {
                    statusBar()->showMessage(
                        QStringLiteral("Arquivo não encontrado: %1").arg(p),
                        6000);
                    return;
                }
                if (p.endsWith(QLatin1String(".zendo"), Qt::CaseInsensitive))
                    openStudy(p);
                else
                    openFile(p);
            });
    m_start->refresh(recents());

    QMenu* mFile = menuBar()->addMenu(QStringLiteral("&Arquivo"));
    mFile->addAction(QStringLiteral("Tela &inicial"), this,
                     &ZendoWindow::showStart);
    mFile->addSeparator();
    mFile->addAction(QStringLiteral("&Abrir projeto 2D (.zencad)…"),
                     QKeySequence::Open, this, &ZendoWindow::openDialog);
    mFile->addAction(QStringLiteral("Abrir &estudo 3D (.zendo)…"),
                     this, &ZendoWindow::openStudyDialog);
    // R55: Ctrl+S GRAVA. Até aqui ele chamava o saveStudyDialog direto, então
    // todo save pedia o nome e mandava confirmar "substituir" — mesmo com o
    // arquivo aberto e o título da janela já sendo o dele. O dogfooding da R54
    // bateu nisso a cada salvada. Salvar não pergunta nada; quem quer escolher
    // o nome usa Salvar como. (O "…" do rótulo antigo era a promessa de um
    // diálogo que agora não vem — some junto.)
    // A recuperação da R48 fica MELHOR, não pior: lá o m_studyPath é a ORIGEM
    // (o arquivo do usuário, nunca o autosave) e o comentário daquela leva já
    // pedia esta semântica — "Salvar tem que ir pro arquivo do usuário (ou
    // pedir um nome, se ele nunca salvou)". Origem vazia → cai no diálogo.
    mFile->addAction(QStringLiteral("&Salvar estudo 3D"), QKeySequence::Save,
                     this, &ZendoWindow::saveStudyQuick);
    mFile->addAction(QStringLiteral("Salvar &como…"), QKeySequence::SaveAs,
                     this, &ZendoWindow::saveStudyDialog);
    mFile->addAction(QStringLiteral("Salvar como &pacote (leva texturas)…"),
                     this, &ZendoWindow::savePackageDialog);   // R38
    mFile->addSeparator();
    mFile->addAction(QStringLiteral("Exportar gl&TF (Blender, com materiais)…"),
                     this, [this] {
                         const QString f = QFileDialog::getSaveFileName(
                             this, QStringLiteral("Exportar glTF"),
                             bridgeSuggestion(QStringLiteral("Modelo"))
                                 .replace(QStringLiteral(".zencad"),
                                          QStringLiteral(".gltf")),
                             QStringLiteral("glTF 2.0 (*.gltf)"));
                         if (f.isEmpty()) return;
                         const int nf = m_vp->exportGltf(f);
                         statusBar()->showMessage(
                             nf > 0 ? QStringLiteral(
                                          "glTF exportado (%1 triângulos, "
                                          "cores e texturas juntas).")
                                          .arg(nf)
                                    : QStringLiteral("Nada para exportar."),
                             8000);
                     });
    mFile->addAction(QStringLiteral("Exportar &OBJ (Blender)…"), this, [this] {
        const QString f = QFileDialog::getSaveFileName(
            this, QStringLiteral("Exportar OBJ"),
            bridgeSuggestion(QStringLiteral("Modelo")).replace(
                QStringLiteral(".zencad"), QStringLiteral(".obj")),
            QStringLiteral("Wavefront OBJ (*.obj)"));
        if (f.isEmpty()) return;
        const int nf = m_vp->exportObj(f);
        statusBar()->showMessage(
            nf > 0 ? QStringLiteral("OBJ exportado (%1 triângulos) — abra no "
                                    "Blender: File > Import > Wavefront.")
                         .arg(nf)
                   : QStringLiteral("Nada para exportar."),
            8000);
    });
    // R15: o caminho de VOLTA — qualquer móvel da internet entra na cena
    mFile->addAction(
        QStringLiteral("&Importar OBJ (componente)…"), this, [this] {
            const QString f = QFileDialog::getOpenFileName(
                this, QStringLiteral("Importar OBJ"), QString(),
                QStringLiteral("Wavefront OBJ (*.obj)"));
            if (f.isEmpty()) return;
            const QStringList eixos{
                QStringLiteral("Z pra cima (CAD/Zendo)"),
                QStringLiteral("Y pra cima (Blender/internet)")};
            bool ok = false;
            const QString eixo = QInputDialog::getItem(
                this, QStringLiteral("Importar OBJ"),
                QStringLiteral("Qual eixo aponta PRA CIMA no arquivo?"),
                eixos, 1, false, &ok);
            if (!ok) return;
            const double esc = QInputDialog::getDouble(
                this, QStringLiteral("Importar OBJ"),
                QStringLiteral("Fator de escala (1 = arquivo em metros; "
                               "0,01 = em centímetros):"),
                1.0, 0.0001, 1000.0, 4, &ok);
            if (!ok) return;
            const QString nome =
                m_vp->importObjComponent(f, esc, eixo == eixos[1]);
            if (!nome.isEmpty()) m_vp->armInsert(nome);
        });
    mFile->addSeparator();
    mFile->addAction(QStringLiteral("Sai&r"), QKeySequence::Quit,
                     this, &QWidget::close);

    QMenu* mCam = menuBar()->addMenu(QStringLiteral("&Câmera"));
    mCam->addAction(QStringLiteral("Vista &inicial"), QKeySequence(Qt::Key_Home),
                    m_vp, &Viewport3D::resetCamera);
    mCam->addAction(QStringLiteral("En&quadrar tudo (Zoom Extents)"),
                    QKeySequence(QStringLiteral("Shift+Z")), m_vp,
                    &Viewport3D::zoomExtents);

    // R52: VISTAS PADRÃO. O dogfooding da R51 esbarrou nisto: pra pôr uma
    // janela na fachada eu precisava VER a fachada, e o único caminho era
    // orbitar no olho. Todo modelador tem isto; o Zendo não tinha.
    // Valem triplo (razão do Fable pra promover da lista dos "menores"):
    //  1) são QAction → entram SOZINHAS na cola gerada do menu Ajuda;
    //  2) dão um caminho de enquadrar sem depender do botão do MEIO —
    //     quem usa trackpad (e o robô do dogfooding) não consegue orbitar;
    //  3) servem de enquadramento manual pra foto.
    // ATALHO: Ctrl+número, NUNCA número puro — o Zendo tem type-anytime e
    // "1" solto é uma MEDIDA indo pro VCB, não um comando.
    // A convenção sai da viewMatrix: dir=(cos p·cos y, cos p·sin y, sin p),
    // e o olho fica em target+dir·dist — então yaw=-90 (olho no sul) É a
    // fachada frontal, e pitch=89 (o teto do clamp) é a planta.
    QMenu* mVistas = mCam->addMenu(QStringLiteral("&Vistas padrão"));
    struct Vista { const char* nome; const char* tecla; float yaw, pitch; };
    static const Vista kVistas[] = {
        {"&Frente",     "Ctrl+1", -90.0f,  0.0f},
        {"&Trás",       "Ctrl+2",  90.0f,  0.0f},
        {"&Esquerda",   "Ctrl+3", 180.0f,  0.0f},
        {"&Direita",    "Ctrl+4",   0.0f,  0.0f},
        {"T&opo (planta)", "Ctrl+5", -90.0f, 89.0f},
        {"&Isométrica", "Ctrl+6", -135.0f, 35.264f},   // atan(1/√2): a iso real
    };
    for (const Vista& v : kVistas) {
        const float yaw = v.yaw, pitch = v.pitch;
        const QString nome = QString::fromUtf8(v.nome).remove(QLatin1Char('&'));
        mVistas->addAction(QString::fromUtf8(v.nome),
                           QKeySequence(QString::fromLatin1(v.tecla)), this,
                           [this, yaw, pitch, nome] {
                               m_vp->setWalkthrough(false);   // sai da 1ª pessoa
                               m_vp->zoomExtents();  // alvo/raio do bbox AGORA
                               // NÃO usar setCameraPose aqui: ele reescreve
                               // target/dist a partir de m_center/m_radius, que
                               // só são calculados no finishScene (load/rebuild)
                               // — durante a modelagem estão CONGELADOS no
                               // estudo em branco (origem, raio 14). O
                               // zoomExtents acima viraria no-op e a vista
                               // miraria a origem: casa modelada a 20 m sairia
                               // do quadro. Leio o que ele acabou de calcular e
                               // troco só o ÂNGULO.
                               float y0, p0, d0, t[3];
                               m_vp->cameraState(y0, p0, d0, t);
                               m_vp->setCameraState(yaw, pitch, d0, t);
                               statusBar()->showMessage(
                                   QStringLiteral("Vista: %1").arg(nome), 3000);
                           });
    }
    QAction* actOrtho =
        mCam->addAction(QStringLiteral("Projeção &paralela (técnica)"));
    actOrtho->setCheckable(true);
    connect(actOrtho, &QAction::toggled, this,
            [this](bool on) { m_vp->setOrtho(on); });
    QAction* actWalk = mCam->addAction(
        QStringLiteral("&Walkthrough (primeira pessoa)"),
        QKeySequence(Qt::Key_F8));
    actWalk->setCheckable(true);
    connect(actWalk, &QAction::toggled, this,
            [this](bool on) { m_vp->setWalkthrough(on); });
    connect(m_vp, &Viewport3D::walkthroughChanged, actWalk,
            [actWalk](bool on) {           // Esc dentro do walk sincroniza aqui
                QSignalBlocker b(actWalk);
                actWalk->setChecked(on);
            });
    mCam->addAction(QStringLiteral("&Posicionar câmera (clique = ficar de pé)"),
                    QKeySequence(Qt::SHIFT | Qt::Key_F8), m_vp,
                    &Viewport3D::armPositionCamera);
    mCam->addAction(QStringLiteral("Campo de &visão…"), this, [this] {
        bool ok = false;
        const double f = QInputDialog::getDouble(
            this, QStringLiteral("Campo de visão"),
            QStringLiteral("FOV (graus, 15–90; 42 = padrão):"), 42.0, 15.0,
            90.0, 0, &ok);
        if (ok) m_vp->setFov(float(f));
    });
    mCam->addAction(QStringLiteral("&Neblina…"), this, [this] {
        bool ok = false;
        const double d = QInputDialog::getDouble(
            this, QStringLiteral("Neblina"),
            QStringLiteral("Densidade (0 desliga; 0,03 leve · 0,10 fechada):"),
            0.03, 0.0, 0.5, 3, &ok);
        if (ok) m_vp->setFog(d > 1e-6, float(d));
    });
    mCam->addSeparator();
    mCam->addAction(QStringLiteral("S&ol e sombras…"), this, [this] {
        if (m_vp->sunOn()) {
            const auto r = QMessageBox::question(
                this, QStringLiteral("Sol"),
                QStringLiteral("Sombras estão LIGADAS. Ajustar (Yes) ou "
                               "desligar (No)?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            if (r == QMessageBox::Cancel) return;
            if (r == QMessageBox::No) {
                m_vp->setSun(false, 9, 15.0, -3.73);
                return;
            }
        }
        bool ok = false;
        const int month = QInputDialog::getInt(
            this, QStringLiteral("Sol"), QStringLiteral("Mês (1–12):"), 9, 1,
            12, 1, &ok);
        if (!ok) return;
        const double hour = QInputDialog::getDouble(
            this, QStringLiteral("Sol"),
            QStringLiteral("Hora (0–24; 15,5 = 15h30):"), 15.0, 0.0, 24.0, 1,
            &ok);
        if (!ok) return;
        const double lat = QInputDialog::getDouble(
            this, QStringLiteral("Sol"),
            QStringLiteral("Latitude (° — Fortaleza = -3,73):"), -3.73, -90.0,
            90.0, 2, &ok);
        if (ok) m_vp->setSun(true, month, hour, lat);
    });
    mCam->addSeparator();
    mCam->addAction(QStringLiteral("&Salvar cena…"), this, [this] {
        bool ok = false;
        const QString n = QInputDialog::getText(
            this, QStringLiteral("Cena"), QStringLiteral("Nome da cena:"),
            QLineEdit::Normal,
            QStringLiteral("Cena %1").arg(m_scenes.size() + 1), &ok);
        if (!ok || n.trimmed().isEmpty()) return;
        Scene s;
        s.name = n.trimmed();
        m_vp->cameraState(s.yaw, s.pitch, s.dist, s.tgt);
        m_scenes.erase(std::remove_if(m_scenes.begin(), m_scenes.end(),
                                      [&](const Scene& x) {
                                          return x.name == s.name;
                                      }),
                       m_scenes.end());
        m_scenes.push_back(s);
        refreshScenesPanel();                              // R16
        statusBar()->showMessage(
            QStringLiteral("Cena \"%1\" salva (vai junto no .zendo).").arg(s.name),
            4000);
    });
    mCam->addAction(QStringLiteral("Ir para &cena…"), this, [this] {
        if (m_scenes.empty()) {
            statusBar()->showMessage(QStringLiteral("Nenhuma cena salva."), 3000);
            return;
        }
        QStringList names;
        for (const Scene& s : m_scenes) names << s.name;
        bool ok = false;
        const QString n = QInputDialog::getItem(
            this, QStringLiteral("Cenas"), QStringLiteral("Ir para:"), names, 0,
            false, &ok);
        if (!ok) return;
        for (const Scene& s : m_scenes)
            if (s.name == n)                 // G6: transição suave de cena
                m_vp->animateCameraTo(s.yaw, s.pitch, s.dist, s.tgt);
    });
    mCam->addSeparator();                    // R36: o Fotógrafo
    mCam->addAction(QStringLiteral("&Render fotorrealista…"), this,
                    &ZendoWindow::renderDialog);

    QMenu* mModel = menuBar()->addMenu(QStringLiteral("&Modelo"));
    mModel->addAction(QStringLiteral("Desenhar &retângulo na face"),
                      QKeySequence(Qt::Key_R), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Rect);
                      });
    mModel->addAction(QStringLiteral("Desenhar &linha (divide a face)"),
                      QKeySequence(Qt::Key_L), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Line);
                      });
    mModel->addAction(QStringLiteral("Desenhar círcul&o na face/chão"),
                      QKeySequence(Qt::Key_O), this, [this] {
                          m_vp->setPolySides(0);   // círculo liso de novo
                          m_vp->setTool(Viewport3D::Tool::Circle);
                      });
    mModel->addAction(QStringLiteral("&Empurrar/Puxar (arrastar)"),
                      QKeySequence(Qt::Key_P), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Pull);
                      });
    mModel->addAction(QStringLiteral("Empurrar/Puxar por &valor…"),
                      this, &ZendoWindow::pushPullDialog);
    mModel->addAction(QStringLiteral("Offset da &face (interativo)"),
                      QKeySequence(Qt::Key_F), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Offset);
                      });
    mModel->addAction(
        QStringLiteral("Escala da te&xtura da seleção…"), this, [this] {
            const double cur = m_vp->textureScaleSelected();
            if (cur < 0) {
                QMessageBox::information(
                    this, QStringLiteral("Escala da textura"),
                    QStringLiteral("Selecione antes um sólido (ou face) que "
                                   "já esteja vestido com textura."));
                return;
            }
            bool ok = false;
            const double v = QInputDialog::getDouble(
                this, QStringLiteral("Escala da textura"),
                QStringLiteral("Metros por tile (1.0 = tijolo em escala "
                               "real):"),
                cur, 0.05, 100.0, 2, &ok);
            if (ok) m_vp->setTextureScaleSelected(v);
        });
    mModel->addAction(QStringLiteral("Trans&feridor (girar interativo)"),
                      QKeySequence(Qt::Key_K), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Rotate);
                      });
    mModel->addAction(QStringLiteral("Escala viva (&Z)"),
                      QKeySequence(Qt::Key_Z), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Scale);
                      });
    mModel->addAction(QStringLiteral("&Arco (2 pontos + curvar)"),
                      QKeySequence(Qt::Key_A), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Arc);
                      });
    mModel->addAction(QStringLiteral("Pin&tar (balde)"),
                      QKeySequence(Qt::Key_T), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Paint);
                      });
    mModel->addAction(QStringLiteral("Balde com cor personali&zada…"),
                      this, [this] {
                          const QColor c = QColorDialog::getColor(
                              QColor(194, 160, 99), this,
                              QStringLiteral("Cor do balde"));
                          if (c.isValid())
                              m_vp->setActiveColor(float(c.redF()),
                                                   float(c.greenF()),
                                                   float(c.blueF()));
                      });
    mModel->addAction(QStringLiteral("&Mover sólido"), QKeySequence(Qt::Key_M),
                      this, [this] { m_vp->startMoveCopy(false); });
    mModel->addAction(QStringLiteral("&Copiar sólido"), QKeySequence(Qt::Key_C),
                      this, [this] { m_vp->startMoveCopy(true); });
    mModel->addAction(QStringLiteral("Mo&ver sólido em Z…"),
                      QKeySequence(Qt::Key_V), this, [this] {
                          if (!m_vp->hasSelection()) {
                              statusBar()->showMessage(
                                  QStringLiteral("Selecione um sólido "
                                                 "(duplo clique) primeiro."),
                                  4000);
                              return;
                          }
                          bool ok = false;
                          const double dz = QInputDialog::getDouble(
                              this, QStringLiteral("Mover em Z"),
                              QStringLiteral("Subir (m) — negativo desce "
                                             "(ex.: 3,10 põe a laje sobre as "
                                             "paredes):"),
                              3.10, -100.0, 100.0, 2, &ok);
                          if (ok) m_vp->moveSelectedZ(dz);
                      });
    mModel->addAction(QStringLiteral("Tel&hado (sobre a seleção)…"),
                      this, [this] {
                          bool ok = false;
                          const QString kind = QInputDialog::getItem(
                              this, QStringLiteral("Telhado"),
                              QStringLiteral("Estilo:"),
                              {QStringLiteral("Duas águas"),
                               QStringLiteral("Quatro águas")},
                              0, false, &ok);
                          if (!ok) return;
                          const double h = QInputDialog::getDouble(
                              this, QStringLiteral("Telhado"),
                              QStringLiteral("Altura da cumeeira (m):"), 1.20,
                              0.1, 20.0, 2, &ok);
                          if (!ok) return;
                          const double b = QInputDialog::getDouble(
                              this, QStringLiteral("Telhado"),
                              QStringLiteral("Beiral (m):"), 0.50, 0.0, 5.0, 2,
                              &ok);
                          if (ok)
                              m_vp->roofSelected(
                                  h, b, kind.contains(QLatin1String("Quatro")));
                      });
    mModel->addAction(QStringLiteral("Terre&no (platô sob o modelo)"),
                      this, [this] { m_vp->addTerrain(0.30, 3.0); });
    mModel->addAction(QStringLiteral("&Grudar sólido no que toca"),
                      QKeySequence(Qt::Key_G), this,
                      [this] { m_vp->glueSelected(); });
    mModel->addAction(QStringLiteral("&Apagar (sólido ou aresta)"),
                      QKeySequence::Delete, this,
                      [this] { m_vp->deleteSelected(); });
    mModel->addAction(QStringLiteral("&Borracha (arrastar apaga arestas)"),
                      QKeySequence(Qt::Key_E), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Erase);
                      });
    mModel->addAction(QStringLiteral("Fita métrica (mede + &guia)"),
                      QKeySequence(Qt::Key_Q), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Tape);
                      });
    mModel->addAction(QStringLiteral("&Cota (3 cliques — mede e persiste)"),
                      QKeySequence(Qt::Key_D), this, [this] {
                          m_vp->setTool(Viewport3D::Tool::Dim);
                      });
    mModel->addAction(QStringLiteral("Polígono de &N lados na face/chão…"),
                      this, [this] {
                          bool ok = false;
                          const int n = QInputDialog::getInt(
                              this, QStringLiteral("Polígono"),
                              QStringLiteral("Lados (3–64):"), 6, 3, 64, 1,
                              &ok);
                          if (!ok) return;
                          m_vp->setPolySides(n);
                          m_vp->setTool(Viewport3D::Tool::Circle);
                          statusBar()->showMessage(
                              QStringLiteral("Polígono de %1 lados: centro + "
                                             "raio, como o círculo.").arg(n),
                              6000);
                      });
    mModel->addAction(QStringLiteral("Seguir caminho (Follow &Me)"),
                      this, [this] { m_vp->followMe(); });
    mModel->addAction(QStringLiteral("Follow Me pelo PERÍ&METRO da face"),
                      this, [this] { m_vp->armFollowPerimeter(); });
    mModel->addSeparator();               // R17: a OFICINA
    mModel->addAction(QStringLiteral("Inspecionar sólidos (furos)"),
                      this, [this] { m_vp->inspectSolids(); });
    mModel->addAction(QStringLiteral("Consertar sólido (tampar furos)"),
                      this, [this] { m_vp->fixSelectedSolid(); });
    mModel->addAction(QStringLiteral("Limpar modelo (coplanares + purge)"),
                      this, [this] { m_vp->cleanupModel(); });
    QMenu* mEsp = mModel->addMenu(QStringLiteral("Espelhar seleção"));
    mEsp->addAction(QStringLiteral("No eixo &X"), this,
                    [this] { m_vp->mirrorSelected(0); });
    mEsp->addAction(QStringLiteral("No eixo &Y"), this,
                    [this] { m_vp->mirrorSelected(1); });
    mEsp->addAction(QStringLiteral("No eixo &Z"), this,
                    [this] { m_vp->mirrorSelected(2); });
    mModel->addAction(QStringLiteral("Subtrair (booleana — corta o menor do maior)"),
                      this, [this] { m_vp->subtractSelected(); });
    mModel->addAction(QStringLiteral("Unir (booleana — 2 prismas de eixo paralelo)"),
                      this, [this] { m_vp->uniteSelected(); });
    mModel->addSeparator();
    mModel->addAction(QStringLiteral("Criar GR&UPO da seleção…"),
                      QKeySequence(QStringLiteral("Ctrl+G")), this, [this] {
                          bool ok = false;
                          const QString n = QInputDialog::getText(
                              this, QStringLiteral("Grupo"),
                              QStringLiteral("Nome do grupo:"),
                              QLineEdit::Normal, QStringLiteral("Grupo 1"),
                              &ok);
                          if (ok) m_vp->makeGroup(n);
                      });
    mModel->addAction(QStringLiteral("&Escada…"), this, [this] {   // R41
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Escada"));
        QFormLayout* form = new QFormLayout(&dlg);
        QDoubleSpinBox* sw = new QDoubleSpinBox(&dlg);
        sw->setRange(0.3, 5.0);  sw->setValue(0.90);  sw->setSuffix(" m");
        QDoubleSpinBox* sh = new QDoubleSpinBox(&dlg);
        sh->setRange(0.4, 12.0); sh->setValue(m_vp->wallHeight());
        sh->setSuffix(" m");
        QDoubleSpinBox* sr = new QDoubleSpinBox(&dlg);
        sr->setRange(0.15, 0.60); sr->setValue(0.27); sr->setSuffix(" m");
        form->addRow(QStringLiteral("Largura:"), sw);
        form->addRow(QStringLiteral("Desnível a vencer:"), sh);
        form->addRow(QStringLiteral("Piso do degrau:"), sr);
        QDialogButtonBox* bb = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        bb->button(QDialogButtonBox::Ok)
            ->setText(QStringLiteral("Posicionar…"));
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(bb);
        if (dlg.exec() == QDialog::Accepted)
            m_vp->armStair(sw->value(), sh->value(), sr->value());
    });
    mModel->addAction(QStringLiteral("Guarda-cor&po…"), this, [this] {   // R43
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Guarda-corpo"));
        QFormLayout* form = new QFormLayout(&dlg);
        QDoubleSpinBox* sh = new QDoubleSpinBox(&dlg);
        sh->setRange(0.5, 2.0);  sh->setValue(1.10);  sh->setSuffix(" m");
        QDoubleSpinBox* sg = new QDoubleSpinBox(&dlg);
        sg->setRange(0.5, 3.0);  sg->setValue(1.50);  sg->setSuffix(" m");
        form->addRow(QStringLiteral("Altura:"), sh);
        form->addRow(QStringLiteral("Vão entre montantes:"), sg);
        QDialogButtonBox* bb = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        bb->button(QDialogButtonBox::Ok)
            ->setText(QStringLiteral("Posicionar…"));
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(bb);
        if (dlg.exec() == QDialog::Accepted)
            m_vp->armGuard(sh->value(), sg->value());
    });
    mModel->addAction(QStringLiteral("Laje com a&bertura…"), this, [this] {
        QDialog dlg(this);                                             // R43
        dlg.setWindowTitle(QStringLiteral("Laje com abertura"));
        QFormLayout* form = new QFormLayout(&dlg);
        QDoubleSpinBox* sz = new QDoubleSpinBox(&dlg);
        sz->setRange(0.2, 30.0); sz->setValue(m_vp->wallHeight());
        sz->setSuffix(" m");
        QDoubleSpinBox* st = new QDoubleSpinBox(&dlg);
        st->setRange(0.05, 1.0); st->setValue(0.15); st->setSuffix(" m");
        form->addRow(QStringLiteral("Cota do topo:"), sz);
        form->addRow(QStringLiteral("Espessura:"), st);
        QDialogButtonBox* bb = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        bb->button(QDialogButtonBox::Ok)
            ->setText(QStringLiteral("Posicionar…"));
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(bb);
        if (dlg.exec() == QDialog::Accepted)
            m_vp->armSlabHole(sz->value(), st->value());
    });
    mModel->addAction(QStringLiteral("Desa&grupar"),
                      this, [this] { m_vp->ungroupSelected(); });
    mModel->addAction(QStringLiteral("Sair do conte&xto"),
                      this, [this] { m_vp->exitContext(); });
    mModel->addAction(QStringLiteral("Atribuir TA&G à seleção…"),
                      this, [this] {
                          bool ok = false;
                          const QString n = QInputDialog::getText(
                              this, QStringLiteral("Tag"),
                              QStringLiteral("Nome da tag (ex.: Telhado, "
                                             "Hidráulica):"),
                              QLineEdit::Normal, QString(), &ok);
                          if (ok) m_vp->assignTag(n);
                      });
    mModel->addAction(QStringLiteral("&Desfazer edição 3D"),
                      QKeySequence::Undo, this, [this] {
                          if (!m_vp->undoLast())
                              statusBar()->showMessage(
                                  QStringLiteral("Nada para desfazer."), 3000);
                      });
    mModel->addAction(QStringLiteral("Re&fazer edição 3D"),
                      QKeySequence::Redo, this, [this] {
                          if (!m_vp->redoLast())
                              statusBar()->showMessage(
                                  QStringLiteral("Nada para refazer."), 3000);
                      });
    mModel->addSeparator();
    mModel->addAction(QStringLiteral("Ocultar sólido"), QKeySequence(Qt::Key_H),
                      this, [this] { m_vp->hideSelected(); });
    mModel->addAction(QStringLiteral("Isolar sólido"),
                      QKeySequence(QStringLiteral("Shift+H")), this,
                      [this] { m_vp->isolateSelected(); });
    mModel->addAction(QStringLiteral("Mostrar tudo"),
                      QKeySequence(QStringLiteral("Ctrl+H")), this,
                      [this] { m_vp->showAll(); });
    mModel->addSeparator();
    mModel->addAction(QStringLiteral("&Pé-direito…"),
                      this, &ZendoWindow::heightDialog);

    QMenu* mComp = menuBar()->addMenu(QStringLiteral("Co&mponente"));
    mComp->addAction(QStringLiteral("&Criar do sólido selecionado…"),
                     this, [this] {
                         bool ok = false;
                         const QString n = QInputDialog::getText(
                             this, QStringLiteral("Componente"),
                             QStringLiteral("Nome:"), QLineEdit::Normal,
                             QStringLiteral("JANELA"), &ok);
                         if (ok) m_vp->makeComponent(n.trimmed());
                     });
    mComp->addAction(QStringLiteral("&Inserir…"), this, [this] {
        const QStringList names = m_vp->componentNames();
        if (names.isEmpty()) {
            statusBar()->showMessage(
                QStringLiteral("Crie um componente primeiro."), 4000);
            return;
        }
        bool ok = false;
        const QString n = QInputDialog::getItem(
            this, QStringLiteral("Inserir componente"),
            QStringLiteral("Qual:"), names, 0, false, &ok);
        if (!ok) return;
        m_vp->armInsert(n);
        statusBar()->showMessage(
            QStringLiteral("Clique no modelo para posicionar \"%1\".").arg(n),
            6000);
    });
    mComp->addAction(QStringLiteral("&Redefinir a partir do selecionado"),
                     this, [this] { m_vp->redefineComponent(); });

    // OUTLINER: dock "Estrutura" — lista viva dos sólidos
    QDockWidget* dock = new QDockWidget(QStringLiteral("Estrutura"), this);
    dock->setObjectName(QStringLiteral("estrutura"));
    m_outliner = new QListWidget(dock);
    dock->setWidget(m_outliner);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();
    QMenu* mView = menuBar()->addMenu(QStringLiteral("&Ver"));
    mView->addAction(QStringLiteral("&Tags (visibilidade)…"), this, [this] {
        const QStringList tags = m_vp->allTags();
        if (tags.isEmpty()) {
            statusBar()->showMessage(
                QStringLiteral("Nenhuma tag ainda — Modelo → Atribuir tag."),
                5000);
            return;
        }
        bool ok = false;
        const QString t = QInputDialog::getItem(
            this, QStringLiteral("Tags"),
            QStringLiteral("Alternar visibilidade da tag:"), tags, 0, false,
            &ok);
        if (!ok) return;
        const auto r = QMessageBox::question(
            this, QStringLiteral("Tag %1").arg(t),
            QStringLiteral("Mostrar (Yes) ou ocultar (No) os sólidos da "
                           "tag \"%1\"?").arg(t),
            QMessageBox::Yes | QMessageBox::No);
        m_vp->setTagVisible(t, r == QMessageBox::Yes);
    });
    QAction* actDay = mView->addAction(
        QStringLiteral("Ambiente AMANHECER (claro p/ criar)"));
    actDay->setCheckable(true);
    actDay->setChecked(true);            // R5: o ateliê nasce iluminado
    connect(actDay, &QAction::toggled, this,
            [this](bool on) { m_vp->setDay(on); });
    mView->addSeparator();
    {   // estilos de exibição
        QActionGroup* grp = new QActionGroup(this);
        const QStringList nomes{QStringLiteral("Estilo &normal"),
                                QStringLiteral("Estilo &monocromático"),
                                QStringLiteral("Estilo raio-&x")};
        for (int i = 0; i < 3; ++i) {
            QAction* a = mView->addAction(nomes[i]);
            a->setCheckable(true);
            a->setChecked(i == 0);
            grp->addAction(a);
            connect(a, &QAction::triggered, this,
                    [this, i] { m_vp->setStyle(i); });
        }
        mView->addSeparator();
        mView->addAction(QStringLiteral("&Seção ao vivo…"), this, [this] {
            bool ok = false;
            const QString k = QInputDialog::getItem(
                this, QStringLiteral("Seção ao vivo"),
                QStringLiteral("Plano (ou desligar):"),
                {QStringLiteral("Y (transversal)"),
                 QStringLiteral("X (longitudinal)"),
                 QStringLiteral("Desligar")},
                0, false, &ok);
            if (!ok) return;
            if (k.startsWith(QLatin1String("Des"))) {
                m_vp->setClip(false, 'Y', 0);
                return;
            }
            const double pos = QInputDialog::getDouble(
                this, QStringLiteral("Seção"),
                QStringLiteral("Posição do plano (m):"), 5.0, -1e6, 1e6, 2,
                &ok);
            if (ok)
                m_vp->setClip(true, k.startsWith('Y') ? 'Y' : 'X', pos);
        });
        mView->addAction(QStringLiteral("Seção na &face (clicar)…"), this,
                         [this] { m_vp->armClipFace(); });
        mView->addAction(QStringLiteral("&Deslizar última seção (arrastar)…"),
                         this, [this] { m_vp->armClipDrag(); });   // R34
        mView->addAction(QStringLiteral("&Remover última seção"), this,
                         [this] { m_vp->removeLastSection(); });
        mView->addAction(QStringLiteral("Aplicar &textura na seleção…"),
                         QKeySequence(Qt::Key_X), this, [this] {
                             const QString f = QFileDialog::getOpenFileName(
                                 this, QStringLiteral("Textura"), QString(),
                                 QStringLiteral(
                                     "Imagens (*.png *.jpg *.jpeg *.bmp)"));
                             if (f.isEmpty()) return;
                             bool ok = false;
                             const double s = QInputDialog::getDouble(
                                 this, QStringLiteral("Textura"),
                                 QStringLiteral("Tamanho do ladrilho (m):"),
                                 1.0, 0.05, 50.0, 2, &ok);
                             if (ok) m_vp->applyTexture(f, s);
                         });
        mView->addSeparator();
    }
    QAction* actDock = dock->toggleViewAction();
    actDock->setText(QStringLiteral("&Estrutura (outliner)"));
    actDock->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    mView->addAction(actDock);
    m_outlinerDebounce = new QTimer(this);
    m_outlinerDebounce->setSingleShot(true);
    m_outlinerDebounce->setInterval(250);
    connect(m_outlinerDebounce, &QTimer::timeout, this,
            &ZendoWindow::refreshOutliner);
    connect(m_vp, &Viewport3D::structureChanged, this,
            [this] { m_outlinerDebounce->start(); });
    connect(m_outliner, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* it) {
                if (m_outlinerBusy) return;
                const int i = it->data(Qt::UserRole).toInt();
                if (i >= 0)
                    m_vp->setPartHidden(i, it->checkState() == Qt::Unchecked);
            });
    connect(m_outliner, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* it) {
                if (m_outlinerBusy) return;
                const int i = it->data(Qt::UserRole).toInt();
                if (i >= 0) m_vp->selectPart(i);
            });

    // R5/R9/R12: PALETA DE MATERIAIS — duas gavetas, agora com CATEGORIAS:
    // TINTAS (48 cores, combo de família) e TEXTURAS (combo de categoria
    // + busca — 150+ materiais pedem filtro, não rolagem)
    QDockWidget* matDock =
        new QDockWidget(QStringLiteral("Materiais"), this);
    matDock->setObjectName(QStringLiteral("materiais"));
    QTabWidget* matTabs = new QTabWidget(matDock);
    auto mkMatList = [&] {
        QListWidget* l = new QListWidget;
        l->setViewMode(QListView::IconMode);
        l->setIconSize(QSize(34, 34));
        l->setGridSize(QSize(44, 44));
        l->setResizeMode(QListView::Adjust);
        l->setMovement(QListView::Static);
        return l;
    };
    m_materials = mkMatList();
    m_materialsTex = mkMatList();
    m_matFam = new QComboBox;
    m_texFam = new QComboBox;
    m_texBusca = new QLineEdit;
    m_texBusca->setPlaceholderText(QStringLiteral("buscar…"));
    m_texBusca->setClearButtonEnabled(true);
    auto mkPage = [&](QWidget* topo1, QWidget* topo2, QListWidget* lista) {
        QWidget* page = new QWidget(matTabs);
        QVBoxLayout* v = new QVBoxLayout(page);
        v->setContentsMargins(4, 4, 4, 4);
        v->setSpacing(4);
        if (topo2) {
            QHBoxLayout* h = new QHBoxLayout;
            h->setSpacing(4);
            h->addWidget(topo1, 1);
            h->addWidget(topo2, 1);
            v->addLayout(h);
        } else {
            v->addWidget(topo1);
        }
        v->addWidget(lista, 1);
        return page;
    };
    matTabs->addTab(mkPage(m_matFam, nullptr, m_materials),
                    QStringLiteral("Tintas"));
    matTabs->addTab(mkPage(m_texFam, m_texBusca, m_materialsTex),
                    QStringLiteral("Texturas"));
    // R14: a aba do MOBILIÁRIO — clique arma, clique na cena posiciona
    m_moveis = mkMatList();
    m_moveis->setIconSize(QSize(52, 52));
    m_moveis->setGridSize(QSize(62, 62));
    matTabs->addTab(m_moveis, QStringLiteral("Móveis"));
    matDock->setWidget(matTabs);
    addDockWidget(Qt::RightDockWidgetArea, matDock);
    connect(m_matFam, &QComboBox::currentIndexChanged, this,
            [this] { refreshMaterials(); });
    connect(m_texFam, &QComboBox::currentIndexChanged, this,
            [this] { refreshMaterials(); });
    connect(m_texBusca, &QLineEdit::textChanged, this,
            [this] { refreshMaterials(); });
    // R9: a biblioteca nasce no disco (só o que faltar) e entra na paleta
    for (const auto& [nome, arq] : matlib::ensureLibrary())
        m_vp->addLibraryTexture(nome, arq);
    // R10: as FOTOGRÁFICAS — pack CC0 embutido no app (assets/materiais)
    {
        const QDir foto(QCoreApplication::applicationDirPath() +
                        QStringLiteral("/assets/materiais"));
        for (const QFileInfo& fi : foto.entryInfoList(
                 {QStringLiteral("*.jpg"), QStringLiteral("*.png")},
                 QDir::Files, QDir::Name))
            m_vp->addLibraryTexture(fi.completeBaseName(),
                                    fi.absoluteFilePath());
    }
    // R14: o mobiliário de fábrica entra na biblioteca de componentes
    for (const moblib::Movel& mv : moblib::gerarTodos()) {
        m_vp->addLibraryComponent(mv.nome, mv.mesh, mv.cores, mv.organico);
        QListWidgetItem* it = new QListWidgetItem(movelThumb(mv), QString());
        it->setToolTip(QStringLiteral("%1 — clique aqui e depois clique na "
                                      "cena para posicionar")
                           .arg(mv.nome));
        it->setData(Qt::UserRole, 3);
        it->setData(Qt::UserRole + 1, mv.nome);
        m_moveis->addItem(it);
    }
    connect(m_moveis, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* it) {
                const QString nome = it->data(Qt::UserRole + 1).toString();
                m_vp->armInsert(nome);
                m_selInfo->setText(
                    QStringLiteral("\"%1\" na mão — clique no chão ou numa "
                                   "face para posicionar (Esc solta).")
                        .arg(nome));
                m_vp->setFocus();
            });
    refreshMaterials();
    const auto armaMaterial = [this](QListWidgetItem* it) {
        const int kind = it->data(Qt::UserRole).toInt();
        // R6: a paleta ARMA O BALDE — daí é só clicar nas faces
        if (kind == 0) {
            const QColor c = it->data(Qt::UserRole + 1).value<QColor>();
            m_vp->setActiveColor(float(c.redF()), float(c.greenF()),
                                 float(c.blueF()));
        } else if (kind == 1) {
            m_vp->setActiveTexture(it->data(Qt::UserRole + 1).toString(),
                                   1.0);
        } else {
            textureDialog();
        }
    };
    connect(m_materials, &QListWidget::itemClicked, this, armaMaterial);
    connect(m_materialsTex, &QListWidget::itemClicked, this, armaMaterial);
    // R13: duplo-clique numa textura pergunta a ESCALA e arma o balde nela
    connect(m_materialsTex, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* it) {
                if (it->data(Qt::UserRole).toInt() != 1) return;
                static double ultima = 1.0;
                bool ok = false;
                const double v = QInputDialog::getDouble(
                    this, QStringLiteral("Escala da textura"),
                    QStringLiteral("Metros por tile:"), ultima, 0.05, 100.0,
                    2, &ok);
                if (!ok) return;
                ultima = v;
                m_vp->setActiveTexture(
                    it->data(Qt::UserRole + 1).toString(), v);
            });
    mView->addAction(QStringLiteral("&Materiais (paleta)"),
                     QKeySequence(QStringLiteral("Ctrl+M")), matDock,
                     [matDock] { matDock->setVisible(!matDock->isVisible()); });

    // R16: A BANDEJA — os painéis vivos (Info, Sol, Estilo, Cenas)
    QDockWidget* trayDock = new QDockWidget(QStringLiteral("Bandeja"), this);
    trayDock->setObjectName(QStringLiteral("bandeja"));
    QScrollArea* trayScroll = new QScrollArea(trayDock);
    trayScroll->setWidgetResizable(true);
    trayScroll->setFrameShape(QFrame::NoFrame);
    QWidget* trayCol = new QWidget;
    QVBoxLayout* tv = new QVBoxLayout(trayCol);
    tv->setContentsMargins(10, 4, 10, 10);   // R33: respiro nas seções
    tv->setSpacing(4);                        // (o fôlego vem do margin-top
                                              //  do cabeçalho-com-fio)
    // (1) INFO DA ENTIDADE — vivo, acompanha a seleção
    QGroupBox* gInfo = new QGroupBox(QStringLiteral("Informações"));
    QVBoxLayout* vi = new QVBoxLayout(gInfo);
    QLabel* lInfo = new QLabel(QStringLiteral("Nada selecionado"));
    // SEM wordWrap: label hfw dentro de QGroupBox colapsa pra 1 linha (o
    // groupbox não propaga heightForWidth) — as linhas do Info são curtas
    lInfo->setWordWrap(false);
    vi->addWidget(lInfo);
    connect(m_vp, &Viewport3D::entityInfo, lInfo, &QLabel::setText);
    tv->addWidget(gInfo);
    // (2) SOL E SOMBRAS — a hora do dia num arrasto
    QGroupBox* gSol = new QGroupBox(QStringLiteral("Sol e sombras"));
    QVBoxLayout* vs = new QVBoxLayout(gSol);
    QCheckBox* cSol = new QCheckBox(QStringLiteral("Sombras ligadas"));
    QLabel* lHora = new QLabel(QStringLiteral("12:00"));
    QSlider* sHora = new QSlider(Qt::Horizontal);
    sHora->setRange(12, 36);                     // meias-horas: 6h às 18h
    sHora->setValue(24);
    QComboBox* cMes = new QComboBox;
    cMes->addItems({QStringLiteral("Janeiro"), QStringLiteral("Fevereiro"),
                    QStringLiteral("Março"), QStringLiteral("Abril"),
                    QStringLiteral("Maio"), QStringLiteral("Junho"),
                    QStringLiteral("Julho"), QStringLiteral("Agosto"),
                    QStringLiteral("Setembro"), QStringLiteral("Outubro"),
                    QStringLiteral("Novembro"), QStringLiteral("Dezembro")});
    cMes->setCurrentIndex(6);
    const auto aplicaSol = [this, cSol, sHora, cMes, lHora] {
        const double hora = sHora->value() / 2.0;
        lHora->setText(QStringLiteral("%1:%2")
                           .arg(int(hora), 2, 10, QLatin1Char('0'))
                           .arg(sHora->value() % 2 ? QStringLiteral("30")
                                                   : QStringLiteral("00")));
        m_vp->setSun(cSol->isChecked(), cMes->currentIndex() + 1, hora,
                     -3.73);                     // Fortaleza
    };
    connect(cSol, &QCheckBox::toggled, this, aplicaSol);
    connect(sHora, &QSlider::valueChanged, this, aplicaSol);
    connect(cMes, &QComboBox::currentIndexChanged, this, aplicaSol);
    QHBoxLayout* hs = new QHBoxLayout;
    hs->addWidget(sHora, 1);
    hs->addWidget(lHora);
    vs->addWidget(cSol);
    vs->addLayout(hs);
    vs->addWidget(cMes);
    tv->addWidget(gSol);
    // (3) ESTILO E AMBIENTE
    QGroupBox* gEst = new QGroupBox(QStringLiteral("Estilo e ambiente"));
    QVBoxLayout* ve = new QVBoxLayout(gEst);
    QComboBox* cEst = new QComboBox;
    cEst->addItems({QStringLiteral("Normal (cores)"),
                    QStringLiteral("Monocromático (washi)"),
                    QStringLiteral("Raio-X (translúcido)")});
    connect(cEst, &QComboBox::currentIndexChanged, this,
            [this](int i) { m_vp->setStyle(i); });
    QCheckBox* cDia = new QCheckBox(QStringLiteral("Ambiente Amanhecer"));
    cDia->setChecked(true);
    connect(cDia, &QCheckBox::toggled, this,
            [this](bool on) { m_vp->setDay(on); });
    QCheckBox* cOrto = new QCheckBox(QStringLiteral("Câmera paralela"));
    connect(cOrto, &QCheckBox::toggled, this,
            [this](bool on) { m_vp->setOrtho(on); });
    QCheckBox* cNebl = new QCheckBox(QStringLiteral("Neblina"));
    QSlider* sNebl = new QSlider(Qt::Horizontal);
    sNebl->setRange(1, 100);
    sNebl->setValue(25);
    const auto aplicaNebl = [this, cNebl, sNebl] {
        m_vp->setFog(cNebl->isChecked(), float(sNebl->value()) * 0.002f);
    };
    connect(cNebl, &QCheckBox::toggled, this, aplicaNebl);
    connect(sNebl, &QSlider::valueChanged, this, aplicaNebl);
    ve->addWidget(cEst);
    ve->addWidget(cDia);
    ve->addWidget(cOrto);
    ve->addWidget(cNebl);
    ve->addWidget(sNebl);
    tv->addWidget(gEst);
    // (4) CENAS — capturar e voltar num duplo-clique
    QGroupBox* gCen = new QGroupBox(QStringLiteral("Cenas"));
    QVBoxLayout* vc = new QVBoxLayout(gCen);
    m_scenesList = new QListWidget;
    m_scenesList->setMaximumHeight(120);
    QPushButton* bCena =
        new QPushButton(QStringLiteral("＋ capturar esta vista"));
    connect(bCena, &QPushButton::clicked, this, [this] {
        Scene s;
        s.name = QStringLiteral("Cena %1").arg(m_scenes.size() + 1);
        m_vp->cameraState(s.yaw, s.pitch, s.dist, s.tgt);
        m_scenes.push_back(s);
        refreshScenesPanel();
        statusBar()->showMessage(
            QStringLiteral("\"%1\" capturada (vai junto no .zendo).")
                .arg(s.name),
            5000);
    });
    connect(m_scenesList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* it) {
                for (const Scene& s : m_scenes)
                    if (s.name == it->text())
                        m_vp->setCameraState(s.yaw, s.pitch, s.dist, s.tgt);
            });
    vc->addWidget(m_scenesList);
    vc->addWidget(bCena);
    tv->addWidget(gCen);
    tv->addStretch(1);
    trayScroll->setWidget(trayCol);
    trayDock->setWidget(trayScroll);
    addDockWidget(Qt::RightDockWidgetArea, trayDock);
    mView->addAction(QStringLiteral("&Bandeja (painéis)"),
                     QKeySequence(QStringLiteral("Ctrl+B")), trayDock,
                     [trayDock] {
                         trayDock->setVisible(!trayDock->isVisible());
                     });

    QMenu* mBridge = menuBar()->addMenu(QStringLiteral("P&onte"));
    mBridge->addAction(QStringLiteral("Exportar &ELEVAÇÃO → .zencad…"),
                       this, &ZendoWindow::elevationDialog);
    mBridge->addAction(QStringLiteral("Exportar &CORTE → .zencad…"),
                       this, &ZendoWindow::sectionDialog);

    // R48: AJUDA — 47 levas de features e nenhum lugar que dissesse "como se
    // usa isto". O que nenhum tooltip carrega são os VERBOS INVISÍVEIS: que dá
    // pra DIGITAR a medida, que os pontos coloridos significam algo, que Ctrl
    // muda o que a ferramenta faz. Nada disso é descobrível olhando.
    QMenu* mHelp = menuBar()->addMenu(QStringLiteral("Aj&uda"));
    mHelp->addAction(QStringLiteral("&Primeiros passos"),
                     QKeySequence(Qt::Key_F1), this,
                     [this] { primeirosPassos(true); });
    mHelp->addAction(QStringLiteral("&Atalhos do teclado…"), this, [this] {
        QMessageBox mb(this);
        mb.setWindowTitle(QStringLiteral("Atalhos"));
        mb.setTextFormat(Qt::RichText);
        mb.setText(colaDeAtalhos());
        mb.exec();
    });
    mHelp->addSeparator();
    mHelp->addAction(QStringLiteral("&Sobre o Zendo"), this, [this] {
        QMessageBox::about(
            this, QStringLiteral("Sobre o Zendo"),
            QStringLiteral(
                "<b>Zendo %1</b><br>O espaço 3D do ecossistema Zen.<br><br>"
                "Modelador para arquitetura — irmão do ZenCAD (2D).<br>"
                "Render fotorrealista pelo Blender (GPL, processo separado).<br>"
                "Texturas CC0 do ambientCG · céu CC0 do Poly Haven.<br><br>"
                "<i>Guilherme · Grupo Christus</i>")
                .arg(QCoreApplication::applicationVersion()));
    });

    m_info = new QLabel(this);
    m_selInfo = new QLabel(this);
    m_selInfo->setStyleSheet(QStringLiteral("color: #c2a063;"));   // latão
    statusBar()->addWidget(m_info);
    statusBar()->addWidget(m_selInfo);
    // R33: dica CONTEXTUAL da ferramenta ativa — a cola de atalhos inteira
    // saiu do rodapé (os atalhos moram nos tooltips e nos menus).
    QLabel* hint = new QLabel(this);
    hint->setObjectName(QStringLiteral("toolHint"));
    statusBar()->addPermanentWidget(hint);
    connect(m_vp, &Viewport3D::pickInfo, m_selInfo, &QLabel::setText);
    // R33: MEDIDAS sempre à vista — o type-anytime ganhou um chip com rótulo,
    // como a Measurements box do SketchUp (vazio mostra o traço).
    QLabel* vcbCap = new QLabel(QStringLiteral("MEDIDAS"), this);
    vcbCap->setObjectName(QStringLiteral("vcbCaption"));
    QLabel* vcb = new QLabel(QStringLiteral("—"), this);
    vcb->setObjectName(QStringLiteral("vcbChip"));
    vcb->setAlignment(Qt::AlignCenter);
    statusBar()->addPermanentWidget(vcbCap);
    statusBar()->addPermanentWidget(vcb);
    connect(m_vp, &Viewport3D::vcbText, this, [vcb](const QString& t) {
        vcb->setText(t.isEmpty() ? QStringLiteral("—") : t);
    });
    connect(m_vp, &Viewport3D::pickInfo, this, [this](const QString& t) {
        if (!t.isEmpty()) m_lastInfo = t;      // p/ o dump de QA
    });

    // TOOLBAR lateral — a mão do Zendo, no traço Sumi & Washi.
    // R33: DUAS COLUNAS (o "large tool set"): as 25 ações sempre visíveis,
    // ícones grandes, zero overflow escondido. O QToolBar vira só o host de
    // um grid de QToolButtons ligados às MESMAS QActions.
    QToolBar* tb = new QToolBar(QStringLiteral("Ferramentas"), this);
    tb->setObjectName(QStringLiteral("ferramentas"));
    tb->setMovable(false);
    addToolBar(Qt::LeftToolBarArea, tb);
    QWidget* tbGrid = new QWidget(tb);
    QGridLayout* tbGl = new QGridLayout(tbGrid);
    tbGl->setContentsMargins(3, 4, 3, 4);
    tbGl->setSpacing(2);
    int tbSlot = 0;
    auto tbAdd = [&](QAction* a) {
        QToolButton* b = new QToolButton(tbGrid);
        b->setDefaultAction(a);
        b->setIconSize(QSize(26, 26));
        tbGl->addWidget(b, tbSlot / 2, tbSlot % 2, Qt::AlignHCenter);
        ++tbSlot;
    };
    auto tbGap = [&] {                     // fio de cluster (linha inteira)
        if (tbSlot % 2) ++tbSlot;
        QFrame* f = new QFrame(tbGrid);
        f->setObjectName(QStringLiteral("tbSep"));
        f->setFixedHeight(1);
        tbGl->addWidget(f, tbSlot / 2, 0, 1, 2);
        tbSlot += 2;
    };
    auto tbAct = [&](const char* icon, const QString& tip,
                     std::function<void()> fn) {
        QAction* a = new QAction(zenIcon(icon), tip, this);
        connect(a, &QAction::triggered, this, std::move(fn));
        tbAdd(a);
        return a;
    };
    tb->addWidget(tbGrid);
    auto toolAct = [&](const char* icon, const QString& tip,
                       Viewport3D::Tool t) {
        QAction* a = new QAction(zenIcon(icon), tip, this);
        a->setCheckable(true);
        a->setData(int(t));
        connect(a, &QAction::triggered, this, [this, t] { m_vp->setTool(t); });
        tbAdd(a);
        return a;
    };
    QList<QAction*> toolActs;
    toolActs << toolAct("sel", QStringLiteral("Selecionar (Esc)"),
                        Viewport3D::Tool::Select)
             << toolAct("pencil",
                        QStringLiteral("Lápis / Linha — divide faces e "
                                       "desenha no terreno (L)"),
                        Viewport3D::Tool::Line)
             << toolAct("rect", QStringLiteral("Retângulo (R)"),
                        Viewport3D::Tool::Rect)
             << toolAct("circle", QStringLiteral("Círculo (O)"),
                        Viewport3D::Tool::Circle)
             << toolAct("pull", QStringLiteral("Empurrar/Puxar (P)"),
                        Viewport3D::Tool::Pull)
             << toolAct("erase",
                        QStringLiteral("Borracha — arraste sobre arestas (E)"),
                        Viewport3D::Tool::Erase);
    toolActs.first()->setChecked(true);
    // CURSOR POR FERRAMENTA — a ponta ATIVA coincide com o hotspot: o lápis
    // clica com a PONTA do grafite; formas usam mira fina + ícone-crachá
    // (o glifo de inferência nasce exatamente na mira).
    auto mkCur = [](const char* k) {
        if (qstrcmp(k, "pencil") == 0)
            return QCursor(zenIcon(k).pixmap(26, 26), 5, 21);  // ponta
        if (qstrcmp(k, "move") == 0)
            return QCursor(zenIcon(k).pixmap(26, 26), 13, 13); // centro da cruz
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPen halo(QColor(16, 18, 22, 200), 3.2);   // contorno sumi: legível
        halo.setCapStyle(Qt::RoundCap);            // sobre qualquer face
        p.setPen(halo);
        p.drawLine(8, 2, 8, 14);
        p.drawLine(2, 8, 14, 8);
        QPen ink(QColor(0xdc, 0xd6, 0xc8), 1.4);
        p.setPen(ink);
        p.drawLine(8, 2, 8, 14);
        p.drawLine(2, 8, 14, 8);
        p.drawPixmap(15, 15, zenIcon(k).pixmap(16, 16));   // crachá
        p.end();
        return QCursor(pm, 8, 8);                  // hotspot NA MIRA
    };
    const std::map<int, QCursor> curs{
        {int(Viewport3D::Tool::Select), QCursor(Qt::ArrowCursor)},
        {int(Viewport3D::Tool::Line), mkCur("pencil")},
        {int(Viewport3D::Tool::Rect), mkCur("rect")},
        {int(Viewport3D::Tool::Circle), mkCur("circle")},
        {int(Viewport3D::Tool::Pull), mkCur("pull")},
        {int(Viewport3D::Tool::Erase), mkCur("erase")},
        {int(Viewport3D::Tool::Tape), mkCur("tape")},
        {int(Viewport3D::Tool::Paint), mkCur("paint")},
        {int(Viewport3D::Tool::Arc), mkCur("arc")},
        {int(Viewport3D::Tool::Rotate), mkCur("rotate")},
        {int(Viewport3D::Tool::Scale), mkCur("scale")},
        {int(Viewport3D::Tool::Offset), mkCur("offset")},
        {int(Viewport3D::Tool::Move), mkCur("move")}};
    // R33: uma dica por ferramenta — só o que importa AGORA, no rodapé
    const std::map<int, QString> hints{
        {int(Viewport3D::Tool::Select),
         QStringLiteral("1×: face/aresta · 2×: sólido · 3×: conectado · "
                        "arraste: janela")},
        {int(Viewport3D::Tool::Line),
         QStringLiteral("clique a clique · circuito fechado vira face · "
                        "digite a medida")},
        {int(Viewport3D::Tool::Rect),
         QStringLiteral("2 cliques · digite as medidas exatas")},
        {int(Viewport3D::Tool::Circle),
         QStringLiteral("centro + raio · digite o raio")},
        {int(Viewport3D::Tool::Move),
         QStringLiteral("pegue e solte · setas travam eixo · digite a "
                        "distância")},
        {int(Viewport3D::Tool::Pull),
         QStringLiteral("arraste a face · digite a distância · Ctrl empilha")},
        {int(Viewport3D::Tool::Erase),
         QStringLiteral("arraste sobre as arestas")},
        {int(Viewport3D::Tool::Tape),
         QStringLiteral("2 pontos medem e criam guia · digite pra calibrar")},
        {int(Viewport3D::Tool::Paint),
         QStringLiteral("clique pinta a face · Ctrl: sólido · Alt: "
                        "conta-gotas")},
        {int(Viewport3D::Tool::Arc),
         QStringLiteral("2 pontos + curvar · digite o raio")},
        {int(Viewport3D::Tool::Rotate),
         QStringLiteral("centro, braço e ângulo · Ctrl copia · digite os "
                        "graus")},
        {int(Viewport3D::Tool::Scale),
         QStringLiteral("2 cliques · digite o fator")},
        {int(Viewport3D::Tool::Offset),
         QStringLiteral("arraste a borda da face · digite a distância")},
        {int(Viewport3D::Tool::Dim),
         QStringLiteral("2 pontos (ou 1 clique na aresta) + posicionar · "
                        "Ctrl no 1º clique: angular")}};
    connect(m_vp, &Viewport3D::toolChanged, this,
            [this, toolActs, curs, hints, hint](int t) {
        // R55: ferramenta armada na TELA INICIAL puxa o espaço pra frente.
        // O dogfooding da R54 apertou R na start page e o rodapé anunciou
        // "clique o 1º canto sobre uma face — ou no CHÃO": não existe chão
        // ali, e os cliques morriam no widget da tela inicial. Os atalhos são
        // QAction de janela — disparam com a start page na frente.
        // Trazer o espaço (em vez de bloquear o atalho) é o padrão que a casa
        // já tem: openStudy/openFile também fazem showSpace() ao concluir —
        // "ação que opera no espaço traz o espaço". Bloquear seria um 3º
        // padrão. Select fica de fora: Esc e todo desarme emitem
        // toolChanged(Select) e não podem arrancar ninguém da tela inicial.
        if (t != int(Viewport3D::Tool::Select) &&
            m_stack && m_stack->currentWidget() != m_vp)
            showSpace();
        for (QAction* a : toolActs) a->setChecked(a->data().toInt() == t);
        const auto it = curs.find(t);
        if (it != curs.end()) m_vp->setCursor(it->second);
        const auto ih = hints.find(t);
        hint->setText(ih != hints.end() ? ih->second : QString());
    });
    hint->setText(hints.at(int(Viewport3D::Tool::Select)));
    tbGap();
    tbAct("offset", QStringLiteral("Offset interativo (F)"),
          [this] { m_vp->setTool(Viewport3D::Tool::Offset); });
    tbAct("move", QStringLiteral("Mover sólido (M)"),
          [this] { m_vp->startMoveCopy(false); });
    tbAct("copy", QStringLiteral("Copiar sólido (C)"),
          [this] { m_vp->startMoveCopy(true); });
    tbAct("rotate", QStringLiteral("Transferidor — girar interativo (K)"),
          [this] { m_vp->setTool(Viewport3D::Tool::Rotate); });
    tbAct("scale", QStringLiteral("Escala viva — fator no gesto (Z)"),
          [this] { m_vp->setTool(Viewport3D::Tool::Scale); });
    tbAct("arc", QStringLiteral("Arco — 2 pontos + curvar (A)"),
          [this] { m_vp->setTool(Viewport3D::Tool::Arc); });
    tbAct("paint", QStringLiteral("Balde de pintura (T)"),
          [this] { m_vp->setTool(Viewport3D::Tool::Paint); });
    tbAct("glue", QStringLiteral("Grudar no que toca (G)"),
          [this] { m_vp->glueSelected(); });
    tbAct("hide", QStringLiteral("Ocultar sólido (H)"),
          [this] { m_vp->hideSelected(); });
    tbGap();
    tbAct("roof", QStringLiteral("Telhado sobre a seleção…"), [this] {
        bool ok = false;
        const double h = QInputDialog::getDouble(
            this, QStringLiteral("Telhado"),
            QStringLiteral("Cumeeira (m):"), 1.20, 0.1, 20.0, 2, &ok);
        if (ok) m_vp->roofSelected(h, 0.5, false);
    });
    tbAct("tree", QStringLiteral("Estrutura (Ctrl+E)"),
          [actDock] { actDock->trigger(); });
    QAction* actScene = mCam->actions().at(2);
    tbAct("scene", QStringLiteral("Salvar cena da câmera…"),
          [actScene] { actScene->trigger(); });
    // R5: a bancada completa — o que era só menu/atalho agora tem rosto
    tbGap();
    tbAct("tape", QStringLiteral("Fita métrica + guia (Q)"),
          [this] { m_vp->setTool(Viewport3D::Tool::Tape); });
    tbAct("tex", QStringLiteral("Textura na seleção…"),
          [this] { textureDialog(); });
    tbAct("poly", QStringLiteral("Polígono de N lados…"), [this] {
        bool ok = false;
        const int n = QInputDialog::getInt(
            this, QStringLiteral("Polígono"),
            QStringLiteral("Lados (3–64):"), 6, 3, 64, 1, &ok);
        if (!ok) return;
        m_vp->setPolySides(n);
        m_vp->setTool(Viewport3D::Tool::Circle);
    });
    tbAct("follow",
          QStringLiteral("Follow Me (perfil pelo caminho do lápis)"),
          [this] { m_vp->followMe(); });
    tbAct("grupo", QStringLiteral("Criar grupo da seleção (Ctrl+G)"), [this] {
        bool ok = false;
        const QString n = QInputDialog::getText(
            this, QStringLiteral("Grupo"),
            QStringLiteral("Nome do grupo:"), QLineEdit::Normal,
            QStringLiteral("Grupo 1"), &ok);
        if (ok) m_vp->makeGroup(n);
    });
    tbGap();
    tbAct("undo", QStringLiteral("Desfazer (Ctrl+Z)"),
          [this] { m_vp->undoLast(); });
    tbAct("redo", QStringLiteral("Refazer (Ctrl+Y)"),
          [this] { m_vp->redoLast(); });

    // R48: AUTOSAVE a cada 3 min — agora ele DEVOLVE o trabalho.
    // Até aqui ele era teatro: gravava <estudo>.zendo~ que NINGUÉM lia (o
    // arquivo não era aberto em lugar nenhum do projeto), e o `return` quando
    // não havia caminho deixava justamente o usuário mais exposto — o que
    // abriu o app, modelou 2h e nunca salvou — com proteção ZERO, enquanto a
    // barra de status anunciava "Autosave:" na cara dele.
    QTimer* autosave = new QTimer(this);
    connect(autosave, &QTimer::timeout, this, [this] { fazerAutosave(); });
    autosave->start(3 * 60 * 1000);

    m_doc = makeDoc();
    m_vp->setPlant(PlantScene{}, 0.0);   // R3: mundo vazio, sem 2D no coração
}

void ZendoWindow::setQaSides(int n) { m_vp->setPolySides(n); }

// R5: textura na seleção (toolbar/paleta) — arquivo + escala do ladrilho
void ZendoWindow::textureDialog() {
    const QString f = QFileDialog::getOpenFileName(
        this, QStringLiteral("Textura"), QString(),
        QStringLiteral("Imagens (*.png *.jpg *.jpeg *.bmp)"));
    if (f.isEmpty()) return;
    bool ok = false;
    const double s = QInputDialog::getDouble(
        this, QStringLiteral("Textura"),
        QStringLiteral("Tamanho do ladrilho (m):"), 1.0, 0.05, 50.0, 2, &ok);
    if (!ok) return;
    m_vp->applyTexture(f, s);
    refreshMaterials();
}

// R14: thumbnail ISOMÉTRICA de um móvel — projeta as faces (câmera no canto
// +X+Y+Z), pinta de trás pra frente com a cor da face sombreada pela normal.
QIcon movelThumb(const moblib::Movel& mv) {
    const int S = 52;
    QImage im(S, S, QImage::Format_ARGB32_Premultiplied);
    im.fill(Qt::transparent);
    QPainter pt(&im);
    pt.setRenderHint(QPainter::Antialiasing);
    const cad::HalfEdgeMesh& m = mv.mesh;
    using Idx = cad::HalfEdgeMesh::Idx;
    const double c30 = 0.866, s30 = 0.5;
    auto proj = [&](const cad::Point3& w) {
        return QPointF(c30 * (w.x - w.y), -(w.z + s30 * (w.x + w.y)));
    };
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (std::size_t v = 0; v < m.vertexCount(); ++v) {
        const QPointF q = proj(m.vertex(Idx(v)).p);
        x0 = std::min(x0, q.x()); x1 = std::max(x1, q.x());
        y0 = std::min(y0, q.y()); y1 = std::max(y1, q.y());
    }
    const double esc =
        (S - 8) / std::max(1e-6, std::max(x1 - x0, y1 - y0));
    const QPointF off((S - (x1 + x0) * esc) / 2.0,
                      (S - (y1 + y0) * esc) / 2.0);
    struct FD { double d; Idx f; };
    std::vector<FD> ordem;
    for (std::size_t f = 0; f < m.faceCount(); ++f) {
        const cad::Point3 c = m.faceCentroid(Idx(f));
        ordem.push_back({c.x + c.y + c.z, Idx(f)});
    }
    std::sort(ordem.begin(), ordem.end(),
              [](const FD& a, const FD& b) { return a.d < b.d; });
    for (const FD& fd : ordem) {
        const cad::Vec3 n = m.faceNormal(fd.f);
        if (n.x + n.y + n.z <= 0.0) continue;      // costas
        QPolygonF poly;
        for (const Idx v : m.faceVertices(fd.f))
            poly << proj(m.vertex(v).p) * esc + off;
        const auto it = mv.cores.find(fd.f);
        const std::array<float, 3> cor =
            it != mv.cores.end() ? it->second
                                 : std::array<float, 3>{0.9f, 0.88f, 0.83f};
        const double lit =
            0.55 + 0.45 * std::max(0.0, n.x * 0.30 - n.y * 0.45 + n.z * 0.84);
        pt.setPen(QPen(QColor(46, 42, 36, 150), 0.8));
        pt.setBrush(QColor(int(cor[0] * 255 * lit), int(cor[1] * 255 * lit),
                           int(cor[2] * 255 * lit)));
        pt.drawPolygon(poly);
    }
    return QIcon(QPixmap::fromImage(im));
}

// R16: a lista de cenas da Bandeja espelha m_scenes
void ZendoWindow::refreshScenesPanel() {
    if (!m_scenesList) return;
    m_scenesList->clear();
    for (const Scene& s : m_scenes) m_scenesList->addItem(s.name);
}

// R12: categoria de uma textura — nomes curados têm mapa; o padrão da
// varredura ("Categoria NNN") cai no regex; o resto é textura do usuário.
static QString categoriaDe(const QString& nome) {
    static const QHash<QString, QString> kMapa = {
        {QStringLiteral("Tijolo aparente"), QStringLiteral("Tijolo")},
        {QStringLiteral("Tijolo branco"), QStringLiteral("Tijolo")},
        {QStringLiteral("Tijolo rústico"), QStringLiteral("Tijolo")},
        {QStringLiteral("Madeira clara"), QStringLiteral("Madeira")},
        {QStringLiteral("Madeira escura"), QStringLiteral("Madeira")},
        {QStringLiteral("Madeira de demolição"), QStringLiteral("Madeira")},
        {QStringLiteral("Deck"), QStringLiteral("Madeira")},
        {QStringLiteral("Parquet carvalho"), QStringLiteral("Piso madeira")},
        {QStringLiteral("Concreto liso"), QStringLiteral("Concreto")},
        {QStringLiteral("Concreto queimado"), QStringLiteral("Concreto")},
        {QStringLiteral("Concreto aparente"), QStringLiteral("Concreto")},
        {QStringLiteral("Reboco"), QStringLiteral("Reboco")},
        {QStringLiteral("Reboco branco"), QStringLiteral("Reboco")},
        {QStringLiteral("Pedra"), QStringLiteral("Rocha")},
        {QStringLiteral("Ardósia"), QStringLiteral("Telha")},
        {QStringLiteral("Telha cerâmica"), QStringLiteral("Telha")},
        {QStringLiteral("Telha de barro"), QStringLiteral("Telha")},
        {QStringLiteral("Telha metálica"), QStringLiteral("Telha metálica")},
        {QStringLiteral("Piso branco"), QStringLiteral("Ladrilho")},
        {QStringLiteral("Porcelanato"), QStringLiteral("Ladrilho")},
        {QStringLiteral("Piso travertino"), QStringLiteral("Travertino")},
        {QStringLiteral("Mármore"), QStringLiteral("Mármore")},
        {QStringLiteral("Mármore cinza"), QStringLiteral("Mármore")},
        {QStringLiteral("Grama"), QStringLiteral("Grama")},
        {QStringLiteral("Gramado"), QStringLiteral("Grama")},
        {QStringLiteral("Areia"), QStringLiteral("Terra")},
        {QStringLiteral("Terra batida"), QStringLiteral("Terra")},
        {QStringLiteral("Aço escovado"), QStringLiteral("Metal")},
        {QStringLiteral("Chapa de aço"), QStringLiteral("Metal")},
        {QStringLiteral("Asfalto"), QStringLiteral("Asfalto")},
        {QStringLiteral("Brita"), QStringLiteral("Brita")},
        {QStringLiteral("Pedra portuguesa"), QStringLiteral("Calçamento")},
        {QStringLiteral("Carpete bege"), QStringLiteral("Carpete")},
    };
    const auto it = kMapa.find(nome);
    if (it != kMapa.end()) return it.value();
    static const QRegularExpression kNum(
        QStringLiteral("^(.+)\\s+\\d+[A-Za-z]?$"));
    const QRegularExpressionMatch m = kNum.match(nome);
    if (m.hasMatch()) return m.captured(1);
    return QStringLiteral("Minhas texturas");
}

// R12: repovoa um combo de filtro preservando a escolha (sem loop de sinal)
static void repovoaFiltro(QComboBox* cb, const QString& todas,
                          const QStringList& itens) {
    if (!cb) return;
    const QString atual = cb->currentText();
    cb->blockSignals(true);
    cb->clear();
    cb->addItem(todas);
    cb->addItems(itens);
    const int ix = cb->findText(atual);
    if (ix > 0) cb->setCurrentIndex(ix);
    cb->blockSignals(false);
}

// R5/R9/R12: a PALETA DE MATERIAIS — tintas por família + texturas por
// categoria, com filtros vivos
void ZendoWindow::refreshMaterials() {
    if (!m_materials) return;
    m_materials->clear();
    struct C { const char* fam; const char* nome; int r, g, b; };
    static const C kCores[] = {
        // Washi & Cal
        {"Washi & Cal", "Washi", 234, 228, 214},
        {"Washi & Cal", "Cal", 245, 242, 235},
        {"Washi & Cal", "Linho", 226, 217, 196},
        {"Washi & Cal", "Marfim", 240, 233, 210},
        {"Washi & Cal", "Cru", 216, 203, 175},
        {"Washi & Cal", "Areia", 206, 186, 150},
        // Terras
        {"Terras", "Terracota", 161, 102, 79},
        {"Terras", "Tijolo", 150, 74, 58},
        {"Terras", "Barro", 178, 124, 92},
        {"Terras", "Ferrugem", 138, 72, 44},
        {"Terras", "Ocre", 190, 142, 74},
        {"Terras", "Caramelo", 172, 120, 60},
        // Madeiras
        {"Madeiras", "Pinho", 196, 168, 124},
        {"Madeiras", "Carvalho", 158, 122, 82},
        {"Madeiras", "Madeira", 122, 84, 53},
        {"Madeiras", "Nogueira", 96, 66, 44},
        {"Madeiras", "Ipê", 78, 52, 36},
        {"Madeiras", "Ébano", 52, 38, 30},
        // Verdes
        {"Verdes", "Sálvia", 112, 143, 127},
        {"Verdes", "Musgo", 85, 106, 71},
        {"Verdes", "Oliva", 122, 124, 72},
        {"Verdes", "Verde-chá", 158, 172, 130},
        {"Verdes", "Floresta", 58, 82, 58},
        {"Verdes", "Eucalipto", 130, 158, 142},
        // Azuis
        {"Azuis", "Azul-cinza", 94, 125, 150},
        {"Azuis", "Índigo", 56, 70, 102},
        {"Azuis", "Petróleo", 44, 86, 94},
        {"Azuis", "Céu", 148, 178, 198},
        {"Azuis", "Ardósia-azul", 92, 104, 120},
        {"Azuis", "Noite", 36, 44, 60},
        // Quentes
        {"Quentes", "Latão", 194, 160, 99},
        {"Quentes", "Latão claro", 213, 183, 128},
        {"Quentes", "Cobre", 152, 94, 62},
        {"Quentes", "Vinho", 108, 52, 50},
        {"Quentes", "Mostarda", 196, 158, 66},
        {"Quentes", "Coral", 196, 110, 88},
        // Cinzas & Concretos
        {"Cinzas & Concretos", "Prata", 200, 202, 204},
        {"Cinzas & Concretos", "Concreto", 176, 174, 168},
        {"Cinzas & Concretos", "Cimento", 154, 152, 146},
        {"Cinzas & Concretos", "Zinco", 138, 144, 148},
        {"Cinzas & Concretos", "Chumbo", 108, 108, 106},
        {"Cinzas & Concretos", "Grafite", 76, 76, 76},
        // Sumi & Profundos
        {"Sumi & Profundos", "Sumi", 46, 42, 36},
        {"Sumi & Profundos", "Café", 62, 48, 38},
        {"Sumi & Profundos", "Berinjela", 74, 56, 72},
        {"Sumi & Profundos", "Verde-sumi", 44, 52, 44},
        {"Sumi & Profundos", "Preto-azulado", 24, 28, 34},
        {"Sumi & Profundos", "Carvão", 34, 32, 30},
    };
    auto swatch = [](const QColor& c) {
        QPixmap px(34, 34);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(60, 56, 50), 1));
        p.setBrush(c);
        p.drawRoundedRect(1, 1, 32, 32, 4, 4);
        return QIcon(px);
    };
    QStringList fams;
    for (const C& c : kCores)
        if (!fams.contains(QString::fromUtf8(c.fam)))
            fams.append(QString::fromUtf8(c.fam));
    repovoaFiltro(m_matFam, QStringLiteral("Todas as famílias"), fams);
    const QString famSel = m_matFam && m_matFam->currentIndex() > 0
                               ? m_matFam->currentText()
                               : QString();
    for (const C& c : kCores) {
        if (!famSel.isEmpty() && QString::fromUtf8(c.fam) != famSel)
            continue;
        QListWidgetItem* it =
            new QListWidgetItem(swatch(QColor(c.r, c.g, c.b)), QString());
        it->setToolTip(QStringLiteral("%1 · %2")
                           .arg(QString::fromUtf8(c.nome),
                                QString::fromUtf8(c.fam)));
        it->setData(Qt::UserRole, 0);
        it->setData(Qt::UserRole + 1, QColor(c.r, c.g, c.b));
        m_materials->addItem(it);
    }
    if (!m_materialsTex) return;
    m_materialsTex->clear();
    const QStringList nomes = m_vp->textureNames();
    QStringList cats;
    for (const QString& n : nomes) {
        const QString c = categoriaDe(n);
        if (!cats.contains(c)) cats.append(c);
    }
    std::sort(cats.begin(), cats.end(), [](const QString& a,
                                           const QString& b) {
        return a.localeAwareCompare(b) < 0;
    });
    repovoaFiltro(m_texFam, QStringLiteral("Todas as categorias"), cats);
    const QString catSel = m_texFam && m_texFam->currentIndex() > 0
                               ? m_texFam->currentText()
                               : QString();
    const QString busca = m_texBusca ? m_texBusca->text().trimmed()
                                     : QString();
    for (const QString& n : nomes) {
        if (!catSel.isEmpty() && categoriaDe(n) != catSel) continue;
        if (!busca.isEmpty() && !n.contains(busca, Qt::CaseInsensitive) &&
            !categoriaDe(n).contains(busca, Qt::CaseInsensitive))
            continue;
        const QImage img = m_vp->textureImage(n);
        QListWidgetItem* it = new QListWidgetItem(
            QIcon(QPixmap::fromImage(img.scaled(34, 34, Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation))),
            QString());
        it->setToolTip(QStringLiteral("%1 · %2").arg(n, categoriaDe(n)));
        it->setData(Qt::UserRole, 1);
        it->setData(Qt::UserRole + 1, m_vp->texturePath(n));
        m_materialsTex->addItem(it);
    }
    QListWidgetItem* add = new QListWidgetItem(QStringLiteral("＋"));
    add->setToolTip(QStringLiteral("Adicionar textura…"));
    add->setData(Qt::UserRole, 2);
    m_materialsTex->addItem(add);
}

bool ZendoWindow::openFile(const QString& path) {
    auto doc = makeDoc();
    QString err;
    if (!loadProject(path, *doc, m_layouts, m_styles, m_settings, &err)) {
        QMessageBox::warning(this, QStringLiteral("Zendo"),
                             QStringLiteral("Não foi possível abrir:\n%1").arg(err));
        return false;
    }
    m_doc = std::move(doc);
    // R3: o conector traduz a planta num pacote neutro — o viewport nunca
    // vê entidades 2D
    m_vp->setPlant(importPlant(*m_doc, m_vp->wallHeight()),
                   m_vp->wallHeight());
    m_sourcePath = QFileInfo(path).absoluteFilePath();
    m_studyPath.clear();
    setWindowTitle(QStringLiteral("%1")
                       .arg(QFileInfo(path).completeBaseName()));
    m_info->setText(QStringLiteral("%1 · %2 parede(s) · pé-direito %3 m")
                        .arg(QFileInfo(path).fileName())
                        .arg(m_vp->wallCount())
                        .arg(m_vp->wallHeight(), 0, 'f', 2));
    addRecent(m_sourcePath);
    showSpace();
    return true;
}

// ---------------------------------------------------------------------------
//  Tela inicial: recentes (QSettings) e a troca stack <-> espaço
// ---------------------------------------------------------------------------
void ZendoWindow::showSpace() { m_stack->setCurrentWidget(m_vp); }

void ZendoWindow::showStart() {
    m_start->refresh(recents());
    m_stack->setCurrentWidget(m_start);
}

QStringList ZendoWindow::recents() const {
    return QSettings(QStringLiteral("Zen"), QStringLiteral("Zendo"))
        .value(QStringLiteral("recentes")).toStringList();
}

void ZendoWindow::addRecent(const QString& path) {
    if (path.isEmpty()) return;
    QStringList r = recents();
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > 12) r.removeLast();
    QSettings(QStringLiteral("Zen"), QStringLiteral("Zendo"))
        .setValue(QStringLiteral("recentes"), r);
}

// ---------------------------------------------------------------------------
//  Documento de estudo (.zendo)
// ---------------------------------------------------------------------------
void ZendoWindow::refreshOutliner() {
    if (!m_outliner || !m_outliner->isVisible()) return;
    m_outlinerBusy = true;
    m_outliner->clear();
    auto addPart = [&](int i, bool indent) {
        QListWidgetItem* it = new QListWidgetItem(
            (indent ? QStringLiteral("      ") : QString()) +
            m_vp->partLabel(i));
        it->setData(Qt::UserRole, i);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(m_vp->partHidden(i) ? Qt::Unchecked : Qt::Checked);
        m_outliner->addItem(it);
    };
    // G5: hierarquia — grupos primeiro (cabeçalho 📦 + membros), soltos depois
    QStringList groups;
    for (int i = 0; i < m_vp->partCount(); ++i) {
        const QString g = m_vp->partGroup(i);
        if (!g.isEmpty() && !groups.contains(g)) groups.append(g);
    }
    for (const QString& g : groups) {
        QListWidgetItem* hd =
            new QListWidgetItem(QStringLiteral("📦 %1").arg(g));
        hd->setData(Qt::UserRole, -1);
        hd->setFlags(Qt::ItemIsEnabled);
        m_outliner->addItem(hd);
        for (int i = 0; i < m_vp->partCount(); ++i)
            if (m_vp->partGroup(i) == g) addPart(i, true);
    }
    for (int i = 0; i < m_vp->partCount(); ++i)
        if (m_vp->partGroup(i).isEmpty()) addPart(i, false);
    m_outlinerBusy = false;
}

QJsonArray ZendoWindow::scenesJson() const {
    QJsonArray arr;
    for (const Scene& s : m_scenes)
        arr.append(QJsonObject{
            {"name", s.name}, {"yaw", s.yaw}, {"pitch", s.pitch},
            {"dist", s.dist},
            {"target", QJsonArray{s.tgt[0], s.tgt[1], s.tgt[2]}}});
    return arr;
}

void ZendoWindow::setScenesJson(const QJsonArray& arr) {
    m_scenes.clear();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Scene s;
        s.name = o.value("name").toString();
        s.yaw = float(o.value("yaw").toDouble());
        s.pitch = float(o.value("pitch").toDouble());
        s.dist = float(o.value("dist").toDouble());
        const QJsonArray t = o.value("target").toArray();
        s.tgt[0] = float(t.at(0).toDouble());
        s.tgt[1] = float(t.at(1).toDouble());
        s.tgt[2] = float(t.at(2).toDouble());
        if (!s.name.isEmpty()) m_scenes.push_back(s);
    }
    refreshScenesPanel();                              // R16
}

// grava o estudo SEM mexer em título/estado — usado pelo save e pelo autosave
bool ZendoWindow::writeStudyFile(const QString& path) {
    QJsonObject root;
    root["app"] = QStringLiteral("Zendo");
    root["version"] = 1;
    if (!m_sourcePath.isEmpty())
        root["source"] =
            QDir(QFileInfo(path).absolutePath()).relativeFilePath(m_sourcePath);
    root["wallHeight"] = m_vp->wallHeight();
    float yaw = 0, pitch = 0, dist = 0, tgt[3] = {0, 0, 0};
    m_vp->cameraState(yaw, pitch, dist, tgt);
    root["camera"] = QJsonObject{{"yaw", yaw}, {"pitch", pitch}, {"dist", dist},
                                 {"target", QJsonArray{tgt[0], tgt[1], tgt[2]}}};
    root["meshes"] = m_vp->studyMeshes();
    root["sketch"] = m_vp->sketchJson();
    root["guides"] = m_vp->guidesJson();       // fita métrica (G4)
    root["dimensions"] = m_vp->dimsJson();     // R26: cotas 3D persistentes
    root["sections"] = m_vp->sectionsJson();   // R32: seções persistem
    root["day"] = m_vp->day();                 // ambiente (R5)
    root["components"] = m_vp->compsJson();
    root["scenes"] = scenesJson();
    root["textures"] =
        m_vp->texturesJson(QFileInfo(path).absolutePath());
    // Miniatura embutida (JPEG base64) — a tela inicial mostra o card.
    if (m_vp->isVisible()) {
        const QImage snap = m_vp->grabFramebuffer().scaledToWidth(
            512, Qt::SmoothTransformation);
        if (!snap.isNull()) {
            QByteArray jpg;
            QBuffer buf(&jpg);
            buf.open(QIODevice::WriteOnly);
            snap.save(&buf, "JPG", 82);
            root["thumb"] = QString::fromLatin1(jpg.toBase64());
        }
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

// ===========================================================================
//  R48: RECUPERAÇÃO — o autosave que devolve o trabalho.
//  Mora em %APPDATA%/Zendo/recuperacao (mesmo padrão do materiais/), NUNCA na
//  pasta do usuário: o autosave não é obra dele e não pode sujar nem tocar o
//  arquivo dele. Cobre o estudo nunca salvo, que era o desprotegido.
// ===========================================================================
// R48: a raiz é INJETÁVEL — mesma forma do findBlender(raizUnica) da R47, e
// pela mesma razão (que eu deixei passar aqui): sem isto, `--qa-limpeza`
// apagava o autosave REAL da máquina e `--qa-autosave` gravava a cena de
// teste por cima do trabalho de verdade. O QA nunca pode tocar no que é do
// usuário — e provas medidas numa pasta compartilhada não provam nada.
QString ZendoWindow::s_recDir;
void ZendoWindow::setPastaRecuperacao(const QString& d) { s_recDir = d; }

QString ZendoWindow::pastaRecuperacao() {
    if (!s_recDir.isEmpty()) return s_recDir;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/recuperacao");
}
QString ZendoWindow::arqSentinela() {
    return pastaRecuperacao() + QStringLiteral("/sessao.aberta");
}

// A função REAL do autosave — o timer chama, o QA chama a MESMA (lição
// R27/R28: flag que simula o resultado não prova o caminho).
QString ZendoWindow::fazerAutosave() {
    if (!m_vp->edited()) return QString();
    if (!m_lock && !m_qaAutosave) return QString();   // sem o dono, não escreve
    const QString dir = pastaRecuperacao();
    if (!QDir().mkpath(dir)) return QString();
    const QString arq = dir + QStringLiteral("/auto.zendo");
    // ESCRITA ATÔMICA (mesmo precedente do .stage+rename da R47): truncar o
    // destino direto significa que uma queda NO MEIO da gravação deixa o
    // único autosave bom truncado. E a janela não é pequena por acaso — o
    // writeStudyFile começa por grabFramebuffer(): se quem derruba o app é o
    // driver de vídeo, a queda cai preferencialmente DENTRO do autosave.
    const QString tmp = arq + QStringLiteral(".tmp");
    if (!writeStudyFile(tmp)) return QString();
    QFile::remove(arq);
    if (!QFile::rename(tmp, arq)) {
        QFile::remove(tmp);
        return QString();
    }
    // de quem é este trabalho? (vazio = estudo que nunca foi salvo)
    QFile o(dir + QStringLiteral("/auto.origem"));
    if (o.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        o.write(m_studyPath.toUtf8());
    statusBar()->showMessage(
        QStringLiteral("Trabalho protegido às %1")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm"))),
        2500);
    return arq;
}

// DECISÃO pura e testável: 0 = nada a recuperar · 1 = oferecer · 2 = o
// autosave é mais VELHO que o arquivo salvo (obsoleto — descartar).
// A regra do 2 é o que separa recuperação de bug: oferecer restaurar algo
// mais velho que o disco é pior que não oferecer nada.
int ZendoWindow::avaliarRecuperacao(QString& arq, QString& origem) const {
    arq = pastaRecuperacao() + QStringLiteral("/auto.zendo");
    origem.clear();
    if (!QFileInfo::exists(arqSentinela())) return 0;   // saiu limpo
    if (!QFileInfo::exists(arq)) return 0;
    QFile o(pastaRecuperacao() + QStringLiteral("/auto.origem"));
    if (o.open(QIODevice::ReadOnly | QIODevice::Text))
        origem = QString::fromUtf8(o.readAll()).trimmed();
    if (!origem.isEmpty() && QFileInfo::exists(origem) &&
        QFileInfo(origem).lastModified() >= QFileInfo(arq).lastModified())
        return 2;
    return 1;
}

// CUIDADO com a diferença (peguei isto antes de compilar): apagar a SENTINELA
// só é correto na saída limpa. Salvar ou descartar a oferta acontecem com a
// sessão ABERTA — se levassem a sentinela junto, uma queda dali em diante
// ficaria invisível e o trabalho seguinte se perderia calado.
void ZendoWindow::limparAutosave() {
    QFile::remove(pastaRecuperacao() + QStringLiteral("/auto.zendo"));
    QFile::remove(pastaRecuperacao() + QStringLiteral("/auto.origem"));
}

// R48: o arquivo RESERVADO para a oferta em curso (ver a corrida no
// iniciarProtecao) — o autosave nunca escreve neste nome.
void ZendoWindow::limparOferta() {
    QFile::remove(pastaRecuperacao() + QStringLiteral("/oferta.zendo"));
    QFile::remove(pastaRecuperacao() + QStringLiteral("/oferta.origem"));
}

void ZendoWindow::limparRecuperacao() {   // só na saída limpa
    limparAutosave();
    limparOferta();
    QFile::remove(arqSentinela());
}

// Chamado pelo main SÓ no modo interativo (QA nunca vê oferta nem cria
// sentinela — modal em caminho de boot trava headless, lição da R38).
void ZendoWindow::iniciarProtecao() {
    // DUAS INSTÂNCIAS: sem dono único, a 2ª janela apagava a sentinela da 1ª
    // ao fechar (cegando a detecção de queda da sessão VIVA), e as duas
    // gravando nos mesmos nomes podiam intercalar conteúdo de um estudo com
    // a origem do outro — aí "Recuperar" + Ctrl+S sobrescreveria um arquivo
    // BOM do usuário. O Qt já resolve dono/PID/lock-obsoleto: QLockFile.
    // Degradação honesta: a 2ª janela fica sem proteção (e sem oferta) —
    // muito melhor que destruir a da 1ª.
    QDir().mkpath(pastaRecuperacao());
    m_lock = std::make_unique<QLockFile>(pastaRecuperacao() +
                                         QStringLiteral("/sessao.lock"));
    m_lock->setStaleLockTime(0);              // dono morto = lock liberado
    if (!m_lock->tryLock(0)) {
        m_lock.reset();
        statusBar()->showMessage(
            QStringLiteral("Outra janela do Zendo já está protegendo o "
                           "trabalho — esta sessão fica sem autosave."),
            8000);
        return;
    }
    limparOferta();                           // resto de uma oferta anterior
    QString arq, origem;
    const int d = avaliarRecuperacao(arq, origem);
    if (d == 2) limparAutosave();             // obsoleto: some sem incomodar
    if (d == 1) {
        // A CORRIDA (achada na revisão, antes de shipar): a oferta é
        // NÃO-MODAL de propósito — então o usuário pode ignorá-la e começar a
        // modelar. Três minutos depois o timer gravaria por cima do
        // auto.zendo que a oferta ainda vai abrir: ele clicaria "Recuperar" e
        // receberia o próprio trabalho novo, com o da queda apagado pela
        // mesma feature que prometia devolvê-lo. RESERVO o arquivo agora, num
        // nome que o autosave não toca — o timer volta a gravar em auto.zendo
        // limpo e os dois trabalhos coexistem.
        const QString ofArq = pastaRecuperacao() + QStringLiteral("/oferta.zendo");
        const QString ofOri = pastaRecuperacao() + QStringLiteral("/oferta.origem");
        if (QFile::rename(arq, ofArq)) {
            QFile::rename(pastaRecuperacao() + QStringLiteral("/auto.origem"),
                          ofOri);
            ofertarRecuperacao(ofArq, origem);
        } else {
            ofertarRecuperacao(arq, origem);  // não deu: oferece o original
        }
    }
    QDir().mkpath(pastaRecuperacao());        // sentinela DESTA sessão
    QFile s(arqSentinela());
    if (s.open(QIODevice::WriteOnly | QIODevice::Truncate)) s.write("1");
}

void ZendoWindow::ofertarRecuperacao(const QString& arq,
                                     const QString& origem) {
    const QString quem = origem.isEmpty()
                             ? QStringLiteral("um estudo que não chegou a ser "
                                              "salvo")
                             : QFileInfo(origem).fileName();
    QMessageBox* mb = new QMessageBox(this);      // NÃO-modal (padrão R38)
    mb->setAttribute(Qt::WA_DeleteOnClose);
    mb->setWindowTitle(QStringLiteral("Recuperação"));
    mb->setIcon(QMessageBox::Question);
    mb->setText(QStringLiteral("O Zendo fechou sem salvar da última vez.\n\n"
                               "Guardei o trabalho de %1 (%2). Quer de volta?")
                    .arg(quem,
                         QFileInfo(arq).lastModified().toString(
                             QStringLiteral("dd/MM 'às' HH:mm"))));
    QPushButton* rec =
        mb->addButton(QStringLiteral("Recuperar"), QMessageBox::AcceptRole);
    mb->addButton(QStringLiteral("Descartar"), QMessageBox::RejectRole);
    connect(mb, &QMessageBox::finished, this, [this, mb, rec, arq, origem] {
        if (mb->clickedButton() != rec) {
            limparOferta();     // descartou a oferta; o autosave DESTA sessão
            return;             // e a sentinela seguem intactos
        }
        if (!openStudy(arq)) {
            limparOferta();     // não abriu (truncado?): não se reoferece
            return;             // pra sempre a cada queda seguinte
        }
        // o recuperado NÃO é o documento: Salvar tem que ir pro arquivo do
        // usuário (ou pedir um nome, se ele nunca salvou).
        m_studyPath = origem;
        setWindowTitle(origem.isEmpty()
                           ? QStringLiteral("Recuperado")
                           : QFileInfo(origem).completeBaseName());
        m_vp->markEdited();          // ainda não está no disco DELE
        // o openStudy pôs o AUTOSAVE nos Recentes e no rodapé — ele vira o
        // card nº 1 da tela inicial e some do disco 1 linha depois (clicar
        // dava "não foi possível abrir"). O documento é o do usuário.
        QStringList r = recents();
        r.removeAll(QFileInfo(arq).absoluteFilePath());
        QSettings(QStringLiteral("Zen"), QStringLiteral("Zendo"))
            .setValue(QStringLiteral("recentes"), r);
        if (m_info)
            m_info->setText(origem.isEmpty()
                                ? QStringLiteral("Recuperado (não salvo)")
                                : QFileInfo(origem).fileName());
        limparOferta();              // a oferta cumpriu o papel
        statusBar()->showMessage(
            QStringLiteral("Trabalho recuperado — salve para guardar."), 8000);
    });
    // MODAL de propósito (a regra "não-modal" da R38 vale pra AVISO em
    // caminho de load; isto é uma PERGUNTA que não pode ser ignorada com
    // segurança). Não-modal deixava o usuário editar com a oferta na tela —
    // e "Recuperar" jogaria fora o que ele acabou de fazer, sem perguntar.
    // Headless nunca chega aqui: o main só chama iniciarProtecao sem --shot.
    mb->exec();
}

// R48: a cola de atalhos é GERADA varrendo as QActions. Uma lista escrita à
// mão já nasceria velha (e estaria errada na leva seguinte); assim, atalho
// novo aparece aqui sozinho — e a varredura é a própria prova de cobertura.
QString ZendoWindow::colaDeAtalhos() const {
    QMap<QString, QString> pares;            // texto → atalho (ordena sozinho)
    for (const QAction* a : findChildren<QAction*>()) {
        if (a->shortcut().isEmpty()) continue;
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        t.remove(QStringLiteral("…"));
        if (!t.isEmpty()) pares.insert(t, a->shortcut().toString(
                                              QKeySequence::NativeText));
    }
    QString h = QStringLiteral(
        "<b>Atalhos dos comandos</b><br><table cellpadding='3'>");
    for (auto it = pares.cbegin(); it != pares.cend(); ++it)
        h += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
                 .arg(it.value(), it.key());
    h += QStringLiteral("</table>");
    // Os GESTOS (o que muda no meio da ação) não são QActions e nenhuma
    // varredura os acha — moram aqui e nos Primeiros passos.
    // R52: e a CÂMERA morava em lugar NENHUM. O dogfooding da R51 abriu esta
    // cola procurando como se orbita e não achou — porque órbita/pan/zoom são
    // eventos de mouse no viewport, não QAction: a varredura acima, que eu
    // achei elegante na R48, NUNCA PODERIA vê-los. O gerado-do-código tem um
    // ponto cego, e o remédio é o mesmo bloco manual que já existia aqui
    // embaixo. (Fonte: Viewport3D::mouseMoveEvent/wheelEvent.)
    h += QStringLiteral(
        "<br><b>Câmera (mouse)</b><table cellpadding='3'>"
        "<tr><td><b>botão do MEIO</b></td><td>arrastar ORBITA "
        "(em torno do ponto sob o cursor)</td></tr>"
        "<tr><td><b>botão DIREITO</b></td><td>arrastar dá PAN · "
        "Shift+meio também</td></tr>"
        "<tr><td><b>roda</b></td><td>ZOOM no ponto sob o cursor</td></tr>"
        "<tr><td><b>Ctrl+1…6</b></td><td>vistas padrão — o caminho sem "
        "botão do meio</td></tr>"
        "</table>");
    h += QStringLiteral(
        "<br><b>No meio da ação</b><table cellpadding='3'>"
        "<tr><td><b>digitar</b></td><td>define a medida exata</td></tr>"
        "<tr><td><b>Ctrl</b></td><td>Pull: empilha · Mover: copia · "
        "Balde: sólido inteiro</td></tr>"
        "<tr><td><b>Alt</b></td><td>Balde: conta-gotas</td></tr>"
        "<tr><td><b>Shift</b></td><td>trava a inferência</td></tr>"
        "<tr><td><b>← ↑ →</b></td><td>travam o eixo</td></tr>"
        "<tr><td><b>Esc</b></td><td>cancela · sai do contexto</td></tr>"
        "<tr><td><b>Enter</b></td><td>repete a última ferramenta</td></tr>"
        "</table>");
    return h;
}

// R48/R50: os VERBOS INVISÍVEIS — não é tour, é uma folha que se lê em 30s.
// R50 (feedback "achei horrível"): era um QTextBrowser BRANCO com HTML cru e
// um botão "Close" em inglês, no meio de um app todo sumi — parecia alerta do
// sistema, não instrução do ateliê. Agora é widget nativo com a tinta da casa:
// o numeral em latão faz a hierarquia (não o negrito), as teclas viram chip
// (a linguagem do VCB, R33) e o único latão SÓLIDO é o botão — porque é a
// única coisa que se clica.
void ZendoWindow::primeirosPassos(bool forcado) {
    QSettings cfg(QStringLiteral("Zen"), QStringLiteral("Zendo"));
    if (!forcado && cfg.value(QStringLiteral("ajuda/vistos")).toBool()) return;

    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("ppPanel"));
    dlg.setWindowTitle(QStringLiteral("Primeiros passos"));
    // a caption escura vem sozinha: o DarkTitleFilter da R34 está no qApp e
    // pega toda janela nova. (Incluir o ZendoChrome aqui puxaria windows.h
    // pro arquivo que fala com o kernel — a colisão de macros da R33.)

    QHBoxLayout* raiz = new QHBoxLayout(&dlg);
    raiz->setContentsMargins(0, 0, 0, 0);
    raiz->setSpacing(0);
    QFrame* spine = new QFrame(&dlg);        // a lombada latão dos docks
    spine->setObjectName(QStringLiteral("ppSpine"));
    spine->setFixedWidth(3);
    spine->setAutoFillBackground(true);      // sem isto o QSS não pinta
    raiz->addWidget(spine);

    QWidget* sheet = new QWidget(&dlg);
    sheet->setObjectName(QStringLiteral("ppSheet"));
    raiz->addWidget(sheet, 1);
    QVBoxLayout* v = new QVBoxLayout(sheet);
    v->setContentsMargins(34, 30, 34, 26);
    v->setSpacing(0);

    const auto rotulo = [&](const QString& txt, const char* obj) {
        QLabel* l = new QLabel(txt, sheet);
        l->setObjectName(QString::fromLatin1(obj));
        l->setWordWrap(true);
        return l;
    };
    const auto fio = [&](const char* obj) {
        QFrame* f = new QFrame(sheet);
        f->setObjectName(QString::fromLatin1(obj));
        f->setFixedHeight(1);
        f->setAutoFillBackground(true);
        return f;
    };

    v->addWidget(rotulo(QStringLiteral("ZENDO · PRIMEIROS PASSOS"), "ppCaption"));
    v->addSpacing(10);
    v->addWidget(rotulo(QStringLiteral("Quatro coisas que o app\nnão conta sozinho"),
                        "ppTitle"));
    v->addSpacing(18);
    v->addWidget(fio("ppRule"));
    v->addSpacing(22);

    // --- os três verbos: numeral latão | (título + corpo + teclas) ---------
    struct Passo {
        const char* num;
        const char* head;
        const char* body;
        QStringList teclas;
    };
    const QVector<Passo> passos{
        // R52: a navegação virou o 01 porque é a coisa nº 1 que o app não
        // contava — literalmente em lugar NENHUM (nem aqui, nem na cola). Foi
        // a primeira parede do dogfooding: sem saber orbitar, não dá pra ver
        // a fachada, e sem ver a fachada não dá pra fazer nada nela.
        {"01", "Gire a vista com o botão do MEIO",
         "Arrastar com o botão do meio orbita em torno do ponto sob o cursor; "
         "o botão direito desloca; a roda dá zoom onde o mouse está. Sem mouse "
         "de três botões? Ctrl+1 a Ctrl+6 dão as vistas prontas — frente, "
         "topo, isométrica.",
         {QStringLiteral("meio"), QStringLiteral("direito"),
          QStringLiteral("roda"), QStringLiteral("Ctrl+1…6")}},
        {"02", "Digite a medida — em qualquer ferramenta",
         "Desenhe um retângulo grosso modo e digite 4;3 — ele vira 4×3 m. "
         "Puxe uma parede e digite 2,80. Não há campo pra clicar: comece a "
         "digitar e o número aparece no rodapé, à direita.",
         {QStringLiteral("4;3"), QStringLiteral("2,80"), QStringLiteral("Enter")}},
        {"03", "Os pontos coloridos são o motor de precisão",
         "Movendo o mouse, o Zendo mostra o que achou: extremo, meio, sobre a "
         "aresta, sobre a face. Clique quando o ponto certo aparecer e o "
         "desenho nasce exato — sem zoom, sem sorte.",
         {QStringLiteral("Shift"), QStringLiteral("← ↑ →")}},
        {"04", "Ctrl muda o que a ferramenta faz",
         "Ctrl+Pull empilha um volume novo em vez de mover a face. "
         "Ctrl+Mover copia em vez de mover. Ctrl+Balde pinta o sólido "
         "inteiro; Alt+Balde vira conta-gotas.",
         {QStringLiteral("Ctrl"), QStringLiteral("Alt")}},
    };
    for (const Passo& p : passos) {
        QHBoxLayout* linha = new QHBoxLayout;
        linha->setContentsMargins(0, 0, 0, 0);
        linha->setSpacing(18);
        QLabel* num = rotulo(QString::fromUtf8(p.num), "ppNum");
        num->setFixedWidth(38);
        num->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        linha->addWidget(num, 0, Qt::AlignTop);
        QVBoxLayout* col = new QVBoxLayout;
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(5);
        col->addWidget(rotulo(QString::fromUtf8(p.head), "ppHead"));
        col->addWidget(rotulo(QString::fromUtf8(p.body), "ppBody"));
        QHBoxLayout* chips = new QHBoxLayout;
        chips->setContentsMargins(0, 3, 0, 0);
        chips->setSpacing(6);
        for (const QString& t : p.teclas) {
            QLabel* k = new QLabel(t, sheet);
            k->setObjectName(QStringLiteral("ppKey"));
            chips->addWidget(k, 0, Qt::AlignLeft);
        }
        chips->addStretch();
        col->addLayout(chips);
        linha->addLayout(col, 1);
        v->addLayout(linha);
        v->addSpacing(24);
    }

    v->addWidget(fio("ppRuleSoft"));
    v->addSpacing(16);
    QLabel* caminho = rotulo(
        QStringLiteral("O caminho curto"), "ppPathStrong");
    v->addWidget(caminho);
    v->addSpacing(4);
    v->addWidget(rotulo(
        QStringLiteral("Retângulo no chão  →  Pull pra cima  →  duplo-clique "
                       "numa face seleciona o sólido  →  Balde texturiza  →  "
                       "Câmera ▸ Render fotorrealista.\nO motor de render se "
                       "instala sozinho na primeira foto."),
        "ppPath"));
    v->addSpacing(14);
    v->addWidget(rotulo(
        QStringLiteral("Se travar: Esc cancela qualquer coisa · Ctrl+Z "
                       "desfaz · o rodapé sempre diz o que a ferramenta "
                       "espera de você · F1 traz esta folha de volta."),
        "ppFoot"));
    v->addStretch();
    v->addSpacing(18);

    QHBoxLayout* rod = new QHBoxLayout;
    rod->addStretch();
    QPushButton* go = new QPushButton(QStringLiteral("Começar"), sheet);
    go->setObjectName(QStringLiteral("ppGo"));
    go->setCursor(Qt::PointingHandCursor);
    go->setDefault(true);
    connect(go, &QPushButton::clicked, &dlg, &QDialog::accept);
    rod->addWidget(go);
    v->addLayout(rod);

    dlg.setFixedSize(600, 742);   // R52: 4º passo (a navegação) entrou
    dlg.exec();
    // Aparece UMA vez na vida e nunca mais — F1 e o menu Ajuda trazem de
    // volta. (Um checkbox "não mostrar de novo" só criaria estado ambíguo
    // pra resolver um incômodo que não existe: sem ele, já não repete.)
    cfg.setValue(QStringLiteral("ajuda/vistos"), true);
}

ZendoWindow::~ZendoWindow() = default;   // R48 (QLockFile completo aqui)

void ZendoWindow::closeEvent(QCloseEvent* e) {
    if (!m_vp->edited()) {
        limparRecuperacao();          // saída limpa: nada a recuperar
        e->accept();
        return;
    }
    const auto r = QMessageBox::question(
        this, QStringLiteral("Zendo"),
        QStringLiteral("Há edições 3D não salvas. Salvar o estudo antes de "
                       "fechar?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (r == QMessageBox::Cancel) {
        e->ignore();
        return;
    }
    if (r == QMessageBox::Save) {
        saveStudyDialog();
        if (m_vp->edited()) {           // usuário cancelou o diálogo de salvar
            e->ignore();
            return;
        }
    }
    // R48: saiu por escolha (salvou ou descartou) — não é queda. Nada a
    // oferecer no próximo boot.
    limparRecuperacao();
    e->accept();
}

bool ZendoWindow::saveStudy(const QString& path) {
    if (!writeStudyFile(path)) {          // mesmo JSON do autosave (com thumb)
        QMessageBox::warning(this, QStringLiteral("Zendo"),
                             QStringLiteral("Não foi possível gravar:\n%1")
                                 .arg(path));
        return false;
    }
    m_studyPath = QFileInfo(path).absoluteFilePath();
    addRecent(m_studyPath);
    m_vp->markSaved();
    limparAutosave();    // R48: está no disco DELE — nada a recuperar (mas a
                         // sentinela fica: a sessão continua aberta)
    setWindowTitle(QStringLiteral("%1")
                       .arg(QFileInfo(path).completeBaseName()));
    statusBar()->showMessage(QStringLiteral("Estudo salvo em %1")
                                 .arg(QFileInfo(path).fileName()), 5000);
    return true;
}

bool ZendoWindow::openStudy(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Zendo"),
                             QStringLiteral("Não foi possível abrir:\n%1")
                                 .arg(f.errorString()));
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value("app").toString() != QLatin1String("Zendo")) {
        QMessageBox::warning(this, QStringLiteral("Zendo"),
                             QStringLiteral("Este arquivo não é um estudo "
                                            ".zendo válido."));
        return false;
    }

    // o 2D de origem (contexto do chão): relativo ao .zendo; ausente = só 3D
    QString src = root.value("source").toString();
    QString warn;
    auto doc = makeDoc();
    if (!src.isEmpty()) {
        const QString abs =
            QDir(QFileInfo(path).absolutePath()).absoluteFilePath(src);
        QString err;
        if (QFileInfo::exists(abs) &&
            loadProject(abs, *doc, m_layouts, m_styles, m_settings, &err)) {
            m_sourcePath = QFileInfo(abs).absoluteFilePath();
        } else {
            warn = QStringLiteral(" · 2D de origem ausente (%1)").arg(src);
            m_sourcePath.clear();
        }
    }
    m_doc = std::move(doc);

    const double wh = root.value("wallHeight").toDouble(0.0);
    PlantScene ps;                       // R3: planta só se o 2D existir
    if (!m_sourcePath.isEmpty())
        ps = importPlant(*m_doc, wh >= 0.5 ? wh : m_vp->wallHeight());
    if (!m_vp->setStudy(ps, root.value("meshes").toArray(), wh)) {
        QMessageBox::warning(this, QStringLiteral("Zendo"),
                             QStringLiteral("Estudo corrompido: a malha não "
                                            "passou na verificação."));
        return false;
    }
    m_vp->setSketchJson(root.value("sketch").toArray());
    m_vp->setGuidesJson(root.value("guides").toArray());
    m_vp->setDimsJson(root.value("dimensions").toArray());
    m_vp->setSectionsJson(root.value("sections").toArray());
    m_vp->setDay(root.value("day").toBool(true));   // R5
    m_vp->setCompsJson(root.value("components").toArray());
    setScenesJson(root.value("scenes").toArray());
    m_vp->setTexturesJson(root.value("textures").toArray(),
                          QFileInfo(path).absolutePath());
    const QJsonObject cam = root.value("camera").toObject();
    if (!cam.isEmpty()) {
        const QJsonArray t = cam.value("target").toArray();
        const float tgt[3] = {float(t.at(0).toDouble()),
                              float(t.at(1).toDouble()),
                              float(t.at(2).toDouble())};
        m_vp->setCameraState(float(cam.value("yaw").toDouble()),
                             float(cam.value("pitch").toDouble()),
                             float(cam.value("dist").toDouble()), tgt);
    }
    m_studyPath = QFileInfo(path).absoluteFilePath();
    setWindowTitle(QStringLiteral("%1")
                       .arg(QFileInfo(path).completeBaseName()));
    m_info->setText(QStringLiteral("%1 · %2 parede(s) · pé-direito %3 m%4")
                        .arg(QFileInfo(path).fileName())
                        .arg(m_vp->wallCount())
                        .arg(m_vp->wallHeight(), 0, 'f', 2)
                        .arg(warn));
    addRecent(m_studyPath);
    showSpace();
    // R38: textura referenciada e NÃO carregada avisa — o estudo abre cinza
    // em silêncio quando o .zendo viaja sem os arquivos (lição da entrega).
    const QStringList faltam = m_vp->missingTextures();
    if (!faltam.isEmpty()) {
        // NÃO-modal: um estudo com textura faltante não pode travar QA
        // headless nem prender o usuário — avisa e deixa trabalhar.
        QMessageBox* mb = new QMessageBox(
            QMessageBox::Warning, QStringLiteral("Texturas não encontradas"),
            QStringLiteral("%1 textura(s) deste estudo não foram achadas e "
                           "as faces vão aparecer sem revestimento:\n\n• %2\n\n"
                           "Dica: use Arquivo → \"Salvar como pacote…\" na "
                           "máquina de origem — as texturas viajam junto.")
                .arg(faltam.size())
                .arg(faltam.mid(0, 8).join(QStringLiteral("\n• "))),
            QMessageBox::Ok, this);
        mb->setAttribute(Qt::WA_DeleteOnClose);
        mb->show();
    }
    return true;
}

// R38: SALVAR COMO PACOTE — copia as texturas usadas pra ./assets ao lado do
// .zendo e re-aponta a lib; o texturesJson grava relativo, então o pacote
// abre texturado em QUALQUER máquina (a armadilha da entrega, morta).
void ZendoWindow::savePackageDialog() {
    const QString sugg = m_studyPath.isEmpty()
                             ? QDir::homePath() + QStringLiteral("/Estudo.zendo")
                             : m_studyPath;
    const QString out = QFileDialog::getSaveFileName(
        this, QStringLiteral("Salvar como pacote (leva as texturas junto)"),
        sugg, QStringLiteral("Estudo 3D Zendo (*.zendo)"));
    if (out.isEmpty()) return;
    const QDir dir = QFileInfo(out).absoluteDir();
    const QString assets = dir.absolutePath() + QStringLiteral("/assets");
    if (!QDir().mkpath(assets)) {
        statusBar()->showMessage(
            QStringLiteral("Não consegui criar a pasta assets."), 6000);
        return;
    }
    int copiadas = 0;
    for (const auto& [name, file] : m_vp->usedTextureFiles()) {
        const QString destino = assets + QStringLiteral("/") + name;
        if (QFileInfo(file).absoluteFilePath() !=
            QFileInfo(destino).absoluteFilePath()) {
            QFile::remove(destino);            // sobrescreve versão velha
            if (!QFile::copy(file, destino)) continue;
        }
        m_vp->retargetTexture(name, destino);
        ++copiadas;
    }
    if (saveStudy(out))
        statusBar()->showMessage(
            QStringLiteral("Pacote salvo: %1 textura(s) em ./assets — pode "
                           "zipar a pasta e mandar.")
                .arg(copiadas),
            8000);
}

void ZendoWindow::openDialog() {
    const QString f = QFileDialog::getOpenFileName(
        this, QStringLiteral("Abrir projeto 2D"), QString(),
        QStringLiteral("Projeto ZenCAD (*.zencad)"));
    if (!f.isEmpty()) openFile(f);
}

void ZendoWindow::openStudyDialog() {
    const QString f = QFileDialog::getOpenFileName(
        this, QStringLiteral("Abrir estudo 3D"), QString(),
        QStringLiteral("Estudo Zendo (*.zendo)"));
    if (!f.isEmpty()) openStudy(f);
}

// R55: o Ctrl+S de verdade — grava onde já está, e só pede nome se não houver
// arquivo ainda. É o par do saveStudyDialog (Salvar como), não um substituto.
void ZendoWindow::saveStudyQuick() {
    if (m_studyPath.isEmpty()) {
        saveStudyDialog();
        return;
    }
    saveStudy(m_studyPath);      // já avisa sozinho se a escrita falhar
}

void ZendoWindow::saveStudyDialog() {
    QString sugg = m_studyPath;
    if (sugg.isEmpty() && !m_sourcePath.isEmpty()) {
        const QFileInfo si(m_sourcePath);
        sugg = si.absolutePath() + "/" + si.completeBaseName() +
               QStringLiteral(".zendo");
    }
    const QString f = QFileDialog::getSaveFileName(
        this, QStringLiteral("Salvar estudo 3D"), sugg,
        QStringLiteral("Estudo Zendo (*.zendo)"));
    if (!f.isEmpty()) saveStudy(f);
}

void ZendoWindow::heightDialog() {
    bool ok = false;
    const double h = QInputDialog::getDouble(
        this, QStringLiteral("Pé-direito"),
        QStringLiteral("Altura das paredes (m) — regenera do 2D e DESCARTA "
                       "as edições 3D:"),
        m_vp->wallHeight(), 1.0, 12.0, 2, &ok);
    if (!ok) return;
    m_vp->setWallHeight(h);
    // R3: re-importar a planta é papel do conector, não do viewport
    if (!m_sourcePath.isEmpty()) m_vp->setPlant(importPlant(*m_doc, h), h);
}

void ZendoWindow::pushPullDialog() {
    if (!m_vp->hasSelection()) {
        statusBar()->showMessage(
            QStringLiteral("Selecione uma face primeiro (clique nela)."), 4000);
        return;
    }
    bool ok = false;
    const double d = QInputDialog::getDouble(
        this, QStringLiteral("Empurrar/Puxar"),
        QStringLiteral("Distância (m) — negativo empurra para dentro:"),
        0.20, -50.0, 50.0, 2, &ok);
    if (!ok || std::abs(d) < 1e-9) return;
    if (m_vp->pushPullSelected(d))
        statusBar()->showMessage(
            QStringLiteral("Face %1 %2 m.")
                .arg(d > 0 ? QStringLiteral("puxada") : QStringLiteral("empurrada"))
                .arg(std::abs(d), 0, 'f', 2), 5000);
}

void ZendoWindow::paintDialog() {
    if (!m_vp->hasSelection()) {
        statusBar()->showMessage(
            QStringLiteral("Selecione uma face primeiro (clique nela)."), 4000);
        return;
    }
    const QColor c = QColorDialog::getColor(
        QColor(0xc2, 0xa0, 0x63), this, QStringLiteral("Pintar face"));
    if (c.isValid() && m_vp->paintSelected(c))
        statusBar()->showMessage(QStringLiteral("Face pintada."), 3000);
}

// ---------------------------------------------------------------------------
//  Ponte (F4): elevações e cortes de volta pro ZenCAD
// ---------------------------------------------------------------------------
QString ZendoWindow::bridgeSuggestion(const QString& stem) const {
    const QString base = !m_sourcePath.isEmpty() ? m_sourcePath : m_studyPath;
    if (base.isEmpty()) return stem + QStringLiteral(".zencad");
    const QFileInfo bi(base);
    return bi.absolutePath() + "/" + bi.completeBaseName() + " - " + stem +
           QStringLiteral(".zencad");
}

void ZendoWindow::elevationDialog() {
    const QStringList dirs{QStringLiteral("Sul"), QStringLiteral("Norte"),
                           QStringLiteral("Leste"), QStringLiteral("Oeste")};
    bool ok = false;
    const QString d = QInputDialog::getItem(
        this, QStringLiteral("Elevação"), QStringLiteral("Vista de:"), dirs, 0,
        false, &ok);
    if (!ok) return;
    const char c = d == QLatin1String("Sul")     ? 'S'
                   : d == QLatin1String("Norte") ? 'N'
                   : d == QLatin1String("Leste") ? 'L' : 'O';
    const QString f = QFileDialog::getSaveFileName(
        this, QStringLiteral("Exportar elevação"),
        bridgeSuggestion(QStringLiteral("Elevacao ") + d),
        QStringLiteral("Projeto ZenCAD (*.zencad)"));
    if (f.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bridge::Result r =
        bridge::exportElevation(m_vp->meshPointers(), c, f);
    QApplication::restoreOverrideCursor();
    statusBar()->showMessage(
        r.ok ? QStringLiteral("Elevação %1 exportada (%2 arestas visíveis) — "
                              "abra no ZenCAD: %3")
                   .arg(d).arg(r.edgeSegs).arg(QFileInfo(f).fileName())
             : QStringLiteral("Falhou: %1").arg(r.error), 8000);
}

void ZendoWindow::sectionDialog() {
    QStringList kinds{
        QStringLiteral("Plano Y — olhando NORTE (corte transversal)"),
        QStringLiteral("Plano Y — olhando SUL"),
        QStringLiteral("Plano X — olhando LESTE (corte longitudinal)"),
        QStringLiteral("Plano X — olhando OESTE")};
    // R31: com a seção ao vivo LIGADA (R30), a 1ª opção exporta exatamente o
    // corte que está na tela — plano arbitrário incluso.
    if (m_vp->clipOn())
        kinds.prepend(QStringLiteral("Plano da SEÇÃO ATUAL (o corte da tela)"));
    bool ok = false;
    const QString k = QInputDialog::getItem(
        this, QStringLiteral("Corte"), QStringLiteral("Plano e sentido:"),
        kinds, 0, false, &ok);
    if (!ok) return;
    if (k.startsWith(QLatin1String("Plano da SEÇÃO"))) {
        const QString f = QFileDialog::getSaveFileName(
            this, QStringLiteral("Exportar corte"),
            bridgeSuggestion(QStringLiteral("Corte plano atual")),
            QStringLiteral("Projeto ZenCAD (*.zencad)"));
        if (f.isEmpty()) return;
        const QVector4D c = m_vp->clipPlane();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bridge::Result r = bridge::exportSectionPlane(
            m_vp->meshPointers(), {c.x(), c.y(), c.z()}, c.w(), f);
        QApplication::restoreOverrideCursor();
        statusBar()->showMessage(
            r.ok ? QStringLiteral("Corte exportado (%1 sólidos cortados, %2 "
                                  "arestas de fundo) — abra no ZenCAD: %3")
                       .arg(r.cutLoops).arg(r.edgeSegs)
                       .arg(QFileInfo(f).fileName())
                 : QStringLiteral("Falhou: %1").arg(r.error), 8000);
        return;
    }
    const char axis = k.contains(QLatin1String("Plano Y")) ? 'Y' : 'X';
    const int sign = (k.contains(QLatin1String("NORTE")) ||
                      k.contains(QLatin1String("LESTE"))) ? +1 : -1;
    const double pos = QInputDialog::getDouble(
        this, QStringLiteral("Corte"),
        QStringLiteral("Posição do plano (%1 = valor, em m):").arg(axis),
        5.0, -1e6, 1e6, 2, &ok);
    if (!ok) return;
    const QString f = QFileDialog::getSaveFileName(
        this, QStringLiteral("Exportar corte"),
        bridgeSuggestion(QStringLiteral("Corte %1=%2").arg(axis).arg(pos)),
        QStringLiteral("Projeto ZenCAD (*.zencad)"));
    if (f.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bridge::Result r =
        bridge::exportSection(m_vp->meshPointers(), axis, pos, sign, f);
    QApplication::restoreOverrideCursor();
    statusBar()->showMessage(
        r.ok ? QStringLiteral("Corte exportado (%1 sólidos cortados, %2 "
                              "arestas de fundo) — abra no ZenCAD: %3")
                   .arg(r.cutLoops).arg(r.edgeSegs).arg(QFileInfo(f).fileName())
             : QStringLiteral("Falhou: %1").arg(r.error), 8000);
}

void ZendoWindow::setCameraPose(float yawDeg, float pitchDeg, float distFactor) {
    m_vp->setCameraPose(yawDeg, pitchDeg, distFactor);
}

// ===========================================================================
//  R36 — O FOTÓGRAFO: Blender como motor de render invisível (processo
//  separado; GPL não contamina). A cena vem da VISTA ATUAL + sol da Bandeja.
// ===========================================================================
namespace {
// R47: raizUnica != vazio → procura SÓ ali (o QA injeta uma raiz de teste e
// não pode enxergar — nem destruir — o Blender real da máquina).
QString findBlender(const QString& raizUnica = QString()) {
    QStringList roots;
    if (!raizUnica.isEmpty()) {
        roots << raizUnica;
    } else {
        const QString local = qEnvironmentVariable("LOCALAPPDATA");
        if (!local.isEmpty())
            roots << local + QStringLiteral("/Zen/render");   // nosso portable
        roots << QStringLiteral("C:/Program Files/Blender Foundation");
    }
    for (const QString& root : roots) {
        // R47: a versão PINADA (a testada contra o render_cena.py) vem antes
        // do glob — a ordenação lexical abaixo escolhe "blender-4.5.9" em vez
        // de "blender-4.5.11" ('9' > '1'). DECISÃO consciente: uma versão
        // ANTIGA já instalada continua satisfazendo (não forçamos re-download
        // de 400 MB a cada bump da trinca; a 4.5 é LTS e o render_cena.py é
        // compatível na série) — mas se a pinada existir, ela ganha.
        const QString pin = root + QStringLiteral("/") +
                            BlenderInstaller::pastaPadrao() +
                            QStringLiteral("/blender.exe");
        if (QFileInfo::exists(pin)) return pin;
        const QDir d(root);
        for (const QFileInfo& fi : d.entryInfoList(
                 QDir::Dirs | QDir::NoDotAndDotDot,
                 QDir::Name | QDir::Reversed)) {          // versão mais nova
            const QString exe =
                fi.absoluteFilePath() + QStringLiteral("/blender.exe");
            if (QFileInfo::exists(exe)) return exe;
        }
    }
    if (!raizUnica.isEmpty()) return QString();   // QA não cai no PATH do dev
    return QStandardPaths::findExecutable(QStringLiteral("blender"));
}
}  // namespace

// R52: o yaw que o enquadrarFoto espera é o azimute da POSIÇÃO do olho (ele
// planta a câmera em c+(cos,sin)·d e mira o centro) — a convenção da ÓRBITA.
// No walkthrough m_yaw é o azimute do OLHAR: o setWalkthrough soma 180° ao
// entrar, justamente porque as duas convenções são opostas. Sem desfazer isso,
// quem está de pé diante da fachada sul e pede "Enquadrar" recebe uma foto dos
// FUNDOS — calada. É a 3ª aparição desta família (a meia-volta é da R27); a
// conversão mora aqui, num lugar só, com o nome do que ela faz.
double ZendoWindow::yawFoto() const {
    const double y = double(m_vp->yawAtual());
    return m_vp->walkOn() ? y + 180.0 : y;
}

bool ZendoWindow::buildRenderJob(const QString& outPng, double elevDeg,
                                 double azimDeg, int samples, int resX,
                                 int resY, bool interior, bool hdri,
                                 bool enquadrar, QString& blender,
                                 QStringList& args, QString& err) {
    blender = findBlender();
    if (blender.isEmpty()) {
        err = QStringLiteral("Blender não encontrado.");
        return false;
    }
    const QString script = QCoreApplication::applicationDirPath() +
                           QStringLiteral("/assets/render_cena.py");
    if (!QFileInfo::exists(script)) {
        err = QStringLiteral("assets/render_cena.py não encontrado.");
        return false;
    }
    const QString gltf =
        QDir::tempPath() + QStringLiteral("/zen_fotografo.gltf");
    if (m_vp->exportGltf(gltf) <= 0) {
        err = QStringLiteral("nada para renderizar (cena vazia?).");
        return false;
    }
    cad::Point3 eye, tgt;
    if (enquadrar) {
        // R52: o achado do dogfooding. A câmera de MODELAGEM olha de cima —
        // e o render de cima mostra a BARRIGA da esfera do HDRI (cinza morto),
        // não o céu. Provado A/B: mesma cena, mesmo HDRI, mesmo sol; só a
        // altura muda e o céu azul aparece.
        // Troco a FONTE do eye/tgt do job — NÃO a câmera do viewport. Mexer
        // na câmera do usuário pediria restore, esbarraria em undo/cenas e
        // destruiria a vista de trabalho de quem só queria uma foto.
        cad::Point3 lo, hi;
        if (m_vp->boundsFoto(lo, hi))
            Viewport3D::enquadrarFoto(lo, hi, yawFoto(), double(m_vp->fovY()),
                                      double(resX) / std::max(1, resY),
                                      eye, tgt);
        else
            m_vp->cameraWorld(eye, tgt);      // cena vazia: nada a enquadrar
    } else {
        m_vp->cameraWorld(eye, tgt);
    }
    args.clear();
    args << QStringLiteral("--background") << QStringLiteral("--factory-startup")
         << QStringLiteral("--python") << script << QStringLiteral("--")
         << gltf << outPng;
    for (const double v : {eye.x, eye.y, eye.z, tgt.x, tgt.y, tgt.z,
                           elevDeg, azimDeg, double(m_vp->fovY())})
        args << QString::number(v, 'f', 4);
    args << QString::number(resX) << QString::number(resY)
         << QString::number(samples) << QString::number(interior ? 1 : 0);
    // R46: céu real — o HDRI embarcado no assets (16º arg; "-" = Nishita)
    const QString ceu = QCoreApplication::applicationDirPath() +
                        QStringLiteral("/assets/ceu_dia.hdr");
    args << (hdri && QFileInfo::exists(ceu) ? ceu : QStringLiteral("-"));
    return true;
}

// R47: PRESETS do Fotógrafo — um macro que resolve os 4 controles de uma vez.
// PURA e estática de propósito: o combo do diálogo chama esta função, e o QA
// chama a MESMA (lição R27/R28: flag que seta o estado final não testa nada;
// tem que exercitar a função real). O preset NÃO é estado — o documento só
// guarda os valores resolvidos; o combo é um atalho.
void ZendoWindow::presetFotografo(int preset, int& meiaHora, int& ceu,
                                  bool& interior, int& qual) {
    switch (preset) {
        case 0:                       // Entardecer dourado (sol físico rasante)
            meiaHora = 33; ceu = 0; interior = false; qual = 1; break;
        case 1:                       // Meio-dia neutro (céu real)
            meiaHora = 24; ceu = 1; interior = false; qual = 1; break;
        case 2:                       // Interior acolhedor
            meiaHora = 30; ceu = 1; interior = true; qual = 1; break;
        default: break;               // Personalizado: não toca em nada
    }
}

// R47: garante o MOTOR antes de abrir o Estúdio. Ordem importa: resolver
// primeiro significa que não há configuração pra preservar através de um
// download de 400 MB — a reentrada é só chamar o fluxo de novo.
// Qualquer falha (sem curl/tar, sem rede, hash ruim) degrada pro caminho
// manual de sempre: o app nunca fica sem saída.
bool ZendoWindow::garantirMotor() {
    if (!findBlender().isEmpty()) return true;
    QString falta;
    const bool automatico = BlenderInstaller::ferramentasOk(&falta) &&
                            !BlenderInstaller::destinoPadrao().isEmpty();
    if (!automatico) {
        const auto r = QMessageBox::question(
            this, QStringLiteral("Render fotorrealista"),
            QStringLiteral("O motor de render (Blender, gratuito) não está "
                           "instalado, e não consigo baixá-lo sozinho aqui "
                           "(%1).\n\nAbrir a página de download? Depois de "
                           "instalar, é só clicar aqui de novo.")
                .arg(falta.isEmpty() ? QStringLiteral("sem pasta de destino")
                                     : falta),
            QMessageBox::Yes | QMessageBox::No);
        if (r == QMessageBox::Yes)
            QDesktopServices::openUrl(
                QUrl(QStringLiteral("https://www.blender.org/download/")));
        return false;
    }
    if (QMessageBox::question(
            this, QStringLiteral("Render fotorrealista"),
            QStringLiteral("Para fotografar, o Zendo precisa do motor de "
                           "render (Blender — gratuito e livre).\n\nBaixar e "
                           "instalar agora? São cerca de %1 MB, uma única "
                           "vez.")
                .arg(BlenderInstaller::bytesPadrao() / 1000000),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return false;

    QProgressDialog prog(QStringLiteral("Baixando o motor de render…"),
                         QStringLiteral("Cancelar"), 0, 100, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(0);
    prog.setAutoClose(false);
    prog.setAutoReset(false);
    prog.setValue(0);
    BlenderInstaller inst;
    QEventLoop loop;
    bool ok = false, fim = false;
    QString erro;
    connect(&inst, &BlenderInstaller::fase, &prog, [&prog](const QString& t) {
        prog.setLabelText(t);
        // extração não tem progresso: barra indeterminada (range 0,0)
        prog.setRange(0, t.startsWith(QStringLiteral("Baixando")) ? 100 : 0);
    });
    connect(&inst, &BlenderInstaller::progresso, &prog,
            [&prog](qint64 feito, qint64 total) {
                if (total > 0) prog.setValue(int(feito * 100 / total));
            });
    connect(&inst, &BlenderInstaller::terminou, &loop,
            [&](bool o, const QString& e, const QString&) {
                if (fim) return;          // primeiro resultado ganha
                ok = o; erro = e; fim = true; loop.quit();
            });
    connect(&prog, &QProgressDialog::canceled, &inst,
            &BlenderInstaller::cancelar);
    inst.instalar(BlenderInstaller::urlPadrao(), BlenderInstaller::sha256Padrao(),
                  BlenderInstaller::destinoPadrao(),
                  BlenderInstaller::bytesPadrao());
    if (!fim) loop.exec();     // instalar() pode falhar ANTES do exec (sem
                               // disco/ferramenta) — exec() aí travaria o app
    // ARMADILHA DO Qt (auditoria da R47): QProgressDialog::closeEvent EMITE
    // canceled() — o close() abaixo dispararia cancelar() e sobrescreveria o
    // resultado (sucesso viraria "cancelado" em silêncio, e toda mensagem de
    // erro seria engolida). Desligar ANTES de fechar.
    QObject::disconnect(&prog, &QProgressDialog::canceled, &inst,
                        &BlenderInstaller::cancelar);
    prog.close();
    if (!ok) {
        if (erro != QLatin1String("cancelado"))
            QMessageBox::warning(
                this, QStringLiteral("Motor de render"),
                QStringLiteral("Não consegui instalar o motor: %1.\n\nDá pra "
                               "instalar o Blender manualmente (blender.org) "
                               "que o Zendo acha sozinho.")
                    .arg(erro));
        return false;
    }
    return !findBlender().isEmpty();
}

// R37: o diálogo do ESTÚDIO — hora do sol, qualidade, resolução e barra de
// progresso REAL (parseia o "Sample x/y" que o Cycles imprime no stdout).
void ZendoWindow::renderDialog() {
    if (!garantirMotor()) return;              // R47
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("O Fotógrafo"));
    QVBoxLayout* v = new QVBoxLayout(&dlg);
    QLabel* lHora = new QLabel(&dlg);
    QSlider* sHora = new QSlider(Qt::Horizontal, &dlg);
    sHora->setRange(12, 36);                       // meias horas: 6h às 18h
    sHora->setValue(int((m_vp->sunOn() ? m_vp->sunHour() : 10.0) * 2));
    const auto horaTxt = [lHora, sHora] {
        lHora->setText(QStringLiteral("Sol às %1:%2")
                           .arg(sHora->value() / 2, 2, 10, QLatin1Char('0'))
                           .arg(sHora->value() % 2 ? QStringLiteral("30")
                                                   : QStringLiteral("00")));
    };
    connect(sHora, &QSlider::valueChanged, &dlg, horaTxt);
    horaTxt();
    QComboBox* cQual = new QComboBox(&dlg);
    cQual->addItems({QStringLiteral("Rascunho (rápido)"),
                     QStringLiteral("Normal"),
                     QStringLiteral("Alta (mais lenta)")});
    cQual->setCurrentIndex(1);
    QComboBox* cRes = new QComboBox(&dlg);
    cRes->addItems({QStringLiteral("1280 × 720"),
                    QStringLiteral("1920 × 1080"),
                    QStringLiteral("3840 × 2160 (4K)")});
    cRes->setCurrentIndex(1);
    QCheckBox* cInt = new QCheckBox(
        QStringLiteral("Interior (luz de preenchimento)"), &dlg);
    cInt->setChecked(m_vp->walkOn());       // R40: auto no walkthrough
    QComboBox* cCeu = new QComboBox(&dlg);  // R46: céu real
    cCeu->addItems({QStringLiteral("Sol físico (hora acima)"),
                    QStringLiteral("Céu real (foto HDRI)")});
    connect(cCeu, &QComboBox::currentIndexChanged, &dlg,
            [sHora, lHora](int i) {         // HDRI: a hora vem da foto
                sHora->setEnabled(i == 0);
                lHora->setEnabled(i == 0);
            });
    // R47: CENÁRIO (presets) — aplica na hora e o usuário VÊ os controles se
    // moverem; mexer em qualquer um depois volta pra "Personalizado" (nada de
    // preset fantasma "ativo" que ninguém sabe se está valendo).
    QComboBox* cPreset = new QComboBox(&dlg);
    cPreset->addItems({QStringLiteral("Entardecer dourado"),
                       QStringLiteral("Meio-dia neutro"),
                       QStringLiteral("Interior acolhedor"),
                       QStringLiteral("Personalizado")});
    cPreset->setCurrentIndex(3);            // começa com o que já está na tela
    bool aplicando = false;                 // guard: senão o próprio apply
                                            // flipa pra "Personalizado"
    connect(cPreset, &QComboBox::currentIndexChanged, &dlg, [&](int i) {
        if (i > 2) return;
        int mh = sHora->value(), ce = cCeu->currentIndex(),
            qu = cQual->currentIndex();
        bool inte = cInt->isChecked();
        presetFotografo(i, mh, ce, inte, qu);
        aplicando = true;
        sHora->setValue(mh);
        cCeu->setCurrentIndex(ce);
        cInt->setChecked(inte);
        cQual->setCurrentIndex(qu);
        aplicando = false;
    });
    const auto viraPersonalizado = [&aplicando, cPreset] {
        if (!aplicando) cPreset->setCurrentIndex(3);
    };
    connect(sHora, &QSlider::valueChanged, &dlg, viraPersonalizado);
    connect(cCeu, &QComboBox::currentIndexChanged, &dlg, viraPersonalizado);
    connect(cQual, &QComboBox::currentIndexChanged, &dlg, viraPersonalizado);
    connect(cInt, &QCheckBox::toggled, &dlg, viraPersonalizado);
    QFormLayout* topo = new QFormLayout;
    topo->addRow(QStringLiteral("Cenário:"), cPreset);
    v->addLayout(topo);
    v->addWidget(lHora);
    v->addWidget(sHora);
    // R52: A CÂMERA DA FOTO. Era invisível e sempre a do viewport — e como se
    // modela olhando de CIMA, a foto saía com a barriga cinza do HDRI no lugar
    // do céu. COMBO e não checkbox: a escolha aparece por extenso, e checkbox
    // marcado é fácil de não ver.
    // O padrão NÃO reenquadra sempre: câmera alta é enquadramento LEGÍTIMO —
    // a planta humanizada da R35 (telhado oculto, vista de cima) foi entregue
    // 2× assim. Deriva do que o usuário está fazendo:
    //   • em walkthrough → ele está de pé DENTRO da cena: a vista é a foto;
    //   • olhando muito de cima (pitch > 50°) → é vista de trabalho: enquadra;
    //   • resto → respeita.
    QComboBox* cCam = new QComboBox;
    cCam->addItems({QStringLiteral("Como estou vendo"),
                    QStringLiteral("Enquadrar para foto (nível do olho)")});
    const bool deCima = !m_vp->walkOn() && m_vp->pitchAtual() > 50.0f;
    cCam->setCurrentIndex(deCima ? 1 : 0);
    // DECIDIR e EXPLICAR são coisas diferentes. O default só vira em 50° (ser
    // conservador com a intenção do usuário), mas o céu some bem antes: ele
    // entra no quadro só enquanto o mergulho for menor que MEIO FOV. Com
    // pitch=38° e fov=42° a foto já sai sem céu — avisar ali e deixar a
    // escolha com ele é honesto; decidir por ele, não.
    const bool semCeu = !m_vp->walkOn() &&
                        m_vp->pitchAtual() > m_vp->fovY() * 0.5f;
    QFormLayout* form = new QFormLayout;
    form->addRow(QStringLiteral("Câmera:"), cCam);
    form->addRow(QStringLiteral("Céu:"), cCeu);
    form->addRow(QStringLiteral("Qualidade:"), cQual);
    form->addRow(QStringLiteral("Resolução:"), cRes);
    v->addLayout(form);
    if (semCeu) {   // ENSINA o porquê, em vez de decidir calado
        QLabel* dica = new QLabel(QStringLiteral(
            "Você está olhando de cima — daí a foto sai sem céu."));
        dica->setObjectName(QStringLiteral("ppFoot"));
        dica->setWordWrap(true);
        v->addWidget(dica);
    }
    v->addWidget(cInt);
    QDialogButtonBox* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Fotografar…"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString base = m_studyPath.isEmpty()
                             ? QDir::homePath() + QStringLiteral("/Render.png")
                             : QFileInfo(m_studyPath).absolutePath() +
                                   QStringLiteral("/Render - ") +
                                   QFileInfo(m_studyPath).completeBaseName() +
                                   QStringLiteral(".png");
    const QString out = QFileDialog::getSaveFileName(
        this, QStringLiteral("Salvar render"), base,
        QStringLiteral("Imagem PNG (*.png)"));
    if (out.isEmpty()) return;

    static const int kSamples[] = {32, 128, 384};
    static const int kResX[] = {1280, 1920, 3840};
    static const int kResY[] = {720, 1080, 2160};
    double elev = 30.0, azim = 235.0;
    Viewport3D::sunAnglesFor(m_vp->sunOn() ? m_vp->sunMonth() : 7,
                             sHora->value() / 2.0, m_vp->sunLat(), elev, azim);
    QString blender, err;
    QStringList args;
    const bool enquadrar = cCam->currentIndex() == 1;
    if (!buildRenderJob(out, elev, azim, kSamples[cQual->currentIndex()],
                        kResX[cRes->currentIndex()],
                        kResY[cRes->currentIndex()], cInt->isChecked(),
                        cCeu->currentIndex() == 1, enquadrar, blender, args,
                        err)) {
        statusBar()->showMessage(QStringLiteral("Render: %1").arg(err), 6000);
        return;
    }
    QProgressDialog* prog = new QProgressDialog(
        QStringLiteral("Fotografando no Cycles…"),
        QStringLiteral("Cancelar"), 0, 100, this);
    prog->setWindowModality(Qt::WindowModal);
    prog->setMinimumDuration(0);
    prog->setValue(0);
    QProcess* p = new QProcess(this);
    connect(p, &QProcess::readyReadStandardOutput, prog, [p, prog] {
        const QString txt = QString::fromLocal8Bit(p->readAllStandardOutput());
        const QRegularExpression re(QStringLiteral("Sample (\\d+)/(\\d+)"));
        auto it = re.globalMatch(txt);
        while (it.hasNext()) {
            const auto m = it.next();
            const int tot = m.captured(2).toInt();
            if (tot > 0)
                prog->setValue(m.captured(1).toInt() * 100 / tot);
        }
    });
    connect(prog, &QProgressDialog::canceled, p, &QProcess::kill);
    connect(p, &QProcess::finished, this,
            [this, p, prog, out](int code, QProcess::ExitStatus) {
                prog->close();
                prog->deleteLater();
                if (code == 0 && QFileInfo::exists(out)) {
                    statusBar()->showMessage(
                        QStringLiteral("Render pronto: %1")
                            .arg(QFileInfo(out).fileName()), 8000);
                    QDesktopServices::openUrl(QUrl::fromLocalFile(out));
                } else {
                    statusBar()->showMessage(
                        QStringLiteral("Render cancelado ou falhou (código %1).")
                            .arg(code), 8000);
                }
                p->deleteLater();
            });
    p->start(blender, args);
}

void ZendoWindow::shootAndQuit(const QString& pngPath) {
    QTimer::singleShot(1200, this, [this, pngPath] {
        if (m_qaNight) m_vp->setDay(false);          // R5: modo Noite
        if (m_qaNewStudy) {              // R1: estudo do zero com Ensō-san
            showSpace();
            m_vp->addScaleFigure();
        }
        if (!m_qaRectX.isEmpty()) m_vp->qaRectExact(m_qaRectX);   // R2
        if (m_qaRect[0] >= 0.0)
            m_vp->qaRect(m_qaRect[0], m_qaRect[1], m_qaRect[2], m_qaRect[3]);
        if (m_qaLine[0] >= 0.0)
            m_vp->qaLine(m_qaLine[0], m_qaLine[1], m_qaLine[2], m_qaLine[3]);
        if (!m_qaPencil.isEmpty()) {         // "x1,y1,x2,y2,..." cliques
            QVector<double> pts;
            for (const QString& t : m_qaPencil.split(','))
                pts.push_back(t.toDouble());
            m_vp->qaPencil(pts);
        }
        if (m_qaStyle >= 0) m_vp->setStyle(m_qaStyle);
        if (!m_qaClip.isEmpty()) {           // "Y,5.4"
            const QStringList p = m_qaClip.split(',');
            m_vp->setClip(true, p.value(0).at(0).toLatin1(),
                          p.value(1).toDouble());
        }
        if (!m_qaSun.isEmpty()) {            // "mes,hora[,lat]"
            const QStringList p = m_qaSun.split(',');
            m_vp->setSun(true, p.value(0).toInt(), p.value(1).toDouble(),
                         p.value(2, QStringLiteral("-3.73")).toDouble());
        }
        if (!m_qaCircle.isEmpty()) {
            const QStringList p = m_qaCircle.split(',');
            if (p.size() == 4)
                m_vp->qaCircle(p[0].toDouble(), p[1].toDouble(),
                               p[2].toDouble(), p[3].toDouble());
        }
        if (m_qaPickX >= 0.0 && m_qaPickY >= 0.0)
            m_vp->qaPick(m_qaPickX, m_qaPickY);
        if (!m_qaVcb.isEmpty()) m_vp->vcbApply(m_qaVcb);
        if (m_qaDblPick[0] >= 0.0)
            m_vp->qaDoublePick(m_qaDblPick[0], m_qaDblPick[1]);
        if (m_qaGlue) m_vp->glueSelected();
        if (std::abs(m_qaPull) > 1e-9)
            m_vp->pushPullSelected(m_qaPull);
        if (!m_qaMkComp.isEmpty()) m_vp->makeComponent(m_qaMkComp);
        if (!m_qaInsComp.isEmpty()) {        // "NAME,x1,y1[,x2,y2...]"
            const QStringList p = m_qaInsComp.split(',');
            for (int i = 1; i + 1 < p.size(); i += 2)
                m_vp->qaInsertComp(p[0], p[i].toDouble(), p[i + 1].toDouble());
        }
        if (!m_qaMove.isEmpty()) {
            const QStringList p = m_qaMove.split(',');
            if (p.size() == 4)
                m_vp->qaMove(p[0].toDouble(), p[1].toDouble(),
                             p[2].toDouble(), p[3].toDouble(), m_qaMoveCopy);
        }
        if (std::abs(m_qaMoveZ) > 1e-9) m_vp->moveSelectedZ(m_qaMoveZ);
        if (std::abs(m_qaRot) > 1e-9) m_vp->rotateSelected(m_qaRot);
        if (m_qaScale > 0.0) m_vp->scaleSelected(m_qaScale);
        if (m_qaRedef) m_vp->redefineComponent();
        if (m_qaOffset > 0.0) m_vp->offsetSelectedFace(m_qaOffset);
        if (!m_qaRoof.isEmpty()) {               // "h[,beiral[,4]]"
            const QStringList p = m_qaRoof.split(',');
            m_vp->roofSelected(p.value(0).toDouble(),
                               p.value(1, QStringLiteral("0")).toDouble(),
                               p.value(2) == QLatin1String("4"));
        }
        if (m_qaTerrain) m_vp->addTerrain(0.30, 3.0);
        if (m_qaDel) m_vp->deleteSelected();
        if (!m_qaPaint.isEmpty()) {
            const QStringList c = m_qaPaint.split(',');
            if (c.size() == 3)
                m_vp->paintSelected(QColor(c[0].toInt(), c[1].toInt(),
                                           c[2].toInt()));
        }
        for (int i = 0; i < m_qaUndo; ++i) m_vp->undoLast();
        for (int i = 0; i < m_qaRedo; ++i) m_vp->redoLast();   // G2
        if (!m_qaMkScene.isEmpty()) {        // captura a câmera corrente
            Scene s;
            s.name = m_qaMkScene;
            m_vp->cameraState(s.yaw, s.pitch, s.dist, s.tgt);
            m_scenes.push_back(s);
        }
        if (!m_qaGoScene.isEmpty())
            for (const Scene& s : m_scenes)
                if (s.name == m_qaGoScene)
                    m_vp->setCameraState(s.yaw, s.pitch, s.dist, s.tgt);
        if (!m_qaImpObj.isEmpty()) {   // R15: "arquivo|escala|y" (antes do
            const QStringList p = m_qaImpObj.split('|');   // selbox limpar)
            const QString nome = m_vp->importObjComponent(
                p.value(0), p.value(1, QStringLiteral("1")).toDouble(),
                p.value(2) == QLatin1String("y"));
            if (!nome.isEmpty()) m_vp->qaInsertComp(nome, 0.5, 0.55);
        }
        if (m_qaInspect) m_vp->inspectSolids();                        // R17
        if (m_qaFixSolid) m_vp->fixSelectedSolid();                    // R17
        if (m_qaCleanup) m_vp->cleanupModel();                         // R17
        if (!m_qaMirror.isEmpty())                                     // R17
            m_vp->mirrorSelected(m_qaMirror == QLatin1String("x")   ? 0
                                 : m_qaMirror == QLatin1String("y") ? 1
                                                                    : 2);
        if (!m_qaSelBox.isEmpty()) m_vp->qaSelBox(m_qaSelBox);         // G3
        // R35/R38: tex/texscale DEPOIS de pick/dblpick E selbox — "textura na
        // seleção" (única OU múltipla) precisa da seleção pronta.
        if (!m_qaTex.isEmpty()) {            // "arquivo|escala" pós-seleção
            const QStringList p = m_qaTex.split('|');
            m_vp->applyTexture(p.value(0),
                               p.value(1, QStringLiteral("1")).toDouble());
        }
        if (!m_qaTexScale.isEmpty())                               // R13
            m_vp->setTextureScaleSelected(m_qaTexScale.toDouble());
        if (m_qaSubtract) m_vp->subtractSelected();                    // R21
        if (m_qaUnite) m_vp->uniteSelected();                          // R24
        if (!m_qaSketch3d.isEmpty()) m_vp->qaSketch3d(m_qaSketch3d);   // G2
        if (m_qaFollow) m_vp->followMe();                              // G4
        if (!m_qaArray.isEmpty()) m_vp->vcbApply(m_qaArray);   // G4 pós-cópia
        if (m_qaOrtho) m_vp->setOrtho(true);                           // G6
        if (m_qaFog > 1e-6) m_vp->setFog(true, float(m_qaFog));        // G6
        if (!m_qaMkGroup.isEmpty()) m_vp->makeGroup(m_qaMkGroup);      // G5
        if (!m_qaTagSet.isEmpty()) m_vp->assignTag(m_qaTagSet);        // G5
        if (!m_qaTagVis.isEmpty()) {                                   // G5
            const QStringList p = m_qaTagVis.split(',');
            if (p.size() == 2)
                m_vp->setTagVisible(p[0], p[1].toInt() != 0);
        }
        if (!m_qaArcQ.isEmpty()) m_vp->qaArc(m_qaArcQ);                // R7
        if (!m_qaProtr.isEmpty()) m_vp->qaProtractor(m_qaProtr);       // R7
        if (!m_qaScaleQ.isEmpty()) m_vp->qaScaleTo(m_qaScaleQ);        // R7
        if (!m_qaFmPerim.isEmpty()) m_vp->qaFmPerim(m_qaFmPerim);      // R8
        if (!m_qaBucket.isEmpty()) {                                   // R6
            const QStringList p = m_qaBucket.split(',');
            if (p.size() == 3)
                m_vp->setActiveColor(p[0].toFloat() / 255.0f,
                                     p[1].toFloat() / 255.0f,
                                     p[2].toFloat() / 255.0f);
        }
        if (!m_qaPaintAt.isEmpty()) m_vp->qaPaintAt(m_qaPaintAt);      // R6
        if (!m_qaPalette.isEmpty()) {                                  // R5
            const QStringList p = m_qaPalette.split(',');
            if (p.size() == 3)
                m_vp->paletteApplyColor(p[0].toFloat() / 255.0f,
                                        p[1].toFloat() / 255.0f,
                                        p[2].toFloat() / 255.0f);
        }
        if (!m_qaMoveM.isEmpty())                                      // R4
            m_vp->qaMoveLate(m_qaMoveM, false);
        if (!m_qaPencilX.isEmpty()) m_vp->qaPencilExact(m_qaPencilX);  // R4
        if (!m_qaCtxAt.isEmpty()) {                                    // G5
            const QStringList p = m_qaCtxAt.split(',');
            if (p.size() == 2)
                m_vp->qaCtxAt(p[0].toDouble(), p[1].toDouble());
        }
        if (!m_qaTape.isEmpty()) m_vp->qaTape(m_qaTape);               // G4
        if (!m_qaStair.isEmpty()) m_vp->qaStair(m_qaStair);            // R41
        if (!m_qaGuard.isEmpty()) m_vp->qaGuard(m_qaGuard);            // R43
        if (!m_qaSlabHole.isEmpty()) m_vp->qaSlabHole(m_qaSlabHole);   // R43
        if (!m_qaDim3d.isEmpty()) m_vp->qaDim3d(m_qaDim3d);            // R26
        if (!m_qaDimClick.isEmpty()) m_vp->qaDimClick(m_qaDimClick);   // R27
        if (!m_qaWalk.isEmpty()) m_vp->qaWalk(m_qaWalk);               // R27
        if (!m_qaWalkSim.isEmpty()) m_vp->qaWalkSim(m_qaWalkSim);      // R28
        if (!m_qaClipFace.isEmpty()) m_vp->qaClipFace(m_qaClipFace);   // R30
        if (!m_qaClipPlane.isEmpty()) m_vp->qaClipPlane(m_qaClipPlane); // R30
        if (!m_qaClipSlide.isEmpty())                                  // R34
            m_vp->qaClipSlide(m_qaClipSlide.toDouble());
        if (!m_qaClipDrag.isEmpty()) m_vp->qaClipDrag(m_qaClipDrag);   // R34
        if (!m_qaDimAng.isEmpty()) m_vp->qaDimAng(m_qaDimAng);         // R34
        if (m_qaWalkEnter) m_vp->setWalkthrough(true);   // R27: caminho do F8
        if (!m_qaPosCam.isEmpty()) m_vp->qaPosCam(m_qaPosCam);         // R28
        if (!m_qaErase.isEmpty()) {                                // G2/R8
            const QStringList c = m_qaErase.split(',');
            if (c.size() >= 2)
                m_vp->qaErase(c[0].toDouble(), c[1].toDouble(),
                              c.size() > 2 &&
                                  c[2] == QLatin1String("hide"));
        }
        if (!m_qaVMove.isEmpty()) m_vp->qaVertexMove(m_qaVMove);       // G2
        if (!m_qaHover.isEmpty()) m_vp->qaHover(m_qaHover);   // G1: inferência
        if (m_qaInferBench > 0) m_vp->qaInferBench(m_qaInferBench);   // R61
        if (m_qaPickParity > 0) m_vp->qaPickParity(m_qaPickParity);  // R61
        if (!m_qaStale.isEmpty()) {                                 // R61
            const QStringList c = m_qaStale.split(',');
            if (c.size() == 2) m_vp->qaStale(c[0].toDouble(), c[1].toDouble());
        }
        if (m_qaCtrlS) {   // R55: o Ctrl+S de verdade. Se ele abrir diálogo
            // num app headless, ESTE processo trava e o timeout mata a QA —
            // o teste falha alto, que é como um teste tem que falhar.
            saveStudyQuick();
            emit m_vp->pickInfo(
                QStringLiteral("qa-ctrls: gravou em [%1]").arg(m_studyPath));
        }
        if (!m_qaBalde.isEmpty()) {          // R55: balde + hover DE VERDADE
            const QStringList p = m_qaBalde.split(',');
            if (p.size() == 2) {
                const bool antes = m_stack->currentWidget() == m_vp;
                m_vp->setTool(Viewport3D::Tool::Paint);
                const bool depois = m_stack->currentWidget() == m_vp;
                emit m_vp->pickInfo(
                    QStringLiteral("qa-stack: antes=%1 depois=%2")
                        .arg(antes ? "espaco" : "tela-inicial")
                        .arg(depois ? "espaco" : "tela-inicial"));
                m_vp->qaMouseMove(p[0].toDouble(), p[1].toDouble());
            }
        }
        if (!m_qaObj.isEmpty()) {            // exports SÓ depois das edições
            const int nf = m_vp->exportObj(m_qaObj);
            m_lastInfo = QStringLiteral("obj tris=%1").arg(nf);
        }
        if (!m_qaGltf.isEmpty()) {
            const int nf = m_vp->exportGltf(m_qaGltf);
            m_lastInfo = QStringLiteral("gltf tris=%1").arg(nf);
        }
        if (m_qaAutosave) {                  // R48: a função REAL do timer
            const QString arq = fazerAutosave();
            // sentinela desta "sessão" — sem ela, o run seguinte não teria
            // como saber que houve queda (é ela que define queda).
            QFile s(arqSentinela());
            if (s.open(QIODevice::WriteOnly | QIODevice::Truncate))
                s.write("1");
            // PROVA POR CONTEÚDO (lição R46/R47): a geometria DENTRO do
            // arquivo tem que ser a mesma da cena viva. Comparo só os meshes
            // — o thumb do JSON é um grab de tela e varia entre chamadas.
            const QByteArray mem =
                QJsonDocument(m_vp->studyMeshes()).toJson();
            QByteArray disco;
            QFile f(arq);
            if (f.open(QIODevice::ReadOnly))
                disco = QJsonDocument(
                            QJsonDocument::fromJson(f.readAll())
                                .object()
                                .value(QStringLiteral("meshes"))
                                .toArray())
                            .toJson();
            const QString shaMem = QString::fromLatin1(
                QCryptographicHash::hash(mem, QCryptographicHash::Sha256)
                    .toHex());
            const QString shaDisco = QString::fromLatin1(
                QCryptographicHash::hash(disco, QCryptographicHash::Sha256)
                    .toHex());
            m_lastInfo = QStringLiteral("autosave arq=[%1] bytes=%2 "
                                        "meshes_sha_memoria=%3 "
                                        "meshes_sha_arquivo=%4 iguais=%5")
                             .arg(arq)
                             .arg(QFileInfo(arq).size())
                             .arg(shaMem.left(16))
                             .arg(shaDisco.left(16))
                             .arg(!disco.isEmpty() && shaMem == shaDisco);
        }
        if (m_qaDirtyBase) {                 // R48: o ciclo do recuperado
            // Reproduz o BLOQUEIA da auditoria: depois de RECUPERAR, a pilha
            // de undo está vazia mas a cena está suja (não existe em disco).
            // Um Ctrl+Z inocente zerava m_edited (undoLast fazia
            // m_edited = !m_undo.empty()) e o app fechava SEM PERGUNTAR,
            // apagando a recuperação junto. As horas evaporavam.
            const bool antes = m_vp->edited();       // pós-edição do --rectx
            m_vp->markEdited();                      // = o que a recuperação faz
            m_vp->undoLast();                        // "me arrependi"
            m_lastInfo = QStringLiteral("dirtybase antes=%1 apos_undo=%2 "
                                        "(esperado 1 1; sem o fix daria 1 0 "
                                        "= trabalho perdido em silencio)")
                             .arg(antes)
                             .arg(m_vp->edited());
        }
        if (m_qaProtecao) {                  // R48: o iniciarProtecao REAL
            // prova do fix da CORRIDA: depois de ofertar, o auto.zendo tem
            // que ter sido RESERVADO (virou oferta.zendo) — assim o timer de
            // 3 min volta a gravar em auto.zendo sem tocar no que a oferta
            // ainda vai abrir.
            iniciarProtecao();
            const QString d = pastaRecuperacao();
            m_lastInfo = QStringLiteral("protecao auto=%1 oferta=%2 "
                                        "oferta_origem=%3 sentinela=%4 "
                                        "(esperado 0 1 1 1)")
                             .arg(QFileInfo::exists(d + QStringLiteral("/auto.zendo")))
                             .arg(QFileInfo::exists(d + QStringLiteral("/oferta.zendo")))
                             .arg(QFileInfo::exists(d + QStringLiteral("/oferta.origem")))
                             .arg(QFileInfo::exists(arqSentinela()));
        }
        if (m_qaRecovery) {                  // R48: a DECISÃO real
            QString arq, origem;
            const int d = avaliarRecuperacao(arq, origem);
            m_lastInfo = QStringLiteral("recovery decisao=%1 (%2) origem=[%3] "
                                        "sentinela=%4 auto=%5")
                             .arg(d)
                             .arg(d == 0   ? QStringLiteral("nada a oferecer")
                                  : d == 1 ? QStringLiteral("OFERECER")
                                           : QStringLiteral("obsoleto"))
                             .arg(origem)
                             .arg(QFileInfo::exists(arqSentinela()))
                             .arg(QFileInfo::exists(arq));
        }
        if (m_qaLimpeza) {                   // R48: a saída limpa real
            limparRecuperacao();
            m_lastInfo = QStringLiteral("limpeza: sentinela=%1 auto=%2 "
                                        "(esperado 0 0)")
                             .arg(QFileInfo::exists(arqSentinela()))
                             .arg(QFileInfo::exists(
                                 pastaRecuperacao() +
                                 QStringLiteral("/auto.zendo")));
        }
        if (m_qaAjuda) {                     // R48: cobertura da ajuda
            int comAtalho = 0;
            for (const QAction* a : findChildren<QAction*>())
                if (!a->shortcut().isEmpty()) ++comAtalho;
            const QString cola = colaDeAtalhos();
            m_lastInfo = QStringLiteral("ajuda acoes_com_atalho=%1 "
                                        "linhas_na_cola=%2 menu_ajuda=%3 "
                                        "versao=[%4]")
                             .arg(comAtalho)
                             .arg(cola.count(QStringLiteral("<tr>")))
                             .arg(menuBar()
                                      ->findChild<QMenu*>(
                                          QString(), Qt::FindDirectChildrenOnly)
                                  != nullptr)
                             .arg(QCoreApplication::applicationVersion());
        }
        if (!m_qaEngine.isEmpty()) {         // R47: "url;sha256;dir[;bytes]"
            const QStringList p = m_qaEngine.split(';');
            if (p.size() >= 3) {
                BlenderInstaller inst;       // sem modal no caminho da flag
                QEventLoop loop;
                bool ok = false, fim = false;
                QString erro, exe;
                connect(&inst, &BlenderInstaller::terminou, &loop,
                        [&](bool o, const QString& e, const QString& x) {
                            ok = o; erro = e; exe = x; fim = true; loop.quit();
                        });
                inst.instalar(p[0], p[1], p[2],
                              p.size() > 3 ? p[3].toLongLong() : 0);
                if (!fim) loop.exec();
                // PROVA POR CONTEÚDO, não por existência (lição R46 — o PNG
                // velho que mentiu "ok"): sha256 do blender.exe que REALMENTE
                // está no disco, achado por um findBlender fresco na raiz
                // injetada (que nunca enxerga o Blender real da máquina).
                QString shaExe;
                const QString achado = findBlender(p[2]);
                QFile fexe(achado);
                if (!achado.isEmpty() && fexe.open(QIODevice::ReadOnly)) {
                    QCryptographicHash h(QCryptographicHash::Sha256);
                    if (h.addData(&fexe))
                        shaExe = QString::fromLatin1(h.result().toHex());
                }
                m_lastInfo = QStringLiteral(
                                 "engine ok=%1 erro=[%2] bytes=%3 hash_calc=%4 "
                                 "hash_esp=%5 tar_rc=%6 findBlender=[%7] "
                                 "exe_sha256=%8")
                                 .arg(ok)
                                 .arg(erro)
                                 .arg(inst.bytesBaixados())
                                 .arg(inst.hashCalculado().left(16))
                                 .arg(p[1].left(16))
                                 .arg(inst.codigoTar())
                                 .arg(achado)
                                 .arg(shaExe.left(16));
            }
        }
        if (m_qaPreset >= 0) {               // R47: presets pela função REAL
            int mh = 20, ce = 0, qu = 1;
            bool inte = false;
            presetFotografo(m_qaPreset, mh, ce, inte, qu);
            m_lastInfo =
                QStringLiteral("preset %1 -> hora=%2:%3 ceu=%4 interior=%5 "
                               "qual=%6")
                    .arg(m_qaPreset)
                    .arg(mh / 2, 2, 10, QLatin1Char('0'))
                    .arg(mh % 2 ? QStringLiteral("30") : QStringLiteral("00"))
                    .arg(ce)
                    .arg(inte)
                    .arg(qu);
        }
        if (!m_qaRender.isEmpty()) {         // R36: Fotógrafo ponta a ponta
            QString blender, err;
            QStringList rargs;
            double relev = 30.0, razim = 235.0;
            if (m_vp->sunOn()) m_vp->sunAngles(relev, razim);
            if (buildRenderJob(m_qaRender, relev, razim, 128, 1920, 1080,
                               m_vp->walkOn(), m_qaHdri, m_qaEnquadrar,
                               blender, rargs, err)) {
                QFile::remove(m_qaRender);   // R46: "ok" com PNG velho mentia
                const int rc = QProcess::execute(blender, rargs);
                m_lastInfo = QStringLiteral("render rc=%1 ok=%2")
                                 .arg(rc)
                                 .arg(QFileInfo::exists(m_qaRender));
            } else {
                m_lastInfo = QStringLiteral("render falhou: %1").arg(err);
            }
        }
        if (!m_qaSaveAs.isEmpty()) saveStudy(m_qaSaveAs);
        if (!m_qaElev.isEmpty()) {              // "S,arquivo.zencad"
            const int comma = m_qaElev.indexOf(',');
            const bridge::Result r = bridge::exportElevation(
                m_vp->meshPointers(), m_qaElev.at(0).toUpper().toLatin1(),
                m_qaElev.mid(comma + 1));
            m_lastInfo = QStringLiteral("elev ok=%1 segs=%2 %3")
                             .arg(r.ok).arg(r.edgeSegs).arg(r.error);
        }
        if (!m_qaCut.isEmpty()) {               // "Y,5.4,+,arquivo.zencad"
            const QStringList p = m_qaCut.split(',');
            if (p.size() >= 4) {
                const bridge::Result r = bridge::exportSection(
                    m_vp->meshPointers(), p[0].at(0).toUpper().toLatin1(),
                    p[1].toDouble(), p[2] == QLatin1String("-") ? -1 : 1,
                    QStringList(p.mid(3)).join(','));
                m_lastInfo = QStringLiteral("cut ok=%1 loops=%2 segs=%3 %4")
                                 .arg(r.ok).arg(r.cutLoops).arg(r.edgeSegs)
                                 .arg(r.error);
            }
        }
        if (!m_qaCutPlane.isEmpty()) {          // R31: "a,b,c,d,arquivo.zencad"
            const QStringList p = m_qaCutPlane.split(',');
            if (p.size() >= 5) {
                const bridge::Result r = bridge::exportSectionPlane(
                    m_vp->meshPointers(),
                    {p[0].toDouble(), p[1].toDouble(), p[2].toDouble()},
                    p[3].toDouble(), QStringList(p.mid(4)).join(','));
                m_lastInfo =
                    QStringLiteral("cutplane ok=%1 loops=%2 segs=%3 %4")
                        .arg(r.ok).arg(r.cutLoops).arg(r.edgeSegs)
                        .arg(r.error);
            }
        }
        if (!m_qaVista.isEmpty()) {
            // R52: dispara a QAction REAL da vista (acha pelo texto do menu) e
            // dumpa o estado que ela produziu. A regra é da R27/R28 e tem 25
            // levas: QA de câmera exercita a FUNÇÃO, não o estado final —
            // replicar a conta aqui provaria a minha aritmética, não o produto.
            // Item D foi o único desta leva sem sonda, e foi o único com bug
            // (o zoomExtents virava no-op sob o setCameraPose). Não é acaso.
            QAction* alvo = nullptr;
            for (QAction* a : findChildren<QAction*>()) {
                QString t = a->text();
                t.remove(QLatin1Char('&'));
                if (t.compare(m_qaVista, Qt::CaseInsensitive) == 0) {
                    alvo = a;
                    break;
                }
            }
            if (alvo) alvo->trigger();
            float y, p, d, t[3];
            m_vp->cameraState(y, p, d, t);
            m_lastInfo = QStringLiteral("vista '%1' achou=%2 yaw=%3 pitch=%4 "
                                        "alvo=(%5,%6,%7) dist=%8")
                             .arg(m_qaVista)
                             .arg(alvo != nullptr)
                             .arg(double(y), 0, 'f', 1)
                             .arg(double(p), 0, 'f', 1)
                             .arg(double(t[0]), 0, 'f', 2)
                             .arg(double(t[1]), 0, 'f', 2)
                             .arg(double(t[2]), 0, 'f', 2)
                             .arg(double(d), 0, 'f', 2);
        }
        if (m_qaFoto) {
            // R52: prova do enquadramento SEM abrir o diálogo e — de
            // propósito — SEM --cam. Era a flag de conveniência do QA que
            // enquadrava tudo bonito e escondeu por 15 levas que o usuário
            // fotografa da vista de trabalho. Aqui a régua é a cena crua.
            // Dois números importam: (a) 'dechao' — o bbox de foto EXCLUIU o
            // terreno? (senão a casa vira formiga, R45); (b) 'decima' — o
            // DEFAULT que o combo escolheria com a câmera como está.
            cad::Point3 lo, hi, eye, tgt;
            const bool ok = m_vp->boundsFoto(lo, hi);
            if (ok)
                Viewport3D::enquadrarFoto(lo, hi, yawFoto(),
                                          double(m_vp->fovY()), 16.0 / 9.0,
                                          eye, tgt);
            const bool deCima = !m_vp->walkOn() && m_vp->pitchAtual() > 50.0f;
            // eye.x/eye.y ENTRAM no dump: sem eles o azimute é invisível, e
            // foi exatamente aí que morava o bug do walk (a câmera plantava
            // 180° errada e fotografava os fundos — o dump de eye.z passava
            // igual). Prova que não mostra o eixo do erro não é prova.
            m_lastInfo =
                QStringLiteral("foto ok=%1 bbox=%2x%3x%4 olho=(%5,%6,%7) "
                               "alvo=(%8,%9,%10) decima=%11 pitch=%12 walk=%13")
                    .arg(ok)
                    .arg(hi.x - lo.x, 0, 'f', 2)
                    .arg(hi.y - lo.y, 0, 'f', 2)
                    .arg(hi.z - lo.z, 0, 'f', 2)
                    .arg(eye.x, 0, 'f', 2).arg(eye.y, 0, 'f', 2)
                    .arg(eye.z, 0, 'f', 2)
                    .arg(tgt.x, 0, 'f', 2).arg(tgt.y, 0, 'f', 2)
                    .arg(tgt.z, 0, 'f', 2)
                    .arg(deCima)
                    .arg(double(m_vp->pitchAtual()), 0, 'f', 1)
                    .arg(m_vp->walkOn());
        }
        if (m_qaI18n) {
            // R52: a PROVA do tradutor, por CONTEÚDO (lição da R47: "existe"
            // não é prova). Não olho o QTranslator nem o .qm: pergunto ao
            // PRÓPRIO Qt qual texto ele poria nos botões que ele escreve
            // sozinho — é exatamente o "Cancel" que o dogfooding achou.
            // Isto regride EM SILÊNCIO (basta o .qm não ser copiado no
            // deploy), e silêncio foi o que custou caro na R42/R51.
            QDialogButtonBox bb(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                QDialogButtonBox::Close);
            const auto txt = [&bb](QDialogButtonBox::StandardButton b) {
                QPushButton* p = bb.button(b);
                return p ? p->text().remove(QLatin1Char('&')) : QStringLiteral("?");
            };
            m_lastInfo = QStringLiteral("i18n ok=%1 cancel='%2' close='%3'")
                             .arg(txt(QDialogButtonBox::Cancel) ==
                                  QStringLiteral("Cancelar"))
                             .arg(txt(QDialogButtonBox::Cancel),
                                  txt(QDialogButtonBox::Close));
        }
        {
            // R48: o dump é INCONDICIONAL. Isto aqui era uma lista de 40+
            // flags que TODA leva nova tinha que lembrar de atualizar — e não
            // lembrava: mordeu na R42 (--dblpick/--redef mudos, 2 runs
            // perdidos às cegas) e quase de novo na R43. 3ª mordida = espécie,
            // e a cura já tem precedente aqui (o disarmGestures único matou o
            // stale-arm na R34). Regra nova, sem manutenção: se tem --shot,
            // tem prova ao lado. Um .txt a mais nunca custou nada; voar às
            // cegas custou levas.
            QFile dbg(pngPath + QStringLiteral(".txt"));
            if (dbg.open(QIODevice::WriteOnly | QIODevice::Text))
                dbg.write((QStringLiteral("info: %1\n").arg(m_lastInfo) +
                           m_vp->debugSelState()).toUtf8());
        }
        const bool wantWin = pngPath.endsWith(QLatin1String(".win.png"));
        if (wantWin) {                 // R16: deixa o layout assentar (Info)
            m_vp->grabFramebuffer();   // força o paint que emite entityInfo
            for (int i = 0; i < 3; ++i)          // LayoutRequest é adiado —
                QCoreApplication::processEvents();   // 1 rodada não basta
        }
        const QImage img =
            wantWin ? grab().toImage() : m_vp->grabFramebuffer();
        const bool ok = !img.isNull() && img.save(pngPath);
        QApplication::exit(ok ? 0 : 1);
    });
}
