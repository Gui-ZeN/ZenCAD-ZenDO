// src/tools/smoke_main.cpp
// Exercita o kernel CAD inteiro SEM Qt/OpenGL: insere/consulta/seleciona,
// herança de cor ByLayer, emissão para RenderBatch e undo/redo.
// Retorna 0 se todos os checks passarem (usado pelo ctest "smoke").
#include "core/document/DrawingManager.hpp"
#include "core/spatial/Quadtree.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/interaction/ToolController.hpp"
#include "core/snap/SnapEngine.hpp"
#include "core/edit/GeometryOps.hpp"
#include "core/edit/ConstructOps.hpp"
#include "core/edit/IntersectOps.hpp"
#include "core/cli/CommandParser.hpp"
#include "core/geometry/MText.hpp"
#include "core/geometry/Dimension.hpp"
#include "core/geometry/Hatch.hpp"
#include "core/geometry/Wipeout.hpp"
#include "core/geometry/Region.hpp"
#include "core/geometry/Table.hpp"
#include "core/geometry/Ellipse.hpp"
#include "core/snap/Polar.hpp"
#include "core/snap/SnapEngine.hpp"
#include "core/edit/TrimOps.hpp"
#include "core/edit/SegmentOps.hpp"
#include "core/edit/BoundaryTrace.hpp"
#include "core/edit/LoopExtract.hpp"
#include "core/edit/ArrayPathOps.hpp"
#include "core/edit/BooleanOps.hpp"
#include "core/document/Style.hpp"
#include "core/geometry/LinePattern.hpp"
#include "core/text/StrokeFont.hpp"
#include "core/edit/ExtendOps.hpp"
#include "core/edit/ChamferOps.hpp"
#include "core/command/commands/ExplodeCmd.hpp"
#include "core/command/commands/ArrayCmd.hpp"
#include "core/command/commands/ChamferCmd.hpp"
#include "core/command/commands/FilletCmd.hpp"
#include "core/command/commands/OffsetCmd.hpp"
#include "io/DxfWriter.hpp"
#include "io/DxfReader.hpp"
#include "core/geometry/PointEntity.hpp"
#include "core/geometry/Spline.hpp"
#include "core/geometry/Wall.hpp"
#include "core/mesh/HalfEdgeMesh.hpp"
#include "core/geometry/BlockRef.hpp"
#include "core/geometry/AttDef.hpp"
#include "core/command/commands/MakeBlockCmd.hpp"
#include "core/edit/GripOps.hpp"
#include "core/command/commands/GripEditCmd.hpp"
#include "core/command/commands/StretchCmd.hpp"
#include "core/edit/DivideOps.hpp"
#include "core/edit/CleanupOps.hpp"
#include "core/edit/TangentOps.hpp"
#include "core/edit/Tangent3Ops.hpp"
#include "core/edit/SplineOps.hpp"
#include "core/edit/ModifyOps.hpp"
#include "core/edit/InquiryOps.hpp"
#include "core/geometry/XLine.hpp"
#include "core/geometry/RayLine.hpp"
#include "core/geometry/MLine.hpp"
#include "core/geometry/Leader.hpp"
#include "core/geometry/MLeader.hpp"
#include "core/edit/ReviseCloud.hpp"
#include "core/layout/Layout.hpp"
#include "core/document/Layer.hpp"
#include "core/command/commands/MacroCmd.hpp"
#include "core/command/commands/ReplaceCmd.hpp"
#include <cstdio>
#include "core/math/Matrix4.hpp"
#include <cmath>
#include <vector>
#include "core/command/commands/AddEntityCmd.hpp"
#include "core/command/commands/DeleteEntityCmd.hpp"
#include "core/command/commands/EraseCmd.hpp"
#include "core/document/Layer.hpp"
#include "core/math/Ray.hpp"

#include <iostream>
#include <memory>
#include <fstream>
#include <iterator>

using namespace cad;

static int g_failures = 0;

static void check(bool cond, const char* what) {
    std::cout << (cond ? "[ PASS ] " : "[ FAIL ] ") << what << "\n";
    if (!cond) ++g_failures;
}

int main() {
    std::cout << "=== CadCore smoke test (headless) ===\n";

    const AABB world = AABB::fromCenterHalf({0, 0, 0}, {1e6, 1e6, 1e6});
    DrawingManager doc(std::make_unique<Quadtree>(world, /*maxDepth*/ 12, /*split*/ 8));

    // --- Camadas: cria WALLS (vermelho) ----------------------------------
    Layer wall;
    wall.name  = "WALLS";
    wall.color = Rgba{255, 0, 0, 255};
    doc.layers().add(wall);
    check(doc.layers().contains("WALLS"), "camada WALLS registrada");

    // --- Insere 1000 linhas verticais via Command ------------------------
    const int N = 1000;
    for (int i = 0; i < N; ++i) {
        auto ln = std::make_unique<Line>(Point3{double(i), 0, 0}, Point3{double(i), 10, 0});
        ln->setLayer("WALLS");
        doc.execute(std::make_unique<AddEntityCmd>(std::move(ln)));
    }
    check(doc.count() == static_cast<std::size_t>(N), "1000 entidades inseridas via Command");

    // --- Query espacial: janela x in [100,110] ---------------------------
    const AABB win = AABB{ {100, -1, -1}, {110, 11, 1} };
    auto hits = doc.query(win);
    std::cout << "        -> query devolveu " << hits.size() << " candidatos\n";
    check(hits.size() >= 9 && hits.size() <= 60,
          "query espacial retorna subconjunto pequeno (nao varre os 1000)");

    // --- Picking exato: cursor em (500, 5) deve achar a linha x=500 ------
    Ray pickRay;
    pickRay.origin = Point3{500.0, 5.0, 0.0};
    const EntityId picked = doc.pick(pickRay, 0.25);
    check(picked != kInvalidId, "pick encontrou uma entidade");
    if (const Entity* e = doc.getEntity(picked))
        std::cout << "        -> pick tipo=" << e->typeName()
                  << " layer=" << e->layer() << " id=" << e->id() << "\n";

    // --- resolveColor ByLayer herda o vermelho de WALLS ------------------
    bool red = false;
    if (const Entity* e = doc.getEntity(picked)) {
        const Rgba c = e->resolveColor(doc.layers());
        red = (c.r == 255 && c.g == 0 && c.b == 0);
    }
    check(red, "resolveColor herda a cor da camada (ByLayer)");

    // --- Emissao geometrica para RenderBatch -----------------------------
    RenderBatch batch;
    doc.forEach([&](const Entity& e) { e.emitTo(batch); });
    check(batch.segmentCount() == static_cast<std::size_t>(N),
          "emitTo() gera 1 segmento por linha");

    // --- Circle: bounding box, hitTest e tesselacao ----------------------
    {
        Circle c(Point3{0, 0, 0}, 5.0);
        const AABB bb = c.boundingBox();
        check(std::abs(bb.min.x + 5.0) < 1e-9 && std::abs(bb.max.x - 5.0) < 1e-9,
              "Circle bounding box = centro +/- raio");

        Ray onRim;    onRim.origin    = Point3{5.0, 0.0, 0.0};  // sobre o perimetro
        Ray atCenter; atCenter.origin = Point3{0.0, 0.0, 0.0};
        check(static_cast<bool>(c.hitTest(onRim, 0.25)), "Circle hitTest acerta no perimetro");
        check(!c.hitTest(atCenter, 0.25),                "Circle hitTest NAO acerta no centro");

        RenderBatch cb; c.emitTo(cb);
        check(cb.segmentCount() > 8, "Circle emite poligono de aproximacao");
        std::cout << "        -> Circle r=5 tesselado em " << cb.segmentCount()
                  << " segmentos\n";
    }

    // --- Arc: abertura, bounding box e transform (rotacao) ---------------
    {
        const double hpi = 1.57079632679489661923;        // pi/2
        Arc a(Point3{0, 0, 0}, 10.0, 0.0, hpi);           // quarto, 1o quadrante
        const AABB bb = a.boundingBox();
        check(std::abs(bb.min.x) < 1e-9 && std::abs(bb.max.x - 10.0) < 1e-9 &&
              std::abs(bb.min.y) < 1e-9 && std::abs(bb.max.y - 10.0) < 1e-9,
              "Arc bounding box do quarto = [0,10] x [0,10]");

        Ray onArc;  onArc.origin  = Point3{0.0, 10.0, 0.0};   // extremo (angulo pi/2)
        Ray offArc; offArc.origin = Point3{-10.0, 0.0, 0.0};  // angulo pi, fora
        check(static_cast<bool>(a.hitTest(onArc, 0.25)), "Arc hitTest acerta dentro da abertura");
        check(!a.hitTest(offArc, 0.25),                  "Arc hitTest ignora fora da abertura");

        a.transform(Matrix4::rotationZ(hpi));                 // gira +90 graus
        Ray rotated; rotated.origin = Point3{-10.0, 0.0, 0.0}; // agora e o novo extremo
        check(static_cast<bool>(a.hitTest(rotated, 0.25)),
              "Arc apos rotacao de 90 graus acerta o novo extremo");
    }

    // --- Polyline: aberta vs fechada, bounding box e hitTest -------------
    {
        std::vector<Point3> sq = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};

        Polyline closed(sq, /*closed*/ true);
        const AABB bb = closed.boundingBox();
        check(std::abs(bb.min.x) < 1e-9 && std::abs(bb.max.x - 10.0) < 1e-9 &&
              std::abs(bb.min.y) < 1e-9 && std::abs(bb.max.y - 10.0) < 1e-9,
              "Polyline bounding box = [0,10] x [0,10]");

        RenderBatch cb; closed.emitTo(cb);
        check(cb.segmentCount() == 4, "Polyline fechada emite 4 arestas");

        Ray onEdge; onEdge.origin = Point3{5.0, 0.0, 0.0};   // sobre a aresta de baixo
        Ray inside; inside.origin = Point3{5.0, 5.0, 0.0};   // centro vazio
        check(static_cast<bool>(closed.hitTest(onEdge, 0.25)),
              "Polyline hitTest acerta numa aresta");
        check(!closed.hitTest(inside, 0.25),
              "Polyline hitTest ignora o interior vazio");

        Polyline open(sq, /*closed*/ false);
        RenderBatch ob; open.emitTo(ob);
        check(ob.segmentCount() == 3, "Polyline aberta emite 3 arestas (nao fecha)");
    }

    // --- ToolController: desenho interativo (sem Qt) ---------------------
    {
        DrawingManager d2(std::make_unique<Quadtree>(world, 12, 8));
        ToolController tc(d2);

        tc.setTool(ToolKind::Line);
        check(!tc.onPoint(Point3{0, 0, 0}),   "Line tool: 1o ponto nao comete");
        check(tc.onPoint(Point3{10, 0, 0}),   "Line tool: 2o ponto comete a linha");
        check(d2.count() == 1,                "Line tool criou 1 entidade");
        check(tc.hasPending(),                "Line tool encadeia a partir do fim");

        RenderBatch pv;
        tc.buildPreview(Point3{10, 10, 0}, pv);
        check(pv.segmentCount() == 1,         "preview da linha = 1 segmento");

        tc.setTool(ToolKind::Circle);         // troca de tool limpa pendencias
        check(!tc.hasPending(),               "trocar de tool limpa pendencias");
        tc.onPoint(Point3{0, 0, 0});          // centro
        check(tc.onPoint(Point3{5, 0, 0}),    "Circle tool: 2o ponto comete o circulo");
        check(d2.count() == 2,                "Circle tool criou 1 entidade");
    }

    // --- Seleção + edição (Move/Copy/Erase) via ToolController -----------
    {
        DrawingManager d4(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId lid = d4.addEntity(
            std::make_unique<Line>(Point3{0, 0, 0}, Point3{100, 0, 0}));
        ToolController tc(d4);

        tc.selectAt(Point3{50, 0, 0}, 1.0, false);
        check(tc.selection().size() == 1, "selectAt seleciona a linha");

        tc.setTool(ToolKind::Move);
        check(!tc.onPoint(Point3{0, 0, 0}),     "Move: base nao comete");
        check(tc.onPoint(Point3{0, 50, 0}),     "Move: destino comete");
        check(std::abs(d4.getEntity(lid)->boundingBox().min.y - 50.0) < 1e-9,
              "linha moveu +50 em Y");
        d4.undo();
        check(std::abs(d4.getEntity(lid)->boundingBox().min.y) < 1e-9,
              "undo do Move restaura a posicao");

        tc.selectAt(Point3{50, 0, 0}, 1.0, false);  // re-seleciona (comando limpou)
        tc.setTool(ToolKind::Copy);
        tc.onPoint(Point3{0, 0, 0});
        check(tc.onPoint(Point3{0, 80, 0}),     "Copy: destino comete");
        check(d4.count() == 2,                  "Copy criou uma nova entidade");
        d4.undo();
        check(d4.count() == 1,                  "undo do Copy remove a copia");

        tc.selectAt(Point3{50, 0, 0}, 1.0, false);
        check(tc.eraseSelected(),               "Erase remove a selecao");
        check(d4.count() == 0,                  "documento vazio apos Erase");
        d4.undo();
        check(d4.count() == 1,                  "undo do Erase restaura");
    }

    // --- Verb-Noun: comando ANTES da seleção -----------------------------
    {
        DrawingManager d7(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId l = d7.addEntity(
            std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        ToolController tc(d7);

        tc.setTool(ToolKind::Move);   // sem seleção
        check(tc.phase() == ToolController::EditPhase::Selecting,
              "Verb-Noun: comando sem selecao entra em Selecting");
        check(tc.selectingObjects(), "fase Selecting roteia cliques p/ selecao");

        tc.selectAt(Point3{5, 0, 0}, 1.0, false);
        tc.confirmSelection();        // Enter
        check(tc.phase() == ToolController::EditPhase::Base,
              "confirmar selecao -> fase Base");

        check(!tc.onPoint(Point3{0, 0, 0}), "ponto-base nao comete");
        check(tc.onPoint(Point3{0, 30, 0}), "Verb-Noun comete o Move");
        check(std::abs(d7.getEntity(l)->boundingBox().min.y - 30.0) < 1e-9,
              "moveu +30 (fluxo Verb-Noun)");
    }

    // --- Rotate / Scale / Mirror via ToolController ----------------------
    {
        DrawingManager d6(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId lid = d6.addEntity(
            std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        ToolController tc(d6);
        tc.selectAt(Point3{5, 0, 0}, 1.0, false);

        // Rotate 90° em torno de (0,0): (10,0) -> (0,10)
        tc.setTool(ToolKind::Rotate);
        tc.onPoint(Point3{0, 0, 0});
        check(tc.onPoint(Point3{0, 1, 0}), "Rotate comete");
        {
            const AABB bb = d6.getEntity(lid)->boundingBox();
            check(std::abs(bb.max.y - 10.0) < 1e-6 && std::abs(bb.max.x) < 1e-6,
                  "Rotate 90 graus: (10,0) -> (0,10)");
        }
        d6.undo();
        check(std::abs(d6.getEntity(lid)->boundingBox().max.x - 10.0) < 1e-6,
              "undo do Rotate");

        // Scale x2 em torno de (0,0): (10,0) -> (20,0)  [destino a distancia 2]
        tc.selectAt(Point3{5, 0, 0}, 1.0, false);   // re-seleciona (comando limpou)
        tc.setTool(ToolKind::Scale);
        tc.onPoint(Point3{0, 0, 0});
        check(tc.onPoint(Point3{2, 0, 0}), "Scale comete");
        check(std::abs(d6.getEntity(lid)->boundingBox().max.x - 20.0) < 1e-6,
              "Scale x2: (10,0) -> (20,0)");
        d6.undo();
        check(std::abs(d6.getEntity(lid)->boundingBox().max.x - 10.0) < 1e-6,
              "undo do Scale");

        // Mirror sobre o eixo X (base (0,0), destino (1,0)): cria cópia refletida
        d6.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 5, 0}));
        tc.clearSelection();
        tc.selectAt(Point3{5, 2.5, 0}, 1.0, false);
        const std::size_t before = d6.count();
        tc.setTool(ToolKind::Mirror);
        tc.onPoint(Point3{0, 0, 0});
        check(tc.onPoint(Point3{1, 0, 0}), "Mirror comete");
        check(d6.count() == before + 1, "Mirror cria 1 copia (mantem original)");
        d6.undo();
        check(d6.count() == before, "undo do Mirror remove a copia");
    }

    // --- Construção: Retângulo e Arco 3-pontos ---------------------------
    {
        DrawingManager d9(std::make_unique<Quadtree>(world, 12, 8));
        ToolController tc(d9);

        tc.setTool(ToolKind::Rectangle);
        tc.onPoint(Point3{0, 0, 0});
        check(tc.onPoint(Point3{10, 5, 0}), "Rectangle comete");
        check(d9.count() == 1, "Rectangle criou polilinha");
        {
            const AABB bb = d9.getEntity(1)->boundingBox();
            check(std::abs(bb.min.x) < 1e-9 && std::abs(bb.max.x - 10.0) < 1e-9 &&
                  std::abs(bb.min.y) < 1e-9 && std::abs(bb.max.y - 5.0) < 1e-9,
                  "Rectangle bbox = [0,10]x[0,5]");
        }

        tc.setTool(ToolKind::Arc3);
        tc.onPoint(Point3{0, 0, 0});
        tc.onPoint(Point3{5, 5, 0});
        check(tc.onPoint(Point3{10, 0, 0}), "Arc3 comete (3 pontos)");
        check(d9.count() == 2, "Arc3 criou um arco");
    }

    // --- CommandParser ---------------------------------------------------
    {
        const Point3 last{10, 20, 0};
        check(parseCommandLine("L", last).kind == ParsedInput::Kind::Command &&
              parseCommandLine("L", last).command == "LINE", "alias L -> LINE");
        const ParsedInput abs = parseCommandLine("10,5", last);
        check(abs.kind == ParsedInput::Kind::Point && std::abs(abs.point.x - 10.0) < 1e-9 &&
              std::abs(abs.point.y - 5.0) < 1e-9 && !abs.relative, "coord absoluta 10,5");
        const ParsedInput rel = parseCommandLine("@3,4", last);
        check(rel.kind == ParsedInput::Kind::Point && std::abs(rel.point.x - 13.0) < 1e-9 &&
              std::abs(rel.point.y - 24.0) < 1e-9 && rel.relative, "coord relativa @3,4");
        check(parseCommandLine("U", last).control == "UNDO", "controle U -> UNDO");
    }

    // --- GeometryOps + Offset --------------------------------------------
    {
        const auto ix = intersectInfiniteLines(Point3{0, 0, 0}, Point3{10, 0, 0},
                                               Point3{5, -5, 0}, Point3{5, 5, 0});
        check(ix.has_value() && std::abs(ix->x - 5.0) < 1e-9 && std::abs(ix->y) < 1e-9,
              "intersectInfiniteLines em (5,0)");

        DrawingManager d8(std::make_unique<Quadtree>(world, 12, 8));
        d8.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        ToolController tc(d8);
        tc.selectAt(Point3{5, 0, 0}, 1.0, false);
        tc.setTool(ToolKind::Offset);
        check(tc.phase() == ToolController::EditPhase::Base,
              "Offset com selecao previa -> fase Base");
        check(tc.onPoint(Point3{5, 3, 0}), "Offset comete (ponto-lado acima)");
        check(d8.count() == 2, "Offset criou uma copia");
        bool at3 = false;
        d8.forEach([&](const Entity& e) {
            if (std::abs(e.boundingBox().min.y - 3.0) < 1e-6) at3 = true;
        });
        check(at3, "linha deslocada 3 unidades acima");
        d8.undo();
        check(d8.count() == 1, "undo do Offset");
    }

    // --- Interseção (IntersectOps + snap de Intersection) ----------------
    {
        DrawingManager dA(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId l1 = dA.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        const EntityId l2 = dA.addEntity(std::make_unique<Line>(Point3{5, -5, 0}, Point3{5, 5, 0}));
        const auto ix = intersectEntities(*dA.getEntity(l1), *dA.getEntity(l2));
        check(ix.size() == 1 && std::abs(ix[0].x - 5.0) < 1e-9 && std::abs(ix[0].y) < 1e-9,
              "intersectEntities Line x Line em (5,0)");
        SnapEngine snap;
        const SnapResult r = snap.resolve(Point3{5.3, 0.3, 0}, 1.0, dA);
        check(r && r.type == SnapType::Intersection && std::abs(r.point.x - 5.0) < 1e-9,
              "snap captura Intersection em (5,0)");
    }

    // --- Trim (via ToolController) ---------------------------------------
    {
        DrawingManager dB(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId target = dB.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        const EntityId cut    = dB.addEntity(std::make_unique<Line>(Point3{5, -5, 0}, Point3{5, 5, 0}));
        ToolController tc(dB);
        tc.setTool(ToolKind::Trim);
        (void)cut;                                // todas as entidades são arestas de corte
        tc.trimClick(target, Point3{8, 0, 0});    // 1 clique: apara o alvo (remove x>5)
        check(std::abs(dB.getEntity(target)->boundingBox().max.x - 5.0) < 1e-6,
              "Trim cortou a linha em x=5");
        dB.undo();
        check(std::abs(dB.getEntity(target)->boundingBox().max.x - 10.0) < 1e-6, "undo do Trim");
    }

    // --- Fillet (via ToolController) -------------------------------------
    {
        DrawingManager dC(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId la = dC.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{20, 0, 0}));
        const EntityId lb = dC.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{0, 20, 0}));
        ToolController tc(dC);
        tc.setTool(ToolKind::Fillet);             // raio padrão 10
        tc.filletClick(la, Point3{10, 0, 0});
        tc.filletClick(lb, Point3{0, 10, 0});
        check(dC.count() == 3, "Fillet adicionou o arco (2 linhas + arco)");
        dC.undo();
        check(dC.count() == 2, "undo do Fillet");

        // Fillet Line + Circle (P1 #1): linha aparada + círculo mantido + arco.
        DrawingManager dF2(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId fl  = dF2.addEntity(std::make_unique<Line>(Point3{-20, 5, 0}, Point3{20, 5, 0}));
        const EntityId fc  = dF2.addEntity(std::make_unique<Circle>(Point3{0, 0, 0}, 10.0));
        ToolController tcF2(dF2);
        tcF2.setFilletRadius(3.0);
        tcF2.setTool(ToolKind::Fillet);
        tcF2.filletClick(fl, Point3{12, 5, 0});   // lado direito da linha
        tcF2.filletClick(fc, Point3{8, 6, 0});    // círculo perto do canto superior-direito
        bool f2Arc = false;
        dF2.forEach([&](const Entity& e) { if (std::string(e.typeName()) == "ARC") f2Arc = true; });
        check(dF2.count() == 3 && f2Arc, "Fillet Line+Circle gera arco de concordancia");
    }

    // --- Chamfer (chanfro) ----------------------------------------------
    {
        // chamferLines: duas perpendiculares, recuo 5 cada -> bevel = sqrt(50).
        const ChamferResult cr = chamferLines(Line{Point3{0, 0, 0}, Point3{20, 0, 0}},
                                              Line{Point3{0, 0, 0}, Point3{0, 20, 0}}, 5.0, 5.0);
        const double bl = std::hypot(cr.bevel.end().x - cr.bevel.start().x,
                                     cr.bevel.end().y - cr.bevel.start().y);
        check(cr.ok && std::abs(bl - std::sqrt(50.0)) < 1e-6, "chamferLines: bevel = sqrt(50)");

        DrawingManager dCh(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId ca = dCh.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{20, 0, 0}));
        const EntityId cb = dCh.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{0, 20, 0}));
        ToolController tc(dCh);
        tc.setTool(ToolKind::Chamfer);            // recuo padrão 5
        tc.chamferClick(ca, Point3{10, 0, 0});
        tc.chamferClick(cb, Point3{0, 10, 0});
        check(dCh.count() == 3, "Chamfer adicionou o bevel (2 linhas + bevel)");
        dCh.undo();
        check(dCh.count() == 2, "undo do Chamfer");
    }

    // --- Parser: número solto vira Distance ------------------------------
    {
        const ParsedInput d = parseCommandLine("50", Point3{0, 0, 0});
        check(d.kind == ParsedInput::Kind::Distance && std::abs(d.distance - 50.0) < 1e-9,
              "parser: numero solto -> Distance");
    }

    // --- Anotação: StrokeFont, MText, Dimension, Hatch -------------------
    {
        // Fonte de traços: "AB" deve gerar vários segmentos.
        const auto seg = strokeText("AB", Point3{0, 0, 0}, 6.0, 0.0);
        check(seg.size() >= 4 && (seg.size() % 2) == 0,
              "strokeText('AB') gera segmentos (pares de pontos)");
        check(strokeTextWidth("AB", 6.0) > 0.0, "strokeTextWidth > 0");

        // MText emite a geometria do texto.
        MText t(Point3{0, 0, 0}, "CAD", 5.0);
        RenderBatch tb; t.emitTo(tb);
        check(tb.segmentCount() > 0, "MText emite segmentos");
        check(t.boundingBox().valid(), "MText bounding box valido");

        // Dimension linear de (0,0)-(50,0), linha a y=10.
        Dimension d = Dimension::linear(Point3{0, 0, 0}, Point3{50, 0, 0}, Point3{0, 10, 0});
        RenderBatch db; d.emitTo(db);
        check(db.segmentCount() > 0, "Dimension linear emite geometria");

        // Hatch de um quadrado 0..10: tem contorno + linhas de preenchimento.
        std::vector<std::vector<Point3>> loops = {
            {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}};
        Hatch h(loops, 0.7853981634, 2.0);
        RenderBatch hb; h.emitTo(hb);
        check(hb.segmentCount() > 4, "Hatch emite contorno + preenchimento");
        Ray inside; inside.origin = Point3{5, 5, 0};
        check(static_cast<bool>(h.hitTest(inside, 0.1)),
              "Hatch hitTest acerta no interior");
    }

    // --- Linetype / Ellipse / Extend / Explode / Array ------------------
    {
        // Linetype: tracejado quebra um segmento longo em vários traços.
        std::vector<Point3> seg = {Point3{0, 0, 0}, Point3{40, 0, 0}};
        const auto dashed = applyLinePattern(seg, LineStyle::Dashed, 1.0);
        check(dashed.size() > seg.size(), "applyLinePattern (Dashed) gera varios tracos");
        check(applyLinePattern(seg, LineStyle::Continuous, 1.0).size() == 2,
              "Continuous nao altera");
        check(lineStyleFromName("CENTER") == LineStyle::Center, "lineStyleFromName CENTER");

        // Ellipse.
        Ellipse e(Point3{0, 0, 0}, Vec3{20, 0, 0}, 0.5);
        RenderBatch eb; e.emitTo(eb);
        check(eb.segmentCount() > 20 && e.boundingBox().valid(), "Ellipse emite e tem bbox");

        // Extend: linha (0,0)-(5,0) estende ate a reta vertical x=10.
        const auto ext = extendLineToBoundary(Line{Point3{0, 0, 0}, Point3{5, 0, 0}},
                                              Point3{10, -5, 0}, Point3{10, 5, 0},
                                              Point3{5, 0, 0});
        check(ext.has_value() && std::abs(ext->end().x - 10.0) < 1e-6,
              "extendLineToBoundary estende ate x=10");

        // Explode de polilinha fechada (quadrado) -> 4 linhas.
        DrawingManager dE(std::make_unique<Quadtree>(world, 12, 8));
        std::vector<Point3> sq = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
        const EntityId pid = dE.addEntity(std::make_unique<Polyline>(sq, true));
        dE.execute(std::make_unique<ExplodeCmd>(pid));
        check(dE.count() == 4, "Explode: polilinha fechada -> 4 linhas");
        dE.undo();
        check(dE.count() == 1, "undo do Explode");

        // Array retangular 2x3 de 1 linha -> 6 entidades.
        DrawingManager dA(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId lid = dA.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{5, 0, 0}));
        dA.execute(std::make_unique<ArrayCmd>(ArrayCmd::rectangular({lid}, 2, 3, 10.0, 10.0)));
        check(dA.count() == 6, "Array retangular 2x3 -> 6 entidades");
        dA.undo();
        check(dA.count() == 1, "undo do Array");
    }

    // --- DXF round-trip (writer -> reader) ------------------------------
    {
        DrawingManager dW(std::make_unique<Quadtree>(world, 12, 8));
        dW.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 5, 0}));
        dW.addEntity(std::make_unique<Circle>(Point3{3, 3, 0}, 4.0));
        dW.addEntity(std::make_unique<Arc>(Point3{0, 0, 0}, 5.0, 0.0, 1.5707963267948966));
        const std::string path = "smoke_roundtrip.dxf";
        check(writeDxf(dW, path), "writeDxf grava o arquivo");

        DrawingManager dR(std::make_unique<Quadtree>(world, 12, 8));
        const int n = readDxf(path, dR);
        check(n == 3 && dR.count() == 3, "readDxf reconstroi as 3 entidades");

        bool lineOk = false;
        dR.forEach([&](const Entity& e) {
            const auto* l = dynamic_cast<const Line*>(&e);
            if (l && std::abs(l->boundingBox().max.x - 10.0) < 1e-4 &&
                     std::abs(l->boundingBox().max.y - 5.0) < 1e-4) lineOk = true;
        });
        check(lineOk, "DXF round-trip preserva a geometria da linha");
        std::remove(path.c_str());
    }

    // --- DXF round-trip: Dimension, Hatch e Ellipse (P1 #3) -------------
    {
        DrawingManager dW(std::make_unique<Quadtree>(world, 12, 8));
        dW.addEntity(std::make_unique<Dimension>(
            Dimension::linear(Point3{0, 0, 0}, Point3{20, 0, 0}, Point3{0, 5, 0}, 2.5)));
        dW.addEntity(std::make_unique<Hatch>(
            std::vector<std::vector<Point3>>{{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}},
            HatchPattern::ANSI31, 30.0, 2.0));
        dW.addEntity(std::make_unique<Ellipse>(
            Ellipse::fromCenterAxes(Point3{0, 0, 0}, Vec3{20, 0, 0}, 8.0)));
        const std::string path = "smoke_rt2.dxf";
        check(writeDxf(dW, path), "writeDxf grava Dimension+Hatch+Ellipse");

        DrawingManager dR(std::make_unique<Quadtree>(world, 12, 8));
        readDxf(path, dR);
        int nDim = 0, nHat = 0, nEll = 0;
        Point3 ellMax{};
        dR.forEach([&](const Entity& e) {
            const std::string t = e.typeName();
            if (t == "DIMENSION") nDim++;
            if (t == "HATCH") nHat++;
            if (t == "ELLIPSE") { nEll++; ellMax = e.boundingBox().max; }
        });
        check(nDim == 1 && nHat == 1 && nEll == 1,
              "DXF round-trip: Dimension, Hatch e Ellipse sobrevivem");
        check(std::abs(ellMax.x - 20.0) < 0.1 && std::abs(ellMax.y - 8.0) < 0.1,
              "DXF round-trip: Ellipse mantem os eixos (20x8)");
        std::remove(path.c_str());
    }

    // --- DXF round-trip: BLOCK/INSERT + seção HEADER --------------------
    {
        DrawingManager dW(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId a = dW.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        const EntityId b = dW.addEntity(std::make_unique<Line>(Point3{10, 0, 0}, Point3{10, 4, 0}));
        dW.execute(std::make_unique<MakeBlockCmd>(std::vector<EntityId>{a, b},
                                                  Point3{0, 0, 0}, std::string("PORTA")));
        const BlockDefinition* def = dW.blocks().find("PORTA");
        dW.addEntity(BlockRef::fromDefinition(*def, Matrix4::translation(Vec3{50, 0, 0})));

        const std::string path = "smoke_blk.dxf";
        check(writeDxf(dW, path), "writeDxf grava BLOCK/INSERT");

        std::ifstream f(path);
        const std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        check(all.find("HEADER") != std::string::npos && all.find("$ACADVER") != std::string::npos,
              "DXF tem secao HEADER com $ACADVER");
        check(all.find("BLOCKS") != std::string::npos && all.find("INSERT") != std::string::npos,
              "DXF tem secao BLOCKS e entidade INSERT");

        DrawingManager dR(std::make_unique<Quadtree>(world, 12, 8));
        readDxf(path, dR);
        check(dR.blocks().contains("PORTA"), "readDxf registra a definicao PORTA na biblioteca");
        int nIns = 0; double insMaxX = 0.0;
        dR.forEach([&](const Entity& e) {
            if (std::string(e.typeName()) == "INSERT") {
                nIns++;
                const AABB bb = e.boundingBox();
                if (bb.valid() && bb.max.x > insMaxX) insMaxX = bb.max.x;
            }
        });
        check(nIns == 2, "DXF round-trip: 2 INSERT sobrevivem");
        check(std::abs(insMaxX - 60.0) < 1e-6, "insert deslocado +50 (x max = 60)");
        std::remove(path.c_str());
    }

    // --- Point / Spline / Block / Grips / Stretch -----------------------
    {
        PointEntity pt(Point3{5, 5, 0});
        RenderBatch pb; pt.emitTo(pb);
        check(pb.segmentCount() == 2, "PointEntity emite um '+' (2 segmentos)");
        Ray rp; rp.origin = Point3{5.1, 5.1, 0};
        check(static_cast<bool>(pt.hitTest(rp, 1.0)), "PointEntity hitTest perto do ponto");

        Spline sp(std::vector<Point3>{{0, 0, 0}, {10, 10, 0}, {20, 0, 0}, {30, 10, 0}});
        check(sp.sample().size() > 4, "Spline.sample gera polilinha");
        RenderBatch sb2; sp.emitTo(sb2);
        check(sb2.segmentCount() > 4 && sp.boundingBox().valid(), "Spline emite e tem bbox");

        // Block: 2 linhas -> 1 bloco; undo -> 2.
        DrawingManager dBlk(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId b1 = dBlk.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        const EntityId b2 = dBlk.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{0, 10, 0}));
        dBlk.execute(std::make_unique<MakeBlockCmd>(std::vector<EntityId>{b1, b2}, Point3{0, 0, 0}));
        check(dBlk.count() == 1, "MakeBlock: 2 linhas viram 1 bloco");
        bool isBlock = false;
        dBlk.forEach([&](const Entity& e) { if (dynamic_cast<const BlockRef*>(&e)) isBlock = true; });
        check(isBlock, "a entidade resultante e um BlockRef");
        dBlk.undo();
        check(dBlk.count() == 2, "undo do MakeBlock restaura as 2 linhas");

        // Biblioteca de blocos nomeados + INSERT + EXPLODE.
        DrawingManager dLib(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId l1 = dLib.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        const EntityId l2 = dLib.addEntity(std::make_unique<Line>(Point3{10, 0, 0}, Point3{10, 5, 0}));
        dLib.execute(std::make_unique<MakeBlockCmd>(std::vector<EntityId>{l1, l2},
                                                    Point3{0, 0, 0}, std::string("PORTA")));
        check(dLib.blocks().contains("PORTA"), "MakeBlock nomeado registra a definicao na biblioteca");
        check(dLib.count() == 1, "criacao substitui as 2 linhas por 1 insert");

        // INSERT: instancia a definicao transladada +100 em X, escala 2x.
        const BlockDefinition* pdef = dLib.blocks().find("PORTA");
        check(pdef != nullptr && pdef->members.size() == 2, "definicao PORTA tem 2 membros");
        const Matrix4 ix = Matrix4::translation(Vec3{100, 0, 0}) * Matrix4::scale(Vec3{2, 2, 1});
        auto ins = BlockRef::fromDefinition(*pdef, ix);
        check(ins->blockName() == "PORTA", "insert carrega o nome do bloco");
        const AABB ib = ins->boundingBox();
        // membro mais a leste: x=10 local -> *2 +100 = 120.
        check(std::abs(ib.max.x - 120.0) < 1e-6, "insert escala 2x + translada 100 (x max = 120)");
        const EntityId insId = dLib.addEntity(std::move(ins));
        check(dLib.count() == 2, "insert adiciona 1 entidade");

        // EXPLODE do insert -> vira 2 linhas (nos membros ja transformados).
        dLib.execute(std::make_unique<ExplodeCmd>(insId));
        check(dLib.count() == 3, "explode do bloco: 1 insert -> 2 linhas (total 3)");
        dLib.undo();
        check(dLib.count() == 2, "undo do explode restaura o insert");

        // Grips.
        Line gl(Point3{0, 0, 0}, Point3{10, 0, 0});
        check(gripsOf(gl).size() == 2, "gripsOf(Line) = 2");
        const auto moved = withGripMoved(gl, 1, Point3{10, 5, 0});
        check(moved && std::abs(moved->boundingBox().max.y - 5.0) < 1e-9,
              "withGripMoved move o extremo");

        DrawingManager dG(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId gid = dG.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        dG.execute(std::make_unique<GripEditCmd>(gid, 1, Point3{10, 8, 0}));
        check(std::abs(dG.getEntity(gid)->boundingBox().max.y - 8.0) < 1e-9, "GripEditCmd moveu o grip");
        dG.undo();
        check(std::abs(dG.getEntity(gid)->boundingBox().max.y) < 1e-9, "undo do GripEdit");

        // Stretch: só o extremo (10,0) cai na janela -> sobe 5 em Y.
        DrawingManager dStr(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId sid = dStr.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        const AABB win{Point3{8, -2, -1}, Point3{12, 2, 1}};
        dStr.execute(std::make_unique<StretchCmd>(std::vector<EntityId>{sid}, win, Vec3{0, 5, 0}));
        check(std::abs(dStr.getEntity(sid)->boundingBox().max.y - 5.0) < 1e-9 &&
              std::abs(dStr.getEntity(sid)->boundingBox().min.y) < 1e-9,
              "Stretch moveu so o vertice dentro da janela");
        dStr.undo();
        check(std::abs(dStr.getEntity(sid)->boundingBox().max.y) < 1e-9, "undo do Stretch");
    }

    // --- Circle 2P/3P, retangulo chanfrado, Divide/Measure --------------
    {
        const Circle c2 = circle2Points(Point3{0, 0, 0}, Point3{10, 0, 0});
        check(std::abs(c2.radius() - 5.0) < 1e-9 && std::abs(c2.center().x - 5.0) < 1e-9,
              "circle2Points: centro (5,0) raio 5");

        bool ok = false;
        const Circle c3 = circle3Points(Point3{0, 0, 0}, Point3{10, 0, 0}, Point3{0, 10, 0}, ok);
        check(ok && std::abs(c3.center().x - 5.0) < 1e-6 && std::abs(c3.center().y - 5.0) < 1e-6,
              "circle3Points: circuncentro (5,5)");

        const Polyline rc = rectangleChamfer(Point3{0, 0, 0}, Point3{20, 10, 0}, 2.0);
        check(rc.vertices().size() == 8, "rectangleChamfer: 8 vertices");

        Line ln(Point3{0, 0, 0}, Point3{100, 0, 0});
        const auto dv = dividePoints(ln, 4);
        check(dv.size() == 3 && std::abs(dv[0].x - 25.0) < 1e-6 && std::abs(dv[1].x - 50.0) < 1e-6,
              "dividePoints: linha em 4 partes -> 3 pontos");
        const auto mv = measurePoints(ln, 30.0);
        check(mv.size() == 3 && std::abs(mv[2].x - 90.0) < 1e-6, "measurePoints: linha @30 -> 3 pontos");

        Circle ci(Point3{0, 0, 0}, 10.0);
        check(dividePoints(ci, 6).size() == 6, "dividePoints: circulo (fechado) -> 6 pontos");

        const Arc3Result ar = arcStartEndRadius(Point3{0, 0, 0}, Point3{10, 0, 0}, 5.0);
        check(ar.ok && std::abs(ar.arc.radius() - 5.0) < 1e-6 &&
              std::abs(ar.arc.center().x - 5.0) < 1e-6,
              "arcStartEndRadius: semicirculo raio 5, centro (5,0)");
        const Arc3Result aa = arcStartEndAngle(Point3{0, 0, 0}, Point3{10, 0, 0}, 3.141592653589793);
        check(aa.ok && std::abs(aa.arc.radius() - 5.0) < 1e-6,
              "arcStartEndAngle: 180 graus -> raio 5");
        const Arc3Result ad = arcStartEndDirection(Point3{0, 0, 0}, Point3{10, 0, 0}, Point3{0, 1, 0});
        check(ad.ok && std::abs(ad.arc.radius() - 5.0) < 1e-6 &&
              std::abs(ad.arc.center().x - 5.0) < 1e-6,
              "arcStartEndDirection: tangente vertical -> semicirculo raio 5");
    }

    // --- Polilinha com ARCO (bulge): retangulo arredondado, explode, DXF -----
    {
        // bulge 1 = semicirculo (CCW). p0->p1 em +x -> apex em (5,-5).
        Polyline pa(std::vector<Point3>{{0, 0, 0}, {10, 0, 0}}, std::vector<double>{1.0, 0.0}, false);
        check(pa.hasArcs(), "Polyline.hasArcs detecta bulge");
        const std::vector<Point3> sp = pa.sampledPoints();
        check(sp.size() > 5, "Polyline com bulge tessela o arco");
        double minY = 1e9;
        for (const Point3& q : sp) minY = std::min(minY, q.y);
        check(std::abs(minY + 5.0) < 0.2, "semicirculo (bulge 1) abaula ate y=-5 (CCW)");

        const Polyline rf = rectangleFillet(Point3{0, 0, 0}, Point3{40, 20, 0}, 4.0);
        check(rf.vertices().size() == 8 && rf.closed() && rf.hasArcs(),
              "rectangleFillet: 8 vertices, fechado, com arcos");

        DrawingManager dRF(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId rid = dRF.addEntity(std::make_unique<Polyline>(rf));
        dRF.execute(std::make_unique<ExplodeCmd>(rid));
        check(dRF.count() == 8, "Explode do ret. arredondado -> 8 entidades");
        int nLine = 0, nArc = 0;
        dRF.forEach([&](const Entity& e) {
            if (dynamic_cast<const Line*>(&e)) nLine++;
            else if (dynamic_cast<const Arc*>(&e)) nArc++;
        });
        check(nLine == 4 && nArc == 4, "Explode: 4 linhas + 4 arcos");

        // DXF round-trip preserva o bulge (codigo 42).
        DrawingManager dW2(std::make_unique<Quadtree>(world, 12, 8));
        dW2.addEntity(std::make_unique<Polyline>(
            std::vector<Point3>{{0, 0, 0}, {10, 0, 0}}, std::vector<double>{0.5, 0.0}, false));
        const std::string bp = "smoke_bulge.dxf";
        writeDxf(dW2, bp);
        DrawingManager dR2(std::make_unique<Quadtree>(world, 12, 8));
        readDxf(bp, dR2);
        bool bulgeOk = false;
        dR2.forEach([&](const Entity& e) {
            if (const auto* pl = dynamic_cast<const Polyline*>(&e))
                if (std::abs(pl->bulgeAt(0) - 0.5) < 1e-6) bulgeOk = true;
        });
        check(bulgeOk, "DXF round-trip preserva o bulge (0.5)");
        std::remove(bp.c_str());
    }

    // --- Circle TTR (tangente a 2 entidades) + B-spline CV --------------
    {
        Line la(Point3{0, 0, 0}, Point3{10, 0, 0});   // eixo X
        Line lb(Point3{0, 0, 0}, Point3{0, 10, 0});   // eixo Y
        const std::optional<Circle> ttr = circleTanTanRadius(la, lb, 5.0, Point3{3, 3, 0});
        check(ttr && std::abs(ttr->center().x - 5.0) < 1e-5 &&
              std::abs(ttr->center().y - 5.0) < 1e-5 && std::abs(ttr->radius() - 5.0) < 1e-6,
              "circleTanTanRadius: 2 retas -> centro (5,5) raio 5");

        const std::vector<Point3> ctrl{{0, 0, 0}, {10, 20, 0}, {30, -10, 0}, {40, 10, 0}};
        const std::vector<Point3> bs = bsplinePoints(ctrl, 3, 12);
        check(bs.size() > 10, "bsplinePoints amostra a curva CV");
        check(std::abs(bs.front().x - ctrl.front().x) < 1e-6 &&
              std::abs(bs.back().x - ctrl.back().x) < 1e-6,
              "B-spline clamped comeca/termina nos pontos extremos");
    }

    // --- Arco eliptico + Circulo TTT (3 retas) --------------------------
    {
        const Ellipse ea = Ellipse::fromCenterAxesArc(Point3{0, 0, 0}, Vec3{10, 0, 0}, 5.0,
                                                      0.0, 1.5707963267948966);
        check(ea.isArc(), "Ellipse::isArc true para arco eliptico");
        RenderBatch eb; ea.emitTo(eb);
        check(eb.segmentCount() > 2, "arco eliptico emite segmentos");
        check(!Ellipse::fromCenterAxes(Point3{0, 0, 0}, Vec3{10, 0, 0}, 5.0).isArc(),
              "elipse completa NAO e arco");

        Line t1(Point3{0, 0, 0}, Point3{10, 0, 0});
        Line t2(Point3{0, 0, 0}, Point3{0, 10, 0});
        Line t3(Point3{10, 0, 0}, Point3{0, 10, 0});
        const std::optional<Circle> ttt = circleTanTanTan(t1, t2, t3, Point3{3, 3, 0});
        check(ttt && std::abs(ttt->center().x - 2.9289) < 1e-2 &&
              std::abs(ttt->center().y - 2.9289) < 1e-2,
              "circleTanTanTan: incirculo ~ (2.93, 2.93)");

        // Hatch com padrões: ANSI37 (xadrez, 2 famílias) > Lines (1 família).
        std::vector<std::vector<Point3>> sq{{{0, 0, 0}, {20, 0, 0}, {20, 20, 0}, {0, 20, 0}}};
        Hatch hL(sq);
        RenderBatch hb1; hL.emitTo(hb1);
        Hatch hX(sq, HatchPattern::ANSI37, 0.0, 1.0);
        RenderBatch hb2; hX.emitTo(hb2);
        check(hX.pattern() == HatchPattern::ANSI37, "Hatch guarda o padrao ANSI37");
        check(hb2.segmentCount() > hb1.segmentCount(), "Hatch ANSI37 gera mais linhas que Lines");

        // Cotas encadeadas: Continue comeca no fim da anterior; Baseline na mesma base.
        const Dimension d0 = Dimension::linear(Point3{0, 0, 0}, Point3{10, 0, 0}, Point3{0, -5, 0}, 2.5);
        const Dimension dc = Dimension::continueLinear(d0, Point3{25, 0, 0}, 2.5);
        check(std::abs(dc.p1().x - 10.0) < 1e-6, "Cota Continue comeca no fim da anterior (x=10)");
        const Dimension db = Dimension::baselineLinear(d0, Point3{25, 0, 0}, 2.5, 7.5);
        check(std::abs(db.p1().x - 0.0) < 1e-6, "Cota Baseline comeca na mesma base (x=0)");
    }

    // --- XLINE/RAY + Break/Join/Lengthen + OSNAP Tangent ----------------
    {
        const XLine xl = XLine::fromTwoPoints(Point3{0, 0, 0}, Point3{1, 1, 0});
        RenderBatch xb; xl.emitTo(xb);
        check(xb.segmentCount() >= 1 && xl.boundingBox().valid(), "XLine emite + bbox pequeno");
        Ray rr; rr.origin = Point3{5, 5, 0};
        check(static_cast<bool>(xl.hitTest(rr, 0.1)), "XLine hitTest sobre a reta infinita");
        const RayLine ry = RayLine::fromTwoPoints(Point3{0, 0, 0}, Point3{1, 0, 0});
        Ray rb; rb.origin = Point3{-5, 0, 0};
        check(!ry.hitTest(rb, 0.1), "RayLine nao acerta atras da origem");

        Line ln(Point3{0, 0, 0}, Point3{100, 0, 0});
        check(breakLine(ln, Point3{30, 0, 0}, Point3{60, 0, 0}).size() == 2, "breakLine -> 2 partes");
        const std::optional<Line> jn = joinLines(Line{Point3{0, 0, 0}, Point3{10, 0, 0}},
                                                 Line{Point3{10, 0, 0}, Point3{25, 0, 0}});
        check(jn && std::abs(jn->end().x - 25.0) < 1e-6, "joinLines colineares -> 0..25");
        check(std::abs(lengthenLine(ln, 20.0, true).end().x - 120.0) < 1e-6,
              "lengthenLine +20 no fim -> x=120");

        DrawingManager dT(std::make_unique<Quadtree>(world, 12, 8));
        dT.addEntity(std::make_unique<Circle>(Point3{0, 0, 0}, 10.0));
        SnapEngine sn;
        const Point3 fromP{20, 0, 0};
        const SnapResult rt = sn.resolve(Point3{5.2, 8.5, 0}, 2.0, dT, &fromP);
        check(rt && rt.type == SnapType::Tangent, "OSNAP Tangent de ponto externo a circulo");
    }

    // --- Inquiry (area/comprimento) + MLINE -----------------------------
    {
        Circle ci(Point3{0, 0, 0}, 10.0);
        check(std::abs(entityArea(ci) - 3.141592653589793 * 100.0) < 1e-3, "entityArea circulo = pi*r^2");
        check(std::abs(entityLength(ci) - 2.0 * 3.141592653589793 * 10.0) < 1e-3, "entityLength circulo = 2pi*r");
        check(std::abs(entityLength(Line{Point3{0, 0, 0}, Point3{3, 4, 0}}) - 5.0) < 1e-9,
              "entityLength linha 3-4-5 = 5");
        std::vector<Point3> sq{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
        check(std::abs(polygonArea(sq) - 100.0) < 1e-9, "polygonArea quadrado 10 = 100");

        MLine ml(std::vector<Point3>{{0, 0, 0}, {20, 0, 0}}, 4.0, false);
        RenderBatch mb; ml.emitTo(mb);
        check(mb.segmentCount() >= 2 && ml.boundingBox().valid(), "MLine emite 2 bordas + bbox");

        Polyline pw(std::vector<Point3>{{0, 0, 0}, {50, 0, 0}}, false);
        pw.setWidth(4.0);
        RenderBatch wb; pw.emitTo(wb);
        check(wb.fillVertices.size() == 6 && wb.segmentCount() == 0,
              "Polyline larga -> faixa preenchida (2 triangulos, sem linha)");
    }

    // --- Leader + RevCloud + selection cycling + camada travada ----------
    {
        Leader ld(std::vector<Point3>{{0, 0, 0}, {10, 5, 0}, {20, 5, 0}}, "N1", 2.5);
        RenderBatch lb; ld.emitTo(lb);
        Ray lr; lr.origin = Point3{5, 2.5, 0};
        check(lb.segmentCount() > 0 && static_cast<bool>(ld.hitTest(lr, 0.6)),
              "Leader emite + hitTest no segmento");

        const Polyline rc = revisionCloudRect(Point3{0, 0, 0}, Point3{40, 20, 0}, 5.0);
        check(rc.closed() && rc.hasArcs(), "RevCloud: polilinha fechada com arcos (bolhas)");

        DrawingManager dL(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId e1 = dL.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{20, 0, 0}));
        dL.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{20, 0, 0}));  // sobreposta
        ToolController tcL(dL);
        tcL.selectAt(Point3{10, 0, 0}, 1.0, false);
        const EntityId s1 = tcL.selection().empty() ? kInvalidId : tcL.selection().front();
        tcL.selectAt(Point3{10, 0, 0}, 1.0, false);   // mesmo ponto -> cicla
        const EntityId s2 = tcL.selection().empty() ? kInvalidId : tcL.selection().front();
        check(s1 != kInvalidId && s2 != kInvalidId && s1 != s2,
              "Selection cycling alterna entre sobrepostas");

        const std::string lyr = dL.getEntity(e1)->layer();
        Layer* lp = dL.layers().find(lyr);
        if (!lp) { dL.layers().add(Layer{lyr}); lp = dL.layers().find(lyr); }
        lp->locked = true;
        tcL.clearSelection();
        tcL.selectAt(Point3{10, 0, 0}, 1.0, false);
        check(tcL.selection().empty(), "Camada travada nao seleciona");
    }

    // --- Match Properties + Rotate+Copiar --------------------------------
    {
        DrawingManager dM(std::make_unique<Quadtree>(world, 12, 8));
        auto srcLine = std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0});
        srcLine->setLayer("RED");
        const EntityId src = dM.addEntity(std::move(srcLine));
        const EntityId tgt = dM.addEntity(std::make_unique<Line>(Point3{0, 5, 0}, Point3{10, 5, 0}));
        ToolController tcM(dM);
        tcM.matchPropsClick(src);   // fonte
        tcM.matchPropsClick(tgt);   // alvo -> herda a camada "RED"
        check(dM.getEntity(tgt) && dM.getEntity(tgt)->layer() == "RED",
              "Match Properties copia a camada da fonte");

        DrawingManager dR(std::make_unique<Quadtree>(world, 12, 8));
        dR.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        ToolController tcR(dR);
        tcR.selectAt(Point3{5, 0, 0}, 1.0, false);
        tcR.setTool(ToolKind::Rotate);
        tcR.setEditCopy(true);
        tcR.onPoint(Point3{0, 0, 0});    // base
        tcR.onPoint(Point3{0, 10, 0});   // alvo (90 graus)
        check(dR.count() == 2, "Rotacionar+Copiar mantem o original (2 entidades)");

        // Rotate por Referência: ref a 0 graus, destino a 90 -> gira +90.
        DrawingManager dRR(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId er = dRR.addEntity(std::make_unique<Line>(Point3{10, 0, 0}, Point3{20, 0, 0}));
        ToolController tcRR(dRR);
        tcRR.selectAt(Point3{15, 0, 0}, 1.0, false);
        tcRR.setTool(ToolKind::Rotate);
        tcRR.setEditRef(true);
        tcRR.onPoint(Point3{0, 0, 0});   // base
        tcRR.onPoint(Point3{5, 0, 0});   // referência (0 graus)
        tcRR.onPoint(Point3{0, 5, 0});   // destino (90 graus)
        const AABB rb = dRR.getEntity(er)->boundingBox();
        check(std::abs(rb.min.x) < 1e-6 && std::abs(rb.max.x) < 1e-6 && std::abs(rb.min.y - 10.0) < 1e-6,
              "Rotate por Referencia gira +90 graus");

        // Scale por Referência: ref len 10, destino len 20 -> fator 2.
        DrawingManager dSR(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId es = dSR.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        ToolController tcSR(dSR);
        tcSR.selectAt(Point3{5, 0, 0}, 1.0, false);
        tcSR.setTool(ToolKind::Scale);
        tcSR.setEditRef(true);
        tcSR.onPoint(Point3{0, 0, 0});    // base
        tcSR.onPoint(Point3{10, 0, 0});   // referência (len 10)
        tcSR.onPoint(Point3{20, 0, 0});   // destino (len 20)
        check(std::abs(dSR.getEntity(es)->boundingBox().max.x - 20.0) < 1e-6,
              "Scale por Referencia fator 2");

        // Hachura SOLID: contorno quadrado -> triangulação (canal de fill).
        std::vector<std::vector<Point3>> loops{{{0, 0, 0}, {20, 0, 0}, {20, 20, 0}, {0, 20, 0}}};
        Hatch hs(loops, HatchPattern::Solid, 0.0);
        RenderBatch hb; hs.emitTo(hb);
        check(hb.fillVertices.size() >= 6 && hb.fillVertices.size() % 3 == 0,
              "Hachura SOLID triangula o contorno (fill)");

        // MacroCmd: agrupa 2 ReplaceCmd num único passo de undo.
        DrawingManager dm(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId ma = dm.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        const EntityId mbb = dm.addEntity(std::make_unique<Line>(Point3{0, 5, 0}, Point3{10, 5, 0}));
        auto macro = std::make_unique<MacroCmd>("T");
        { auto n = std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}); n->setLayer("X");
          macro->add(std::make_unique<ReplaceCmd>(ma, std::move(n))); }
        { auto n = std::make_unique<Line>(Point3{0, 5, 0}, Point3{10, 5, 0}); n->setLayer("X");
          macro->add(std::make_unique<ReplaceCmd>(mbb, std::move(n))); }
        dm.execute(std::move(macro));
        check(dm.getEntity(ma)->layer() == "X" && dm.getEntity(mbb)->layer() == "X",
              "MacroCmd aplica a todos os sub-comandos");
        dm.undo();
        check(dm.getEntity(ma)->layer() != "X" && dm.getEntity(mbb)->layer() != "X",
              "MacroCmd: 1 undo reverte todos");

        // Offset: fluxo completo (Selecting -> Enter -> lado).
        DrawingManager dOff(std::make_unique<Quadtree>(world, 12, 8));
        dOff.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{20, 0, 0}));
        ToolController tcO(dOff);
        tcO.setTool(ToolKind::Offset);
        tcO.selectAt(Point3{10, 0, 0}, 1.0, false);
        tcO.confirmSelection();
        const bool offDone = tcO.onPoint(Point3{10, 5, 0});
        bool offPar = false;
        dOff.forEach([&](const Entity& en) {
            if (std::abs(en.boundingBox().min.y - 5.0) < 1e-6) offPar = true; });
        check(offDone && dOff.count() == 2 && offPar, "Offset cria paralela a 5 (2 entidades)");

        // Offset de Arc (concêntrico) e Polyline (paralela).
        const Arc oa = offsetArc(Arc{Point3{0, 0, 0}, 10.0, 0.0, 1.5707963267948966},
                                 5.0, Point3{20, 0, 0});
        check(std::abs(oa.radius() - 15.0) < 1e-9 && std::abs(oa.startAngle()) < 1e-9,
              "offsetArc externo: raio 15, angulos iguais");
        const Polyline op = offsetPolyline(
            Polyline{std::vector<Point3>{{0, 0, 0}, {20, 0, 0}, {20, 20, 0}}, false},
            5.0, Point3{10, 5, 0});
        check(op.vertices().size() == 3 && std::abs(op.vertices()[0].y - 5.0) < 1e-9,
              "offsetPolyline: 3 vertices, 1o sobe 5");

        // Lado correto num retângulo FECHADO: fora expande, dentro contrai.
        Polyline rect(std::vector<Point3>{{0, 0, 0}, {10, 0, 0}, {10, 8, 0}, {0, 8, 0}}, true);
        const Polyline oOut = offsetPolyline(rect, 2.0, Point3{-5, 4, 0});  // clique fora
        const Polyline oIn  = offsetPolyline(rect, 2.0, Point3{5, 4, 0});   // clique dentro
        check(oOut.boundingBox().min.x < -1.5 && oIn.boundingBox().min.x > 1.5,
              "offsetPolyline fechado: fora expande, dentro contrai");

        // Align: leva s1->d1 e s2->d2 (move+rotaciona+escala).
        DrawingManager dA(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId aid = dA.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        ToolController tcA(dA);
        tcA.selectAt(Point3{5, 0, 0}, 1.0, false);
        tcA.setTool(ToolKind::Align);
        tcA.onPoint(Point3{0, 0, 0});    // origem 1
        tcA.onPoint(Point3{5, 5, 0});    // destino 1
        tcA.onPoint(Point3{10, 0, 0});   // origem 2
        tcA.onPoint(Point3{5, 15, 0});   // destino 2 -> aplica
        const auto* al = dynamic_cast<const Line*>(dA.getEntity(aid));
        check(al && std::abs(al->start().x - 5) < 1e-6 && std::abs(al->start().y - 5) < 1e-6 &&
              std::abs(al->end().x - 5) < 1e-6 && std::abs(al->end().y - 15) < 1e-6,
              "Align leva s1->d1 e s2->d2");

        // Trim GENÉRICO: círculo cortado por linha -> vira arco (P1 #1).
        DrawingManager dTr(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId tc = dTr.addEntity(std::make_unique<Circle>(Point3{0, 0, 0}, 10.0));
        const EntityId tl = dTr.addEntity(std::make_unique<Line>(Point3{-20, 5, 0}, Point3{20, 5, 0}));
        ToolController tcTr(dTr);
        tcTr.setTool(ToolKind::Trim);
        (void)tl;
        tcTr.trimClick(tc, Point3{0, 10, 0});    // apara o círculo (vs a linha); topo -> some o arco de cima
        bool hasArc = false, hasCircle = false;
        dTr.forEach([&](const Entity& e) {
            const std::string t = e.typeName();
            if (t == "ARC") hasArc = true; if (t == "CIRCLE") hasCircle = true; });
        check(hasArc && !hasCircle && dTr.count() == 2, "Trim: circulo cortado por linha vira arco");

        // Trim GENÉRICO: linha cortada por círculo -> remove o meio (2 segmentos).
        DrawingManager dTr2(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId tcir2 = dTr2.addEntity(std::make_unique<Circle>(Point3{0, 0, 0}, 10.0));
        const EntityId tl2 = dTr2.addEntity(std::make_unique<Line>(Point3{-20, 0, 0}, Point3{20, 0, 0}));
        ToolController tcTr2(dTr2);
        tcTr2.setTool(ToolKind::Trim);
        (void)tcir2;
        tcTr2.trimClick(tl2, Point3{0, 0, 0});    // apara a linha (vs o círculo); centro -> some o meio
        int lineCount = 0;
        dTr2.forEach([&](const Entity& e) { if (std::string(e.typeName()) == "LINE") lineCount++; });
        check(lineCount == 2, "Trim: linha cortada por circulo vira 2 segmentos");

        // Extend GENÉRICO: linha estende até um CÍRCULO (contorno).
        DrawingManager dE(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId el = dE.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{5, 0, 0}));
        const EntityId eb = dE.addEntity(std::make_unique<Circle>(Point3{0, 0, 0}, 10.0));
        ToolController tcE(dE);
        tcE.setTool(ToolKind::Extend);
        tcE.extendClick(eb, Point3{10, 0, 0});   // contorno = círculo
        tcE.extendClick(el, Point3{5, 0, 0});    // alvo = linha; ponta direita -> estende até (10,0)
        check(std::abs(dE.getEntity(el)->boundingBox().max.x - 10.0) < 1e-6,
              "Extend: linha estende ate o circulo (x=10)");

        // Break GENÉRICO: círculo quebrado em 2 pontos -> vira arco.
        DrawingManager dBk(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId bkc = dBk.addEntity(std::make_unique<Circle>(Point3{0, 0, 0}, 10.0));
        ToolController tcBk(dBk);
        tcBk.setTool(ToolKind::BreakTool);
        tcBk.breakClick(bkc, Point3{0, 0, 0});    // 1ª: escolhe o círculo
        tcBk.breakClick(bkc, Point3{10, 0, 0});   // 2ª: ponto 1 (ang 0)
        tcBk.breakClick(bkc, Point3{0, 10, 0});   // 3ª: ponto 2 (ang 90) -> remove o quarto
        bool bkArc = false, bkCirc = false;
        dBk.forEach([&](const Entity& e) {
            const std::string t = e.typeName();
            if (t == "ARC") bkArc = true; if (t == "CIRCLE") bkCirc = true; });
        check(bkArc && !bkCirc, "Break: circulo vira arco");

        // Trim de POLYLINE (aberta): "L" cortada por uma linha vertical em x=10.
        DrawingManager dP(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId pl = dP.addEntity(std::make_unique<Polyline>(
            std::vector<Point3>{{0, 0, 0}, {20, 0, 0}, {20, 20, 0}}, false));
        dP.addEntity(std::make_unique<Line>(Point3{10, -5, 0}, Point3{10, 5, 0}));  // corta em (10,0)
        ToolController tcP(dP);
        tcP.setTool(ToolKind::Trim);
        tcP.trimClick(pl, Point3{5, 0, 0});   // pick no trecho inicial -> some [0..(10,0)]
        check(dP.getEntity(pl) && std::abs(dP.getEntity(pl)->boundingBox().min.x - 10.0) < 1e-6,
              "Trim: polilinha aparada (resta de x=10 em diante)");

        // Lengthen para Arco: +10 de comprimento num arco r=10 -> +1 rad no fim.
        const Arc la = lengthenArc(Arc{Point3{0, 0, 0}, 10.0, 0.0, 1.5707963267948966}, 10.0, true);
        check(std::abs(la.endAngle() - (1.5707963267948966 + 1.0)) < 1e-6,
              "lengthenArc estende o fim (+delta/raio rad)");

        // Trim contra XLINE (reta de construção como aresta de corte).
        DrawingManager dX(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId xln = dX.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{20, 0, 0}));
        dX.addEntity(std::make_unique<XLine>(XLine::fromTwoPoints(Point3{10, -5, 0}, Point3{10, 5, 0})));
        ToolController tcX(dX);
        tcX.setTool(ToolKind::Trim);
        tcX.trimClick(xln, Point3{15, 0, 0});   // pick à direita de x=10 -> some x>10
        check(dX.getEntity(xln) && std::abs(dX.getEntity(xln)->boundingBox().max.x - 10.0) < 1e-6,
              "Trim: linha aparada por XLINE de construcao");

        // Cena "estrela + círculo": linhas pelo centro cruzando um círculo.
        DrawingManager dS(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId hStar = dS.addEntity(std::make_unique<Line>(Point3{-20, 0, 0}, Point3{20, 0, 0}));
        dS.addEntity(std::make_unique<Line>(Point3{0, -20, 0}, Point3{0, 20, 0}));   // vertical pelo centro
        dS.addEntity(std::make_unique<Circle>(Point3{0, 0, 0}, 10.0));
        int starBefore = 0;
        dS.forEach([&](const Entity& e) { if (std::string(e.typeName()) == "LINE") starBefore++; });
        ToolController tcS(dS);
        tcS.setTool(ToolKind::Trim);
        tcS.trimClick(hStar, Point3{5, 0, 0});   // apara a horizontal (corta no centro e no círculo)
        int starAfter = 0;
        dS.forEach([&](const Entity& e) { if (std::string(e.typeName()) == "LINE") starAfter++; });
        check(starAfter > starBefore, "Trim em cena estrela+circulo realmente corta (split)");

        // Trim preserva o tipo de linha (ex.: CENTER) na geometria aparada.
        DrawingManager dCt(std::make_unique<Quadtree>(world, 12, 8));
        auto clNice = std::make_unique<Line>(Point3{-20, 0, 0}, Point3{20, 0, 0});
        clNice->setLineType(LineType{"CENTER"});
        const EntityId clid = dCt.addEntity(std::move(clNice));
        dCt.addEntity(std::make_unique<Line>(Point3{0, -5, 0}, Point3{0, 5, 0}));  // corta no centro
        ToolController tcCt(dCt);
        tcCt.setTool(ToolKind::Trim);
        tcCt.trimClick(clid, Point3{10, 0, 0});   // apara a metade direita (1 peça, in-place)
        const Entity* clRes = dCt.getEntity(clid);
        check(clRes && clRes->lineType().name == "CENTER", "Trim preserva o tipo de linha (CENTER)");

        // Join não-colinear: 2 linhas que se tocam viram uma polilinha.
        DrawingManager dJ(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId j1 = dJ.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        const EntityId j2 = dJ.addEntity(std::make_unique<Line>(Point3{10, 0, 0}, Point3{10, 10, 0}));
        ToolController tcJ(dJ);
        tcJ.setTool(ToolKind::JoinTool);
        tcJ.joinClick(j1, Point3{});
        tcJ.joinClick(j2, Point3{});
        bool jPoly = false; int jLines = 0;
        dJ.forEach([&](const Entity& e) {
            const std::string t = e.typeName();
            if (t == "LWPOLYLINE") jPoly = true; if (t == "LINE") jLines++; });
        check(jPoly && jLines == 0 && dJ.count() == 1,
              "Join nao-colinear: 2 linhas tocando viram polilinha");

        // Entrada numérica: Circle por RAIO digitado (após o centro).
        DrawingManager dV(std::make_unique<Quadtree>(world, 12, 8));
        ToolController tcV(dV);
        tcV.setTool(ToolKind::Circle);
        tcV.onPoint(Point3{0, 0, 0});          // centro
        const bool wantsR = tcV.wantsValue();
        const bool madeC = tcV.onValue(7.0);   // raio digitado
        bool r7 = false;
        dV.forEach([&](const Entity& e) {
            if (const auto* c = dynamic_cast<const Circle*>(&e))
                if (std::abs(c->radius() - 7.0) < 1e-9) r7 = true; });
        check(wantsR && madeC && r7 && dV.count() == 1, "Circle por raio digitado = 7");

        // Entrada numérica: Polígono (6 lados, inscrito) por raio digitado.
        DrawingManager dV2(std::make_unique<Quadtree>(world, 12, 8));
        ToolController tcV2(dV2);
        tcV2.setPolygon(6, true);
        tcV2.setTool(ToolKind::Polygon);
        tcV2.onPoint(Point3{0, 0, 0});
        check(tcV2.onValue(10.0) && dV2.count() == 1, "Poligono por raio digitado cria");

        // Entrada numérica: Retângulo por 2 medidas (largura x altura).
        DrawingManager dV3(std::make_unique<Quadtree>(world, 12, 8));
        ToolController tcV3(dV3);
        tcV3.setTool(ToolKind::Rectangle);
        tcV3.onPoint(Point3{0, 0, 0});             // 1º canto
        const bool wantsDim = tcV3.wantsDimensions();
        const bool madeR = tcV3.onDimensions(30.0, 20.0);
        bool rok = false;
        dV3.forEach([&](const Entity& en) {
            const AABB bb = en.boundingBox();
            if (std::abs(bb.max.x - 30.0) < 1e-9 && std::abs(bb.max.y - 20.0) < 1e-9) rok = true; });
        check(wantsDim && madeR && rok && dV3.count() == 1, "Retangulo por dimensoes 30x20");

        // Entrada numérica: Elipse por meio-eixo MENOR digitado (após centro + eixo maior).
        DrawingManager dV4(std::make_unique<Quadtree>(world, 12, 8));
        ToolController tcV4(dV4);
        tcV4.setTool(ToolKind::Ellipse);
        tcV4.onPoint(Point3{0, 0, 0});          // centro
        tcV4.onPoint(Point3{20, 0, 0});         // fim do eixo maior (a=20)
        const bool wantsE = tcV4.wantsValue();
        const bool madeE = tcV4.onValue(8.0);   // meio-eixo menor
        bool eok = false;
        dV4.forEach([&](const Entity& en) {
            const AABB bb = en.boundingBox();
            if (std::abs(bb.max.x - 20.0) < 0.1 && std::abs(bb.max.y - 8.0) < 0.1) eok = true; });
        check(wantsE && madeE && eok && dV4.count() == 1, "Elipse por eixo menor digitado (20x8)");

        // Hachura em ÁREA FECHADA qualquer (não só polilinha): círculo e elipse.
        DrawingManager dV5(std::make_unique<Quadtree>(world, 12, 8));
        ToolController tcV5(dV5);
        tcV5.setHatch(4, 0.0, 1.0);   // padrão SOLID
        const EntityId cidH = dV5.addEntity(std::make_unique<Circle>(Point3{0, 0, 0}, 10.0));
        tcV5.hatchPick(cidH);
        const EntityId eidH = dV5.addEntity(std::make_unique<Ellipse>(
            Ellipse::fromCenterAxes(Point3{40, 0, 0}, Vec3{15, 0, 0}, 6.0)));
        tcV5.hatchPick(eidH);
        int nHatch = 0;
        dV5.forEach([&](const Entity& en) {
            if (std::string(en.typeName()) == "HATCH") nHatch++; });
        check(nHatch == 2, "Hachura em circulo e elipse (area fechada) cria HATCH");

        // Copy múltiplo (AutoCAD): após o ponto-base, cada clique cola uma cópia.
        DrawingManager dV6(std::make_unique<Quadtree>(world, 12, 8));
        dV6.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        ToolController tcV6(dV6);
        tcV6.setTool(ToolKind::Copy);
        tcV6.selectAt(Point3{5, 0, 0}, 1.0, false);   // seleciona a linha
        tcV6.confirmSelection();                       // Selecting -> Base
        tcV6.onPoint(Point3{0, 0, 0});                 // ponto-base
        tcV6.onPoint(Point3{0, 20, 0});                // 1ª cópia
        tcV6.onPoint(Point3{0, 40, 0});                // 2ª cópia (loop continua)
        const bool loopOn = tcV6.editLoopActive();
        check(dV6.count() == 3 && loopOn,
              "Copy multiplo: 2 cliques = 2 copias, loop ativo");
        tcV6.cancel();                                 // Enter/Esc encerra
        check(!tcV6.editLoopActive(), "Copy multiplo encerra com cancel/Enter");

        // Rastreamento polar: gruda em múltiplos do incremento dentro da tolerância.
        const PolarResult pol1 = polarSnap(Point3{0, 0, 0}, 10.0, 9.5, 45.0, 5.0);
        check(pol1.active && std::abs(pol1.point.x - pol1.point.y) < 1e-6 &&
                  std::abs(pol1.angleDeg - 45.0) < 1e-9,
              "Polar: cursor perto de 45 gruda no raio 45 (x==y)");
        const PolarResult pol2 = polarSnap(Point3{0, 0, 0}, 10.0, 3.6, 45.0, 4.0);
        check(!pol2.active, "Polar: cursor longe de qualquer multiplo fica livre");
        const PolarResult pol3 = polarSnap(Point3{0, 0, 0}, 10.0, 0.3, 15.0, 4.0);
        check(pol3.active && std::abs(pol3.point.y) < 1e-6 && std::abs(pol3.angleDeg) < 1e-9,
              "Polar: incremento 15 gruda no horizontal (0)");

        // OSNAP por tipo: a máscara liga/desliga cada SnapType.
        DrawingManager dSnap(std::make_unique<Quadtree>(world, 12, 8));
        dSnap.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        SnapEngine se;
        const SnapResult sr1 = se.resolve(Point3{0.1, 0.1, 0}, 1.0, dSnap, nullptr, kAllSnaps);
        check(sr1.hit && sr1.type == SnapType::Endpoint, "OSNAP: endpoint capturado (mascara cheia)");
        const SnapResult sr2 = se.resolve(Point3{0.1, 0.1, 0}, 1.0, dSnap, nullptr, snapBit(SnapType::Midpoint));
        check(!sr2.hit, "OSNAP: endpoint ignorado quando so Midpoint ativo");
        const SnapResult sr3 = se.resolve(Point3{5.1, 0.1, 0}, 1.0, dSnap, nullptr, snapBit(SnapType::Midpoint));
        check(sr3.hit && sr3.type == SnapType::Midpoint, "OSNAP: midpoint capturado quando ativo");

        // Camada ON/OFF: camada desligada (on=false) não seleciona; religar volta.
        DrawingManager dLay(std::make_unique<Quadtree>(world, 12, 8));
        auto lnLay = std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0});
        lnLay->setLayer("oculta");
        dLay.addEntity(std::move(lnLay));
        Layer hid; hid.name = "oculta"; hid.on = false; dLay.layers().add(hid);
        ToolController tcL(dLay);
        tcL.selectAt(Point3{5, 0, 0}, 1.0, false);
        check(tcL.selection().empty(), "Camada OFF (on=false) nao seleciona");
        dLay.layers().find("oculta")->on = true;
        tcL.selectAt(Point3{5, 0, 0}, 1.0, false);
        check(tcL.selection().size() == 1, "Camada ON volta a selecionar");

        // Trim NÃO fragmenta: linha cruzando 4 cortes, apara o meio -> 2 peças
        // contínuas [0..4] e [6..10] (e não 4 cacos nos cortes 2/4/6/8).
        Line trimTarget(Point3{0, 0, 0}, Point3{10, 0, 0});
        const std::vector<Point3> cutsT{{2, 0, 0}, {4, 0, 0}, {6, 0, 0}, {8, 0, 0}};
        auto trimmed = splitEntityAt(trimTarget, cutsT, Point3{5, 0, 0});
        bool spans = trimmed.size() == 2;
        if (spans) {
            const AABB b0 = trimmed[0]->boundingBox(), b1 = trimmed[1]->boundingBox();
            const double lo = std::min(b0.min.x, b1.min.x), hi = std::max(b0.max.x, b1.max.x);
            const double midGapL = std::min(b0.max.x, b1.max.x), midGapR = std::max(b0.min.x, b1.min.x);
            spans = std::abs(lo) < 1e-6 && std::abs(hi - 10.0) < 1e-6 &&
                    std::abs(midGapL - 4.0) < 1e-6 && std::abs(midGapR - 6.0) < 1e-6;
        }
        check(spans, "Trim deixa 2 pecas continuas (nao fragmenta nos demais cortes)");

        // Fence: interseção segmento-segmento (base do aparo/extensão por cerca).
        Point3 fx{};
        const bool cross = segmentIntersect(Point3{0, 5, 0}, Point3{10, 5, 0},
                                            Point3{5, 0, 0}, Point3{5, 10, 0}, fx);
        check(cross && std::abs(fx.x - 5.0) < 1e-9 && std::abs(fx.y - 5.0) < 1e-9,
              "Fence: segmentos cruzados dao o ponto (5,5)");
        Point3 fy{};
        const bool par = segmentIntersect(Point3{0, 0, 0}, Point3{10, 0, 0},
                                          Point3{0, 2, 0}, Point3{10, 2, 0}, fy);
        check(!par, "Fence: segmentos paralelos nao cruzam");

        // Estilos nomeados: tabela de cota (add / corrente / remover; Standard fixo).
        StyleTable styles;
        DimStyle ds; ds.name = "Arquitetura"; ds.textHeight = 3.5; ds.decimals = 0; ds.suffix = " mm";
        styles.addDim(ds);
        check(styles.findDim("Arquitetura") != nullptr && styles.allDim().size() == 2,
              "Estilo de cota: criado (Standard + Arquitetura)");
        check(styles.setCurrentDim("Arquitetura") &&
                  std::abs(styles.currentDim().textHeight - 3.5) < 1e-9 &&
                  styles.currentDim().decimals == 0,
              "Estilo de cota: tornar corrente aplica os parametros");
        check(!styles.removeDim("Standard"), "Estilo de cota: nao remove o Standard");
        check(styles.removeDim("Arquitetura") && styles.currentDimName() == "Standard",
              "Estilo de cota: remover volta ao Standard");

        // Multileader multi-chamada: cada traço acumula; Enter vazio conclui.
        DrawingManager dML(std::make_unique<Quadtree>(world, 12, 8));
        ToolController tcML(dML);
        tcML.setTool(ToolKind::MLeaderTool);
        tcML.onPoint(Point3{0, 0, 0});  tcML.onPoint(Point3{5, 5, 0});  tcML.finishStroke();  // chamada 1
        const bool acc1 = (dML.count() == 0) && tcML.mleaderPending();
        tcML.onPoint(Point3{10, 0, 0}); tcML.onPoint(Point3{5, 5, 0});  tcML.finishStroke();  // chamada 2
        tcML.finishStroke();   // Enter vazio -> cria 1 MLeader com 2 chamadas
        bool ml2 = false;
        dML.forEach([&](const Entity& e) {
            if (auto* m = dynamic_cast<const MLeader*>(&e)) ml2 = (m->leaders().size() == 2); });
        check(acc1 && dML.count() == 1 && ml2,
              "Multileader: 2 chamadas acumuladas viram 1 MLeader");

        // Hachura por ponto interno: traça o contorno que envolve o clique.
        DrawingManager dB(std::make_unique<Quadtree>(world, 12, 8));
        dB.addEntity(std::make_unique<Line>(Point3{0, 0, 0},  Point3{10, 0, 0}));
        dB.addEntity(std::make_unique<Line>(Point3{10, 0, 0}, Point3{10, 10, 0}));
        dB.addEntity(std::make_unique<Line>(Point3{10, 10, 0},Point3{0, 10, 0}));
        dB.addEntity(std::make_unique<Line>(Point3{0, 10, 0}, Point3{0, 0, 0}));
        const std::vector<Point3> loop = traceBoundary(dB, Point3{5, 5, 0});
        AABB lb;
        for (const Point3& v : loop) lb.expand(v);
        check(loop.size() >= 3 && lb.valid() &&
                  std::abs(lb.min.x) < 1e-6 && std::abs(lb.min.y) < 1e-6 &&
                  std::abs(lb.max.x - 10.0) < 1e-6 && std::abs(lb.max.y - 10.0) < 1e-6,
              "BoundaryTrace: contorno do quadrado a partir de (5,5)");

        // Array Path: 5 cópias de um círculo ao longo de (0,0)->(100,0).
        Circle apSrc(Point3{0, 0, 0}, 3.0);
        const std::vector<Point3> apPath{{0, 0, 0}, {100, 0, 0}};
        auto apCopies = arrayAlongPath(apSrc, apPath, 5, false);
        bool apOk = apCopies.size() == 5;
        if (apOk) {
            const double xs[5] = {0, 25, 50, 75, 100};
            for (int i = 0; i < 5; ++i) {
                const AABB bb = apCopies[i]->boundingBox();
                const double cx = (bb.min.x + bb.max.x) * 0.5;
                if (std::abs(cx - xs[i]) > 1e-6) apOk = false;
            }
        }
        check(apOk, "Array Path: 5 copias com centros x=0,25,50,75,100");

        // MTEXT real: multilinha (2 linhas mais altas que 1) + justificação.
        MText mt(Point3{0, 0, 0}, "AB\nCDE", 2.5);
        const AABB mtL = mt.boundingBox();
        check((mtL.max.y - mtL.min.y) > 4.0, "MTEXT multilinha: 2 linhas (altura > 1 linha)");
        mt.setJustify(MTextJustify::Center);
        const AABB mtC = mt.boundingBox();
        check(mtC.min.x < -0.1, "MTEXT centralizado desloca para X negativo");

        // Booleanas de polígono: dois quadrados sobrepostos (A 0..10, B 5..15).
        auto polyArea = [](const std::vector<Point3>& p) {
            double a = 0.0;
            for (std::size_t i = 0; i < p.size(); ++i) {
                const Point3& u = p[i]; const Point3& v = p[(i + 1) % p.size()];
                a += u.x * v.y - v.x * u.y;
            }
            return std::abs(a) * 0.5;
        };
        const std::vector<Point3> polyA{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
        const std::vector<Point3> polyB{{5, 5, 0}, {15, 5, 0}, {15, 15, 0}, {5, 15, 0}};
        auto sumArea = [&](const std::vector<std::vector<Point3>>& ls) {
            double s = 0.0; for (const auto& l : ls) s += polyArea(l); return s; };
        const auto bInt = polygonBoolean(polyA, polyB, BoolOp::Intersection);
        const auto bUni = polygonBoolean(polyA, polyB, BoolOp::Union);
        const auto bDif = polygonBoolean(polyA, polyB, BoolOp::Difference);
        check(std::abs(sumArea(bInt) - 25.0)  < 1e-4, "Boolean: intersecao area 25");
        check(std::abs(sumArea(bUni) - 175.0) < 1e-4, "Boolean: uniao area 175");
        check(std::abs(sumArea(bDif) - 75.0)  < 1e-4, "Boolean: diferenca A-B area 75");

        // WIPEOUT: máscara opaca de um quadrado → bbox + fill (triângulos) + borda.
        Wipeout wp(std::vector<Point3>{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}});
        const AABB wb = wp.boundingBox();
        RenderBatch wrb; wp.emitTo(wrb);
        check(std::abs(wb.max.x - 10.0) < 1e-9 && std::abs(wb.max.y - 10.0) < 1e-9 &&
                  wrb.fillVertices.size() == 6 && wrb.lineVertices.size() == 8,
              "Wipeout: bbox 10x10, fill 2 triangulos, borda 4 arestas");

        // REGION: quadrado 10x10 com furo 2x2 → area 96.
        Region rg(std::vector<std::vector<Point3>>{
            {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}},
            {{4, 4, 0}, {6, 4, 0}, {6, 6, 0}, {4, 6, 0}}});
        check(std::abs(rg.area() - 96.0) < 1e-6, "Region: area com furo = 96");

        // TABLE: 2x3, col 20, lin 10 → bbox 60x20; grade emitida.
        Table tb(Point3{0, 0, 0}, 2, 3, 20.0, 10.0);
        tb.setCell(0, 0, "A1");
        const AABB tbb = tb.boundingBox();
        RenderBatch trb; tb.emitTo(trb);
        check(std::abs((tbb.max.x - tbb.min.x) - 60.0) < 1e-6 &&
                  std::abs((tbb.max.y - tbb.min.y) - 20.0) < 1e-6 &&
                  trb.lineVertices.size() >= 14,
              "Table: bbox 60x20 + grade (>=7 segmentos)");

        // HATCH com furo: SOLID externo 10x10 + furo 4x4 → area preenchida < 100.
        Hatch hh(std::vector<std::vector<Point3>>{
                     {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}},
                     {{3, 3, 0}, {7, 3, 0}, {7, 7, 0}, {3, 7, 0}}},
                 HatchPattern::Solid, 0.0, 1.0);
        RenderBatch hrb; hh.emitTo(hrb);
        double triArea = 0.0;
        for (std::size_t i = 0; i + 2 < hrb.fillVertices.size() + 1 && i + 2 < hrb.fillVertices.size(); i += 3) {
            const Point3& a = hrb.fillVertices[i]; const Point3& b = hrb.fillVertices[i + 1];
            const Point3& c = hrb.fillVertices[i + 2];
            triArea += std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;
        }
        check(hrb.fillVertices.size() % 3 == 0 && triArea > 0.0 && triArea < 100.0,
              "Hatch SOLID com furo: area preenchida < 100 (furo subtraido)");

        // PEDIT: inserir e remover vértice de polilinha.
        Polyline pedPl(std::vector<Point3>{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}}, false);
        auto pIns = withVertexInserted(pedPl, Point3{5, 0, 0});
        const auto* pi = dynamic_cast<const Polyline*>(pIns.get());
        check(pi && pi->vertices().size() == 4, "PEDIT: inserir vertice -> 4 vertices");
        auto pRem = withVertexRemoved(*pi, 1);
        const auto* pr = dynamic_cast<const Polyline*>(pRem.get());
        check(pr && pr->vertices().size() == 3, "PEDIT: remover vertice -> 3 vertices");
        Polyline ped2(std::vector<Point3>{{0, 0, 0}, {1, 0, 0}}, false);
        check(!withVertexRemoved(ped2, 0), "PEDIT: nao remove abaixo de 2 vertices");

        // SPLINEDIT: grips = pontos de controle; mover/inserir.
        Spline spl(std::vector<Point3>{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}}, true);
        check(gripsOf(spl).size() == 3, "SPLINEDIT: grips = pontos de controle");
        auto spMov = withGripMoved(spl, 1, Point3{10, 5, 0});
        const auto* sm = dynamic_cast<const Spline*>(spMov.get());
        check(sm && std::abs(sm->controlPoints()[1].y - 5.0) < 1e-9, "SPLINEDIT: move ponto de controle");
        auto spIns = withVertexInserted(spl, Point3{5, 0, 0});
        const auto* si = dynamic_cast<const Spline*>(spIns.get());
        check(si && si->controlPoints().size() == 4, "SPLINEDIT: insere ponto de controle");

        // Fillet RAIO 0 = canto vivo: estende as 2 retas até a interseção, sem arco.
        DrawingManager dFil(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId fl1 = dFil.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{8, 0, 0}));
        const EntityId fl2 = dFil.addEntity(std::make_unique<Line>(Point3{10, 2, 0}, Point3{10, 10, 0}));
        dFil.execute(std::make_unique<FilletCmd>(fl1, fl2, 0.0, Point3{4, 0, 0}, Point3{10, 6, 0}));
        const Entity* nfl1 = dFil.getEntity(fl1);
        check(dFil.count() == 2 && nfl1 && std::abs(nfl1->boundingBox().max.x - 10.0) < 1e-6,
              "Fillet R0: canto vivo (sem arco, 1a reta estende ate x=10)");

        // Tipo de linha CUSTOM: registra um padrão e aplica por nome.
        registerLineType("MEU", std::vector<double>{6.0, 3.0});
        check(customLinePattern("meu") != nullptr, "Linetype custom registrado (case-insensitive)");
        const std::vector<Point3> ltSeg{{0, 0, 0}, {20, 0, 0}};
        const auto ltDash = applyLineTypeByName(ltSeg, "MEU", 1.0);
        check(ltDash.size() > 2 && ltDash.size() % 2 == 0, "Linetype custom vira varios tracos");
        const auto ltCont = applyLineTypeByName(ltSeg, "CONTINUOUS", 1.0);
        check(ltCont.size() == 2, "Linetype CONTINUOUS fica inalterado");

        // OFFSET herda a camada (e props) da linha ORIGINAL.
        DrawingManager dOffset(std::make_unique<Quadtree>(world, 12, 8));
        auto offLn = std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0});
        offLn->setLayer("Parede");
        const EntityId offId = dOffset.addEntity(std::move(offLn));
        dOffset.execute(std::make_unique<OffsetCmd>(offId, 5.0, Point3{5, 5, 0}));
        std::string offNewLayer;
        dOffset.forEach([&](const Entity& en) { if (en.id() != offId) offNewLayer = en.layer(); });
        check(dOffset.count() == 2 && offNewLayer == "Parede", "Offset herda a camada da linha original");

        // Multileader: 2 chamadas para 1 texto.
        std::vector<std::vector<Point3>> leaders{{{0, 0, 0}, {6, 4, 0}}, {{12, 0, 0}, {6, 4, 0}}};
        MLeader mld(leaders, Point3{6, 5, 0}, "A1", 2.5);
        RenderBatch mlb; mld.emitTo(mlb);
        Ray mlr; mlr.origin = Point3{3, 2, 0};   // sobre a 1ª chamada
        check(mlb.segmentCount() > 0 && static_cast<bool>(mld.hitTest(mlr, 0.6)),
              "MLeader: 2 chamadas emite + hitTest");
    }

    // --- Seleção por caixa: Window vs Crossing ---------------------------
    {
        DrawingManager d5(std::make_unique<Quadtree>(world, 12, 8));
        d5.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));    // A: dentro
        d5.addEntity(std::make_unique<Line>(Point3{5, 0, 0}, Point3{50, 0, 0}));    // B: cruza
        d5.addEntity(std::make_unique<Line>(Point3{100, 100, 0}, Point3{110, 110, 0})); // C: fora
        ToolController tc(d5);

        const AABB box{Point3{-1, -1, -1}, Point3{11, 1, 1}};

        tc.selectInBox(box, /*crossing*/ false, false);  // Window
        check(tc.selection().size() == 1, "Window seleciona so o totalmente contido (A)");

        tc.selectInBox(box, /*crossing*/ true, false);   // Crossing
        check(tc.selection().size() == 2, "Crossing seleciona contido + cruzado (A e B)");
    }

    // --- SnapEngine: captura de pontos (OSNAP) ---------------------------
    {
        DrawingManager d3(std::make_unique<Quadtree>(world, 12, 8));
        d3.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{100, 0, 0}));
        SnapEngine snap;

        const SnapResult r1 = snap.resolve(Point3{2, 1, 0}, 5.0, d3);   // perto de (0,0)
        check(r1 && std::abs(r1.point.x) < 1e-9 && std::abs(r1.point.y) < 1e-9 &&
              r1.type == SnapType::Endpoint, "snap captura Endpoint (0,0)");

        const SnapResult r2 = snap.resolve(Point3{50, 2, 0}, 5.0, d3);  // perto de (50,0)
        check(r2 && std::abs(r2.point.x - 50.0) < 1e-9 && r2.type == SnapType::Midpoint,
              "snap captura Midpoint (50,0)");

        const SnapResult rn = snap.resolve(Point3{30, 2, 0}, 5.0, d3);  // longe de pontos notaveis
        check(rn && rn.type == SnapType::Nearest && std::abs(rn.point.x - 30.0) < 1e-6 &&
              std::abs(rn.point.y) < 1e-6, "snap Nearest projeta sobre a linha (30,0)");

        const Point3 fromP{30, 20, 0};
        const SnapResult rperp = snap.resolve(Point3{30.5, 1, 0}, 5.0, d3, &fromP);
        check(rperp && rperp.type == SnapType::Perpendicular &&
              std::abs(rperp.point.x - 30.0) < 1e-6 && std::abs(rperp.point.y) < 1e-6,
              "snap Perpendicular: pe da perpendicular (30,0)");

        const SnapResult r3 = snap.resolve(Point3{50, 40, 0}, 5.0, d3); // longe de tudo
        check(!r3, "sem snap fora da tolerancia");

        d3.addEntity(std::make_unique<Circle>(Point3{0, 0, 0}, 10.0));
        // Quadrante superior (0,10): a linha horizontal não passa ali (sem interseção).
        const SnapResult r4 = snap.resolve(Point3{0.2, 10.4, 0}, 1.0, d3);
        check(r4 && std::abs(r4.point.y - 10.0) < 1e-9 && r4.type == SnapType::Quadrant,
              "snap captura Quadrant do circulo (0,10)");
    }

    // --- Undo / Redo -----------------------------------------------------
    const std::size_t before = doc.count();
    doc.execute(std::make_unique<DeleteEntityCmd>(picked));
    check(doc.count() == before - 1, "delete remove 1 entidade");

    doc.undo();
    check(doc.count() == before, "undo restaura a entidade");
    check(doc.getEntity(picked) != nullptr, "undo preserva o id original");

    doc.redo();
    check(doc.count() == before - 1, "redo reaplica o delete");

    doc.undo();  // deixa o documento intacto
    check(doc.count() == before, "estado final consistente");

    // --- Layouts / Paper Space (Pranchas) --------------------------------
    {
        LayoutTable lts;
        check(lts.empty(), "LayoutTable comeca vazia");

        Layout& L = lts.addDefault("Prancha 1");
        L.paper = PaperSize::A3;
        L.landscape = true;
        check(lts.size() == 1, "add cria 1 prancha");
        check(std::abs(L.widthMm() - 420.0) < 1e-9 && std::abs(L.heightMm() - 297.0) < 1e-9,
              "A3 paisagem = 420x297 mm");
        L.landscape = false;
        check(std::abs(L.widthMm() - 297.0) < 1e-9 && std::abs(L.heightMm() - 420.0) < 1e-9,
              "A3 retrato = 297x420 mm");
        L.landscape = true;

        // Escala 1:50 desenhando em milimetros -> 0.02 mm de papel por unidade.
        check(std::abs(scaleMmPerUnit(unitLengthMm(0), 50.0) - 0.02) < 1e-12,
              "escala 1:50 (mm) = 0.02 mm/unid");
        // Desenhando em metros, 1:100 -> 10 mm de papel por unidade.
        check(std::abs(scaleMmPerUnit(unitLengthMm(3), 100.0) - 10.0) < 1e-9,
              "escala 1:100 (m) = 10 mm/unid");
        // Rótulo de escala: reduções "1:N", ampliações "M:1".
        check(formatScale(50.0) == "1:50", "formatScale(50) = 1:50");
        check(formatScale(0.5) == "2:1",  "formatScale(0.5) = 2:1 (ampliacao)");

        SheetViewport vp;
        vp.xMm = 20; vp.yMm = 20; vp.wMm = 120; vp.hMm = 90;
        vp.modelCx = 0; vp.modelCy = 0;
        vp.scaleDenom = 50;
        vp.mmPerUnit = scaleMmPerUnit(unitLengthMm(0), 50.0);
        L.viewports.push_back(vp);
        check(L.viewports.size() == 1, "viewport adicionado a prancha");

        // Um ponto de modelo 1000 mm (=1 m) a leste do centro sai +20 mm na folha.
        const Point3 pp = vp.toPaper(Point3{1000.0, 0.0, 0.0});
        check(std::abs(pp.x - 100.0) < 1e-9 && std::abs(pp.y - 65.0) < 1e-9,
              "1000mm @1:50 -> +20mm no papel (centro do vp em 80,65)");
        check(vp.contains(pp.x, pp.y), "ponto plotado cai dentro do viewport");
        check(!vp.contains(5.0, 5.0), "ponto fora do retangulo nao esta contido");

        // Ida-e-volta papel<->modelo.
        const Point3 mm = vp.toModel(pp.x, pp.y);
        check(std::abs(mm.x - 1000.0) < 1e-6 && std::abs(mm.y - 0.0) < 1e-6,
              "toModel inverte toPaper");

        lts.setCurrent(0);
        check(lts.currentLayout() == &lts.at(0), "prancha corrente aponta pra 0");
        lts.remove(0);
        check(lts.empty() && lts.currentLayout() == nullptr, "remove esvazia a tabela");
    }

    // --- Atributos de bloco (ATTDEF -> definicao -> INSERT com valor) -----
    {
        DrawingManager dA(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId g1 = dA.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{20, 0, 0}));
        const EntityId a1 = dA.addEntity(std::make_unique<AttDef>(
            Point3{2, 2, 0}, "AMBIENTE", "Nome do ambiente?", "SALA", 2.5));
        dA.execute(std::make_unique<MakeBlockCmd>(std::vector<EntityId>{g1, a1},
                                                  Point3{0, 0, 0}, std::string("ROTULO")));
        const BlockDefinition* rdef = dA.blocks().find("ROTULO");
        check(rdef != nullptr && rdef->attdefs.size() == 1 &&
              rdef->attdefs[0].tag == "AMBIENTE" && rdef->attdefs[0].defValue == "SALA",
              "MakeBlock extrai o ATTDEF para a definicao (tag/padrao)");
        check(rdef->members.size() == 1, "o ATTDEF nao vira geometria fixa do bloco");

        // Inserção com valor customizado renderiza o TEXTO do valor.
        auto ins = BlockRef::fromDefinition(*rdef, Matrix4::translation(Vec3{100, 0, 0}));
        check(ins->attValues().size() == 1 && ins->attValues()[0].value == "SALA",
              "insert nasce com o valor padrao do atributo");
        RenderBatch b0; ins->emitTo(b0);
        ins->setAttValue("AMBIENTE", "COZINHA");
        RenderBatch b1; ins->emitTo(b1);
        check(b1.lineVertices.size() > 0 && b1.lineVertices.size() != b0.lineVertices.size(),
              "trocar o valor do atributo muda o texto renderizado");

        // Explodir materializa o valor como MTEXT comum.
        const std::size_t nParts = ins->explodedClones().size();
        check(nParts == 2, "explode materializa geometria + texto do atributo (2 pecas)");
    }

    // --- AutoSnap: interseção (classe PONTO) vence o Perpendicular deferred --
    {
        DrawingManager dS(std::make_unique<Quadtree>(world, 12, 8));
        dS.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{20, 0, 0}));
        dS.addEntity(std::make_unique<Line>(Point3{10, -10, 0}, Point3{10, 10, 0}));
        SnapEngine snX;
        const Point3 fromX{5, 5, 0};
        const SnapResult rX = snX.resolve(Point3{10.3, 0.2, 0}, 1.0, dS, &fromX);
        check(rX && rX.type == SnapType::Intersection,
              "AutoSnap: intersecao vence o perpendicular deferred no cruzamento");

        // Interseção POLILINHA x LINHA (aresta de retângulo cruzada por linha).
        DrawingManager dP(std::make_unique<Quadtree>(world, 12, 8));
        std::vector<Point3> rc = {{0, 0, 0}, {20, 0, 0}, {20, 10, 0}, {0, 10, 0}};
        dP.addEntity(std::make_unique<Polyline>(rc, true));
        dP.addEntity(std::make_unique<Line>(Point3{5, -5, 0}, Point3{5, 15, 0}));
        SnapEngine snP;
        const Point3 fromP2{2, 5, 0};
        const SnapResult rP = snP.resolve(Point3{5.2, 0.1, 0}, 1.0, dP, &fromP2);
        check(rP && rP.type == SnapType::Intersection,
              "AutoSnap: intersecao POLILINHA x LINHA acende no cruzamento");
    }

    // --- JOIN em seleção: 4 linhas de um retângulo -> 1 polilinha FECHADA --
    {
        DrawingManager dJ(std::make_unique<Quadtree>(world, 12, 8));
        dJ.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        dJ.addEntity(std::make_unique<Line>(Point3{10, 0, 0}, Point3{10, 5, 0}));
        dJ.addEntity(std::make_unique<Line>(Point3{10, 5, 0}, Point3{0, 5, 0}));
        dJ.addEntity(std::make_unique<Line>(Point3{0, 5, 0}, Point3{0, 0, 0}));
        ToolController tcJ(dJ);
        check(tcJ.selectByFilter("LINE", "") == 4, "selectByFilter pega as 4 linhas");
        check(tcJ.joinSelected(), "joinSelected une a corrente de linhas");
        check(dJ.count() == 1, "join: 4 linhas viram 1 entidade");
        bool closedPoly = false;
        dJ.forEach([&](const Entity& e) {
            if (const auto* p = dynamic_cast<const Polyline*>(&e)) closedPoly = p->closed();
        });
        check(closedPoly, "join: resultado e polilinha FECHADA");
        dJ.undo();
        check(dJ.count() == 4, "undo do join restaura as 4 linhas");
    }

    // --- Item 8 do backlog: OVERKILL / REVERSE / BLEND / DIVIDE c/ bloco ---
    {
        // OVERKILL: duplicata invertida + linhas colineares sobrepostas + circulo dup
        DrawingManager dK(std::make_unique<Quadtree>(world, 12, 8));
        dK.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        dK.addEntity(std::make_unique<Line>(Point3{10, 0, 0}, Point3{0, 0, 0}));   // dup (invertida)
        dK.addEntity(std::make_unique<Line>(Point3{5, 0, 0}, Point3{15, 0, 0}));   // sobreposta colinear
        dK.addEntity(std::make_unique<Circle>(Point3{50, 50, 0}, 5.0));
        dK.addEntity(std::make_unique<Circle>(Point3{50, 50, 0}, 5.0));            // dup exata
        ToolController tcK(dK);
        int dups = 0, merged = 0;
        check(tcK.overkillRun(1e-6, dups, merged), "OVERKILL executa (selecao vazia = doc todo)");
        check(dups == 2, "OVERKILL: 2 duplicatas (linha invertida + circulo)");
        check(merged == 1, "OVERKILL: 1 linha absorvida por uniao colinear");
        check(dK.count() == 2, "OVERKILL: sobram a linha unida + 1 circulo");
        double minx = 1e9, maxx = -1e9; bool temLinha = false;
        dK.forEach([&](const Entity& e) {
            if (const auto* l = dynamic_cast<const Line*>(&e)) {
                temLinha = true;
                minx = std::min({minx, l->start().x, l->end().x});
                maxx = std::max({maxx, l->start().x, l->end().x});
            }
        });
        check(temLinha && std::abs(minx) < 1e-9 && std::abs(maxx - 15.0) < 1e-9,
              "OVERKILL: a linha unida cobre 0..15");
        dK.undo();
        check(dK.count() == 5, "undo do OVERKILL restaura as 5 entidades");
    }
    {
        // REVERSE: polilinha aberta inverte a ordem dos vertices
        DrawingManager dR(std::make_unique<Quadtree>(world, 12, 8));
        dR.addEntity(std::make_unique<Polyline>(
            std::vector<Point3>{{0, 0, 0}, {10, 0, 0}, {10, 5, 0}}, false));
        ToolController tcR(dR);
        check(tcR.selectByFilter("LWPOLYLINE", "") == 1, "REVERSE: seleciona a polilinha");
        check(tcR.reverseSelected() == 1, "REVERSE inverte 1 entidade");
        Point3 first{};
        dR.forEach([&](const Entity& e) {
            if (const auto* p = dynamic_cast<const Polyline*>(&e)) first = p->vertices().front();
        });
        check(std::abs(first.x - 10.0) < 1e-9 && std::abs(first.y - 5.0) < 1e-9,
              "REVERSE: o primeiro vertice virou o antigo ultimo");
        dR.undo();
        Point3 back{};
        dR.forEach([&](const Entity& e) {
            if (const auto* p = dynamic_cast<const Polyline*>(&e)) back = p->vertices().front();
        });
        check(std::abs(back.x) < 1e-9 && std::abs(back.y) < 1e-9,
              "undo do REVERSE devolve o sentido original");
    }
    {
        // BLEND: spline tangente entre os extremos mais proximos de 2 linhas
        DrawingManager dB(std::make_unique<Quadtree>(world, 12, 8));
        dB.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        dB.addEntity(std::make_unique<Line>(Point3{20, 5, 0}, Point3{30, 5, 0}));
        ToolController tcB(dB);
        check(tcB.selectByFilter("LINE", "") == 2, "BLEND: seleciona as 2 linhas");
        check(tcB.blendSelected(), "BLEND cria a spline de transicao");
        check(dB.count() == 3, "BLEND: as linhas continuam + 1 spline nova");
        const Spline* sp = nullptr;
        dB.forEach([&](const Entity& e) {
            if (const auto* s = dynamic_cast<const Spline*>(&e)) sp = s;
        });
        check(sp && sp->controlPoints().size() == 4, "BLEND: spline com 4 pontos de controle");
        if (sp) {
            const Point3 a = sp->controlPoints().front(), b = sp->controlPoints().back();
            const bool pontas =
                (std::abs(a.x - 10) < 1e-9 && std::abs(a.y) < 1e-9 &&
                 std::abs(b.x - 20) < 1e-9 && std::abs(b.y - 5) < 1e-9) ||
                (std::abs(b.x - 10) < 1e-9 && std::abs(b.y) < 1e-9 &&
                 std::abs(a.x - 20) < 1e-9 && std::abs(a.y - 5) < 1e-9);
            check(pontas, "BLEND: liga (10,0) a (20,5) — extremos mais proximos");
            check(std::abs(sp->controlPoints()[1].y) < 1e-9 ||
                  std::abs(sp->controlPoints()[2].y) < 1e-9,
                  "BLEND: tangente de saida horizontal (G1 com a linha)");
        }
    }
    {
        // DIVIDE marcando com BLOCO alinhado: linha em 4 partes -> 3 inserts
        DrawingManager dD(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId lid = dD.addEntity(
            std::make_unique<Line>(Point3{0, 0, 0}, Point3{40, 0, 0}));
        BlockDefinition defM;
        defM.name = "MARCO";
        defM.members.push_back(std::make_unique<Line>(Point3{-1, 0, 0}, Point3{1, 0, 0}));
        dD.blocks().add(std::move(defM));
        ToolController tcD(dD);
        tcD.setDivideCount(4);
        tcD.setDivideBlock("MARCO", /*align=*/true);
        tcD.divideClick(lid);
        int inserts = 0;
        dD.forEach([&](const Entity& e) {
            if (dynamic_cast<const BlockRef*>(&e)) ++inserts;
        });
        check(inserts == 3, "DIVIDE com bloco: 3 inserts (4 partes de linha aberta)");
        tcD.setDivideBlock("", false);   // volta a marcar com pontos
        tcD.divideClick(lid);
        int pontos = 0;
        dD.forEach([&](const Entity& e) {
            if (dynamic_cast<const PointEntity*>(&e)) ++pontos;
        });
        check(pontos == 3, "DIVIDE sem bloco segue marcando com pontos");
    }
    {
        // --- Escala de anotação (CANNOSCALE): alturas anotativas re-derivadas
        DrawingManager dA(std::make_unique<Quadtree>(world, 12, 8));
        dA.setAnnoMmPerUnit(20.0);   // 1:50 desenhando em METROS = 20 mm/unid
        auto t = std::make_unique<MText>(Point3{0, 0, 0}, "SALA", 0.1);  // 2mm papel
        t->setAnnotative(true);
        const EntityId tid = dA.addEntity(std::move(t));
        const EntityId fid = dA.addEntity(
            std::make_unique<MText>(Point3{0, 5, 0}, "FIXO", 0.1));      // controle
        auto dm = std::make_unique<Dimension>(
            Dimension::linear({0, 0, 0}, {10, 0, 0}, {5, 2, 0}, 0.1));
        dm->setAnnotative(true);
        dm->setArrowSize(0.08);
        const EntityId did = dA.addEntity(std::move(dm));
        check(dA.applyAnnotationScale(10.0) == 2,   // 1:50 -> 1:100
              "ANNOSCALE: regen atinge só as 2 anotativas");
        const auto* t2 = dynamic_cast<const MText*>(dA.getEntity(tid));
        check(t2 && std::abs(t2->height() - 0.2) < 1e-9,
              "texto anotativo DOBRA em 1:100 (os 2mm de papel preservados)");
        const auto* f2 = dynamic_cast<const MText*>(dA.getEntity(fid));
        check(f2 && std::abs(f2->height() - 0.1) < 1e-9, "texto comum intacto");
        const auto* d2 = dynamic_cast<const Dimension*>(dA.getEntity(did));
        check(d2 && std::abs(d2->textHeight() - 0.2) < 1e-9 &&
              std::abs(d2->arrowSize() - 0.16) < 1e-9,
              "cota anotativa: texto E seta re-derivados");
        // Criação via estilo anotativo: altura digitada = MM DE PAPEL.
        ToolController tcA(dA);
        tcA.setAnnotationHeight(2.5);     // 2.5 mm no papel
        tcA.setTextAnnotative(true);
        tcA.addText(Point3{0, 10, 0}, "ANOT");
        bool okNovo = false;
        dA.forEach([&](const Entity& e) {
            if (const auto* m = dynamic_cast<const MText*>(&e))
                if (m->text() == "ANOT")
                    okNovo = m->annotative() && std::abs(m->height() - 0.25) < 1e-9;
        });
        check(okNovo, "texto novo anotativo nasce com 2.5mm/escala (0.25 u)");
    }
    {
        // --- Estados de visibilidade por inserção (bloco "dinâmico" v1) ----
        BlockDefinition dv;
        dv.name = "PORTA";
        auto mA = std::make_unique<Line>(Point3{0, 0, 0}, Point3{1, 0, 0});
        mA->setLayer("ESQ");
        auto mB = std::make_unique<Line>(Point3{0, 0, 0}, Point3{0, 1, 0});
        mB->setLayer("DIR");
        dv.members.push_back(std::move(mA));
        dv.members.push_back(std::move(mB));
        auto ins = BlockRef::fromDefinition(dv, Matrix4::identity());
        RenderBatch b1; ins->emitTo(b1);
        ins->setHiddenLayers({"DIR"});
        RenderBatch b2; ins->emitTo(b2);
        check(b2.lineVertices.size() == b1.lineVertices.size() / 2,
              "visibilidade por insercao: camada oculta some do emit");
        const AABB bbv = ins->boundingBox();
        check(bbv.valid() && bbv.max.y < 0.5, "bbox ignora os membros ocultos");
        auto cl = ins->clone();
        RenderBatch b3; cl->emitTo(b3);
        check(b3.lineVertices.size() == b2.lineVertices.size(),
              "clone preserva o estado de visibilidade");
    }
    {
        // --- Arcos guiados: SER/SEA por CLIQUE de passagem + SED + PORTA ----
        DrawingManager dP(std::make_unique<Quadtree>(world, 12, 8));
        ToolController tcP(dP);
        // SER: antes o 3º CLIQUE travava a ferramenta; agora é ponto de passagem.
        tcP.setTool(ToolKind::ArcSER);
        tcP.onPoint(Point3{0, 0, 0});
        tcP.onPoint(Point3{10, 0, 0});
        tcP.onPoint(Point3{5, 5, 0});      // passagem -> semicírculo r=5
        check(dP.count() == 1, "ArcSER: 3o CLIQUE cria o arco (ponto de passagem)");
        bool serOk = false;
        dP.forEach([&](const Entity& e) {
            if (const auto* a = dynamic_cast<const Arc*>(&e))
                serOk = std::abs(a->radius() - 5.0) < 1e-6 &&
                        std::abs(a->center().x - 5.0) < 1e-6;
        });
        check(serOk, "ArcSER por clique: raio 5 e centro (5,0)");
        // SEA digitado segue funcionando (valor em graus).
        tcP.setTool(ToolKind::ArcSEA);
        tcP.onPoint(Point3{20, 0, 0});
        tcP.onPoint(Point3{30, 0, 0});
        check(tcP.wantsValue(), "ArcSEA com 2 pontos espera o angulo digitado");
        check(tcP.onValue(180.0) && dP.count() == 2, "ArcSEA: 180 graus cria o arco");
        // SED: direção da tangente inicial.
        tcP.setTool(ToolKind::ArcSED);
        tcP.onPoint(Point3{40, 0, 0});
        tcP.onPoint(Point3{50, 0, 0});
        tcP.onPoint(Point3{40, 5, 0});     // tangente inicial p/ cima
        check(dP.count() == 3, "ArcSED: 3 cliques criam o arco");
        // PORTA: dobradiça + vão + lado => folha (linha) + giro (arco 90°).
        tcP.setTool(ToolKind::Door);
        tcP.onPoint(Point3{0, 20, 0});          // dobradiça
        tcP.onPoint(Point3{0.8, 20, 0});        // outro batente (vão 0.8)
        tcP.onPoint(Point3{0.4, 20.5, 0});      // abre para CIMA
        check(dP.count() == 5, "PORTA: cria folha + arco (2 entidades)");
        bool folhaOk = false, giroOk = false;
        dP.forEach([&](const Entity& e) {
            if (const auto* l = dynamic_cast<const Line*>(&e)) {
                if (std::abs(l->start().y - 20.0) < 1e-9 &&
                    std::abs(l->end().x) < 1e-9 && std::abs(l->end().y - 20.8) < 1e-9)
                    folhaOk = true;   // folha da dobradiça (0,20) até (0,20.8)
            } else if (const auto* a = dynamic_cast<const Arc*>(&e)) {
                if (std::abs(a->radius() - 0.8) < 1e-9 &&
                    std::abs(a->center().x) < 1e-9 && std::abs(a->center().y - 20.0) < 1e-9) {
                    double sweep = a->endAngle() - a->startAngle();
                    while (sweep < 0.0) sweep += kTwoPi;
                    giroOk = std::abs(sweep - kPi / 2.0) < 1e-6;   // quarto de volta
                }
            }
        });
        check(folhaOk, "PORTA: folha aberta a 90 graus no lado clicado");
        check(giroOk, "PORTA: arco de giro de 90 graus centrado na dobradica");
        dP.undo();
        check(dP.count() == 3, "PORTA: undo desfaz folha+arco de uma vez (MacroCmd)");
    }
    {
        // --- PAREDE: faces + esquadria + vãos (janela/porta) + ferramentas --
        Wall wr({{0, 0, 0}, {10, 0, 0}}, 0.30);
        const AABB wb = wr.boundingBox();
        check(std::abs(wb.min.y + 0.15) < 1e-9 && std::abs(wb.max.y - 0.15) < 1e-9 &&
              std::abs(wb.max.x - 10.0) < 1e-9, "parede reta: faces a meia-espessura");
        RenderBatch rb1; wr.emitTo(rb1);
        check(rb1.lineVertices.size() == 8, "parede reta: 2 faces + 2 arremates");
        check(rb1.fillVertices.size() == 6, "parede reta: corpo preenchido (2 tris)");
        // janela: corta as faces e desenha 3 linhas + 2 ombreiras
        wr.addOpening({4.0, 1.2, /*janela*/ 2, 1, false});
        RenderBatch rb2; wr.emitTo(rb2);
        check(rb2.lineVertices.size() == 22, "janela: faces cortadas + simbolo de 3 linhas");
        // porta: vira vão com folha + arco embutidos
        Wall wd({{0, 5, 0}, {10, 5, 0}}, 0.15);
        wd.addOpening({2.0, 0.9, /*porta*/ 1, 1, false});
        RenderBatch rb3; wd.emitTo(rb3);
        check(rb3.lineVertices.size() > 40, "porta na parede: folha + arco emitidos");
        // esquadria no canto em L
        Wall wl({{0, 10, 0}, {5, 10, 0}, {5, 15, 0}}, 0.30);
        const AABB lb = wl.boundingBox();
        check(std::abs(lb.max.x - 5.15) < 1e-9 && std::abs(lb.min.y - 9.85) < 1e-9,
              "canto em L: esquadria externa a meia-espessura");
        // hitTest: corpo pega, fora não
        Ray rr; rr.origin = Point3{2.0, 0.1, 0.0};
        check(wr.hitTest(rr, 0.05).hit, "hitTest: dentro do corpo");
        rr.origin = Point3{2.0, 0.5, 0.0};
        check(!wr.hitTest(rr, 0.05).hit, "hitTest: fora do corpo");
        // ferramentas: desenhar parede + porta pelo clique (vão embutido)
        DrawingManager dW(std::make_unique<Quadtree>(world, 12, 8));
        ToolController tcW(dW);
        tcW.setWallThickness(0.2);
        tcW.setTool(ToolKind::WallTool);
        tcW.onPoint(Point3{0, 20, 0});
        tcW.onPoint(Point3{8, 20, 0});
        tcW.finishStroke(false);
        check(dW.count() == 1, "ferramenta PAREDE cria a Wall no Enter");
        EntityId wid = kInvalidId;
        dW.forEach([&](const Entity& e) { if (dynamic_cast<const Wall*>(&e)) wid = e.id(); });
        tcW.setTool(ToolKind::Door);
        tcW.doorClick(wid, Point3{3.0, 20.05, 0});      // dobradiça na parede
        tcW.doorClick(wid, Point3{3.9, 20.02, 0});      // largura
        tcW.doorClick(wid, Point3{3.4, 21.0, 0});       // abre para CIMA
        const auto* wres = dynamic_cast<const Wall*>(dW.getEntity(wid));
        check(wres && wres->openings().size() == 1 &&
              wres->openings()[0].kind == 1 && wres->openings()[0].side == 1 &&
              std::abs(wres->openings()[0].width - 0.9) < 1e-6,
              "PORTA na parede vira Opening (lado certo, largura 0.9)");
        dW.undo();
        const auto* wund = dynamic_cast<const Wall*>(dW.getEntity(wid));
        check(wund && wund->openings().empty(), "undo remove o vão da porta");
        dW.redo();
        tcW.setTool(ToolKind::WindowTool);
        tcW.windowClick(wid, Point3{6.0, 20.03, 0});
        tcW.windowClick(wid, Point3{7.2, 19.98, 0});
        const auto* wwin = dynamic_cast<const Wall*>(dW.getEntity(wid));
        check(wwin && wwin->openings().size() == 2 && wwin->openings()[1].kind == 2,
              "JANELA na parede vira Opening de 3 linhas");
    }
    {
        // --- UCS 2D: transformadas + retângulo alinhado ao frame ------------
        DrawingManager dU(std::make_unique<Quadtree>(world, 12, 8));
        check(!dU.ucsActive(), "UCS nasce inativo (WCS)");
        const double a30 = 30.0 / 57.29577951308232;
        dU.setUcs(Point3{10, 5, 0}, a30);
        check(dU.ucsActive(), "setUcs ativa o frame");
        const Point3 w = dU.ucsToWorld(Point3{1, 0, 0});
        check(std::abs(w.x - (10 + std::cos(a30))) < 1e-9 &&
              std::abs(w.y - (5 + std::sin(a30))) < 1e-9,
              "ucsToWorld: (1,0) vira origem + eixo X girado");
        const Point3 rt = dU.worldToUcs(dU.ucsToWorld(Point3{3.7, -2.1, 0}));
        check(std::abs(rt.x - 3.7) < 1e-9 && std::abs(rt.y + 2.1) < 1e-9,
              "worldToUcs é o inverso exato de ucsToWorld");
        const Point3 d = dU.ucsDirToWorld(Point3{1, 0, 0});
        check(std::abs(d.x - std::cos(a30)) < 1e-9 && std::abs(d.y - std::sin(a30)) < 1e-9,
              "ucsDirToWorld roda o delta sem transladar");
        // Retângulo por 2 cantos digitados no UCS -> polilinha GIRADA no mundo.
        ToolController tcU(dU);
        tcU.setTool(ToolKind::Rectangle);
        tcU.onPoint(dU.ucsToWorld(Point3{0, 0, 0}));
        tcU.onPoint(dU.ucsToWorld(Point3{4, 2, 0}));
        const Polyline* rp = nullptr;
        dU.forEach([&](const Entity& e) {
            if (const auto* p = dynamic_cast<const Polyline*>(&e)) rp = p;
        });
        check(rp && rp->closed() && rp->vertices().size() == 4, "RECT no UCS cria polilinha fechada");
        if (rp) {
            // lado 0->1 deve apontar na direção do eixo X do UCS (30°)
            const Point3 v0 = rp->vertices()[0], v1 = rp->vertices()[1];
            const double ang = std::atan2(v1.y - v0.y, v1.x - v0.x);
            check(std::abs(ang - a30) < 1e-6, "RECT alinhado ao eixo X do UCS (30 graus)");
        }
    }

    // --- OSNAP novos: Node, Extension e Parallel ---------------------------
    {
        DrawingManager dN(std::make_unique<Quadtree>(world, 12, 8));
        dN.addEntity(std::make_unique<PointEntity>(Point3{5, 5, 0}));
        SnapEngine snN;
        const SnapResult rN = snN.resolve(Point3{5.2, 5.1, 0}, 1.0, dN, nullptr);
        check(rN && rN.type == SnapType::Node &&
              std::abs(rN.point.x - 5.0) < 1e-9 && std::abs(rN.point.y - 5.0) < 1e-9,
              "OSNAP Node: entidade PONTO captura como no");

        // Extension: cursor colinear ALEM do extremo da linha gruda no
        // prolongamento (pe da projecao sobre a reta-suporte).
        DrawingManager dE(std::make_unique<Quadtree>(world, 12, 8));
        dE.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        SnapEngine snE;
        const SnapResult rE = snE.resolve(Point3{15.0, 0.3, 0}, 1.0, dE, nullptr,
                                          kDefaultSnaps | snapBit(SnapType::Extension));
        check(rE && rE.type == SnapType::Extension &&
              std::abs(rE.point.x - 15.0) < 1e-9 && std::abs(rE.point.y) < 1e-9,
              "OSNAP Extension: gruda no prolongamento colinear da linha");
        // Sem o bit de Extension ligado, nada acende ali.
        const SnapResult rE0 = snE.resolve(Point3{15.0, 0.3, 0}, 1.0, dE, nullptr,
                                           kDefaultSnaps);
        check(!rE0.hit, "OSNAP Extension: desligado por padrao (opt-in)");

        // Parallel: com base em (0,5), cursor quase-horizontal gruda na reta
        // paralela a linha-referencia que passa pela base.
        SnapEngine snPar;
        const Point3 basePar{0, 5, 0};
        const SnapResult rPar = snPar.resolve(Point3{7.0, 5.3, 0}, 1.0, dE, &basePar,
                                              snapBit(SnapType::Parallel));
        check(rPar && rPar.type == SnapType::Parallel &&
              std::abs(rPar.point.y - 5.0) < 1e-9 && std::abs(rPar.point.x - 7.0) < 1e-6,
              "OSNAP Parallel: gruda na paralela pela base");
        // Endpoint real vence a extensao (classes: ponto > deferred).
        const SnapResult rCls = snE.resolve(Point3{10.4, 0.2, 0}, 1.0, dE, nullptr,
                                            kDefaultSnaps | snapBit(SnapType::Extension));
        check(rCls && rCls.type == SnapType::Endpoint,
              "OSNAP: endpoint real vence a extensao perto do extremo");
    }

    // --- Camadas: renomear / excluir / purge -------------------------------
    {
        DrawingManager dL(std::make_unique<Quadtree>(world, 12, 8));
        Layer piso; piso.name = "PISO"; piso.color = Rgba{0, 255, 0, 255};
        dL.layers().add(piso);
        Layer vazia; vazia.name = "TEMP";
        dL.layers().add(vazia);
        auto ln = std::make_unique<Line>(Point3{0, 0, 0}, Point3{5, 0, 0});
        ln->setLayer("PISO");
        const EntityId el = dL.addEntity(std::move(ln));

        check(dL.layerUsage("PISO") == 1 && dL.layerUsage("TEMP") == 0,
              "layerUsage conta as entidades por camada");
        check(dL.renameLayer("PISO", "PISO_TERREO"), "renameLayer aceita");
        check(!dL.layers().contains("PISO") && dL.layers().contains("PISO_TERREO"),
              "rename atualiza a tabela");
        check(dL.getEntity(el)->layer() == "PISO_TERREO",
              "rename REMAPEIA as entidades da camada");
        check(!dL.renameLayer("0", "ZERO"), "camada 0 nao renomeia");
        check(!dL.removeLayer("PISO_TERREO", false),
              "remover camada EM USO sem mover e recusado");
        check(dL.removeLayer("PISO_TERREO", true) &&
              dL.getEntity(el)->layer() == "0",
              "remover com mover manda as entidades para a 0");
        const auto purged = dL.purgeLayers("");
        check(purged.size() == 1 && purged[0] == "TEMP" && !dL.layers().contains("TEMP"),
              "purge remove so as camadas vazias (nunca a 0)");
    }

    // --- Redefinicao de bloco: inserts existentes atualizam (+undo) --------
    {
        DrawingManager dR(std::make_unique<Quadtree>(world, 12, 8));
        // v1 do bloco: uma linha de 10.
        const EntityId r1 = dR.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        dR.execute(std::make_unique<MakeBlockCmd>(std::vector<EntityId>{r1},
                                                  Point3{0, 0, 0}, std::string("PECA")));
        // Insercao adicional da v1 em (100,0).
        const BlockDefinition* d1 = dR.blocks().find("PECA");
        const EntityId insId = dR.addEntity(
            BlockRef::fromDefinition(*d1, Matrix4::translation(Vec3{100, 0, 0})));
        const AABB bb1 = dR.getEntity(insId)->boundingBox();

        // v2 do bloco: DUAS linhas (forma um L de 10x10) — redefine "PECA".
        const EntityId r2 = dR.addEntity(std::make_unique<Line>(Point3{200, 0, 0}, Point3{210, 0, 0}));
        const EntityId r3 = dR.addEntity(std::make_unique<Line>(Point3{200, 0, 0}, Point3{200, 10, 0}));
        dR.execute(std::make_unique<MakeBlockCmd>(std::vector<EntityId>{r2, r3},
                                                  Point3{200, 0, 0}, std::string("PECA")));
        const BlockDefinition* d2 = dR.blocks().find("PECA");
        check(d2 && d2->members.size() == 2, "redefinicao sobrescreve a definicao");
        const auto* insV2 = dynamic_cast<const BlockRef*>(dR.getEntity(insId));
        check(insV2 && insV2->members().size() == 2,
              "insert EXISTENTE atualiza para a nova definicao");
        const AABB bb2 = dR.getEntity(insId)->boundingBox();
        check(std::abs((bb2.max.y - bb2.min.y) - (bb1.max.y - bb1.min.y)) > 5.0,
              "bbox do insert reflete a geometria nova");
        dR.undo();
        const auto* insV1 = dynamic_cast<const BlockRef*>(dR.getEntity(insId));
        check(insV1 && insV1->members().size() == 1 &&
              dR.blocks().find("PECA")->members.size() == 1,
              "undo restaura definicao antiga E os inserts antigos");
        dR.redo();
        const auto* insV2b = dynamic_cast<const BlockRef*>(dR.getEntity(insId));
        check(insV2b && insV2b->members().size() == 2 &&
              dR.blocks().find("PECA")->members.size() == 2,
              "redo reaplica a redefinicao nos inserts");
    }

    // --- Cotas ASSOCIATIVAS: a cota segue a geometria-fonte -----------------
    {
        DrawingManager dD(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId lid = dD.addEntity(
            std::make_unique<Line>(Point3{0, 0, 0}, Point3{10, 0, 0}));
        Dimension d = Dimension::linear(Point3{0, 0, 0}, Point3{10, 0, 0},
                                        Point3{5, 5, 0}, 2.5);
        d.setAnchors(DimAnchor{lid, DimAnchor::Which::Start, 0},
                     DimAnchor{lid, DimAnchor::Which::End, 0});
        const EntityId did = dD.addEntity(std::make_unique<Dimension>(d));

        // Estica a linha 10 -> 20 por comando: o regen deve atualizar a cota.
        dD.execute(std::make_unique<ReplaceCmd>(
            lid, std::make_unique<Line>(Point3{0, 0, 0}, Point3{20, 0, 0})));
        const auto* dim = dynamic_cast<const Dimension*>(dD.getEntity(did));
        check(dim && std::abs(dim->p2().x - 20.0) < 1e-9,
              "cota associativa segue o esticao da linha (10->20)");
        dD.undo();
        dim = dynamic_cast<const Dimension*>(dD.getEntity(did));
        check(dim && std::abs(dim->p2().x - 10.0) < 1e-9,
              "undo da fonte devolve a cota a medida antiga");

        // Raio: círculo r=5 -> r=8; a cota de raio deve medir 8.
        const EntityId cid = dD.addEntity(std::make_unique<Circle>(Point3{50, 0, 0}, 5.0));
        Dimension dr = Dimension::radius(Point3{50, 0, 0}, Point3{55, 0, 0}, 2.5);
        dr.setAnchors(DimAnchor{cid, DimAnchor::Which::Center, 0},
                      DimAnchor{cid, DimAnchor::Which::OnCurve, 0});
        const EntityId rid = dD.addEntity(std::make_unique<Dimension>(dr));
        dD.execute(std::make_unique<ReplaceCmd>(
            cid, std::make_unique<Circle>(Point3{50, 0, 0}, 8.0)));
        const auto* rdim = dynamic_cast<const Dimension*>(dD.getEntity(rid));
        const double rr = std::hypot(rdim->p2().x - rdim->p1().x,
                                     rdim->p2().y - rdim->p1().y);
        check(rdim && std::abs(rr - 8.0) < 1e-9,
              "cota de raio associativa segue o raio novo do circulo");
    }

    // --- MTEXT caixa de texto: quebra automática por largura ---------------
    {
        MText livre(Point3{0, 0, 0}, "palavra palavra palavra palavra", 2.5);
        const AABB ba = livre.boundingBox();
        MText caixa(Point3{0, 0, 0}, "palavra palavra palavra palavra", 2.5);
        caixa.setBoxWidth(30.0);   // menor que a largura natural: força quebra
        const AABB bc = caixa.boundingBox();
        check(bc.max.x - bc.min.x <= 31.0,
              "caixa de texto: nenhuma linha excede a largura da caixa");
        check((bc.max.y - bc.min.y) > (ba.max.y - ba.min.y) + 1.0,
              "caixa de texto: o conteudo quebra em multiplas linhas");
    }

    // --- Tipos de linha PADRÃO (DOT/DASHDOT/...) ---------------------------
    {
        registerStandardLineTypes();
        check(customLinePattern("DASHDOT") != nullptr &&
              customLinePattern("PHANTOM") != nullptr &&
              customLinePattern("DOT") != nullptr,
              "familia padrao de tipos de linha registrada");
        const auto segs = applyLineTypeByName(
            {Point3{0, 0, 0}, Point3{40, 0, 0}}, "DASHDOT", 1.0);
        check(segs.size() >= 8 && segs.size() % 2 == 0,
              "DASHDOT gera tracos e pontos alternados");
    }

    // --- Seleção avançada: polígono, ALL/Previous/Last e GRUPOS -------------
    {
        DrawingManager dS(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId c1 = dS.addEntity(std::make_unique<Circle>(Point3{5, 5, 0}, 1.0));
        const EntityId l1 = dS.addEntity(
            std::make_unique<Line>(Point3{-5, 5, 0}, Point3{15, 5, 0}));
        ToolController tc(dS);
        const std::vector<Point3> tri{{0, 0, 0}, {10, 0, 0}, {5, 12, 0}};
        tc.selectInPolygon(tri, /*crossing=*/false, false);
        check(tc.selection().size() == 1 && tc.selection()[0] == c1,
              "WPolygon: so o circulo INTEIRO dentro e selecionado");
        tc.selectInPolygon(tri, /*crossing=*/true, false);
        check(tc.selection().size() == 2,
              "CPolygon: circulo dentro + linha que ATRAVESSA");
        check(tc.selectAllVisible() == 2, "selectAllVisible pega as 2 entidades");
        tc.clearSelection();
        check(tc.selectPrevious() == 2, "Previous restaura a ultima selecao");
        check(tc.selectLastCreated() && tc.selection()[0] == l1,
              "Last seleciona a ultima entidade criada");

        dS.addGroup("PAR", {c1, l1});
        tc.selectAt(Point3{5, 6, 0}, 0.3, false);   // clica no circulo
        check(tc.selection().size() == 2,
              "GROUP: clicar num membro seleciona o grupo inteiro");
    }

    // --- DIMTEDIT: override + offset do texto -------------------------------
    {
        Dimension d = Dimension::linear(Point3{0, 0, 0}, Point3{10, 0, 0},
                                        Point3{5, 3, 0}, 0.5);
        RenderBatch b0; d.emitTo(b0);
        d.setTextOverride("VER DETALHE");
        RenderBatch b1; d.emitTo(b1);
        check(b1.lineVertices.size() != b0.lineVertices.size(),
              "override troca o texto renderizado da cota");
        d.setTextOverride("");
        d.setTextOffset(2.0, 1.5);
        const AABB bb = d.boundingBox();
        check(bb.max.y > 4.5, "offset desloca o texto (bbox acompanha em +Y)");
        auto moved = withGripMoved(d, 0, Point3{8.0, 6.0, 0.0});
        const auto* md = dynamic_cast<const Dimension*>(moved.get());
        check(md && std::abs(md->textOffsetX() - 3.0) < 1e-9 &&
              std::abs(md->textOffsetY() - 3.0) < 1e-9,
              "grip do texto vira offset relativo ao ponto-base");
    }

    // --- Hachura ASSOCIATIVA: segue a fonte ---------------------------------
    {
        DrawingManager dH(std::make_unique<Quadtree>(world, 12, 8));
        const EntityId cid = dH.addEntity(std::make_unique<Circle>(Point3{0, 0, 0}, 5.0));
        std::vector<Point3> loop = extractClosedLoop(*dH.getEntity(cid));
        check(loop.size() >= 3, "extractClosedLoop devolve o contorno do circulo");
        auto h = std::make_unique<Hatch>(std::vector<std::vector<Point3>>{loop},
                                         HatchPattern::Solid, 0.0, 1.0, 0.5);
        h->setSrcIds({cid});
        const EntityId hid = dH.addEntity(std::move(h));
        dH.execute(std::make_unique<ReplaceCmd>(
            cid, std::make_unique<Circle>(Point3{0, 0, 0}, 8.0)));
        const auto* hh = dynamic_cast<const Hatch*>(dH.getEntity(hid));
        const AABB hb = hh->boundingBox();
        check(hh && hb.max.x > 7.5 && hb.min.x < -7.5,
              "hachura associativa re-extrai o loop apos o raio mudar");
        dH.undo();
        const auto* h2 = dynamic_cast<const Hatch*>(dH.getEntity(hid));
        const AABB hb2 = h2->boundingBox();
        check(h2 && hb2.max.x < 5.5, "undo da fonte devolve a hachura antiga");
    }

    // --- Leva "primeiro mês": grips, snaps novos, UM/UB, OOPS, cotas -------
    {
        // Grips do ARCO: 4 alças; mover o extremo muda o ângulo.
        Arc arco(Point3{0, 0, 0}, 10.0, 0.0, 1.5707963);
        check(gripsOf(arco).size() == 4, "arco tem 4 grips");
        auto a2 = withGripMoved(arco, 2, Point3{-10, 0, 0});   // extremo final -> 180°
        const auto* a2p = dynamic_cast<const Arc*>(a2.get());
        check(a2p && std::abs(a2p->endAngle() - 3.14159265) < 1e-3,
              "grip do extremo do arco muda o angulo final");

        // Grip da COTA: p3 move a linha de cota.
        Dimension dl = Dimension::linear(Point3{0,0,0}, Point3{10,0,0}, Point3{5,3,0}, 0.2);
        check(gripsOf(dl).size() == 4, "cota tem 4 grips (texto+p1+p2+p3)");
        auto d3 = withGripMoved(dl, 3, Point3{5, 6, 0});
        check(d3 && std::abs(static_cast<const Dimension*>(d3.get())->p3().y - 6.0) < 1e-9,
              "grip p3 move a linha de cota");

        // OSNAP Insertion (bloco) e GeomCenter (polilinha fechada).
        DrawingManager dI(std::make_unique<Quadtree>(world, 12, 8));
        std::vector<Point3> sq = {{0,0,0},{4,0,0},{4,4,0},{0,4,0}};
        dI.addEntity(std::make_unique<Polyline>(sq, true));
        SnapEngine snI;
        const SnapResult rG = snI.resolve(Point3{2.1, 2.1, 0}, 0.5, dI, nullptr,
                                          snapBit(SnapType::GeomCenter));
        check(rG && rG.type == SnapType::GeomCenter &&
              std::abs(rG.point.x - 2.0) < 1e-9, "GeomCenter no centroide do quadrado");

        // Interseção APARENTE: linhas que NÃO se cruzam de fato.
        DrawingManager dA2(std::make_unique<Quadtree>(world, 12, 8));
        dA2.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{4, 0, 0}));
        dA2.addEntity(std::make_unique<Line>(Point3{10, -5, 0}, Point3{10, -1, 0}));
        const SnapResult rAi = snI.resolve(Point3{10.2, 0.2, 0}, 0.6, dA2, nullptr,
                                           snapBit(SnapType::AppInt));
        check(rAi && rAi.type == SnapType::AppInt &&
              std::abs(rAi.point.x - 10.0) < 1e-9 && std::abs(rAi.point.y) < 1e-9,
              "intersecao APARENTE no cruzamento das extensoes");

        // UM/UB: desfaz em lote até a marca. OOPS: restaura o último ERASE.
        DrawingManager dU(std::make_unique<Quadtree>(world, 12, 8));
        dU.execute(std::make_unique<AddEntityCmd>(
            std::make_unique<Line>(Point3{0,0,0}, Point3{1,0,0})));
        dU.undoMark();
        dU.execute(std::make_unique<AddEntityCmd>(
            std::make_unique<Line>(Point3{0,1,0}, Point3{1,1,0})));
        dU.execute(std::make_unique<AddEntityCmd>(
            std::make_unique<Line>(Point3{0,2,0}, Point3{1,2,0})));
        check(dU.count() == 3 && dU.undoBack() == 2 && dU.count() == 1,
              "UB desfaz ate a marca (2 comandos)");
        const EntityId eid = dU.addEntity(
            std::make_unique<Circle>(Point3{5,5,0}, 1.0));
        dU.execute(std::make_unique<EraseCmd>(std::vector<EntityId>{eid}));
        dU.execute(std::make_unique<AddEntityCmd>(
            std::make_unique<Line>(Point3{9,9,0}, Point3{10,9,0})));   // pós-erase
        const std::size_t antes = dU.count();
        check(dU.oops() == 1 && dU.count() == antes + 1,
              "OOPS restaura o apagado sem desfazer o resto");

        // Cota ORDINATE mede a coordenada; tolerância entra no texto (bbox muda).
        Dimension dor = Dimension::ordinate(Point3{3, 2, 0}, Point3{3.2, 6, 0}, 0.2);
        RenderBatch bo; dor.emitTo(bo);
        check(!bo.lineVertices.empty(), "cota ordinate renderiza leader + texto");
        Dimension dtol = Dimension::linear(Point3{0,0,0}, Point3{10,0,0}, Point3{5,2,0}, 0.2);
        RenderBatch b1t; dtol.emitTo(b1t);
        dtol.setTolerance(0.05, 0.05);
        RenderBatch b2t; dtol.emitTo(b2t);
        check(b2t.lineVertices.size() > b1t.lineVertices.size(),
              "tolerancia acrescenta texto na cota");

        // QDIM: 2 linhas -> cadeia de cotas contínuas pelos endpoints.
        DrawingManager dQ(std::make_unique<Quadtree>(world, 12, 8));
        dQ.addEntity(std::make_unique<Line>(Point3{0, 0, 0}, Point3{4, 0, 0}));
        dQ.addEntity(std::make_unique<Line>(Point3{4, 0, 0}, Point3{9, 0, 0}));
        ToolController tq(dQ);
        tq.selectByFilter("LINE", "");
        check(tq.qdim(Point3{4, -3, 0}) == 2, "QDIM cria 2 cotas continuas");
    }

    // --- MALHA HALF-EDGE (F1 do Zendo): build, ops e picking ----------------
    {
        using HEM = HalfEdgeMesh;
        auto makeCube = [](HEM& m) {   // cubo unitário [0,1]³, faces CCW de fora
            const HEM::Idx v[8] = {
                m.addVertex({0, 0, 0}), m.addVertex({1, 0, 0}),
                m.addVertex({1, 1, 0}), m.addVertex({0, 1, 0}),
                m.addVertex({0, 0, 1}), m.addVertex({1, 0, 1}),
                m.addVertex({1, 1, 1}), m.addVertex({0, 1, 1})};
            m.addFace({v[3], v[2], v[1], v[0]});   // base (normal -Z)
            m.addFace({v[4], v[5], v[6], v[7]});   // tampa (+Z)
            m.addFace({v[0], v[1], v[5], v[4]});   // frente (-Y)
            m.addFace({v[2], v[3], v[7], v[6]});   // trás (+Y)
            m.addFace({v[1], v[2], v[6], v[5]});   // direita (+X)
            m.addFace({v[3], v[0], v[4], v[7]});   // esquerda (-X)
        };

        HEM cube;
        makeCube(cube);
        std::string why;
        check(cube.vertexCount() == 8 && cube.faceCount() == 6 &&
              cube.halfEdgeCount() == 24,
              "half-edge: cubo tem 8v/6f/24he (12 arestas gemeas)");
        check(cube.checkIntegrity(&why), "half-edge: integridade do cubo");
        const Vec3 nTop = cube.faceNormal(1);
        check(std::abs(nTop.z - 1.0) < 1e-9 &&
              std::abs(cube.faceArea(1) - 1.0) < 1e-9,
              "half-edge: normal +Z e area 1 na tampa");
        std::vector<Point3> lines;
        cube.edgeLines(lines);
        check(lines.size() == 24, "edgeLines emite cada aresta UMA vez (12)");

        // pick de cima: acerta a TAMPA no ponto exato
        const auto hit = cube.pickRay({0.3, 0.4, 5.0}, {0.0, 0.0, -1.0});
        check(hit.hit && hit.face == 1 && std::abs(hit.point.z - 1.0) < 1e-9,
              "pickRay de cima acerta a tampa (Moller-Trumbore)");

        // splitEdge na aresta da tampa: vertice novo, ciclos 4->5, integro
        HEM m1; makeCube(m1);
        const HEM::Idx heTop = m1.face(1).he;
        const HEM::Idx w = m1.splitEdge(heTop, {0.5, 0.0, 1.0});
        check(w != HEM::kNone && m1.vertexCount() == 9 &&
              m1.faceVertices(1).size() == 5 && m1.checkIntegrity(&why),
              "splitEdge insere vertice e mantem a malha integra");

        // splitFace na tampa (diagonal): 7 faces, 2 ciclos, integro
        HEM m2; makeCube(m2);
        const auto tv = m2.faceVertices(1);
        const HEM::Idx g = m2.splitFace(1, tv[0], tv[2]);
        check(g != HEM::kNone && m2.faceCount() == 7 &&
              m2.faceVertices(1).size() == 3 && m2.faceVertices(g).size() == 3 &&
              m2.checkIntegrity(&why),
              "splitFace divide a tampa em 2 triangulos integros");
        check(m2.splitFace(1, tv[0], tv[1]) == HEM::kNone,
              "splitFace recusa vertices ja adjacentes");

        // extrudeFace: EMPURRAR/PUXAR — tampa sobe, nascem 4 quads
        HEM m3; makeCube(m3);
        const HEM::Idx cap = m3.extrudeFace(1, 0.5);
        check(cap == 1 && m3.vertexCount() == 12 && m3.faceCount() == 10 &&
              m3.checkIntegrity(&why),
              "extrudeFace cria os 4 quads laterais e preserva o id da tampa");
        check(std::abs(m3.faceCentroid(cap).z - 1.5) < 1e-9 &&
              std::abs(m3.faceArea(cap) - 1.0) < 1e-9,
              "tampa extrudada sobe 0.5 mantendo a area");
        const auto hit2 = m3.pickRay({0.5, 0.5, 5.0}, {0.0, 0.0, -1.0});
        check(hit2.hit && hit2.face == cap &&
              std::abs(hit2.point.z - 1.5) < 1e-9,
              "pick apos extrude acerta a tampa na altura nova");
        // empurrar de volta (negativo) devolve a altura original
        m3.extrudeFace(cap, -0.5);
        check(std::abs(m3.faceCentroid(cap).z - 1.0) < 1e-9 &&
              m3.checkIntegrity(&why),
              "extrude negativo (EMPURRAR) desce a tampa integra");

        // insetFace: DESENHAR NA FACE — retangulo vira face plugavel (keyhole)
        HEM m4; makeCube(m4);
        const HEM::Idx inner = m4.insetFace(
            1, {{0.25, 0.25, 1}, {0.75, 0.25, 1}, {0.75, 0.75, 1}, {0.25, 0.75, 1}});
        check(inner != HEM::kNone && m4.faceCount() == 7 &&
              m4.vertexCount() == 12 && m4.checkIntegrity(&why),
              "insetFace cria a face interna integra (anel keyhole)");
        check(std::abs(m4.faceArea(inner) - 0.25) < 1e-9 &&
              std::abs(m4.faceArea(1) - 0.75) < 1e-9,
              "areas: retangulo 0.25 e anel 0.75 (ponte se cancela)");
        std::vector<Point3> l2;
        m4.edgeLines(l2);
        check(l2.size() == 32, "ponte do keyhole nao vira aresta desenhada");
        std::vector<Point3> tf;
        m4.triangulateFace(1, tf);
        double aT = 0.0;
        for (std::size_t i = 0; i * 3 < tf.size(); ++i)
            aT += 0.5 * (tf[i * 3 + 1] - tf[i * 3]).cross(tf[i * 3 + 2] - tf[i * 3]).length();
        check(std::abs(aT - 0.75) < 1e-6,
              "ear-clipping cobre o anel keyhole por inteiro");
        const HEM::Idx capI = m4.extrudeFace(inner, 0.4);
        check(capI == inner && m4.checkIntegrity(&why),
              "push/pull da face DESENHADA e integro");
        const auto hitI = m4.pickRay({0.5, 0.5, 5.0}, {0.0, 0.0, -1.0});
        check(hitI.hit && hitI.face == inner &&
              std::abs(hitI.point.z - 1.4) < 1e-9,
              "pick acerta o volume que brotou do retangulo (z=1.4)");
        const auto hitR = m4.pickRay({0.1, 0.1, 5.0}, {0.0, 0.0, -1.0});
        check(hitR.hit && hitR.face == 1 &&
              std::abs(hitR.point.z - 1.0) < 1e-9,
              "pick fora do retangulo segue no anel (z=1.0)");

        // LINHA divisora: splitEdge nas duas arestas + splitFace atravessando
        HEM m5; makeCube(m5);
        const auto tvs = m5.faceVertices(1);        // tampa: 4,5,6,7
        // aresta v4->v5 (y=0) e aresta v6->v7 (y=1): pontos médios
        HEM::Idx heA5 = HEM::kNone, heB5 = HEM::kNone;
        for (const HEM::Idx h : m5.faceHalfEdges(1)) {
            if (m5.halfEdge(h).origin == tvs[0]) heA5 = h;
            if (m5.halfEdge(h).origin == tvs[2]) heB5 = h;
        }
        const HEM::Idx wA = m5.splitEdge(heA5, {0.5, 0.0, 1.0});
        const HEM::Idx wB = m5.splitEdge(heB5, {0.5, 1.0, 1.0});
        const HEM::Idx gL = m5.splitFace(1, wA, wB);
        check(gL != HEM::kNone && m5.faceCount() == 7 &&
              m5.checkIntegrity(&why),
              "LINHA na face: splitEdge x2 + splitFace dividem a tampa");
        check(std::abs(m5.faceArea(1) - 0.5) < 1e-9 &&
              std::abs(m5.faceArea(gL) - 0.5) < 1e-9,
              "as duas metades medem 0.5 cada");
        check(m5.extrudeFace(gL, 0.3) == gL && m5.checkIntegrity(&why),
              "meia-tampa dividida pela linha extruda integra");

        // ROUND-TRIP (base do .zendo): despeja verts+faces e reconstroi
        std::vector<std::vector<HEM::Idx>> loops;
        for (HEM::Idx f = 0; f < HEM::Idx(m4.faceCount()); ++f)
            loops.push_back(m4.faceVertices(f));
        HEM m6;
        for (HEM::Idx v = 0; v < HEM::Idx(m4.vertexCount()); ++v)
            m6.addVertex(m4.vertex(v).p);
        bool allOk = true;
        for (const auto& lp : loops) allOk &= (m6.addFace(lp) != HEM::kNone);
        check(allOk && m6.faceCount() == m4.faceCount() &&
              m6.halfEdgeCount() == m4.halfEdgeCount() &&
              m6.checkIntegrity(&why),
              "round-trip por sopa de poligonos reconstroi ate o keyhole");
        const auto hit6 = m6.pickRay({0.5, 0.5, 5.0}, {0.0, 0.0, -1.0});
        check(hit6.hit && std::abs(hit6.point.z - 1.4) < 1e-9,
              "malha reconstruida responde ao pick igual a original");

        // DISSOLVE: apagar a linha desenhada funde as duas metades de volta
        HEM::Idx heDiag = HEM::kNone;   // a diagonal criada pelo splitFace em m5
        for (HEM::Idx h = 0; h < HEM::Idx(m5.halfEdgeCount()); ++h) {
            const auto& e = m5.halfEdge(h);
            if ((e.origin == wA && m5.halfEdge(e.next).origin == wB) ||
                (e.origin == wB && m5.halfEdge(e.next).origin == wA)) {
                heDiag = h;
                break;
            }
        }
        // m5 foi extrudado depois do split — refaz o cenario limpo p/ dissolver
        HEM m7; makeCube(m7);
        HEM::Idx a7 = HEM::kNone, b7 = HEM::kNone;
        const auto tv7 = m7.faceVertices(1);
        for (const HEM::Idx h : m7.faceHalfEdges(1)) {
            if (m7.halfEdge(h).origin == tv7[0]) a7 = h;
            if (m7.halfEdge(h).origin == tv7[2]) b7 = h;
        }
        const HEM::Idx wA7 = m7.splitEdge(a7, {0.5, 0.0, 1.0});
        const HEM::Idx wB7 = m7.splitEdge(b7, {0.5, 1.0, 1.0});
        m7.splitFace(1, wA7, wB7);
        HEM::Idx diag = HEM::kNone;
        for (HEM::Idx h = 0; h < HEM::Idx(m7.halfEdgeCount()); ++h)
            if (m7.halfEdge(h).origin == wA7 &&
                m7.halfEdge(m7.halfEdge(h).next).origin == wB7) { diag = h; break; }
        HEM::Idx gone = HEM::kNone;
        std::string why7;
        check(diag != HEM::kNone && m7.dissolveEdge(diag, &gone) &&
              m7.faceCount() == 6 && m7.checkIntegrity(&why7) &&
              std::abs(m7.faceArea(1) - 1.0) < 1e-9,
              "dissolveEdge apaga a linha e funde as metades (area volta a 1)");
        (void)heDiag;
        HEM m8; makeCube(m8);
        m8.translate({10.0, 0.0, 0.0});
        check(std::abs(m8.faceCentroid(1).x - 10.5) < 1e-9 &&
              m8.checkIntegrity(&why7),
              "translate desloca o solido inteiro");

        // weldUnion: dois cubos EMPILHADOS (faces iguais) viram um solido
        HEM ca; makeCube(ca);
        HEM cb; makeCube(cb);
        cb.translate({0.0, 0.0, 1.0});
        HEM uu;
        check(HEM::weldUnion(ca, cb, uu) && uu.vertexCount() == 12 &&
              uu.faceCount() == 10 && uu.checkIntegrity(&why7),
              "weldUnion: cubos empilhados fundem (12v/10f, integro)");
        const auto hitU = uu.pickRay({0.5, 0.5, 5.0}, {0.0, 0.0, -1.0});
        check(hitU.hit && std::abs(hitU.point.z - 2.0) < 1e-9,
              "uniao responde ao pick no topo novo (z=2)");

        // weldUnion: caixa PEQUENA sobre a face grande (contida) tambem funde
        HEM big; makeCube(big);
        HEM small;
        {
            const HEM::Idx s0 = small.addVertex({0.25, 0.25, 1.0});
            const HEM::Idx s1 = small.addVertex({0.75, 0.25, 1.0});
            const HEM::Idx s2 = small.addVertex({0.75, 0.75, 1.0});
            const HEM::Idx s3 = small.addVertex({0.25, 0.75, 1.0});
            const HEM::Idx t0 = small.addVertex({0.25, 0.25, 1.5});
            const HEM::Idx t1 = small.addVertex({0.75, 0.25, 1.5});
            const HEM::Idx t2 = small.addVertex({0.75, 0.75, 1.5});
            const HEM::Idx t3 = small.addVertex({0.25, 0.75, 1.5});
            small.addFace({s3, s2, s1, s0});   // base (-Z): a interface
            small.addFace({t0, t1, t2, t3});   // topo (+Z)
            small.addFace({s0, s1, t1, t0});
            small.addFace({s2, s3, t3, t2});
            small.addFace({s1, s2, t2, t1});
            small.addFace({s3, s0, t0, t3});
        }
        HEM u2;
        check(HEM::weldUnion(big, small, u2) && u2.checkIntegrity(&why7) &&
              u2.faceCount() == 11,
              "weldUnion: caixa contida na face funde via inset (11f)");
        const auto hitS = u2.pickRay({0.5, 0.5, 5.0}, {0.0, 0.0, -1.0});
        check(hitS.hit && std::abs(hitS.point.z - 1.5) < 1e-9,
              "pick na uniao acerta o topo da caixinha (z=1.5)");

        // weldUnion FLUSH (M2c): meia-caixa que COMPARTILHA bordas com o topo
        HEM big2; makeCube(big2);
        HEM half;
        {
            const HEM::Idx s0 = half.addVertex({0.0, 0.0, 1.0});
            const HEM::Idx s1 = half.addVertex({1.0, 0.0, 1.0});
            const HEM::Idx s2 = half.addVertex({1.0, 0.5, 1.0});
            const HEM::Idx s3 = half.addVertex({0.0, 0.5, 1.0});
            const HEM::Idx t0 = half.addVertex({0.0, 0.0, 1.5});
            const HEM::Idx t1 = half.addVertex({1.0, 0.0, 1.5});
            const HEM::Idx t2 = half.addVertex({1.0, 0.5, 1.5});
            const HEM::Idx t3 = half.addVertex({0.0, 0.5, 1.5});
            half.addFace({s3, s2, s1, s0});
            half.addFace({t0, t1, t2, t3});
            half.addFace({s0, s1, t1, t0});
            half.addFace({s2, s3, t3, t2});
            half.addFace({s1, s2, t2, t1});
            half.addFace({s3, s0, t0, t3});
        }
        HEM u3;
        check(HEM::weldUnion(big2, half, u3) && u3.checkIntegrity(&why7) &&
              u3.faceCount() == 11,
              "weldUnion FLUSH: corta o topo e funde a meia-caixa (11f)");
        const auto hitF = u3.pickRay({0.5, 0.25, 5.0}, {0.0, 0.0, -1.0});
        const auto hitG = u3.pickRay({0.5, 0.75, 5.0}, {0.0, 0.0, -1.0});
        check(hitF.hit && std::abs(hitF.point.z - 1.5) < 1e-9 &&
              hitG.hit && std::abs(hitG.point.z - 1.0) < 1e-9,
              "uniao flush: degrau correto (z=1.5 na metade, z=1.0 no resto)");

        // rotateAxis (R8): girar 90° em torno de X leva +Y para +Z
        {
            HEM rx;
            const HEM::Idx a = rx.addVertex({0, 0, 0});
            const HEM::Idx b = rx.addVertex({1, 0, 0});
            const HEM::Idx c = rx.addVertex({1, 1, 0});
            const HEM::Idx d = rx.addVertex({0, 1, 0});
            rx.addFace({a, b, c, d});
            rx.rotateAxis({0, 0, 0}, {1, 0, 0}, 3.14159265358979 / 2.0);
            check(std::abs(rx.vertex(c).p.y) < 1e-9 &&
                  std::abs(rx.vertex(c).p.z - 1.0) < 1e-9,
                  "rotateAxis (R8): 90 graus em X leva +Y para +Z");
        }

        // moveVertex (G2): sticky move — posição muda, topologia fica intacta
        HEM mv;
        {
            const HEM::Idx a = mv.addVertex({0, 0, 0});
            const HEM::Idx b = mv.addVertex({1, 0, 0});
            const HEM::Idx c = mv.addVertex({1, 1, 0});
            const HEM::Idx d = mv.addVertex({0, 1, 0});
            mv.addFace({a, b, c, d});
            mv.moveVertex(c, {1.4, 1.3, 0.0});
            std::string whyMv;
            check(std::abs(mv.vertex(c).p.x - 1.4) < 1e-12 &&
                  std::abs(mv.vertex(c).p.y - 1.3) < 1e-12 &&
                  mv.checkIntegrity(&whyMv) && mv.faceCount() == 1,
                  "moveVertex (G2): vertice move, topologia integra");
        }
    }

    std::cout << "=== "
              << (g_failures == 0 ? "TODOS OS TESTES PASSARAM" : "FALHAS DETECTADAS")
              << " (" << g_failures << " falhas) ===\n";
    return g_failures == 0 ? 0 : 1;
}
