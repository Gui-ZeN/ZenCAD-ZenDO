// src/zendo/main.cpp â€” Zendo, o espaÃ§o 3D do ecossistema Zen (spike).
// Uso: zendo.exe [projeto.zencad] [--shot saida.png]
#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QSurfaceFormat>
#include <QTranslator>

#include "ui/Theme.hpp"
#include "ZendoWindow.hpp"
#include "ZendoChrome.hpp"   // por Ãºltimo: puxa windows.h (enxuto)

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Zendo"));
    // R48: o "Sobre" precisa dizer qual build Ã©. RITUAL DA LEVA:
    // esta linha e o AppVersion do installer/zencad.iss andam JUNTAS.
    app.setApplicationVersion(QStringLiteral("2.0.52"));
    app.setApplicationDisplayName(QStringLiteral("Zendo"));

    // R52: os botoes que o Qt escreve sozinho (OK/Cancel/Close/Yes/No) vinham
    // EM INGLES. O dogfooding da R51 achou "Cancel" no telhado, no Fotografo e
    // em todo QInputDialog -- num app 100% pt-BR, e ao lado do QFileDialog
    // NATIVO, que vem em portugues: "Salvar/Cancelar" e "OK/Cancel" na mesma
    // tela. O Guilherme ja tinha odiado um "Close" solto na R50; nunca era um
    // caso isolado, era o catalogo inteiro faltando.
    // INCONDICIONAL, nao QLocale::system(): o app e pt-BR por design -- num
    // Windows em ingles o certo continua sendo "Cancelar", nao meio a meio.
    // O windeployqt ja gera translations/qt_pt_BR.qm no build-app-rel e o
    // zencad.iss ja embarca translations\* -- por isso isto sao 5 linhas.
    static QTranslator qtTr;
    const QLocale ptBR(QLocale::Portuguese, QLocale::Brazil);
    const QString trDir =
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
    if (qtTr.load(ptBR, QStringLiteral("qt"), QStringLiteral("_"), trDir) ||
        // fallback do dev-run (build-app/ nao passa pelo windeployqt)
        qtTr.load(ptBR, QStringLiteral("qt"), QStringLiteral("_"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTr);

    app.setStyleSheet(cad::zenTheme(cad::ThemeMode::Dark) +
                      zendo::zendoChrome());   // Sumi + moldura prÃ³pria (R33)
    // R34: toda janela nova (diÃ¡logos) ganha a caption escura ao aparecer
    app.installEventFilter(new zendo::DarkTitleFilter(&app));

    // Ãcone prÃ³prio (cubo no ensÅ); cai no da famÃ­lia se faltar.
    const QString dir = QCoreApplication::applicationDirPath() + "/assets/";
    if (QFileInfo::exists(dir + "zendo.ico"))
        app.setWindowIcon(QIcon(dir + "zendo.ico"));
    else if (QFileInfo::exists(dir + "zencad.ico"))
        app.setWindowIcon(QIcon(dir + "zencad.ico"));

    QString file, shot, cam, pick, pull, rect, undo, line, paint, saveAs,
        elev, cut, move, copy, circle, dblpick, movez, roof, vcb, rot, scal,
        offs, pencil, mkcomp, inscomp, mkscene, goscene, sun, tex, style, clip,
        obj, gltf, hover, erase, vmove, sketch3d, redoN, selbox, tape, sides,
        arr, mkgroup, ctxat, tagset, tagvis, fog, rectx, pencilx, movem,
        palette, bucket, paintat, arcq, protr, scaleq, fmperim, texscale,
        impobj, mirror, dim3d, walk, dimclick, poscam, walksim,
        clipface, clipplane, cutplane, clipslide, dimang, clipdrag, render,
        stair, guard, slabhole, engine, qaBalde;
    int preset = -1;                         // R47
    bool inspect = false, fixsolid = false, cleanup = false, subtract = false;
    bool unite = false;
    bool walkenter = false, qaCtrlS = false;
    bool follow = false, ortho = false, newstudy = false, night = false;
    bool del = false, glue = false, terrain = false, redef = false;
    bool hdri = false;                       // R46: cÃ©u real no --render
    bool qaAutosave = false, qaRecovery = false;   // R48
    bool qaLimpeza = false, qaAjuda = false, qaProtecao = false;
    bool qaI18n = false, qaEnquadrar = false, qaFoto = false;  // R52
    QString qaVista;                                          // R52
    QString recdir;
    bool qaDirty = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--shot") && i + 1 < args.size())
            shot = args[++i];
        else if (args[i] == QLatin1String("--cam") && i + 1 < args.size())
            cam = args[++i];   // "yaw,pitch,fator" (graus, graus, x auto-zoom)
        else if (args[i] == QLatin1String("--pick") && i + 1 < args.size())
            pick = args[++i];  // "nx,ny" (0..1): clique sintÃ©tico p/ QA
        else if (args[i] == QLatin1String("--pull") && i + 1 < args.size())
            pull = args[++i];  // metros: empurrar/puxar a face do --pick
        else if (args[i] == QLatin1String("--rect") && i + 1 < args.size())
            rect = args[++i];  // "x1,y1,x2,y2" (0..1): retangulo na face p/ QA
        else if (args[i] == QLatin1String("--undo") && i + 1 < args.size())
            undo = args[++i];  // nÂº de Ctrl+Z apÃ³s as ediÃ§Ãµes (QA)
        else if (args[i] == QLatin1String("--line") && i + 1 < args.size())
            line = args[++i];  // "x1,y1,x2,y2": linha divisora p/ QA
        else if (args[i] == QLatin1String("--paint") && i + 1 < args.size())
            paint = args[++i]; // "r,g,b": pinta a face selecionada (QA)
        else if (args[i] == QLatin1String("--saveas") && i + 1 < args.size())
            saveAs = args[++i]; // grava o estudo .zendo apos as edicoes (QA)
        else if (args[i] == QLatin1String("--elev") && i + 1 < args.size())
            elev = args[++i];   // "S,saida.zencad": exporta elevacao (QA)
        else if (args[i] == QLatin1String("--cut") && i + 1 < args.size())
            cut = args[++i];    // "Y,5.4,+,saida.zencad": exporta corte (QA)
        else if (args[i] == QLatin1String("--del"))
            del = true;         // apaga a selecao (QA)
        else if (args[i] == QLatin1String("--glue"))
            glue = true;        // gruda o solido selecionado (QA)
        else if (args[i] == QLatin1String("--movez") && i + 1 < args.size())
            movez = args[++i];  // sobe/desce o solido selecionado (m)
        else if (args[i] == QLatin1String("--roof") && i + 1 < args.size())
            roof = args[++i];   // telhado 2 aguas sobre a selecao (altura m)
        else if (args[i] == QLatin1String("--terrain"))
            terrain = true;     // plato sob o modelo
        else if (args[i] == QLatin1String("--vcb") && i + 1 < args.size())
            vcb = args[++i];    // medidas digitadas p/ a ultima operacao
        else if (args[i] == QLatin1String("--rot") && i + 1 < args.size())
            rot = args[++i];    // rotaciona a selecao (graus)
        else if (args[i] == QLatin1String("--scale") && i + 1 < args.size())
            scal = args[++i];   // escala a selecao (fator)
        else if (args[i] == QLatin1String("--offset") && i + 1 < args.size())
            offs = args[++i];   // offset da face selecionada (m)
        else if (args[i] == QLatin1String("--pencil") && i + 1 < args.size())
            pencil = args[++i]; // "x1,y1,x2,y2,...": lapis no chao (QA)
        else if (args[i] == QLatin1String("--mkcomp") && i + 1 < args.size())
            mkcomp = args[++i]; // cria componente da selecao (QA)
        else if (args[i] == QLatin1String("--inscomp") && i + 1 < args.size())
            inscomp = args[++i]; // "NOME,x,y[,x2,y2]": insere instancias (QA)
        else if (args[i] == QLatin1String("--redef"))
            redef = true;       // redefine componente a partir da selecao
        else if (args[i] == QLatin1String("--mkscene") && i + 1 < args.size())
            mkscene = args[++i]; // salva cena com a camera corrente (QA)
        else if (args[i] == QLatin1String("--goscene") && i + 1 < args.size())
            goscene = args[++i]; // vai para a cena salva (QA)
        else if (args[i] == QLatin1String("--sun") && i + 1 < args.size())
            sun = args[++i];    // "mes,hora[,lat]": liga o sol (QA)
        else if (args[i] == QLatin1String("--tex") && i + 1 < args.size())
            tex = args[++i];    // "arquivo|escala": textura na selecao (QA)
        else if (args[i] == QLatin1String("--style") && i + 1 < args.size())
            style = args[++i];  // 0 normal 1 mono 2 raio-x (QA)
        else if (args[i] == QLatin1String("--clip") && i + 1 < args.size())
            clip = args[++i];   // "Y,5.4": secao ao vivo (QA)
        else if (args[i] == QLatin1String("--obj") && i + 1 < args.size())
            obj = args[++i];    // exporta OBJ (QA)
        else if (args[i] == QLatin1String("--gltf") && i + 1 < args.size())
            gltf = args[++i];   // exporta glTF (QA)
        else if (args[i] == QLatin1String("--hover") && i + 1 < args.size())
            hover = args[++i];  // G1: "nx,ny;nx,ny" â€” inferÃªncia tipada (QA)
        else if (args[i] == QLatin1String("--qa-ctrls"))
            qaCtrlS = true;       // R55: exercita Salvar (Ctrl+S) sem dialogo
        else if (args[i] == QLatin1String("--qa-balde") && i + 1 < args.size())
            qaBalde = args[++i];  // R55: "nx,ny" - balde + mouseMove real (QA)
        else if (args[i] == QLatin1String("--redo") && i + 1 < args.size())
            redoN = args[++i];  // G2: nÂº de Ctrl+Y apÃ³s os undo (QA)
        else if (args[i] == QLatin1String("--erase") && i + 1 < args.size())
            erase = args[++i];  // G2: "nx,ny" â€” borracha (QA)
        else if (args[i] == QLatin1String("--vmove") && i + 1 < args.size())
            vmove = args[++i];  // G2: "nx,ny,dx,dy,dz" â€” vÃ©rtice + autofold
        else if (args[i] == QLatin1String("--sketch3d") && i + 1 < args.size())
            sketch3d = args[++i];   // G2: "x,y,z;x,y,z;â€¦" lÃ¡pis em MUNDO
        else if (args[i] == QLatin1String("--selbox") && i + 1 < args.size())
            selbox = args[++i];     // G3: "x1,y1,x2,y2[,del]" caixa de seleÃ§Ã£o
        else if (args[i] == QLatin1String("--tape") && i + 1 < args.size())
            tape = args[++i];       // G4: "x1,y1,x2,y2" fita mÃ©trica (guia)
        else if (args[i] == QLatin1String("--dim3d") && i + 1 < args.size())
            dim3d = args[++i];      // R26: "ax,ay,az,bx,by,bz,cx,cy,cz" cota
        else if (args[i] == QLatin1String("--walk") && i + 1 < args.size())
            walk = args[++i];       // R27: "ex,ey,ez,yaw,pitch" 1Âª pessoa
        else if (args[i] == QLatin1String("--dimclick") && i + 1 < args.size())
            dimclick = args[++i];   // R27: "nx,ny;â€¦" cliques da cota (aresta)
        else if (args[i] == QLatin1String("--walkenter"))
            walkenter = true;       // R27: entra no walk pelo caminho do F8
        else if (args[i] == QLatin1String("--poscam") && i + 1 < args.size())
            poscam = args[++i];     // R28: "nx,ny" posiciona cÃ¢mera 1Âª pessoa
        else if (args[i] == QLatin1String("--walksim") && i + 1 < args.size())
            walksim = args[++i];    // R28: "W,60" testa o integrador do walk
        else if (args[i] == QLatin1String("--clipface") && i + 1 < args.size())
            clipface = args[++i];   // R30: "nx,ny" seÃ§Ã£o no plano da face
        else if (args[i] == QLatin1String("--clipplane") && i + 1 < args.size())
            clipplane = args[++i];  // R30: "a,b,c,d" plano de corte direto
        else if (args[i] == QLatin1String("--cutplane") && i + 1 < args.size())
            cutplane = args[++i];   // R31: "a,b,c,d,arquivo" export do corte
        else if (args[i] == QLatin1String("--clipslide") && i + 1 < args.size())
            clipslide = args[++i];  // R34: desliza a Ãºltima seÃ§Ã£o (m)
        else if (args[i] == QLatin1String("--clipdrag") && i + 1 < args.size())
            clipdrag = args[++i];   // R34: "x0,y0,x1,y1" gesto real de arrasto
        else if (args[i] == QLatin1String("--render") && i + 1 < args.size())
            render = args[++i];     // R36: FotÃ³grafo headless "saida.png"
        else if (args[i] == QLatin1String("--stair") && i + 1 < args.size())
            stair = args[++i];      // R41: "ox,oy,dx,dy,w,h,run" escada
        else if (args[i] == QLatin1String("--guard") && i + 1 < args.size())
            guard = args[++i];      // R43: "x1,y1,x2,y2,z,h,gap" guarda-corpo
        else if (args[i] == QLatin1String("--hdri"))
            hdri = true;            // R46: --render com cÃ©u real
        else if (args[i] == QLatin1String("--qa-autosave"))
            qaAutosave = true;      // R48
        else if (args[i] == QLatin1String("--qa-recovery"))
            qaRecovery = true;
        else if (args[i] == QLatin1String("--qa-limpeza"))
            qaLimpeza = true;
        else if (args[i] == QLatin1String("--qa-ajuda"))
            qaAjuda = true;
        else if (args[i] == QLatin1String("--qa-protecao"))
            qaProtecao = true;
        else if (args[i] == QLatin1String("--qa-dirtybase"))
            qaDirty = true;
        else if (args[i] == QLatin1String("--qa-i18n"))
            qaI18n = true;          // R52: prova que o catalogo do Qt carregou
        else if (args[i] == QLatin1String("--enquadrar"))
            qaEnquadrar = true;     // R52: --render com camera de APRESENTACAO
        else if (args[i] == QLatin1String("--qa-foto"))
            qaFoto = true;          // R52: dumpa o resolver do enquadramento
        else if (args[i] == QLatin1String("--qa-vista") && i + 1 < args.size())
            qaVista = args[++i];    // R52: dispara a QAction REAL da vista
        else if (args[i] == QLatin1String("--qa-recdir") && i + 1 < args.size())
            recdir = args[++i];     // R48: raiz isolada do QA
        else if (args[i] == QLatin1String("--qa-engine") && i + 1 < args.size())
            engine = args[++i];     // R47: "url;sha256;dir[;bytes]"
        else if (args[i] == QLatin1String("--preset") && i + 1 < args.size())
            preset = args[++i].toInt();   // R47: 0..3
        else if (args[i] == QLatin1String("--slabhole") && i + 1 < args.size())
            slabhole = args[++i];   // R43: "x1,y1,x2,y2,hx1,hy1,hx2,hy2,zt,th"
        else if (args[i] == QLatin1String("--dimang") && i + 1 < args.size())
            dimang = args[++i];     // R34: cota angular "a...,b(vÃ©rtice)...,c..."
        else if (args[i] == QLatin1String("--sides") && i + 1 < args.size())
            sides = args[++i];      // G4: cÃ­rculo vira polÃ­gono N lados
        else if (args[i] == QLatin1String("--followme"))
            follow = true;          // G4: varre o perfil pelo rascunho
        else if (args[i] == QLatin1String("--array") && i + 1 < args.size())
            arr = args[++i];        // G4: "x3" ou "/3" apÃ³s mover/rotacionar
        else if (args[i] == QLatin1String("--mkgroup") && i + 1 < args.size())
            mkgroup = args[++i];    // G5: agrupa a multi-seleÃ§Ã£o
        else if (args[i] == QLatin1String("--ctxat") && i + 1 < args.size())
            ctxat = args[++i];      // G5: "nx,ny" entra no contexto
        else if (args[i] == QLatin1String("--tagset") && i + 1 < args.size())
            tagset = args[++i];     // G5: tag na seleÃ§Ã£o
        else if (args[i] == QLatin1String("--tagvis") && i + 1 < args.size())
            tagvis = args[++i];     // G5: "tag,0|1"
        else if (args[i] == QLatin1String("--ortho"))
            ortho = true;           // G6: projeÃ§Ã£o paralela (QA)
        else if (args[i] == QLatin1String("--fog") && i + 1 < args.size())
            fog = args[++i];        // G6: densidade da neblina (QA)
        else if (args[i] == QLatin1String("--newstudy"))
            newstudy = true;        // R1: estudo do zero (EnsÅ-san)
        else if (args[i] == QLatin1String("--rectx") && i + 1 < args.size())
            rectx = args[++i];      // R2: "nx,ny,a,b" retÃ¢ngulo EXATO digitado
        else if (args[i] == QLatin1String("--pencilx") && i + 1 < args.size())
            pencilx = args[++i];    // R4: "nx,ny,len" traÃ§o exato
        else if (args[i] == QLatin1String("--movem") && i + 1 < args.size())
            movem = args[++i];      // R4: move multi pÃ³s-selbox
        else if (args[i] == QLatin1String("--night"))
            night = true;           // R5: ambiente Noite (QA)
        else if (args[i] == QLatin1String("--palette") && i + 1 < args.size())
            palette = args[++i];    // R5: "r,g,b" pinta a seleÃ§Ã£o (QA)
        else if (args[i] == QLatin1String("--bucket") && i + 1 < args.size())
            bucket = args[++i];     // R6: arma o balde "r,g,b"
        else if (args[i] == QLatin1String("--paintat") && i + 1 < args.size())
            paintat = args[++i];    // R6: cliques "nx,ny[,ctrl];â€¦"
        else if (args[i] == QLatin1String("--arcq") && i + 1 < args.size())
            arcq = args[++i];       // R7: "ax,ay,bx,by,raio"
        else if (args[i] == QLatin1String("--protr") && i + 1 < args.size())
            protr = args[++i];      // R7: "cx,cy,rx,ry,ang[,copy]"
        else if (args[i] == QLatin1String("--scaleq") && i + 1 < args.size())
            scaleq = args[++i];     // R7: fator na seleÃ§Ã£o
        else if (args[i] == QLatin1String("--fmperim") && i + 1 < args.size())
            fmperim = args[++i];    // R8: "px,py,cx,cy" perfil + caminho
        else if (args[i] == QLatin1String("--texscale") && i + 1 < args.size())
            texscale = args[++i];   // R13: reveste a seleÃ§Ã£o nessa escala
        else if (args[i] == QLatin1String("--impobj") && i + 1 < args.size())
            impobj = args[++i];     // R15: "arquivo|escala|y" importa+insere
        else if (args[i] == QLatin1String("--inspect"))
            inspect = true;         // R17: relatÃ³rio de furos/integridade
        else if (args[i] == QLatin1String("--fixsolid"))
            fixsolid = true;        // R17: tampa os furos do selecionado
        else if (args[i] == QLatin1String("--cleanup"))
            cleanup = true;         // R17: coplanares + purge
        else if (args[i] == QLatin1String("--mirror") && i + 1 < args.size())
            mirror = args[++i];     // R17: "x|y|z" espelha a seleÃ§Ã£o
        else if (args[i] == QLatin1String("--subtract"))
            subtract = true;        // R21: subtrai o menor sÃ³lido do maior
        else if (args[i] == QLatin1String("--unite"))
            unite = true;            // R24: une os 2 sÃ³lidos selecionados
        else if (args[i] == QLatin1String("--move") && i + 1 < args.size())
            move = args[++i];   // "x1,y1,x2,y2": move o solido selecionado
        else if (args[i] == QLatin1String("--copy") && i + 1 < args.size())
            copy = args[++i];   // idem, copiando
        else if (args[i] == QLatin1String("--circle") && i + 1 < args.size())
            circle = args[++i]; // "cx,cy,rx,ry": circulo na face/chao (QA)
        else if (args[i] == QLatin1String("--dblpick") && i + 1 < args.size())
            dblpick = args[++i]; // "nx,ny": seleciona o SOLIDO inteiro (QA)
        else if (!args[i].startsWith(QLatin1String("--")))
            file = args[i];
    }

    // R48: antes da janela â€” a raiz vale pro boot inteiro
    if (!recdir.isEmpty()) ZendoWindow::setPastaRecuperacao(recdir);
    ZendoWindow w;
    w.resize(1440, 900);
    zendo::applyDarkTitleBar(&w);   // R33: a faixa branca do Windows some
    w.show();
    if (!file.isEmpty()) {
        if (file.endsWith(QLatin1String(".zendo"), Qt::CaseInsensitive))
            w.openStudy(file);
        else
            w.openFile(file);
    }
    if (!cam.isEmpty()) {
        const QStringList p = cam.split(',');
        if (p.size() == 3)
            w.setCameraPose(p[0].toFloat(), p[1].toFloat(), p[2].toFloat());
    }
    if (!pick.isEmpty()) {
        const QStringList p = pick.split(',');
        if (p.size() == 2) w.setQaPick(p[0].toDouble(), p[1].toDouble());
    }
    if (!pull.isEmpty()) w.setQaPull(pull.toDouble());
    if (!rect.isEmpty()) {
        const QStringList p = rect.split(',');
        if (p.size() == 4)
            w.setQaRect(p[0].toDouble(), p[1].toDouble(),
                        p[2].toDouble(), p[3].toDouble());
    }
    if (!undo.isEmpty()) w.setQaUndo(undo.toInt());
    if (!line.isEmpty()) {
        const QStringList p = line.split(',');
        if (p.size() == 4)
            w.setQaLine(p[0].toDouble(), p[1].toDouble(),
                        p[2].toDouble(), p[3].toDouble());
    }
    if (!paint.isEmpty()) w.setQaPaint(paint);
    if (!saveAs.isEmpty()) w.setQaSaveAs(saveAs);
    if (!elev.isEmpty()) w.setQaElev(elev);
    if (!cut.isEmpty()) w.setQaCut(cut);
    if (del) w.setQaDel(true);
    if (glue) w.setQaGlue(true);
    if (!movez.isEmpty()) w.setQaMoveZ(movez.toDouble());
    if (!roof.isEmpty()) w.setQaRoof(roof);
    if (terrain) w.setQaTerrain(true);
    if (!vcb.isEmpty()) w.setQaVcb(vcb);
    if (!rot.isEmpty()) w.setQaRot(rot.toDouble());
    if (!scal.isEmpty()) w.setQaScale(scal.toDouble());
    if (!offs.isEmpty()) w.setQaOffset(offs.toDouble());
    if (!pencil.isEmpty()) w.setQaPencil(pencil);
    if (!mkcomp.isEmpty()) w.setQaMkComp(mkcomp);
    if (!inscomp.isEmpty()) w.setQaInsComp(inscomp);
    if (redef) w.setQaRedef(true);
    if (!mkscene.isEmpty()) w.setQaMkScene(mkscene);
    if (!goscene.isEmpty()) w.setQaGoScene(goscene);
    if (!sun.isEmpty()) w.setQaSun(sun);
    if (!tex.isEmpty()) w.setQaTex(tex);
    if (!style.isEmpty()) w.setQaStyle(style.toInt());
    if (!clip.isEmpty()) w.setQaClip(clip);
    if (!obj.isEmpty()) w.setQaObj(obj);
    if (!gltf.isEmpty()) w.setQaGltf(gltf);
    if (!hover.isEmpty()) w.setQaHover(hover);
    if (!qaBalde.isEmpty()) w.setQaBalde(qaBalde);
    if (qaCtrlS) w.setQaCtrlS(true);
    if (!redoN.isEmpty()) w.setQaRedo(redoN.toInt());
    if (!erase.isEmpty()) w.setQaErase(erase);
    if (!vmove.isEmpty()) w.setQaVMove(vmove);
    if (!sketch3d.isEmpty()) w.setQaSketch3d(sketch3d);
    if (!selbox.isEmpty()) w.setQaSelBox(selbox);
    if (!tape.isEmpty()) w.setQaTape(tape);
    if (!sides.isEmpty()) w.setQaSides(sides.toInt());
    if (follow) w.setQaFollow(true);
    if (!arr.isEmpty()) w.setQaArray(arr);
    if (!mkgroup.isEmpty()) w.setQaMkGroup(mkgroup);
    if (!ctxat.isEmpty()) w.setQaCtxAt(ctxat);
    if (!tagset.isEmpty()) w.setQaTagSet(tagset);
    if (!tagvis.isEmpty()) w.setQaTagVis(tagvis);
    if (ortho) w.setQaOrtho(true);
    if (!fog.isEmpty()) w.setQaFog(fog.toDouble());
    if (newstudy) w.setQaNewStudy(true);
    if (!rectx.isEmpty()) w.setQaRectX(rectx);
    if (!pencilx.isEmpty()) w.setQaPencilX(pencilx);
    if (!movem.isEmpty()) w.setQaMoveM(movem);
    if (night) w.setQaNight(true);
    if (!palette.isEmpty()) w.setQaPalette(palette);
    if (!bucket.isEmpty()) w.setQaBucket(bucket);
    if (!paintat.isEmpty()) w.setQaPaintAt(paintat);
    if (!arcq.isEmpty()) w.setQaArcQ(arcq);
    if (!protr.isEmpty()) w.setQaProtr(protr);
    if (!scaleq.isEmpty()) w.setQaScaleQ(scaleq);
    if (!fmperim.isEmpty()) w.setQaFmPerim(fmperim);
    if (!texscale.isEmpty()) w.setQaTexScale(texscale);
    if (!impobj.isEmpty()) w.setQaImpObj(impobj);
    if (inspect) w.setQaInspect(true);
    if (fixsolid) w.setQaFixSolid(true);
    if (cleanup) w.setQaCleanup(true);
    if (!mirror.isEmpty()) w.setQaMirror(mirror);
    if (subtract) w.setQaSubtract(true);
    if (unite) w.setQaUnite(true);
    if (!dim3d.isEmpty()) w.setQaDim3d(dim3d);
    if (!walk.isEmpty()) w.setQaWalk(walk);
    if (!dimclick.isEmpty()) w.setQaDimClick(dimclick);
    if (walkenter) w.setQaWalkEnter(true);
    if (!poscam.isEmpty()) w.setQaPosCam(poscam);
    if (!walksim.isEmpty()) w.setQaWalkSim(walksim);
    if (!clipface.isEmpty()) w.setQaClipFace(clipface);
    if (!clipplane.isEmpty()) w.setQaClipPlane(clipplane);
    if (!cutplane.isEmpty()) w.setQaCutPlane(cutplane);
    if (!clipslide.isEmpty()) w.setQaClipSlide(clipslide);
    if (!clipdrag.isEmpty()) w.setQaClipDrag(clipdrag);
    if (!render.isEmpty()) w.setQaRender(render);
    if (!stair.isEmpty()) w.setQaStair(stair);
    if (!guard.isEmpty()) w.setQaGuard(guard);
    if (!slabhole.isEmpty()) w.setQaSlabHole(slabhole);
    if (hdri) w.setQaHdri(true);
    if (!engine.isEmpty()) w.setQaEngine(engine);
    if (preset >= 0) w.setQaPreset(preset);
    if (qaAutosave) w.setQaAutosave(true);
    if (qaRecovery) w.setQaRecovery(true);
    if (qaLimpeza) w.setQaLimpeza(true);
    if (qaAjuda) w.setQaAjuda(true);
    if (qaProtecao) w.setQaProtecao(true);
    if (qaDirty) w.setQaDirtyBase(true);
    if (qaI18n) w.setQaI18n(true);
    if (qaEnquadrar) w.setQaEnquadrar(true);
    if (qaFoto) w.setQaFoto(true);
    if (!qaVista.isEmpty()) w.setQaVista(qaVista);
    if (!dimang.isEmpty()) w.setQaDimAng(dimang);
    if (!move.isEmpty()) w.setQaMove(move, false);
    if (!copy.isEmpty()) w.setQaMove(copy, true);
    if (!circle.isEmpty()) w.setQaCircle(circle);
    if (!dblpick.isEmpty()) {
        const QStringList p = dblpick.split(',');
        if (p.size() == 2) w.setQaDblPick(p[0].toDouble(), p[1].toDouble());
    }
    if (!shot.isEmpty()) {
        w.shootAndQuit(shot);            // QA: sem oferta, sem painel
    } else {
        w.iniciarProtecao();             // R48: recuperaÃ§Ã£o + sentinela
        w.primeirosPassos(false);        // R48: sÃ³ no 1Âº boot da vida
    }
    return app.exec();
}
