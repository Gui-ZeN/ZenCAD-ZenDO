// src/zenio/zenio_smoke.cpp
// Teste headless do FORMATO .zencad — o barramento entre os irmãos do
// ecossistema Zen.
//
// POR QUE ESTE TESTE EXISTE, e por que ele não é uma regressão disfarçada.
// A lição da R57: prova que nasce DEPOIS do apontamento, moldada pra reproduzir
// o sintoma conhecido, é teste de regressão — não detector. Este aqui cobre uma
// CLASSE inteira ("o roundtrip do formato mente?") e roda contra o caso são E o
// doente, porque verde só significa alguma coisa se o vermelho for possível.
//
// O que ele exercita:
//   1. ROUNDTRIP: save → load devolve as mesmas contagens e settings.
//   2. VERSION-GATE: arquivo de formato FUTURO é RECUSADO, não lido pela
//      metade. É a mina que o Fable achou — o save gravava `version` desde o
//      primeiro dia e o load nunca lia. Com um instalador único isso nunca
//      doeu (os dois apps saíam sempre da mesma leva); com produtos de release
//      separado vira perda de dados calada.
//   3. CASO SÃO do mesmo gate: version conhecida ABRE. Sem isto, um load que
//      recusa TUDO passaria no item 2 e o teste seria teatro.
//   4. Arquivo alheio é recusado pelo campo `app`.
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>

#include "core/document/DrawingManager.hpp"
#include "core/document/Style.hpp"
#include "core/layout/Layout.hpp"
#include "core/geometry/Line.hpp"
#include "core/spatial/Quadtree.hpp"
#include "zenio/ProjectIo.hpp"

using namespace cad;

static int g_falhas = 0;

static void checa(bool cond, const char* oque) {
    std::printf("  %s %s\n", cond ? "ok  " : "FALHOU", oque);
    if (!cond) ++g_falhas;
}

static DrawingManager novoDoc() {
    const AABB world{{-1000, -1000, -1000}, {1000, 1000, 1000}};
    return DrawingManager(std::make_unique<Quadtree>(world, 12, 8));
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("nao consegui criar diretorio temporario\n");
        return 2;
    }
    const QString path = dir.filePath("t.zencad");

    // ---- 1. Roundtrip: o que entra é o que sai ------------------------------
    int nEnts = 0;
    {
        DrawingManager doc = novoDoc();
        for (int i = 0; i < 7; ++i)
            doc.addEntity(std::make_unique<Line>(Point3{double(i), 0, 0},
                                                 Point3{double(i), 5, 0}));
        nEnts = int(doc.count());

        LayoutTable layouts;
        StyleTable styles;
        ProjectSettings s;
        s.ltScale      = 2.5;      // valores distintos do default: se o load
        s.unitIndex    = 3;        // ignorar o campo, o default vence e o
        s.unitDecimals = 4;        // teste denuncia.
        s.currentLayer = "0";

        QString err;
        checa(saveProject(path, doc, layouts, styles, s, &err),
              "save grava o projeto");

        DrawingManager d2 = novoDoc();
        LayoutTable l2;
        StyleTable st2;
        ProjectSettings s2;
        checa(loadProject(path, d2, l2, st2, s2, &err), "load le de volta");
        checa(int(d2.count()) == nEnts, "contagem de entidades bate");
        checa(s2.ltScale == 2.5, "settings sobrevivem (ltScale)");
        checa(s2.unitIndex == 3 && s2.unitDecimals == 4,
              "settings sobrevivem (unidade)");
    }

    // ---- 2. Version-gate: formato do FUTURO é recusado ----------------------
    // Reescreve só o campo `version` do arquivo bom — o resto do JSON continua
    // perfeitamente legível. Se o gate não existisse, o load abriria numa boa e
    // descartaria em silêncio o que não conhecesse.
    auto regravaCom = [&](double ver, const QString& dest) {
        QFile f(path);
        f.open(QIODevice::ReadOnly);
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        root["version"] = ver;
        QFile o(dest);
        o.open(QIODevice::WriteOnly);
        o.write(QJsonDocument(root).toJson());
    };

    {
        const QString futuro = dir.filePath("futuro.zencad");
        regravaCom(99, futuro);

        DrawingManager d = novoDoc();
        LayoutTable l; StyleTable st; ProjectSettings s;
        QString err;
        const bool abriu = loadProject(futuro, d, l, st, s, &err);
        checa(!abriu, "version 99 (futura) e RECUSADA");
        checa(err.contains("mais novo"), "e o erro DIZ o motivo ao usuario");
    }

    // ---- 2b. Versão fracionária: o gate não pode falhar ABERTO --------------
    // v1.5 não existe. A 1ª versão usava `toInt(1)`, que devolve o DEFAULT (1,
    // "pode abrir") pra valor fracionário — errando pro lado permissivo bem
    // onde devia desconfiar. Achado da auditoria do Fable.
    {
        const QString frac = dir.filePath("frac.zencad");
        regravaCom(1.5, frac);

        DrawingManager d = novoDoc();
        LayoutTable l; StyleTable st; ProjectSettings s;
        QString err;
        checa(!loadProject(frac, d, l, st, s, &err),
              "version fracionaria (1.5) e RECUSADA, nao arredondada pra 1");
    }

    // ---- 3. O caso SÃO do mesmo gate ---------------------------------------
    // Sem este, um load quebrado que recusa TUDO passaria no item 2. É a
    // diferença entre sensibilidade e especificidade.
    {
        const QString atual = dir.filePath("atual.zencad");
        regravaCom(1, atual);

        DrawingManager d = novoDoc();
        LayoutTable l; StyleTable st; ProjectSettings s;
        QString err;
        checa(loadProject(atual, d, l, st, s, &err),
              "version 1 (conhecida) ABRE — o gate nao recusa tudo");
        checa(int(d.count()) == nEnts, "e o conteudo veio junto");
    }

    // ---- 4. Arquivo que não é nosso ----------------------------------------
    {
        const QString alheio = dir.filePath("alheio.zencad");
        QFile o(alheio);
        o.open(QIODevice::WriteOnly);
        o.write("{\"app\":\"OutroCAD\",\"version\":1}");
        o.close();

        DrawingManager d = novoDoc();
        LayoutTable l; StyleTable st; ProjectSettings s;
        QString err;
        checa(!loadProject(alheio, d, l, st, s, &err), "arquivo alheio e recusado");
    }

    std::printf("%s\n", g_falhas ? "ZENIO SMOKE: FALHOU" : "ZENIO SMOKE: ok");
    return g_falhas ? 1 : 0;
}
