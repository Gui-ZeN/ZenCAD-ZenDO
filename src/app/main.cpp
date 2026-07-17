// src/app/main.cpp
#include <QApplication>
#include <QSurfaceFormat>
#include <memory>

#include "app/MainWindow.hpp"
#include "ui/Theme.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/LinePattern.hpp"
#include "core/spatial/Quadtree.hpp"
#include "core/math/AABB.hpp"

using namespace cad;

int main(int argc, char** argv) {
    // Contexto OpenGL 4.6 Core + MSAA 8x (anti-aliasing no framebuffer).
    QSurfaceFormat fmt;
    fmt.setVersion(4, 6);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(8);
    fmt.setDepthBufferSize(24);
    fmt.setSwapInterval(1);          // v-sync -> alvo 60 FPS estavel
    QSurfaceFormat::setDefaultFormat(fmt);

    // Multi-doc: um QOpenGLWidget por aba — contextos COMPARTILHADOS deixam a
    // troca de aba mais leve (sem recriar recursos GL a cada show).
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    app.setApplicationName("ZenCAD");
    // R60: o ZenCAD nunca soube dizer qual build era — só o Zendo versionava.
    // Passou a importar: com instaladores separados, "qual versão do seu
    // ZenCAD?" vira pergunta de suporte, e a resposta não pode ser um encolher
    // de ombros. RITUAL DA LEVA: esta linha, a de zendo/main.cpp e o
    // `#define AppVersion` de installer/common.isi andam JUNTAS.
    app.setApplicationVersion(QStringLiteral("2.0.53"));
    app.setOrganizationName("Grupo Christus");
    app.setStyleSheet(zenTheme(ThemeMode::Dark));   // tema Sumi (escuro) por padrão
    app.setWindowIcon(QIcon(QCoreApplication::applicationDirPath() + "/assets/zencad.ico"));

    registerStandardLineTypes();   // DOT/DASHDOT/DIVIDE/... antes de montar a UI

    // Documento headless-friendly. Quadtree p/ 2D; troque por Octree no 3D.
    const AABB worldBounds = AABB::fromCenterHalf({0, 0, 0}, {1e6, 1e6, 1e6});
    auto doc = std::make_unique<DrawingManager>(
        std::make_unique<Quadtree>(worldBounds, /*maxDepth*/ 12, /*split*/ 16));

    MainWindow window(std::move(doc));
    window.resize(1440, 900);
    window.show();

    // Modo verificação: cadapp --shot <png> [--testdraw] renderiza e encerra.
    const QStringList args = QApplication::arguments();
    // Projeto passado na linha de comando (duplo clique / associação .zencad).
    for (int i = 1; i < args.size(); ++i)
        if (args[i].endsWith(QLatin1String(".zencad"), Qt::CaseInsensitive)) {
            window.openProjectAtStartup(args[i]);
            break;
        }
    if (args.contains("--testdraw")) window.testDraw();
    const int shotIdx = args.indexOf("--shot");
    if (shotIdx >= 0 && shotIdx + 1 < args.size())
        window.requestShot(args[shotIdx + 1]);

    // Modo headless: cadapp projeto.zencad --publish saida.pdf
    // (todas as pranchas com viewport num PDF multipágina; sai sem event loop).
    const int pubIdx = args.indexOf("--publish");
    if (pubIdx >= 0 && pubIdx + 1 < args.size())
        return window.publishAllLayoutsTo(args[pubIdx + 1]) ? 0 : 1;
    // QA headless: renderiza a prancha corrente (CTB aplicado) num PNG.
    const int psIdx = args.indexOf("--plotshot");
    if (psIdx >= 0 && psIdx + 1 < args.size())
        return window.plotShotTo(args[psIdx + 1]) ? 0 : 1;

    return app.exec();
}
