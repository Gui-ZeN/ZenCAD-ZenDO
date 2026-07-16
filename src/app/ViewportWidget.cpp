// src/app/ViewportWidget.cpp
#include "app/ViewportWidget.hpp"

#include <QMatrix4x4>
#include <QVector4D>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QEnterEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QInputDialog>
#include <QDialog>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QMenu>
#include <QString>
#include <vector>
#include <map>
#include <unordered_map>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/geometry/Wipeout.hpp"
#include "core/snap/Polar.hpp"
#include "core/edit/SegmentOps.hpp"
#include "app/CommandTable.hpp"
#include "core/geometry/MText.hpp"
#include "core/geometry/BlockRef.hpp"
#include "core/layout/Layout.hpp"
#include "core/geometry/Dimension.hpp"
#include "core/geometry/Hatch.hpp"
#include "core/geometry/Ellipse.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/Spline.hpp"
#include "core/geometry/PointEntity.hpp"
#include "core/geometry/Spline.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/XLine.hpp"
#include "core/geometry/Leader.hpp"
#include "core/geometry/MLeader.hpp"
#include "core/geometry/Hatch.hpp"
#include "core/edit/ReviseCloud.hpp"
#include "core/document/Layer.hpp"
#include "core/geometry/LinePattern.hpp"
#include "core/edit/ConstructOps.hpp"
#include "core/edit/GripOps.hpp"
#include "core/edit/InquiryOps.hpp"
#include "core/command/commands/GripEditCmd.hpp"
#include "core/command/commands/ReplaceCmd.hpp"
#include "core/math/AABB.hpp"
#include "app/GridRenderer.hpp"
#include "app/CursorHud.hpp"

namespace cad {

namespace {
constexpr char kVertSrc[] = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }
)";

constexpr char kFragSrc[] = R"(#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main() { fragColor = uColor; }
)";

// Ferramentas que SELECIONAM UM objeto (cursor de "pick": quadrado destacado,
// mira curta) — à la AutoCAD. As demais usam a mira longa de desenho.
inline bool isPickTool(ToolKind t) {
    switch (t) {
        case ToolKind::Trim:      case ToolKind::Extend:    case ToolKind::Fillet:
        case ToolKind::Chamfer:   case ToolKind::Offset:    case ToolKind::Divide:
        case ToolKind::Measure:   case ToolKind::CircleTTR: case ToolKind::CircleTTT:
        case ToolKind::BreakTool: case ToolKind::JoinTool:  case ToolKind::Lengthen:
        case ToolKind::MatchProps:case ToolKind::ArrayPath: case ToolKind::Hatch:
            return true;
        default: return false;
    }
}

// Recorta o segmento a->b contra a caixa (Liang-Barsky 2D). Se houver trecho
// dentro, devolve true e escreve em 'mid' o ponto médio do trecho recortado —
// ponto garantidamente sobre a entidade e dentro da janela de aparo do Trim.
bool clippedMidpointInBox(const Point3& a, const Point3& b, const AABB& box, Point3& mid) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    double t0 = 0.0, t1 = 1.0;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.x - box.min.x, box.max.x - a.x,
                         a.y - box.min.y, box.max.y - a.y};
    for (int i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-12) {
            if (q[i] < 0.0) return false;          // paralelo e fora
        } else {
            const double r = q[i] / p[i];
            if (p[i] < 0.0) { if (r > t1) return false; if (r > t0) t0 = r; }
            else            { if (r < t0) return false; if (r < t1) t1 = r; }
        }
    }
    const double tm = (t0 + t1) * 0.5;
    mid = Point3{a.x + dx * tm, a.y + dy * tm, 0.0};
    return true;
}
} // namespace

ViewportWidget::ViewportWidget(DrawingManager* doc, QWidget* parent)
    : QOpenGLWidget(parent), m_doc(doc), m_tools(*doc) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);  // para receber Esc/texto
    setCursor(Qt::BlankCursor);       // esconde a seta do SO; só a mira (crosshair) aparece

    // Rótulo flutuante de entrada dinâmica (estilo AutoCAD).
    m_hud = new QLabel(this);
    m_hud->setStyleSheet(
        "QLabel { background: rgba(20,20,24,205); color: rgb(255,230,150);"
        " padding: 2px 6px; border: 1px solid rgba(255,230,150,110); border-radius: 3px; }");
    m_hud->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hud->hide();

    // Lista de autocomplete de comandos (aparece ao digitar perto do cursor).
    m_cmdList = new QListWidget(this);
    m_cmdList->setFocusPolicy(Qt::NoFocus);
    m_cmdList->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_cmdList->setStyleSheet(
        "QListWidget{background:#202327; color:#e4ded2; border:1px solid #c2a063;"
        " border-radius:4px; font-family:'Consolas','Courier New',monospace; font-size:12px;}"
        "QListWidget::item{padding:1px 10px;}"
        "QListWidget::item:selected{background:#c2a063; color:#191b1e;}");
    m_cmdList->hide();
}

ViewportWidget::~ViewportWidget() {
    makeCurrent();
    m_vbo.destroy();
    m_vao.destroy();
    m_previewVbo.destroy();
    m_previewVao.destroy();
    doneCurrent();
}

void ViewportWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.043f, 0.051f, 0.063f, 1.0f);   // #0b0d10 (Aurora)

    m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
    m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc);
    m_prog.link();

    // VAO/VBO da geometria cometida.
    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_vbo.bind();
    m_prog.bind();
    m_prog.enableAttributeArray(0);
    m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 2, 2 * sizeof(float));
    m_prog.release();
    m_vbo.release();
    m_vao.release();

    // VAO/VBO do preview (rubber-band).
    m_previewVao.create();
    m_previewVao.bind();
    m_previewVbo.create();
    m_previewVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_previewVbo.bind();
    m_prog.bind();
    m_prog.enableAttributeArray(0);
    m_prog.setAttributeBuffer(0, GL_FLOAT, 0, 2, 2 * sizeof(float));
    m_prog.release();
    m_previewVbo.release();
    m_previewVao.release();

    m_glReady = true;
    uploadFromDoc();
}

void ViewportWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    m_cam.setViewport(width(), height());
    if (m_haveData && !m_fitted) {
        m_cam.fit(m_bbMinX, m_bbMinY, m_bbMaxX, m_bbMaxY);
        m_fitted = true;
        if (m_baseScale <= 0.0) m_baseScale = m_cam.scale();
    }
}

void ViewportWidget::paintGL() {
    if (m_paperMode && m_paperLayout) { paintPaper(); return; }
    glClearColor(float(m_canvas.bg.redF()), float(m_canvas.bg.greenF()),
                 float(m_canvas.bg.blueF()), 1.0f);     // fundo do tema (Sumi/Washi)
    glClear(GL_COLOR_BUFFER_BIT);

    double minx, miny, maxx, maxy;
    m_cam.visibleRect(minx, miny, maxx, maxy);
    QMatrix4x4 mvp;
    mvp.ortho(float(minx - m_originX), float(maxx - m_originX),
              float(miny - m_originY), float(maxy - m_originY), -1.0f, 1.0f);

    m_prog.bind();
    m_prog.setUniformValue("uMVP", mvp);

    // Grade adaptativa (atrás de tudo), em cinza escuro. Com UCS ativo a grade
    // é gerada no FRAME DO UCS e girada pro mundo (feedback visual do frame).
    double gridSpacing = 0.0;   // tamanho da célula (mundo) — usado pela mira curta
    if (m_gridOn) {
        std::vector<float> grid;
        if (m_doc && m_doc->ucsActive()) {
            const Point3 c0 = m_doc->worldToUcs(Point3{minx, miny, 0.0});
            const Point3 c1 = m_doc->worldToUcs(Point3{maxx, miny, 0.0});
            const Point3 c2 = m_doc->worldToUcs(Point3{maxx, maxy, 0.0});
            const Point3 c3 = m_doc->worldToUcs(Point3{minx, maxy, 0.0});
            const double ux0 = std::min({c0.x, c1.x, c2.x, c3.x});
            const double uy0 = std::min({c0.y, c1.y, c2.y, c3.y});
            const double ux1 = std::max({c0.x, c1.x, c2.x, c3.x});
            const double uy1 = std::max({c0.y, c1.y, c2.y, c3.y});
            grid = buildGrid(ux0, uy0, ux1, uy1, 0.0, 0.0, m_cam.scale(), gridSpacing);
            for (std::size_t i = 0; i + 1 < grid.size(); i += 2) {
                const Point3 w = m_doc->ucsToWorld(Point3{grid[i], grid[i + 1], 0.0});
                grid[i]     = float(w.x - m_originX);
                grid[i + 1] = float(w.y - m_originY);
            }
        } else {
            grid = buildGrid(minx, miny, maxx, maxy,
                             m_originX, m_originY, m_cam.scale(), gridSpacing);
        }
        if (!grid.empty())
            drawDynamic(grid, QVector4D(float(m_canvas.gridMinor.redF()), float(m_canvas.gridMinor.greenF()),
                                        float(m_canvas.gridMinor.blueF()), 1.0f));   // grade do tema
    }
    m_gridSpacing = gridSpacing;   // passo corrente da grade (p/ o grid snap)

    // Áreas preenchidas (ex.: polilinhas com largura), por faixa de cor.
    for (const auto& f : m_fills)
        drawFilled(f.verts, f.color);

    // Geometria cometida, por faixas de cor (ByLayer). Faixas FINAS via GL_LINES;
    // faixas com lineweight (> ~1.5px) são expandidas em quads (triângulos) para a
    // espessura ser confiável em qualquer driver (glLineWidth é limitado no core).
    if (m_vertexCount > 0) {
        m_vao.bind();
        for (const ColorRun& run : m_colorRuns) {
            const QVector4D col(run.r, run.g, run.b, run.a);   // alpha = transparência
            if (run.width <= 1.5f) {
                m_prog.setUniformValue("uColor", col);
                glDrawArrays(GL_LINES, run.start, run.count);
            }
        }
        m_vao.release();

        // Faixas grossas: triângulos em coordenadas de mundo (largura em px / escala).
        // CACHEADAS entre frames: a expansão só depende da geometria e da ESCALA,
        // então é refeita apenas em rebuild ou mudança de zoom (não a cada paint).
        if (m_thickCacheScale != m_cam.scale()) {
            m_thickCache.clear();
            for (const ColorRun& run : m_colorRuns) {
                if (run.width <= 1.5f) continue;
                const float hw = float(run.width * 0.5 / m_cam.scale());
                std::vector<float> tri;
                tri.reserve(run.count * 6);
                for (int s = run.start; s + 1 < run.start + run.count; s += 2) {
                    const float ax = m_cpuVerts[s * 2],       ay = m_cpuVerts[s * 2 + 1];
                    const float bx = m_cpuVerts[(s + 1) * 2], by = m_cpuVerts[(s + 1) * 2 + 1];
                    const float dx = bx - ax, dy = by - ay;
                    const float l = std::hypot(dx, dy);
                    if (l < 1e-6f) continue;
                    const float nx = -dy / l * hw, ny = dx / l * hw;
                    tri.insert(tri.end(), {ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny,
                                           ax + nx, ay + ny, bx - nx, by - ny, ax - nx, ay - ny});
                }
                if (!tri.empty())
                    m_thickCache.emplace_back(QVector4D(run.r, run.g, run.b, run.a), std::move(tri));
            }
            m_thickCacheScale = m_cam.scale();
        }
        for (const auto& tc : m_thickCache) drawFilled(tc.second, tc.first);
    }

    // Máscaras Wipeout: cor de fundo, POR CIMA das linhas (escondem o que está atrás).
    for (const std::vector<float>& m : m_maskFills)
        drawFilled(m, QVector4D(float(m_canvas.bg.redF()), float(m_canvas.bg.greenF()),
                                float(m_canvas.bg.blueF()), 1.0f));   // máscara = cor de fundo do tema

    // Seleção (cor de acento do tema), por cima do desenho. O destaque é
    // CACHEADO entre frames: só re-emite quando o CONJUNTO selecionado muda
    // ou após rebuild — antes era O(geometria selecionada) a cada mouse-move.
    const QVector4D selColor(float(m_canvas.selection.redF()), float(m_canvas.selection.greenF()),
                             float(m_canvas.selection.blueF()), 1.0f);
    const std::vector<EntityId>& sel = m_tools.selection();
    if (!m_selCacheValid || sel != m_selCacheIds) {
        m_selCacheIds = sel;
        m_selCacheLines.clear();
        m_selCacheFills.clear();
        if (!sel.empty()) {
            RenderBatch sb;
            for (const EntityId id : sel)
                if (const Entity* e = m_doc->getEntity(id)) e->emitTo(sb);
            m_selCacheLines.reserve(sb.lineVertices.size() * 2);
            for (const Point3& v : sb.lineVertices) {
                m_selCacheLines.push_back(float(v.x - m_originX));
                m_selCacheLines.push_back(float(v.y - m_originY));
            }
            m_selCacheFills.reserve(sb.fillVertices.size() * 2);
            for (const Point3& v : sb.fillVertices) {
                m_selCacheFills.push_back(float(v.x - m_originX));
                m_selCacheFills.push_back(float(v.y - m_originY));
            }
        }
        m_selCacheValid = true;
    }
    if (!m_selCacheLines.empty())
        drawDynamic(m_selCacheLines, selColor);   // seleção na cor de acento do tema
    if (!m_selCacheFills.empty())                 // polilinha com largura selecionada
        drawFilled(m_selCacheFills, QVector4D(selColor.x(), selColor.y(), selColor.z(), 0.5f));

    // Grips (alças âmbar) da entidade selecionada, no modo Selecionar.
    if (!m_grips.empty() && m_tools.tool() == ToolKind::None) {
        // Ao arrastar um grip, mostra a geometria resultante (amarelo).
        if (m_gripDrag >= 0 && m_gripEntity != kInvalidId) {
            if (const Entity* e = m_doc->getEntity(m_gripEntity))
                if (auto moved = withGripMoved(*e, m_gripDrag, Point3{m_curX, m_curY, 0.0})) {
                    RenderBatch gb; moved->emitTo(gb);
                    std::vector<float> gv;
                    gv.reserve(gb.lineVertices.size() * 2);
                    for (const Point3& v : gb.lineVertices) {
                        gv.push_back(float(v.x - m_originX));
                        gv.push_back(float(v.y - m_originY));
                    }
                    drawDynamic(gv, QVector4D(1.0f, 0.85f, 0.2f, 1.0f));
                }
        }
        const double h = 4.0 / m_cam.scale();   // ~4 px no mundo
        std::vector<float> gsq;
        for (int i = 0; i < static_cast<int>(m_grips.size()); ++i) {
            const Point3 g = (i == m_gripDrag) ? Point3{m_curX, m_curY, 0.0} : m_grips[i];
            const float x = float(g.x - m_originX), y = float(g.y - m_originY);
            const float x0 = x - float(h), x1 = x + float(h);
            const float y0 = y - float(h), y1 = y + float(h);
            gsq.insert(gsq.end(), {x0, y0, x1, y0,  x1, y0, x1, y1,
                                   x1, y1, x0, y1,  x0, y1, x0, y0});
        }
        drawDynamic(gsq, selColor);   // grips na cor de acento do tema
    }

    // Preview da ferramenta ativa (amarelo).
    RenderBatch preview;
    m_tools.buildPreview(Point3{m_curX, m_curY, 0.0}, preview);
    if (!preview.lineVertices.empty()) {
        std::vector<float> pv;
        pv.reserve(preview.lineVertices.size() * 2);
        for (const Point3& v : preview.lineVertices) {
            pv.push_back(float(v.x - m_originX));
            pv.push_back(float(v.y - m_originY));
        }
        drawDynamic(pv, QVector4D(1.0f, 0.85f, 0.2f, 1.0f));
    }

    // Glifo de snap por TIPO (estilo AutoCAD): quadrado/triângulo/círculo/X/losango.
    if (m_snapResult.hit) {
        const float h = float(6.0 / m_cam.scale());   // ~6 px no mundo
        const std::vector<float> mk = buildSnapMarker(
            m_snapResult.type,
            float(m_snapResult.point.x - m_originX),
            float(m_snapResult.point.y - m_originY), h);
        drawDynamic(mk, QVector4D(0.0f, 0.83f, 0.67f, 1.0f));   // snap verde-água #00d4aa (Aurora)
    }

    // Cursor por AÇÃO (estilo AutoCAD):
    //   • Pick de objeto (Trim, Fillet, Offset…): mira CURTA + quadrado destacado.
    //   • Seleção (modo Selecionar / fase Selecting): mira longa + quadradinho.
    //   • Colocação de ponto (desenho): só a mira longa, sem quadrado.
    if (m_mouseInside) {
        const ToolKind tk = m_tools.tool();
        const bool picking   = isPickTool(tk);
        const bool selecting = !picking && m_tools.selectingObjects();
        const float cx = float(m_curX - m_originX), cy = float(m_curY - m_originY);

        const double arm = picking ? (11.0 / m_cam.scale())
                                   : (gridSpacing > 0.0 ? gridSpacing : 28.0 / m_cam.scale());
        std::vector<float> ch = buildCrosshair(cx, cy, arm);
        if (m_doc && m_doc->ucsActive()) {   // mira gira junto com o UCS
            const double a = m_doc->ucsAngleRad();
            const float ca = float(std::cos(a)), sa = float(std::sin(a));
            for (std::size_t i = 0; i + 1 < ch.size(); i += 2) {
                const float dx = ch[i] - cx, dy = ch[i + 1] - cy;
                ch[i]     = cx + dx * ca - dy * sa;
                ch[i + 1] = cy + dx * sa + dy * ca;
            }
        }
        drawDynamic(ch, QVector4D(float(m_canvas.crosshair.redF()), float(m_canvas.crosshair.greenF()),
                                  float(m_canvas.crosshair.blueF()), 1.0f));

        if (picking || selecting) {
            const float pb = float((picking ? 8.0 : 5.0) / m_cam.scale());
            const std::vector<float> box = {
                cx - pb, cy - pb, cx + pb, cy - pb,   cx + pb, cy - pb, cx + pb, cy + pb,
                cx + pb, cy + pb, cx - pb, cy + pb,   cx - pb, cy + pb, cx - pb, cy - pb,
            };
            drawDynamic(box, QVector4D(float(m_canvas.selection.redF()), float(m_canvas.selection.greenF()),
                                       float(m_canvas.selection.blueF()), 1.0f));
        }
    }

    // Pontos ADQUIRIDOS do rastreamento (à la AutoCAD): cruzinha "+" discreta
    // em cada referência memorizada — o usuário vê de onde a distância/guia sai.
    if (!m_trackPts.empty() && !m_tools.selectingObjects()) {
        const float th = float(5.0 / m_cam.scale());
        std::vector<float> tp;
        tp.reserve(m_trackPts.size() * 8);
        for (const Point3& p : m_trackPts) {
            const float x = float(p.x - m_originX), y = float(p.y - m_originY);
            tp.insert(tp.end(), {x - th, y, x + th, y,  x, y - th, x, y + th});
        }
        drawDynamic(tp, QVector4D(1.0f, 0.62f, 0.25f, 1.0f));   // laranja discreto
    }

    // Guias de rastreamento (OTRACK): linhas tracejadas verdes até o ref alinhado.
    if (!m_trackGuides.empty()) {
        const double s = 2.0 / m_cam.scale();
        const std::vector<Point3> dashed = applyLinePattern(m_trackGuides, LineStyle::Dashed, s);
        std::vector<float> g;
        g.reserve(dashed.size() * 2);
        for (const Point3& v : dashed) {
            g.push_back(float(v.x - m_originX));
            g.push_back(float(v.y - m_originY));
        }
        drawDynamic(g, QVector4D(0.35f, 1.0f, 0.55f, 1.0f));
    }

    // Linha-cerca (FENCE) em construção: pontos fixados + segmento até o cursor.
    if (m_fenceMode && !m_fencePts.empty()) {
        std::vector<Point3> path = m_fencePts;
        path.push_back(Point3{m_curX, m_curY, 0.0});       // borracha até o cursor
        std::vector<float> f;
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            f.push_back(float(path[i].x     - m_originX)); f.push_back(float(path[i].y     - m_originY));
            f.push_back(float(path[i + 1].x - m_originX)); f.push_back(float(path[i + 1].y - m_originY));
        }
        if (!f.empty()) drawDynamic(f, QVector4D(1.0f, 0.45f, 0.2f, 1.0f));   // laranja
    }

    // AREA por pontos: contorno fechado (com borracha até o cursor) + preenchimento leve.
    if (m_tools.tool() == ToolKind::Area && !m_areaPts.empty()) {
        const QVector4D col(float(m_canvas.selection.redF()), float(m_canvas.selection.greenF()),
                            float(m_canvas.selection.blueF()), 1.0f);
        std::vector<Point3> poly = m_areaPts;
        poly.push_back(Point3{m_curX, m_curY, 0.0});          // cursor = próximo vértice
        std::vector<float> f;
        for (std::size_t i = 0; i < poly.size(); ++i) {       // arestas + fechamento
            const Point3& a = poly[i];
            const Point3& b = poly[(i + 1) % poly.size()];
            f.push_back(float(a.x - m_originX)); f.push_back(float(a.y - m_originY));
            f.push_back(float(b.x - m_originX)); f.push_back(float(b.y - m_originY));
        }
        std::vector<float> tri;                                // leque de preenchimento
        for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
            tri.push_back(float(poly[0].x - m_originX));     tri.push_back(float(poly[0].y - m_originY));
            tri.push_back(float(poly[i].x - m_originX));     tri.push_back(float(poly[i].y - m_originY));
            tri.push_back(float(poly[i + 1].x - m_originX)); tri.push_back(float(poly[i + 1].y - m_originY));
        }
        if (!tri.empty()) drawFilled(tri, QVector4D(col.x(), col.y(), col.z(), 0.12f));
        drawDynamic(f, col);
    }

    // Polígono de seleção (WP/CP): arestas fixadas + borracha até o cursor +
    // fechamento provisório de volta ao 1º vértice.
    if (m_selPolyActive && !m_selPolyPts.empty()) {
        std::vector<Point3> path = m_selPolyPts;
        path.push_back(Point3{m_curX, m_curY, 0.0});
        std::vector<float> f;
        for (std::size_t i = 0; i < path.size(); ++i) {
            const Point3& a = path[i];
            const Point3& b = path[(i + 1) % path.size()];
            f.push_back(float(a.x - m_originX)); f.push_back(float(a.y - m_originY));
            f.push_back(float(b.x - m_originX)); f.push_back(float(b.y - m_originY));
        }
        drawDynamic(f, m_selPolyCrossing ? QVector4D(0.30f, 1.0f, 0.5f, 1.0f)
                                         : QVector4D(0.45f, 0.65f, 1.0f, 1.0f));
    }

    // Caixa de TEXTO em arrasto (ferramenta Texto): retângulo latão da largura.
    if (m_textDragActive) {
        const double h = m_tools.annotationHeight() * 1.4 * 3.0;   // ~3 linhas de altura
        const float x0 = float(std::min(m_textDragP1.x, m_curX) - m_originX);
        const float x1 = float(std::max(m_textDragP1.x, m_curX) - m_originX);
        const float y1 = float(std::max(m_textDragP1.y, m_curY) - m_originY);
        const float y0 = float(std::max(m_textDragP1.y, m_curY) - h - m_originY);
        drawDynamic({x0, y0, x1, y0,  x1, y0, x1, y1,  x1, y1, x0, y1,  x0, y1, x0, y0},
                    QVector4D(0.76f, 0.63f, 0.39f, 1.0f));
    }

    // MTEXT selecionado: moldura de CAIXA DE TEXTO (bbox + faixa da largura).
    if (m_tools.tool() == ToolKind::None) {
        const EntityId selId = selectedId();
        if (selId != kInvalidId)
            if (const auto* mt = dynamic_cast<const MText*>(m_doc->getEntity(selId))) {
                AABB tb = mt->boundingBox();
                if (mt->boxWidth() > 0.0) {   // a caixa lógica manda na largura
                    tb.expand(Point3{mt->position().x, mt->position().y, 0.0});
                    tb.expand(Point3{mt->position().x + mt->boxWidth(),
                                     mt->position().y + mt->height(), 0.0});
                }
                const double pad = 2.0 / std::max(m_cam.scale(), 1e-9);
                const float x0 = float(tb.min.x - pad - m_originX);
                const float x1 = float(tb.max.x + pad - m_originX);
                const float y0 = float(tb.min.y - pad - m_originY);
                const float y1 = float(tb.max.y + pad - m_originY);
                drawDynamic({x0, y0, x1, y0,  x1, y0, x1, y1,
                             x1, y1, x0, y1,  x0, y1, x0, y0},
                            QVector4D(0.76f, 0.63f, 0.39f, 0.85f));
            }
    }

    // Caixa de seleção em arrasto: Window (azul sólida) / Crossing (verde tracejada).
    if (m_boxActive) {
        const bool crossing = (m_boxCurWX < m_boxStartWX);
        const float x0 = float(std::min(m_boxStartWX, m_boxCurWX) - m_originX);
        const float x1 = float(std::max(m_boxStartWX, m_boxCurWX) - m_originX);
        const float y0 = float(std::min(m_boxStartWY, m_boxCurWY) - m_originY);
        const float y1 = float(std::max(m_boxStartWY, m_boxCurWY) - m_originY);

        const std::vector<float> fill = {x0, y0, x1, y0, x1, y1,  x0, y0, x1, y1, x0, y1};
        drawFilled(fill, crossing ? QVector4D(0.20f, 1.0f, 0.45f, 0.13f)
                                  : QVector4D(0.25f, 0.5f, 1.0f, 0.13f));

        const QVector4D edgeC = crossing ? QVector4D(0.30f, 1.0f, 0.5f, 1.0f)
                                         : QVector4D(0.45f, 0.65f, 1.0f, 1.0f);
        if (crossing) {  // borda tracejada
            const double dash = 8.0 / m_cam.scale();
            std::vector<float> dl;
            auto edge = [&](double ax, double ay, double bx, double by) {
                const double dx = bx - ax, dy = by - ay, len = std::hypot(dx, dy);
                if (len < 1e-9) return;
                const double ux = dx / len, uy = dy / len;
                for (double t = 0; t < len; t += 2 * dash) {
                    const double t2 = std::min(t + dash, len);
                    dl.push_back(float(ax + ux * t));  dl.push_back(float(ay + uy * t));
                    dl.push_back(float(ax + ux * t2)); dl.push_back(float(ay + uy * t2));
                }
            };
            edge(x0, y0, x1, y0); edge(x1, y0, x1, y1);
            edge(x1, y1, x0, y1); edge(x0, y1, x0, y0);
            drawDynamic(dl, edgeC);
        } else {         // borda sólida
            const std::vector<float> outline = {x0, y0, x1, y0,  x1, y0, x1, y1,
                                                x1, y1, x0, y1,  x0, y1, x0, y0};
            drawDynamic(outline, edgeC);
        }
    }

    m_prog.release();

    // Rótulo de entrada dinâmica: mostra o texto DIGITADO (se houver) ou as
    // dimensões do segmento base->cursor durante um desenho/edição.
    Point3 ref;
    const bool typing = !m_dynInput.isEmpty();
    if (typing || (m_mouseInside && m_tools.referencePoint(ref))) {
        const double sx = (m_curX - minx) / (maxx - minx) * width();
        const double sy = (maxy - m_curY) / (maxy - miny) * height();
        QString hud;
        if (typing && m_dynInput.startsWith('@') && m_dynInput.contains('<')) {
            const int lt = m_dynInput.indexOf('<');
            const QString dist = m_dynInput.mid(1, lt - 1);
            const QString ang  = m_dynInput.mid(lt + 1);
            hud = QStringLiteral("dist %1  ∠ %2°").arg(dist, ang.isEmpty() ? QStringLiteral("_") : ang);
        } else if (typing) {
            hud = QStringLiteral("» ") + m_dynInput;
        } else {
            hud = dynamicInputText(ref, Point3{m_curX, m_curY, 0.0});
        }
        m_hud->setText(hud);
        m_hud->adjustSize();
        m_hud->move(static_cast<int>(sx) + 16, static_cast<int>(sy) - m_hud->height() - 8);
        m_hud->show();
        m_hud->raise();
    } else if (m_hud) {
        m_hud->hide();
    }
}

// Sobe `verts` para o VBO dinâmico REUSANDO a alocação quando couber
// (glBufferSubData via write()); só re-aloca ao crescer. Evita dezenas de
// re-alocações de buffer por frame nos overlays (preview/seleção/grips/etc.).
void ViewportWidget::uploadDynamic(const std::vector<float>& verts) {
    const int bytes = static_cast<int>(verts.size() * sizeof(float));
    m_previewVbo.bind();
    if (bytes <= m_dynVboCapacity) {
        m_previewVbo.write(0, verts.data(), bytes);
    } else {
        const int cap = std::max(bytes, m_dynVboCapacity * 2);   // cresce geométrico
        m_previewVbo.allocate(cap);
        m_previewVbo.write(0, verts.data(), bytes);
        m_dynVboCapacity = cap;
    }
    m_previewVbo.release();
}

void ViewportWidget::drawFilled(const std::vector<float>& verts, const QVector4D& color) {
    if (verts.empty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_previewVao.bind();
    uploadDynamic(verts);
    m_prog.setUniformValue("uColor", color);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(verts.size() / 2));
    m_previewVao.release();
    glDisable(GL_BLEND);
}

void ViewportWidget::drawDynamic(const std::vector<float>& verts, const QVector4D& color) {
    if (verts.empty()) return;
    m_previewVao.bind();
    uploadDynamic(verts);
    m_prog.setUniformValue("uColor", color);
    glDrawArrays(GL_LINES, 0, static_cast<int>(verts.size() / 2));
    m_previewVao.release();
}

double ViewportWidget::snapTolWorld() const {
    return 12.0 / m_cam.scale();   // raio de atração ~12 px
}

// ---------------------------------------------------------------------------
// Paper Space (modo papel): folha em mm 1:1, moldura, selo e viewports.
// ---------------------------------------------------------------------------
void ViewportWidget::setPaperMode(bool on, Layout* layout) {
    if (on) {
        if (!m_paperMode) { m_modelCamSaved = m_cam.state(); m_modelCamValid = true; }
        m_paperMode = true;
        m_paperLayout = layout;
        fitPaper();
    } else {
        m_paperMode = false;
        m_paperLayout = nullptr;
        if (m_modelCamValid) { m_cam.setState(m_modelCamSaved); m_modelCamValid = false; }
    }
    m_paperVpSel = -1; m_paperVpGrip = -1; m_paperVpDragging = false;
    m_mspaceIdx = -1;
    update();
}

void ViewportWidget::fitPaper() {
    if (!m_paperLayout) return;
    m_cam.setViewport(width(), height());
    // Folha em [0,0]-[W,H] mm; fit já adiciona ~10% de folga (a "mesa").
    m_cam.fit(0.0, 0.0, m_paperLayout->widthMm(), m_paperLayout->heightMm());
}

void ViewportWidget::beginPaperViewport() {
    if (!m_paperMode) return;
    m_paperVpCreate = true;
    m_paperVpHasP1 = false;
    emit prompt("Viewport: clique o 1º canto na prancha (Esc cancela).");
    update();
}

void ViewportWidget::cancelPaperViewport() {
    if (!m_paperVpCreate) return;
    m_paperVpCreate = false;
    m_paperVpHasP1 = false;
    emit prompt("Criação de viewport cancelada.");
    update();
}

void ViewportWidget::paintPaper() {
    const Layout& L = *m_paperLayout;
    const double W = L.widthMm(), H = L.heightMm();

    // Fundo "mesa" (mais escuro/neutro que o papel).
    const QColor desk = (m_theme == ThemeMode::Dark) ? QColor(20, 22, 26) : QColor(150, 150, 154);
    glClearColor(float(desk.redF()), float(desk.greenF()), float(desk.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // MVP papel(mm) -> tela.
    double minx, miny, maxx, maxy;
    m_cam.visibleRect(minx, miny, maxx, maxy);
    QMatrix4x4 mvp;
    mvp.ortho(float(minx), float(maxx), float(miny), float(maxy), -1.0f, 1.0f);
    m_prog.bind();
    m_prog.setUniformValue("uMVP", mvp);

    auto rectTris = [](double x0, double y0, double x1, double y1) {
        return std::vector<float>{ float(x0),float(y0), float(x1),float(y0), float(x1),float(y1),
                                   float(x0),float(y0), float(x1),float(y1), float(x0),float(y1) };
    };
    auto rectLines = [](double x0, double y0, double x1, double y1) {
        return std::vector<float>{ float(x0),float(y0), float(x1),float(y0),
                                   float(x1),float(y0), float(x1),float(y1),
                                   float(x1),float(y1), float(x0),float(y1),
                                   float(x0),float(y1), float(x0),float(y0) };
    };
    const QVector4D ink(0.12f, 0.12f, 0.13f, 1.0f);        // traço escuro sobre papel claro
    const QVector4D frameCol(0.20f, 0.45f, 0.72f, 1.0f);   // moldura do viewport (azul discreto)
    auto putText = [&](double px, double py, const std::string& s, double h, const QVector4D& col) {
        if (s.empty()) return;
        MText t(Point3{px, py, 0.0}, s, h);
        RenderBatch tb; t.emitTo(tb);
        std::vector<float> tv; tv.reserve(tb.lineVertices.size() * 2);
        for (const Point3& v : tb.lineVertices) { tv.push_back(float(v.x)); tv.push_back(float(v.y)); }
        drawDynamic(tv, col);
    };

    // Sombra da folha + papel branco.
    const double sh = std::max(W, H) * 0.012;
    drawFilled(rectTris(sh, -sh, W + sh, H - sh), QVector4D(0.0f, 0.0f, 0.0f, 0.30f));
    const QColor paper = (m_theme == ThemeMode::Dark) ? QColor(232, 230, 223) : QColor(252, 252, 250);
    drawFilled(rectTris(0, 0, W, H),
               QVector4D(float(paper.redF()), float(paper.greenF()), float(paper.blueF()), 1.0f));

    // Borda da folha + moldura de margem.
    const double m = L.marginMm;
    drawDynamic(rectLines(0, 0, W, H), ink);
    drawDynamic(rectLines(m, m, W - m, H - m), ink);

    // --- Viewports: Modelo clipado, reusando o VBO do Modelo com MVP próprio ---
    const double vw = std::max(maxx - minx, 1e-9), vh = std::max(maxy - miny, 1e-9);
    const double dpr = devicePixelRatioF();
    const int fbW = int(width() * dpr), fbH = int(height() * dpr);
    for (const SheetViewport& vp : L.viewports) {
        const bool haveModel = (m_vertexCount > 0) || !m_fills.empty();
        if (haveModel) {
            // Modelo->papel->NDC. Vértice do VBO = (modelo - m_origin).
            const double k  = vp.mmPerUnit;
            const double tx = vp.cxMm() + (m_originX - vp.modelCx) * k;
            const double ty = vp.cyMm() + (m_originY - vp.modelCy) * k;
            QMatrix4x4 vpMvp = mvp;
            vpMvp.translate(float(tx), float(ty));
            vpMvp.scale(float(k), float(k));

            const int sx = int((vp.xMm - minx) / vw * fbW);
            const int sy = int((vp.yMm - miny) / vh * fbH);
            const int sw = int(vp.wMm / vw * fbW);
            const int shp = int(vp.hMm / vh * fbH);
            glEnable(GL_SCISSOR_TEST);
            glScissor(sx, sy, sw, shp);
            m_prog.setUniformValue("uMVP", vpMvp);
            // "Plot style" do papel (igual ao remap do PDF): cores CLARAS —
            // brancas na tela escura — viram tinta sobre o papel, senão as
            // paredes brancas somem na folha branca.
            const QVector4D inkVp(0.12f, 0.12f, 0.13f, 1.0f);
            auto paperColor = [&](float r, float g, float b, float a) {
                return (r > 0.78f && g > 0.78f && b > 0.78f)
                     ? QVector4D(inkVp.x(), inkVp.y(), inkVp.z(), a)
                     : QVector4D(r, g, b, a);
            };
            for (const auto& f : m_fills)
                if (!f.annot)
                    drawFilled(f.verts, paperColor(f.color.x(), f.color.y(),
                                                   f.color.z(), f.color.w()));
            if (m_vertexCount > 0) {
                m_vao.bind();
                for (const ColorRun& run : m_colorRuns) {
                    if (run.annot) continue;   // anotativas: re-emitidas abaixo
                    // VP-FREEZE: camada congelada SÓ neste viewport é pulada.
                    if (run.layerIdx >= 0 &&
                        run.layerIdx < int(m_runLayers.size()) &&
                        vp.layerFrozenHere(m_runLayers[std::size_t(run.layerIdx)]))
                        continue;
                    m_prog.setUniformValue("uColor",
                                           paperColor(run.r, run.g, run.b, run.a));
                    glDrawArrays(GL_LINES, run.start, run.count);
                }
                m_vao.release();
            }
            // ANOTATIVAS: re-emitidas POR VIEWPORT com a altura corrigida
            // (mm de papel constantes: fator = escala de anotação / escala
            // do viewport). Poucas entidades — custo desprezível.
            if (m_doc) {
                const double fA = m_doc->annoMmPerUnit() / std::max(k, 1e-12);
                const LayerTable& lt = m_doc->layers();
                m_doc->forEach([&](const Entity& e) {
                    bool annot = false;
                    if (const auto* amt = dynamic_cast<const MText*>(&e)) annot = amt->annotative();
                    else if (const auto* adm = dynamic_cast<const Dimension*>(&e)) annot = adm->annotative();
                    if (!annot) return;
                    const Layer* lay = lt.find(e.layer());
                    if (lay && (!lay->on || lay->frozen)) return;
                    if (vp.layerFrozenHere(e.layer())) return;
                    auto cl = e.clone();
                    if (auto* mt = dynamic_cast<MText*>(cl.get())) {
                        mt->setHeight(mt->height() * fA);
                    } else if (auto* dm = dynamic_cast<Dimension*>(cl.get())) {
                        dm->setTextHeight(dm->textHeight() * fA);
                        if (dm->arrowSize() > 0.0) dm->setArrowSize(dm->arrowSize() * fA);
                    }
                    RenderBatch ab; cl->emitTo(ab);
                    const Rgba c = e.resolveColor(lt);
                    const QVector4D col = paperColor(c.r / 255.0f, c.g / 255.0f,
                                                     c.b / 255.0f, 1.0f);
                    std::vector<float> lv; lv.reserve(ab.lineVertices.size() * 2);
                    for (const Point3& v : ab.lineVertices) {
                        lv.push_back(float(v.x - m_originX));
                        lv.push_back(float(v.y - m_originY));
                    }
                    drawDynamic(lv, col);
                    if (!ab.fillVertices.empty()) {
                        std::vector<float> fv; fv.reserve(ab.fillVertices.size() * 2);
                        for (const Point3& v : ab.fillVertices) {
                            fv.push_back(float(v.x - m_originX));
                            fv.push_back(float(v.y - m_originY));
                        }
                        drawFilled(fv, col);
                    }
                });
            }
            glDisable(GL_SCISSOR_TEST);
            m_prog.setUniformValue("uMVP", mvp);   // volta ao espaço-papel
        }
        // Moldura do viewport + rótulo de escala "1:N".
        drawDynamic(rectLines(vp.xMm, vp.yMm, vp.xMm + vp.wMm, vp.yMm + vp.hMm), frameCol);
        if (vp.scaleDenom > 0.0)
            putText(vp.xMm + 1.5, vp.yMm + 1.5, formatScale(vp.scaleDenom), 3.0, frameCol);
    }

    // --- Destaques: viewport SELECIONADO (latão + grips) e MSPACE (moldura dupla) ---
    if (m_paperVpSel >= 0 && m_paperVpSel < int(L.viewports.size())) {
        const SheetViewport& v = L.viewports[std::size_t(m_paperVpSel)];
        const QVector4D brass(0.76f, 0.63f, 0.39f, 1.0f);
        drawDynamic(rectLines(v.xMm, v.yMm, v.xMm + v.wMm, v.yMm + v.hMm), brass);
        const double g = 4.0 / std::max(m_cam.scale(), 1e-9);   // grip ~4px em mm
        const double gx[4] = {v.xMm, v.xMm + v.wMm, v.xMm + v.wMm, v.xMm};
        const double gy[4] = {v.yMm, v.yMm, v.yMm + v.hMm, v.yMm + v.hMm};
        for (int i = 0; i < 4; ++i)
            drawFilled(rectTris(gx[i] - g, gy[i] - g, gx[i] + g, gy[i] + g), brass);
    }
    if (m_mspaceIdx >= 0 && m_mspaceIdx < int(L.viewports.size())) {
        const SheetViewport& v = L.viewports[std::size_t(m_mspaceIdx)];
        const QVector4D act(0.95f, 0.55f, 0.20f, 1.0f);   // laranja = vista ATIVA
        drawDynamic(rectLines(v.xMm, v.yMm, v.xMm + v.wMm, v.yMm + v.hMm), act);
        const double o = 0.8;                              // moldura dupla (grossa)
        drawDynamic(rectLines(v.xMm - o, v.yMm - o, v.xMm + v.wMm + o, v.yMm + v.hMm + o), act);
    }

    // --- Selo/carimbo paramétrico (canto inferior-direito, dentro da margem) ---
    {
        const double stW = std::min(185.0, (W - 2 * m) * 0.42);
        const double stH = 32.0;
        const double x1 = W - m, x0 = x1 - stW, y0 = m, y1 = y0 + stH;
        const double colX = x0 + stW * 0.26;
        const double rowH = stH / 4.0;
        drawDynamic(rectLines(x0, y0, x1, y1), ink);
        for (int i = 1; i < 4; ++i) {
            const double yy = y0 + rowH * i;
            drawDynamic(std::vector<float>{ float(x0), float(yy), float(x1), float(yy) }, ink);
        }
        drawDynamic(std::vector<float>{ float(colX), float(y0), float(colX), float(y1) }, ink);
        struct Row { const char* label; std::string value; double vh; };
        const Row rows[4] = {
            { "TITULO",  L.title,      3.0 },   // topo
            { "PROJETO", L.project,    2.4 },
            { "AUTOR",   L.author,     2.4 },
            { "ESCALA",  L.scaleLabel, 2.4 },   // base
        };
        for (int i = 0; i < 4; ++i) {
            const double rBottom = y1 - rowH * (i + 1);
            putText(x0 + 1.5,   rBottom + (rowH - 2.0) * 0.5, rows[i].label, 2.0, ink);
            putText(colX + 2.0, rBottom + (rowH - rows[i].vh) * 0.5, rows[i].value, rows[i].vh, ink);
        }
        // Data no canto direito da linha de ESCALA (base).
        putText(x0 + stW * 0.62, y0 + (rowH - 2.4) * 0.5, L.date, 2.4, ink);
    }

    // Preview do viewport em criação (rubber-band amarelo).
    if (m_paperVpCreate && m_paperVpHasP1) {
        drawDynamic(rectLines(m_paperVpP1.x, m_paperVpP1.y, m_curX, m_curY),
                    QVector4D(1.0f, 0.85f, 0.2f, 1.0f));
    }
}

void ViewportWidget::uploadFromDoc() {
    // Agrupa a geometria por cor+espessura resolvidas (ByLayer), pulando ocultas.
    // Buckets em unordered_map (hot path); a ordem estável das faixas é dada por
    // um sort das chaves ao montar os runs (nº de cores distintas é pequeno).
    std::unordered_map<std::uint64_t, std::vector<Point3>> buckets;   // chave = (cor<<8)|espessura
    std::unordered_map<std::uint64_t, std::vector<Point3>> fillBuckets; // triângulos por cor (+bit anotativo)
    std::map<int, std::vector<Point3>> maskBuckets;             // máscaras (Wipeout), pós-linhas

    // Regen INCREMENTAL: o documento anota os ids alterados; invalidamos só
    // essas entradas do cache de tesselação. Cor/tipo de linha/visibilidade
    // são resolvidos abaixo POR REBUILD (fora do cache) — mudanças de camada/
    // tema continuam corretas sem invalidar nada.
    if (m_doc) {
        if (m_doc->fullDirty()) m_emitCache.clear();
        else for (const EntityId id : m_doc->dirtyIds()) m_emitCache.erase(id);
        m_doc->clearDirty();
    }

    AABB bb;
    m_runLayers.clear();   // reconstruído junto com os buckets (VP-freeze)
    if (m_doc) {
        const LayerTable& layers = m_doc->layers();
        m_doc->forEach([&](const Entity& e) {
            const Layer* lay = layers.find(e.layer());
            if (lay && (!lay->on || lay->frozen)) return;   // camada desligada/congelada
            // Tesselação CACHEADA por entidade (a parte cara: arcos/splines/texto).
            auto cit = m_emitCache.find(e.id());
            if (cit == m_emitCache.end()) {
                RenderBatch fresh;
                e.emitTo(fresh);
                cit = m_emitCache.emplace(e.id(), std::move(fresh)).first;
            }
            const RenderBatch& b = cit->second;
            for (const Point3& v : b.lineVertices) bb.expand(v);  // bbox da geometria cheia

            // Tipo de linha: ByLayer -> o da camada; resolve embutido OU custom.
            std::string ltn = e.lineType().name;
            if (ltn == "ByLayer" || ltn.empty())
                ltn = lay ? lay->lineType.name : std::string("CONTINUOUS");
            const std::vector<Point3> dashed =
                applyLineTypeByName(b.lineVertices, ltn, 1.5 * m_ltScale);   // LTSCALE global
            const std::vector<Point3>* src = &dashed;   // custom/embutido; contínuo volta igual

            Rgba c = e.resolveColor(layers);
            if (m_theme == ThemeMode::Light && c.r > 200 && c.g > 200 && c.b > 200) {
                c.r = std::uint8_t(m_canvas.defaultInk.red());     // branco -> tinta no papel
                c.g = std::uint8_t(m_canvas.defaultInk.green());
                c.b = std::uint8_t(m_canvas.defaultInk.blue());
            }
            if (lay && lay->locked) {       // camada travada: esmaece em direção ao FUNDO do tema
                const int bg = (m_canvas.bg.red() + m_canvas.bg.green() + m_canvas.bg.blue()) / 3;
                auto fade = [bg](std::uint8_t v) {
                    return std::uint8_t(bg + (int(v) - bg) * 35 / 100); };
                c.r = fade(c.r); c.g = fade(c.g); c.b = fade(c.b);
            }
            const std::uint32_t ckey = (std::uint32_t(c.r) << 16) |
                                       (std::uint32_t(c.g) << 8) | c.b;
            // Espessura efetiva (mm; ByLayer): -> px de exibição -> quantum 0..255.
            double effMm = e.lineWeight().mm;
            if (effMm < 0.0 && lay) effMm = lay->lineWeight.mm;
            const double px = effMm <= 0.0 ? 1.0 : std::min(12.0, std::max(1.0, effMm * 4.0));
            const std::uint32_t wq = std::min<std::uint32_t>(255,
                                       std::max<std::uint32_t>(1, std::uint32_t(px * 2.0 + 0.5)));
            // Alpha da TRANSPARÊNCIA da camada (0..90% -> 255..25).
            const int tpc = lay ? std::clamp(lay->transparency, 0, 90) : 0;
            const std::uint32_t aq = std::uint32_t(255 * (100 - tpc) / 100);
            // Índice da CAMADA (VP-freeze pula runs por camada nos viewports).
            const std::string& ln = e.layer();
            std::uint64_t li = 0;
            {
                auto it = std::find(m_runLayers.begin(), m_runLayers.end(), ln);
                if (it == m_runLayers.end()) {
                    m_runLayers.push_back(ln);
                    li = m_runLayers.size() - 1;
                } else {
                    li = std::uint64_t(it - m_runLayers.begin());
                }
            }
            // ANOTATIVA: bucket separado — o papel pula esses runs e re-emite
            // a entidade POR VIEWPORT com a altura corrigida (mm constantes).
            bool annot = false;
            if (const auto* amt = dynamic_cast<const MText*>(&e)) annot = amt->annotative();
            else if (const auto* adm = dynamic_cast<const Dimension*>(&e)) annot = adm->annotative();
            const std::uint64_t key = (annot ? (1ull << 62) : 0) |
                                      (li << 40) | (std::uint64_t(ckey) << 16) |
                                      (std::uint64_t(aq) << 8) | wq;
            std::vector<Point3>& vec = buckets[key];
            for (const Point3& v : *src) vec.push_back(v);

            if (!b.fillVertices.empty()) {                 // áreas preenchidas
                if (dynamic_cast<const Wipeout*>(&e)) {
                    // WIPEOUT: preenche com a cor de FUNDO, mas num bucket de MÁSCARA
                    // desenhado DEPOIS das linhas (esconde de verdade o que está atrás).
                    std::vector<Point3>& fv = maskBuckets[0];
                    for (const Point3& v : b.fillVertices) { fv.push_back(v); bb.expand(v); }
                } else if (const auto* ht = dynamic_cast<const Hatch*>(&e);
                           ht && ht->pattern() == HatchPattern::Gradient) {
                    // GRADIENT: cor por triângulo, interpolada ao longo de Y do bbox
                    // (quantizada em ~12 níveis p/ agrupar os buckets de cor).
                    const Rgba c1 = ht->gradientColor1(), c2 = ht->gradientColor2();
                    const AABB hbx = ht->boundingBox();
                    const double y0 = hbx.min.y, dy = std::max(1e-9, hbx.max.y - hbx.min.y);
                    const auto& fvs = b.fillVertices;
                    for (std::size_t i = 0; i + 2 < fvs.size(); i += 3) {
                        const double cy = (fvs[i].y + fvs[i + 1].y + fvs[i + 2].y) / 3.0;
                        double t = std::min(1.0, std::max(0.0, (cy - y0) / dy));
                        t = std::round(t * 12.0) / 12.0;
                        auto lp = [&](std::uint8_t a, std::uint8_t z) {
                            return std::uint8_t(int(a) + int(int(z) - int(a)) * t); };
                        const std::uint32_t gk = (std::uint32_t(lp(c1.r, c2.r)) << 16) |
                                                 (std::uint32_t(lp(c1.g, c2.g)) << 8) | lp(c1.b, c2.b);
                        std::vector<Point3>& fv = fillBuckets[gk];
                        fv.push_back(fvs[i]); fv.push_back(fvs[i + 1]); fv.push_back(fvs[i + 2]);
                        bb.expand(fvs[i]); bb.expand(fvs[i + 1]); bb.expand(fvs[i + 2]);
                    }
                } else {
                    std::vector<Point3>& fv =
                        fillBuckets[std::uint64_t(ckey) | (annot ? (1ull << 32) : 0)];
                    for (const Point3& v : b.fillVertices) { fv.push_back(v); bb.expand(v); }
                }
            }
        });
    }

    if (bb.valid()) {
        m_originX = (bb.min.x + bb.max.x) * 0.5;
        m_originY = (bb.min.y + bb.max.y) * 0.5;
        m_bbMinX = bb.min.x; m_bbMinY = bb.min.y;
        m_bbMaxX = bb.max.x; m_bbMaxY = bb.max.y;
        m_haveData = true;
    } else {
        m_originX = m_originY = 0.0;
    }

    m_cpuVerts.clear();
    m_colorRuns.clear();
    // Chaves ordenadas p/ ordem de desenho ESTÁVEL entre rebuilds (o
    // unordered_map itera em ordem arbitrária; sem isso o z-order piscaria).
    std::vector<std::uint64_t> lineKeys;
    lineKeys.reserve(buckets.size());
    for (const auto& kv : buckets) lineKeys.push_back(kv.first);
    std::sort(lineKeys.begin(), lineKeys.end());
    for (const std::uint64_t key : lineKeys) {
        const std::vector<Point3>& pts = buckets[key];
        const std::uint32_t ck = std::uint32_t((key >> 16) & 0xFFFFFF);
        ColorRun run;
        run.r = float((ck >> 16) & 0xFF) / 255.0f;
        run.g = float((ck >> 8) & 0xFF) / 255.0f;
        run.b = float(ck & 0xFF) / 255.0f;
        run.a = float((key >> 8) & 0xFF) / 255.0f;   // transparência da camada
        run.layerIdx = int((key >> 40) & 0x3FFFFF);  // VP-freeze por viewport
        run.annot = (key >> 62) & 1;                 // anotativa (papel re-emite)
        run.width = float(key & 0xFF) * 0.5f;     // quantum -> px
        run.start = static_cast<int>(m_cpuVerts.size() / 2);
        run.count = static_cast<int>(pts.size());
        for (const Point3& v : pts) {
            m_cpuVerts.push_back(float(v.x - m_originX));
            m_cpuVerts.push_back(float(v.y - m_originY));
        }
        m_colorRuns.push_back(run);
    }
    m_vertexCount = static_cast<int>(m_cpuVerts.size() / 2);

    // Faixas de triângulos preenchidos, rebaseadas na origem (float).
    m_fills.clear();
    std::vector<std::uint64_t> fillKeys;
    fillKeys.reserve(fillBuckets.size());
    for (const auto& kv : fillBuckets) fillKeys.push_back(kv.first);
    std::sort(fillKeys.begin(), fillKeys.end());
    for (const std::uint64_t key : fillKeys) {
        const std::vector<Point3>& pts = fillBuckets[key];
        const QVector4D col(float((key >> 16) & 0xFF) / 255.0f,
                            float((key >> 8) & 0xFF) / 255.0f,
                            float(key & 0xFF) / 255.0f, 1.0f);
        std::vector<float> verts;
        verts.reserve(pts.size() * 2);
        for (const Point3& v : pts) {
            verts.push_back(float(v.x - m_originX));
            verts.push_back(float(v.y - m_originY));
        }
        m_fills.push_back({col, std::move(verts), bool((key >> 32) & 1)});
    }

    // Máscaras (Wipeout): cor de fundo, desenhadas DEPOIS das linhas no paintGL.
    m_maskFills.clear();
    for (const auto& kv : maskBuckets) {
        std::vector<float> verts;
        verts.reserve(kv.second.size() * 2);
        for (const Point3& v : kv.second) {
            verts.push_back(float(v.x - m_originX));
            verts.push_back(float(v.y - m_originY));
        }
        m_maskFills.push_back(std::move(verts));
    }

    if (m_glReady) {
        m_vao.bind();
        m_vbo.bind();
        m_vbo.allocate(m_cpuVerts.data(),
                       static_cast<int>(m_cpuVerts.size() * sizeof(float)));
        m_vbo.release();
        m_vao.release();
    }

    if (m_haveData && !m_fitted && m_cam.width() > 1) {
        m_cam.fit(m_bbMinX, m_bbMinY, m_bbMaxX, m_bbMaxY);
        m_fitted = true;
        if (m_baseScale <= 0.0) m_baseScale = m_cam.scale();
    }
}

void ViewportWidget::fitView() {
    m_fitted = false;   // força o reenquadramento no próximo upload/paint
    rebuild();
}

// ---- Operações de VISTA (zoom/pan estilo AutoCAD) -------------------------
void ViewportWidget::pushViewHistory() {
    m_viewHistory.push_back(m_cam.state());
    if (m_viewHistory.size() > 32) m_viewHistory.erase(m_viewHistory.begin());
}

void ViewportWidget::emitZoomPercent() {
    if (m_baseScale > 0.0)
        emit zoomChanged(static_cast<int>(std::lround(m_cam.scale() / m_baseScale * 100.0)));
}

void ViewportWidget::zoomExtents() { pushViewHistory(); fitView(); emitZoomPercent(); }

void ViewportWidget::zoomIn() {
    pushViewHistory();
    m_cam.zoomAt(1.30, width() * 0.5, height() * 0.5);
    emitZoomPercent(); update();
}

void ViewportWidget::zoomOut() {
    pushViewHistory();
    m_cam.zoomAt(1.0 / 1.30, width() * 0.5, height() * 0.5);
    emitZoomPercent(); update();
}

void ViewportWidget::zoomPrevious() {
    if (m_viewHistory.empty()) { emit prompt("Sem vista anterior."); return; }
    m_cam.setState(m_viewHistory.back());
    m_viewHistory.pop_back();
    emitZoomPercent(); update();
}

void ViewportWidget::beginZoomWindow() {
    m_viewOp = ViewOp::ZoomWindow;
    emit prompt("Zoom Janela: arraste o retângulo da área a ampliar.");
    setCursor(Qt::CrossCursor); update();
}

void ViewportWidget::beginPan() {
    m_viewOp = ViewOp::Pan;
    emit prompt("Pan: arraste para deslocar a vista. Esc para sair.");
    setCursor(Qt::OpenHandCursor); update();
}

void ViewportWidget::beginZoomRealtime() {
    m_viewOp = ViewOp::ZoomRealtime;
    emit prompt("Zoom tempo-real: arraste para cima/baixo. Esc para sair.");
    setCursor(Qt::SizeVerCursor); update();
}

void ViewportWidget::endViewOp() {
    m_viewOp = ViewOp::None;
    m_viewDragging = false;
    m_boxActive = false;
    unsetCursor();
    emitPrompt(); update();
}

void ViewportWidget::rebuild() {
    uploadFromDoc();
    m_selCacheValid = false;       // origem/geometria mudaram: refaz o destaque
    m_thickCacheScale = -1.0;      // e a expansão das linhas grossas
    updateGrips();   // revalida os grips (somem se a entidade foi apagada/explodida)
    update();
}

void ViewportWidget::setTool(ToolKind k) {
    if (k != ToolKind::None) m_lastTool = k;   // memoriza p/ "repetir último comando"
    m_tools.setTool(k);
    m_gripDrag = -1;
    m_areaPts.clear();                          // zera a AREA por pontos ao trocar de ferramenta
    m_distHasP1 = false;
    updateGrips();
    emit toolChanged(static_cast<int>(k));     // sincroniza o botão ativo no ribbon
    m_dynInput.clear(); updateAutocomplete();   // limpa qualquer comando meio-digitado
    setFocus(Qt::OtherFocusReason);             // foco volta ao canvas -> já pode digitar o próximo
    emitPrompt();
    update();
}

void ViewportWidget::updateGrips() {
    m_grips.clear();
    m_gripEntity = kInvalidId;
    const std::vector<EntityId>& sel = m_tools.selection();
    if (sel.size() == 1)
        if (const Entity* e = m_doc->getEntity(sel[0])) {
            m_grips = gripsOf(*e);
            m_gripEntity = sel[0];
        }
    emit selectionChanged();
}

EntityId ViewportWidget::selectedId() const {
    const std::vector<EntityId>& s = m_tools.selection();
    return s.size() == 1 ? s.front() : kInvalidId;
}

std::vector<EntityId> ViewportWidget::selectedIds() const {
    return m_tools.selection();
}

int ViewportWidget::gripUnderCursor(double wx, double wy) const {
    const double tol = snapTolWorld();
    for (int i = 0; i < static_cast<int>(m_grips.size()); ++i)
        if (std::hypot(m_grips[i].x - wx, m_grips[i].y - wy) <= tol) return i;
    return -1;
}

void ViewportWidget::emitPrompt() {
    switch (m_tools.phase()) {
        case ToolController::EditPhase::Selecting:
            emit prompt(QString("Selecione objetos (%1 selec.). Enter/botão-direito confirma.")
                            .arg(m_tools.selection().size()));
            break;
        case ToolController::EditPhase::Base:
            emit prompt("Especifique o ponto-base.");
            break;
        case ToolController::EditPhase::Target:
            if (m_tools.tool() == ToolKind::Copy)
                emit prompt("Ponto de destino — clique para colar cópias; Enter/Esc termina.");
            else
                emit prompt("Especifique o ponto de destino.");
            break;
        default: {
            // Dicas passo-a-passo das ferramentas guiadas (arcos e porta).
            const ToolKind tk = m_tools.tool();
            const std::size_t n = m_tools.pendingCount();
            if (tk == ToolKind::ArcSER || tk == ToolKind::ArcSEA) {
                if (n == 0) emit prompt("Arco: clique o ponto INICIAL.");
                else if (n == 1) emit prompt("Arco: clique o ponto FINAL.");
                else if (n == 2)
                    emit prompt(tk == ToolKind::ArcSER
                        ? QStringLiteral("Arco: digite o RAIO + Enter, ou clique um ponto de PASSAGEM.")
                        : QStringLiteral("Arco: digite o ÂNGULO (graus) + Enter, ou clique um ponto de PASSAGEM."));
            } else if (tk == ToolKind::ArcSCE) {
                if (n == 0) emit prompt("Arco: clique o ponto INICIAL.");
                else if (n == 1) emit prompt("Arco: clique o CENTRO.");
                else if (n == 2) emit prompt("Arco: clique o ponto FINAL (anti-horário a partir do inicial).");
            } else if (tk == ToolKind::ArcCSE) {
                if (n == 0) emit prompt("Arco: clique o CENTRO.");
                else if (n == 1) emit prompt("Arco: clique o ponto INICIAL.");
                else if (n == 2) emit prompt("Arco: clique o ponto FINAL (anti-horário a partir do inicial).");
            } else if (tk == ToolKind::ArcSED) {
                if (n == 0) emit prompt("Arco: clique o ponto INICIAL.");
                else if (n == 1) emit prompt("Arco: clique o ponto FINAL.");
                else if (n == 2) emit prompt("Arco: clique um ponto na DIREÇÃO da tangente inicial.");
            } else if (tk == ToolKind::Door) {
                const int ds = m_tools.doorStage();
                if (ds == 1)      emit prompt("Porta na PAREDE: clique a outra borda do vão (largura).");
                else if (ds == 2) emit prompt("Porta na PAREDE: clique o LADO para onde ela abre.");
                else if (n == 0)  emit prompt("Porta: clique na PAREDE (dobradiça) — ou 3 pontos livres.");
                else if (n == 1)  emit prompt("Porta: clique o OUTRO LADO do vão (define a largura).");
                else if (n == 2)  emit prompt("Porta: clique o LADO para onde ela abre.");
            } else if (tk == ToolKind::WindowTool) {
                emit prompt(m_tools.windowStage() == 1
                    ? QStringLiteral("Janela: clique a outra borda do vão na MESMA parede.")
                    : QStringLiteral("Janela: clique a 1ª borda do vão SOBRE uma parede."));
            } else if (tk == ToolKind::WallTool) {
                if (n == 0) emit prompt(QString("Parede (espessura %1): clique o 1º ponto do EIXO — digite um número + Enter para mudar a espessura.")
                                            .arg(m_tools.wallThickness()));
                else emit prompt("Parede: próximo ponto do eixo — Enter finaliza, C fecha o anel, Esc cancela.");
            }
            break;
        }
    }
}

void ViewportWidget::testDraw() {
    // Demonstra ANOTAÇÃO sobre a geometria semente: cota linear, cota de
    // diâmetro, texto (fonte de traços) e hachura.
    m_tools.setTool(ToolKind::None);
    if (m_doc) {
        m_doc->addEntity(std::make_unique<Dimension>(Dimension::linear(
            Point3{-40, -25, 0}, Point3{40, -25, 0}, Point3{0, -40, 0}, 5.0)));
        m_doc->addEntity(std::make_unique<Dimension>(Dimension::diameter(
            Point3{0, 0, 0}, Point3{18, 0, 0}, 4.0)));
        // Cota ALINHADA na diagonal: texto deve sair legível (não de cabeça p/ baixo).
        m_doc->addEntity(std::make_unique<Dimension>(Dimension::aligned(
            Point3{-40, -25, 0}, Point3{40, 25, 0}, Point3{-18, 24, 0}, 5.0)));
        m_doc->addEntity(std::make_unique<MText>(Point3{-38, 30, 0}, "CADCORE", 6.0));
        std::vector<std::vector<Point3>> loops{
            {{26, -22, 0}, {40, -22, 0}, {40, -10, 0}, {26, -10, 0}}};
        m_doc->addEntity(std::make_unique<Hatch>(std::move(loops), HatchPattern::ANSI37, 0.0, 1.0));
        // Elipse (demonstra a nova primitiva).
        m_doc->addEntity(std::make_unique<Ellipse>(
            Ellipse::fromCenterAxes(Point3{0, 0, 0}, Vec3{34, 0, 0}, 22.0)));
        // Novas construções: hexágono (Polígono) e retângulo chanfrado.
        m_doc->addEntity(std::make_unique<Polyline>(regularPolygon(Point3{-34, 30, 0}, 6, 7.0)));
        m_doc->addEntity(std::make_unique<Polyline>(
            rectangleChamfer(Point3{18, -32, 0}, Point3{42, -14, 0}, 3.0)));
        // Retângulo arredondado (polilinha com arcos/bulge).
        m_doc->addEntity(std::make_unique<Polyline>(
            rectangleFillet(Point3{-46, -34, 0}, Point3{-18, -16, 0}, 5.0)));
        // Linha de construção (XLINE) infinita atravessando o desenho.
        m_doc->addEntity(std::make_unique<XLine>(
            XLine::fromTwoPoints(Point3{0, 0, 0}, Point3{1.0, 0.32, 0.0})));
        // Polilinha com LARGURA (faixa preenchida).
        {
            auto wide = std::make_unique<Polyline>(
                std::vector<Point3>{{18, -34, 0}, {44, -34, 0}, {44, -14, 0}}, false);
            wide->setWidth(3.0);
            m_doc->addEntity(std::move(wide));
        }
        // Chamada (LEADER) com texto.
        m_doc->addEntity(std::make_unique<Leader>(
            std::vector<Point3>{{-8, 8, 0}, {-22, 22, 0}, {-34, 22, 0}}, "FURO", 3.0));
        // Nuvem de revisão (revision cloud).
        m_doc->addEntity(std::make_unique<Polyline>(
            revisionCloudRect(Point3{8, -10, 0}, Point3{34, 6, 0}, 3.0)));
        // Multileader: 2 chamadas para 1 texto.
        m_doc->addEntity(std::make_unique<MLeader>(
            std::vector<std::vector<Point3>>{{{14, 30, 0}, {22, 40, 0}}, {{30, 30, 0}, {22, 40, 0}}},
            Point3{22, 41, 0}, "2x", 3.0));
        // Camada de espessura grossa (lineweight) + uma linha nela.
        { Layer g; g.name = "Grosso"; g.lineWeight.mm = 1.2; m_doc->layers().add(g); }
        { auto thick = std::make_unique<Line>(Point3{-46, 14, 0}, Point3{-46, 40, 0});
          thick->setLayer("Grosso"); m_doc->addEntity(std::move(thick)); }
        // Hachura SOLID (preenchimento sólido via ear clipping).
        m_doc->addEntity(std::make_unique<Hatch>(
            std::vector<std::vector<Point3>>{{{-44, -8, 0}, {-30, -8, 0}, {-30, 4, 0}, {-44, 4, 0}}},
            HatchPattern::Solid, 0.0));
    }
    // Demonstra Spline (curva), Ponto (marcador +) e GRIPS (alças azuis) numa
    // entidade selecionada (a polilínha externa → grips nos 4 cantos).
    if (m_doc) {
        m_doc->addEntity(std::make_unique<Spline>(std::vector<Point3>{
            {-42, -30, 0}, {-20, -12, 0}, {0, -34, 0}, {20, -12, 0}, {42, -30, 0}}));
        m_doc->addEntity(std::make_unique<PointEntity>(Point3{-34, 28, 0}));
        m_doc->addEntity(std::make_unique<PointEntity>(Point3{34, 28, 0}));
        m_doc->addEntity(std::make_unique<Polyline>(
            std::vector<Point3>{{-25, -18, 0}, {25, -18, 0}, {25, 18, 0}, {-25, 18, 0}}, true));
    }
    m_tools.setTool(ToolKind::None);
    m_tools.selectAt(Point3{25, 0, 0}, 1.5, false);   // borda direita da polilínha (grips nos 4 cantos)
    updateGrips();
    // Demonstra: marcador OSNAP por tipo (triângulo = Midpoint), pickbox e
    // entrada dinâmica (texto digitado »50 perto do cursor).
    m_curX = 0; m_curY = 18;
    m_snapResult.hit = true;
    m_snapResult.type = SnapType::Perpendicular;   // demo do novo glifo perpendicular
    m_snapResult.point = Point3{0, 18, 0};
    m_dynInput = QStringLiteral("C");           // demo do autocomplete de comandos
    m_cursorScreen = QPoint(610, 330);
    m_mouseInside = true;
    rebuild();
    updateAutocomplete();
}

void ViewportWidget::mousePressEvent(QMouseEvent* e) {
    m_lastMouse = e->position().toPoint();
    // Modo papel (Fase 2): a vista é só de leitura — pan (botão do meio) e zoom
    // (roda) seguem, mas o clique esquerdo não aciona ferramenta/seleção (a
    // criação de viewport entra na Fase 3). Operações de vista (Pan/Zoom-janela
    // pelo menu) continuam valendo.
    if (m_paperMode && m_viewOp == ViewOp::None && e->button() == Qt::LeftButton) {
        double px, py; m_cam.screenToWorld(e->position().x(), e->position().y(), px, py);
        if (m_paperVpCreate) {                      // criando viewport: 2 cliques
            if (!m_paperVpHasP1) {
                m_paperVpP1 = Point3{px, py, 0.0};
                m_paperVpHasP1 = true;
                emit prompt("Viewport: clique o canto oposto (Esc cancela).");
            } else {
                const double x = std::min(m_paperVpP1.x, px), y = std::min(m_paperVpP1.y, py);
                const double w = std::abs(px - m_paperVpP1.x), h = std::abs(py - m_paperVpP1.y);
                m_paperVpCreate = false; m_paperVpHasP1 = false;
                if (w > 2.0 && h > 2.0) emit paperViewportDrawn(x, y, w, h);
                else emit prompt("Viewport muito pequeno — cancelado.");
            }
            update();
            return;
        }
        if (m_mspaceIdx >= 0) return;   // MSPACE: clique esquerdo não seleciona viewport
        // Seleção + grips de viewport (mover/redimensionar).
        Layout* L = m_paperLayout;
        if (!L) return;
        const double tol = 6.0 / std::max(m_cam.scale(), 1e-9);   // ~6px em mm
        if (m_paperVpSel >= 0 && m_paperVpSel < int(L->viewports.size())) {
            const SheetViewport& v = L->viewports[std::size_t(m_paperVpSel)];
            const double gx[4] = {v.xMm, v.xMm + v.wMm, v.xMm + v.wMm, v.xMm};
            const double gy[4] = {v.yMm, v.yMm, v.yMm + v.hMm, v.yMm + v.hMm};
            for (int i = 0; i < 4; ++i)
                if (std::abs(px - gx[i]) <= tol && std::abs(py - gy[i]) <= tol) {
                    m_paperVpGrip = i;               // canto: redimensiona
                    m_paperVpDragging = true;
                    update();
                    return;
                }
        }
        int hit = -1;                                // topo da pilha primeiro
        for (int i = int(L->viewports.size()) - 1; i >= 0; --i)
            if (L->viewports[std::size_t(i)].contains(px, py)) { hit = i; break; }
        m_paperVpSel = hit;
        if (hit >= 0) {
            m_paperVpGrip = 4;                       // corpo: move
            m_paperVpDragging = true;
            m_paperDragOffX = px - L->viewports[std::size_t(hit)].xMm;
            m_paperDragOffY = py - L->viewports[std::size_t(hit)].yMm;
            emit prompt("Viewport selecionado: arraste move, grips redimensionam, "
                        "Del exclui, duplo clique entra na vista (MSPACE).");
        }
        update();
        return;
    }
    // Modo papel + botão direito num viewport selecionado: menu de contexto.
    if (m_paperMode && e->button() == Qt::RightButton && m_paperVpSel >= 0 &&
        m_paperLayout && m_paperVpSel < int(m_paperLayout->viewports.size())) {
        QMenu menu(this);
        QAction* aSc  = menu.addAction("Escala do viewport...");
        QAction* aLk  = menu.addAction("Travar viewport");
        aLk->setCheckable(true);
        aLk->setChecked(m_paperLayout->viewports[std::size_t(m_paperVpSel)].locked);
        QAction* aVf  = menu.addAction("Congelar camadas neste viewport...");
        QAction* aDel = menu.addAction("Excluir viewport");
        QAction* sel  = menu.exec(e->globalPosition().toPoint());
        if (sel == aSc) {
            emit paperViewportScaleRequested(m_paperVpSel);
        } else if (sel == aVf) {
            emit paperViewportLayersRequested(m_paperVpSel);
        } else if (sel == aLk) {
            SheetViewport& v = m_paperLayout->viewports[std::size_t(m_paperVpSel)];
            v.locked = aLk->isChecked();
            emit prompt(v.locked ? "Viewport travado (a vista/escala não muda no MSPACE)."
                                 : "Viewport destravado.");
            update();
        } else if (sel == aDel) {
            m_paperLayout->viewports.erase(
                m_paperLayout->viewports.begin() + m_paperVpSel);
            m_paperVpSel = -1; m_mspaceIdx = -1;
            emit prompt("Viewport excluído.");
            update();
        }
        return;
    }
    // Operação de VISTA ativa: o botão esquerdo conduz a vista, não a ferramenta.
    if (m_viewOp != ViewOp::None && e->button() == Qt::LeftButton) {
        pushViewHistory();
        m_viewDragging = true;
        if (m_viewOp == ViewOp::Pan) setCursor(Qt::ClosedHandCursor);
        if (m_viewOp == ViewOp::ZoomWindow) {
            double wx, wy; m_cam.screenToWorld(e->position().x(), e->position().y(), wx, wy);
            m_boxActive = true; m_boxAdd = false;
            m_boxStartScreen = m_lastMouse;
            m_boxStartWX = wx; m_boxStartWY = wy; m_boxCurWX = wx; m_boxCurWY = wy;
        }
        update();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        double wx, wy;
        m_cam.screenToWorld(e->position().x(), e->position().y(), wx, wy);
        const ToolKind tk = m_tools.tool();
        // UCS por cliques (one-shot, antes de qualquer ferramenta/seleção):
        // 1º clique = ORIGEM; com direção, 2º clique = direção do EIXO X.
        if (m_ucsMode != 0) {
            const Point3 p = m_snapResult.hit ? m_snapResult.point : Point3{wx, wy, 0.0};
            if (m_ucsMode == 1) {               // só origem (mantém o ângulo)
                m_doc->setUcs(p, m_doc->ucsAngleRad());
                m_ucsMode = 0;
                emit prompt(QString("UCS: origem em (%1, %2).")
                                .arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2));
            } else if (m_ucsMode == 2) {        // origem + direção: aguarda o 2º
                m_ucsP1 = p;
                m_ucsMode = 3;
                emit prompt("UCS: clique um ponto na direção do novo eixo X.");
            } else {                            // 2º clique: define o ângulo
                const double ang = std::atan2(p.y - m_ucsP1.y, p.x - m_ucsP1.x);
                m_doc->setUcs(m_ucsP1, ang);
                m_ucsMode = 0;
                emit prompt(QString("UCS: origem (%1, %2), eixo X a %3°.")
                                .arg(m_ucsP1.x, 0, 'f', 2).arg(m_ucsP1.y, 0, 'f', 2)
                                .arg(ang * 57.29577951308232, 0, 'f', 1));
            }
            update();
            return;
        }
        // QDIM: o clique posiciona a linha da CADEIA de cotas da seleção
        // (antes do fluxo de seleção — a seleção corrente é a fonte).
        if (m_qdimActive) {
            m_qdimActive = false;
            const int n = m_tools.qdim(Point3{wx, wy, 0.0});
            rebuild();
            emit prompt(n > 0
                ? QString("QDIM: %1 cota(s) contínua(s) criada(s).").arg(n)
                : QStringLiteral("QDIM: a seleção não tem endpoints suficientes."));
            return;
        }
        // Seleção por POLÍGONO (WP/CP): cada clique é um vértice; Enter fecha.
        if (m_selPolyActive) {
            m_selPolyPts.push_back(Point3{wx, wy, 0.0});
            emit prompt(QString("Polígono de seleção: %1 vértice(s) — Enter fecha, Esc cancela.")
                            .arg(m_selPolyPts.size()));
            update();
            return;
        }
        if (m_peditInsert && m_gripEntity != kInvalidId) {
            m_peditInsert = false;
            if (const Entity* ent = m_doc->getEntity(m_gripEntity))
                if (auto neu = withVertexInserted(*ent, Point3{wx, wy, 0.0})) {
                    m_doc->execute(std::make_unique<ReplaceCmd>(m_gripEntity, std::move(neu)));
                    updateGrips(); rebuild();
                    emit prompt("Vértice inserido.");
                }
            return;
        }
        if (m_fenceMode && (tk == ToolKind::Trim || tk == ToolKind::Extend)) {
            // Modo cerca: cada clique acrescenta um vértice à linha-cerca.
            m_fencePts.push_back(m_snapResult.hit ? m_snapResult.point : Point3{wx, wy, 0.0});
            emit prompt(QString("Fence (%1 pts): clique cruzando o que aparar; Enter aplica, Esc cancela.")
                            .arg(m_fencePts.size()));
            update();
            return;
        }
        // PORTA/JANELA sensíveis à PAREDE: pick da entidade sob o clique e o
        // ToolController decide (vão embutido na Wall ou fluxo legado).
        if (tk == ToolKind::Door || tk == ToolKind::WindowTool) {
            const Point3 pt = m_snapResult.hit ? m_snapResult.point : Point3{wx, wy, 0.0};
            Ray pr; pr.origin = Point3{wx, wy, 0.0};
            const EntityId pid = m_doc->pick(pr, snapTolWorld());
            if (tk == ToolKind::Door) m_tools.doorClick(pid, pt);
            else                      m_tools.windowClick(pid, pt);
            m_lastPoint = pt;
            rebuild();
            emitPrompt();
            update();
            return;
        }
        if ((tk == ToolKind::DimRadius || tk == ToolKind::DimDiameter) && m_tools.dimNeedsCircle()) {
            // Cota Raio/Diâmetro estilo AutoCAD: 1º clique escolhe o CÍRCULO/ARCO
            // (centro + raio reais); o 2º clique só posiciona a cota (raio correto).
            Ray r; r.origin = Point3{wx, wy, 0.0};
            m_tools.dimCirclePick(m_doc->pick(r, snapTolWorld()));
            rebuild();
            emitPrompt();
            return;
        }
        if (tk == ToolKind::Text) {
            // Texto estilo CAIXA (à la MTEXT): o press inicia o arrasto da
            // LARGURA da caixa; o release abre o editor IN-PLACE no canvas.
            const Point3 p0 = m_snapResult.hit ? m_snapResult.point : Point3{wx, wy, 0.0};
            m_textDragActive = true;
            m_textDragP1 = p0;
            emit prompt("Texto: arraste a largura da caixa e solte "
                        "(clique simples = texto livre, sem quebra).");
            update();
        } else if (tk == ToolKind::Hatch) {
            // Hachura: pick de uma área fechada; se o clique não pega entidade,
            // traça o contorno que envolve o ponto (BOUNDARY por ponto interno).
            Ray r; r.origin = Point3{wx, wy, 0.0};
            const EntityId hid = m_doc->pick(r, snapTolWorld());
            if (hid != kInvalidId) m_tools.hatchPick(hid);
            else                   m_tools.hatchAtPoint(Point3{wx, wy, 0.0});
            rebuild();
        } else if (tk == ToolKind::Inquiry) {
            // Consulta: clique uma entidade -> loga tipo, comprimento e área.
            Ray r; r.origin = Point3{wx, wy, 0.0};
            if (const Entity* e = m_doc->getEntity(m_doc->pick(r, snapTolWorld())))
                emit prompt(QString("Consulta: %1 | Compr.: %2 | Área: %3")
                                .arg(e->typeName())
                                .arg(entityLength(*e), 0, 'f', 2)
                                .arg(entityArea(*e), 0, 'f', 2));
        } else if (tk == ToolKind::Dist) {
            // DIST: dois pontos -> distância, dX, dY e ângulo na linha de comando.
            const Point3 p = m_snapResult.hit ? m_snapResult.point : Point3{wx, wy, 0.0};
            if (!m_distHasP1) {
                m_distP1 = p; m_distHasP1 = true;
                emit prompt("DIST: clique o segundo ponto.");
            } else {
                const double dx = p.x - m_distP1.x, dy = p.y - m_distP1.y;
                emit prompt(QString("Dist: %1 | dX: %2 | dY: %3 | Ângulo: %4 graus")
                                .arg(std::hypot(dx, dy), 0, 'f', 3)
                                .arg(dx, 0, 'f', 3).arg(dy, 0, 'f', 3)
                                .arg(std::atan2(dy, dx) * 180.0 / 3.14159265358979, 0, 'f', 2));
                m_distHasP1 = false;
            }
        } else if (tk == ToolKind::Area) {
            // AREA por pontos: acumula vértices; mostra a área/perímetro correntes.
            // Enter fecha e reporta; Esc cancela.
            const Point3 p = m_snapResult.hit ? m_snapResult.point : Point3{wx, wy, 0.0};
            m_areaPts.push_back(p);
            if (m_areaPts.size() >= 3)
                emit prompt(QString("AREA (%1 pts): %2 u² | Perímetro: %3 — clique + pontos, Enter fecha, Esc cancela.")
                                .arg(m_areaPts.size())
                                .arg(polygonArea(m_areaPts), 0, 'f', 2)
                                .arg(polygonPerimeter(m_areaPts, true), 0, 'f', 2));
            else
                emit prompt(QString("AREA: clique os pontos do contorno (%1). Enter fecha, Esc cancela.")
                                .arg(m_areaPts.size()));
            update();
        } else if (tk == ToolKind::Trim) {
            // Trim: inicia uma caixa de arrasto (igual ao modo Selecionar). No
            // release decidimos: clique simples = apara o trecho clicado; arrasto
            // = janela de aparo (apara TODA entidade que cruza o retângulo).
            m_boxActive = true;
            m_boxAdd = false;
            m_boxStartScreen = e->position().toPoint();
            m_boxStartWX = wx; m_boxStartWY = wy;
            m_boxCurWX = wx;   m_boxCurWY = wy;
            update();
        } else if (tk == ToolKind::Fillet ||
                   tk == ToolKind::Chamfer || tk == ToolKind::Extend ||
                   tk == ToolKind::Divide || tk == ToolKind::Measure ||
                   tk == ToolKind::CircleTTR || tk == ToolKind::CircleTTT ||
                   tk == ToolKind::BreakTool || tk == ToolKind::JoinTool ||
                   tk == ToolKind::Lengthen || tk == ToolKind::MatchProps ||
                   tk == ToolKind::ArrayPath) {
            // Pick de uma entidade (edição por clique).
            Ray r; r.origin = Point3{wx, wy, 0.0};
            const EntityId id = m_doc->pick(r, snapTolWorld());
            const Point3 at{wx, wy, 0.0};
            if      (tk == ToolKind::MatchProps) m_tools.matchPropsClick(id);
            else if (tk == ToolKind::Fillet)    m_tools.filletClick(id, at);
            else if (tk == ToolKind::Chamfer)   m_tools.chamferClick(id, at);
            else if (tk == ToolKind::Extend)    m_tools.extendClick(id, at);
            else if (tk == ToolKind::Divide)    m_tools.divideClick(id);
            else if (tk == ToolKind::Measure)   m_tools.measureClick(id);
            else if (tk == ToolKind::CircleTTR) m_tools.ttrClick(id, at);
            else if (tk == ToolKind::CircleTTT) m_tools.tttClick(id, at);
            else if (tk == ToolKind::BreakTool) m_tools.breakClick(id, at);
            else if (tk == ToolKind::JoinTool)  m_tools.joinClick(id, at);
            else if (tk == ToolKind::ArrayPath) { if (m_tools.arrayPathClick(id)) setTool(ToolKind::None); }
            else                                m_tools.lengthenClick(id, at);
            rebuild();
        } else if (m_tools.selectingObjects() &&
                   (m_gripDrag = gripUnderCursor(wx, wy)) >= 0) {
            // Clique sobre um grip da entidade selecionada: inicia o arrasto.
            update();
        } else if (m_tools.selectingObjects()) {
            // Selecionando (modo None ou edição na fase Selecting): inicia a caixa;
            // clique/Window/Crossing é decidido no release.
            m_boxActive = true;
            m_boxAdd = (e->modifiers() & Qt::ShiftModifier);
            m_boxStartScreen = e->position().toPoint();
            m_boxStartWX = wx; m_boxStartWY = wy;
            m_boxCurWX = wx;   m_boxCurWY = wy;
            update();
        } else {
            // Colocando pontos (Base/Target): snap (se ligado), senão Ortho.
            Point3 pref;
            const Point3* pfrom = m_tools.referencePoint(pref) ? &pref : nullptr;
            const SnapResult sr = m_snapEnabled
                ? m_snap.resolve(Point3{wx, wy, 0.0}, snapTolWorld(), *m_doc, pfrom, m_snapMask)
                : SnapResult{};
            Point3 pt;
            if (sr.hit) {
                pt = sr.point;
                // Associatividade de cota: informa em QUAL entidade o ponto
                // grudou (consumida pelo onPoint das ferramentas de cota).
                m_tools.setNextPointAnchor(sr.entity, static_cast<int>(sr.type), sr.point);
            } else {
                if (!applyTracking(wx, wy) && !applyPolar(wx, wy)) {
                    applyOrtho(wx, wy);                       // só age se Ortho ligado
                    if (!m_orthoOn) applyGridSnap(wx, wy);    // senão, grid snap (se ligado)
                }
                pt = Point3{wx, wy, 0.0};
            }
            // M2P: acumula 2 cliques (com OSNAP) e entrega o ponto MÉDIO.
            if (m_m2pActive) {
                if (!m_m2pHasFirst) {
                    m_m2pFirst = pt;
                    m_m2pHasFirst = true;
                    emit prompt("M2P: clique o 2º ponto de referência.");
                    update();
                    return;
                }
                m_m2pActive = false;
                m_m2pHasFirst = false;
                pt = Point3{(m_m2pFirst.x + pt.x) * 0.5,
                            (m_m2pFirst.y + pt.y) * 0.5, 0.0};
                emit prompt(QString("M2P: ponto médio (%1, %2) aplicado.")
                                .arg(pt.x, 0, 'f', 2).arg(pt.y, 0, 'f', 2));
            }
            m_tools.onPoint(pt);
            m_lastPoint = pt;
            m_trackPts.clear();
            m_trackPts.push_back(pt);   // rastreia a partir do ponto recém-colocado
            m_trackGuides.clear();
            rebuild();
            emitPrompt();
        }
    } else if (e->button() == Qt::RightButton) {
        // Menu de contexto de grips: clicar com o direito numa polilinha selecionada
        // oferece inserir/remover vértice (PEDIT).
        if (m_gripEntity != kInvalidId) {
            double rwx, rwy;
            m_cam.screenToWorld(e->position().x(), e->position().y(), rwx, rwy);
            const Entity* ge = m_doc->getEntity(m_gripEntity);
            if (dynamic_cast<const Polyline*>(ge) || dynamic_cast<const Spline*>(ge)) {
                const int gi = gripUnderCursor(rwx, rwy);
                QMenu menu(this);
                QAction* aIns = menu.addAction("Inserir vértice aqui");
                QAction* aRem = (gi >= 0) ? menu.addAction("Remover este vértice") : nullptr;
                QAction* chosen = menu.exec(e->globalPosition().toPoint());
                if (chosen == aIns) {
                    if (auto neu = withVertexInserted(*ge, Point3{rwx, rwy, 0.0})) {
                        m_doc->execute(std::make_unique<ReplaceCmd>(m_gripEntity, std::move(neu)));
                        updateGrips(); rebuild(); emitPrompt();
                    }
                    return;
                }
                if (chosen && chosen == aRem) {
                    if (auto neu = withVertexRemoved(*ge, gi)) {
                        m_doc->execute(std::make_unique<ReplaceCmd>(m_gripEntity, std::move(neu)));
                        updateGrips(); rebuild(); emitPrompt();
                    }
                    return;
                }
                if (chosen != nullptr) return;   // escolheu algo do menu; não cai no default
                // menu fechado sem escolha: segue para o comportamento padrão.
            }
        }
        // Botão-direito: conclui o traço, confirma a seleção (Verb-Noun) ou cancela.
        if (m_tools.strokeInProgress()) {
            m_tools.finishStroke();
            rebuild();
        } else if (m_tools.selectingObjects() && !m_tools.selection().empty()) {
            m_tools.confirmSelection();
        } else if (m_tools.tool() == ToolKind::None && m_lastTool != ToolKind::None) {
            setTool(m_lastTool);          // botão-direito ocioso = repete último comando
        } else {
            m_tools.cancel();
        }
        emitPrompt();
        update();
    }
}

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    // Igual ao AutoCAD: duplo-clique no botão do meio (roda) = Zoom Tudo.
    if (e->button() == Qt::MiddleButton) { zoomExtents(); return; }
    // Modo papel: duplo clique DENTRO de um viewport entra no MSPACE (a roda
    // passa a dar zoom na vista e o meio a fazer pan da vista); duplo clique
    // FORA do viewport ativo volta ao papel (PSPACE) — como no AutoCAD.
    if (m_paperMode && e->button() == Qt::LeftButton && m_paperLayout) {
        double px, py; m_cam.screenToWorld(e->position().x(), e->position().y(), px, py);
        int hit = -1;
        for (int i = int(m_paperLayout->viewports.size()) - 1; i >= 0; --i)
            if (m_paperLayout->viewports[std::size_t(i)].contains(px, py)) { hit = i; break; }
        if (m_mspaceIdx >= 0 && hit != m_mspaceIdx) {
            m_mspaceIdx = -1;
            emit prompt("PSPACE: de volta ao papel.");
        } else if (hit >= 0) {
            m_mspaceIdx = hit;
            m_paperVpSel = -1;
            emit prompt("MSPACE: roda = zoom da vista, botão do meio = pan da vista; "
                        "duplo clique fora (ou Esc) volta ao papel.");
        }
        update();
        return;
    }
    // Duplo-clique num texto = editar o conteúdo (DDEDIT); num bloco com
    // atributos = editar os valores (ATTEDIT), como no AutoCAD.
    if (e->button() == Qt::LeftButton) {
        double wx, wy; m_cam.screenToWorld(e->position().x(), e->position().y(), wx, wy);
        Ray r; r.origin = Point3{wx, wy, 0.0};
        if (const Entity* ent = m_doc->getEntity(m_doc->pick(r, snapTolWorld()))) {
            if (const auto* mt = dynamic_cast<const MText*>(ent)) {
                // Edição IN-PLACE (caixa de texto sobre o próprio desenho).
                m_tools.selectAt(Point3{wx, wy, 0.0}, snapTolWorld(), false);
                updateGrips();
                emit selectionChanged();
                beginTextBoxEdit(mt->position(), mt->boxWidth(), ent->id());
                update();
                return;
            }
            if (const auto* br = dynamic_cast<const BlockRef*>(ent);
                br && !br->attValues().empty()) {
                m_tools.selectAt(Point3{wx, wy, 0.0}, snapTolWorld(), false);
                updateGrips();
                emit selectionChanged();
                emit editBlockAttrsRequested();
                update();
                return;
            }
            if (const auto* dim = dynamic_cast<const Dimension*>(ent)) {
                // DIMTEDIT: duplo clique edita o OVERRIDE do texto da cota.
                m_tools.selectAt(Point3{wx, wy, 0.0}, snapTolWorld(), false);
                updateGrips();
                emit selectionChanged();
                bool ok = false;
                const QString s = QInputDialog::getText(
                    this, "Texto da cota",
                    "Texto forçado (vazio = medida real):", QLineEdit::Normal,
                    QString::fromStdString(dim->textOverride()), &ok);
                if (ok) {
                    auto neu = std::unique_ptr<Dimension>(
                        static_cast<Dimension*>(dim->clone().release()));
                    neu->setTextOverride(s.toStdString());
                    m_doc->execute(std::make_unique<ReplaceCmd>(ent->id(), std::move(neu)));
                    rebuild();
                    emit prompt(s.isEmpty() ? "Cota de volta à medida real."
                                            : "Texto da cota substituído.");
                }
                update();
                return;
            }
        }
    }
    QOpenGLWidget::mouseDoubleClickEvent(e);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* e) {
    const QPoint p = e->position().toPoint();
    m_cursorScreen = p;
    m_trackLocked = false;   // recalculado por applyTracking quando alinhado a uma guia
    double wx, wy;
    m_cam.screenToWorld(p.x(), p.y(), wx, wy);

    // Operação de VISTA em arrasto: Pan e Zoom tempo-real (Janela usa a caixa abaixo).
    if (m_viewDragging && (e->buttons() & Qt::LeftButton)) {
        if (m_viewOp == ViewOp::Pan) {
            m_cam.panPixels(p.x() - m_lastMouse.x(), p.y() - m_lastMouse.y());
            m_lastMouse = p; update(); return;
        }
        if (m_viewOp == ViewOp::ZoomRealtime) {
            const int dy = m_lastMouse.y() - p.y();          // arrastar p/ cima = ampliar
            m_cam.zoomAt(std::pow(1.01, dy), width() * 0.5, height() * 0.5);
            m_lastMouse = p; emitZoomPercent(); update(); return;
        }
    }

    // --- Modo PAPEL: arrasto de viewport (mover/grip), pan do MSPACE e pan da
    //     folha; sem snap/ferramentas de modelo. -----------------------------
    if (m_paperMode) {
        m_curX = wx; m_curY = wy;
        Layout* L = m_paperLayout;
        if (m_paperVpDragging && (e->buttons() & Qt::LeftButton) && L &&
            m_paperVpSel >= 0 && m_paperVpSel < int(L->viewports.size())) {
            SheetViewport& v = L->viewports[std::size_t(m_paperVpSel)];
            if (m_paperVpGrip == 4) {                        // mover pelo corpo
                v.xMm = wx - m_paperDragOffX;
                v.yMm = wy - m_paperDragOffY;
            } else {                                         // canto: redimensiona
                double x0 = v.xMm, y0 = v.yMm, x1 = v.xMm + v.wMm, y1 = v.yMm + v.hMm;
                switch (m_paperVpGrip) {
                    case 0: x0 = wx; y0 = wy; break;         // BL
                    case 1: x1 = wx; y0 = wy; break;         // BR
                    case 2: x1 = wx; y1 = wy; break;         // TR
                    case 3: x0 = wx; y1 = wy; break;         // TL
                }
                if (x1 - x0 >= 5.0 && y1 - y0 >= 5.0) {      // mínimo 5mm
                    v.xMm = x0; v.yMm = y0; v.wMm = x1 - x0; v.hMm = y1 - y0;
                }
            }
            emit cursorMoved(wx, wy);
            update();
            return;
        }
        if (e->buttons() & Qt::MiddleButton) {
            if (m_mspaceIdx >= 0 && L && m_mspaceIdx < int(L->viewports.size()) &&
                !L->viewports[std::size_t(m_mspaceIdx)].locked) {
                // MSPACE: o meio arrasta a VISTA do viewport ativo (não a folha).
                SheetViewport& v = L->viewports[std::size_t(m_mspaceIdx)];
                const double mmPerPx = 1.0 / std::max(m_cam.scale(), 1e-9);
                const double dxMm =  (p.x() - m_lastMouse.x()) * mmPerPx;
                const double dyMm = -(p.y() - m_lastMouse.y()) * mmPerPx;  // Y de tela p/ baixo
                v.modelCx -= dxMm / v.mmPerUnit;
                v.modelCy -= dyMm / v.mmPerUnit;
            } else {
                m_cam.panPixels(p.x() - m_lastMouse.x(), p.y() - m_lastMouse.y());
            }
            m_lastMouse = p;
        }
        emit cursorMoved(wx, wy);
        update();
        return;
    }

    // Arrastando a caixa de seleção: o canto livre acompanha o cursor.
    if (m_boxActive) {
        if (e->buttons() & Qt::LeftButton) {
            m_boxCurWX = wx; m_boxCurWY = wy;
            emit cursorMoved(wx, wy);
            update();
            return;
        }
        m_boxActive = false;   // botão solto fora do viewport -> não deixa a janela presa
    }

    Point3 snapRef;
    const Point3* snapFrom = m_tools.referencePoint(snapRef) ? &snapRef : nullptr;
    const bool panning = (e->buttons() & Qt::MiddleButton);
    if (panning) {
        m_cam.panPixels(p.x() - m_lastMouse.x(), p.y() - m_lastMouse.y());
        m_lastMouse = p;
        m_snapResult = SnapResult{};   // sem snap enquanto arrasta a vista
        m_curX = wx; m_curY = wy;
    } else if (m_snapEnabled &&
               (m_snapResult = m_snap.resolve(Point3{wx, wy, 0.0}, snapTolWorld(), *m_doc, snapFrom, m_snapMask)).hit) {
        // Atração magnética SÓ quando o ponto de snap está na vizinhança do
        // cursor. Snaps "deferred" (Perpendicular/Tangente) podem cair LONGE
        // (o pé da perpendicular) — nesse caso o crosshair fica livre, o glifo
        // acende no pé e o CLIQUE cai lá (o press refaz o resolve) — sem "ímã".
        const double dAtt = std::hypot(m_snapResult.point.x - wx,
                                       m_snapResult.point.y - wy);
        if (dAtt <= snapTolWorld() * 1.5) {
            m_curX = m_snapResult.point.x;
            m_curY = m_snapResult.point.y;
        } else {
            m_curX = wx; m_curY = wy;   // crosshair livre; marcador indica o alvo
        }
        // Só pontos NOTÁVEIS viram referência de rastreamento — Nearest/Perp/
        // Tangente seguem o cursor e poluiriam o rastro (expulsando a referência
        // real, ex.: o endpoint pairado ao seguir a própria linha).
        const SnapType st = m_snapResult.type;
        if (st == SnapType::Endpoint || st == SnapType::Midpoint ||
            st == SnapType::Center   || st == SnapType::Quadrant ||
            st == SnapType::Intersection)
            acquireTrackPoint(m_snapResult.point);   // memoriza como referência p/ rastreamento
        m_trackGuides.clear();
    } else {
        m_snapResult = SnapResult{};
        if (!applyTracking(wx, wy) && !applyPolar(wx, wy)) {  // OTRACK, senão Polar,
            applyOrtho(wx, wy);                               // senão Ortho,
            if (!m_orthoOn) applyGridSnap(wx, wy);            // senão grid snap
        }
        m_curX = wx; m_curY = wy;
    }
    // A lista de autocomplete acompanha o cursor (como o HUD de entrada dinâmica).
    if (m_cmdList && m_cmdList->isVisible())
        m_cmdList->move(m_cursorScreen.x() + 16, m_cursorScreen.y() + 18);

    emit cursorMoved(m_curX, m_curY);
    update();   // sempre, para mover o preview/cursor
}

void ViewportWidget::acquireTrackPoint(const Point3& p) {
    for (const Point3& q : m_trackPts)
        if (std::abs(q.x - p.x) < 1e-6 && std::abs(q.y - p.y) < 1e-6) return;
    m_trackPts.push_back(p);
    if (m_trackPts.size() > 2) m_trackPts.erase(m_trackPts.begin());  // mantém os 2 últimos
}

bool ViewportWidget::applyTracking(double& wx, double& wy) {
    m_trackGuides.clear();
    // Só rastreia durante a colocação de pontos (não no modo seleção).
    if (!m_otrackEnabled || !m_snapEnabled || m_trackPts.empty() || m_tools.selectingObjects())
        return false;
    const double tol = snapTolWorld();
    bool lockX = false, lockY = false;
    Point3 vref, href;
    for (const Point3& pt : m_trackPts) {
        if (!lockX && std::abs(wx - pt.x) <= tol) { wx = pt.x; lockX = true; vref = pt; }
        if (!lockY && std::abs(wy - pt.y) <= tol) { wy = pt.y; lockY = true; href = pt; }
    }
    if (lockX) {  // guia vertical do ref até o cursor
        m_trackGuides.push_back(Point3{wx, vref.y, 0.0});
        m_trackGuides.push_back(Point3{wx, wy, 0.0});
    }
    if (lockY) {  // guia horizontal do ref até o cursor
        m_trackGuides.push_back(Point3{href.x, wy, 0.0});
        m_trackGuides.push_back(Point3{wx, wy, 0.0});
    }
    // Guarda o eixo único travado para a "distância ao longo da guia" (valor digitado).
    m_trackLocked = (lockX != lockY);   // só com UM eixo travado a direção é única
    if (lockX && !lockY) {
        m_trackLockRef = vref;
        m_trackLockDir = Point3{0.0, wy >= vref.y ? 1.0 : -1.0, 0.0};
    } else if (lockY && !lockX) {
        m_trackLockRef = href;
        m_trackLockDir = Point3{wx >= href.x ? 1.0 : -1.0, 0.0, 0.0};
    }
    return lockX || lockY;
}

void ViewportWidget::applyOrtho(double& wx, double& wy) const {
    if (!m_orthoOn) return;
    Point3 ref;
    if (!m_tools.referencePoint(ref)) return;
    // UCS ativo: o ortho tranca nos EIXOS DO UCS (frame de trabalho girado).
    if (m_doc && m_doc->ucsActive()) {
        const Point3 cu = m_doc->worldToUcs(Point3{wx, wy, 0.0});
        const Point3 ru = m_doc->worldToUcs(ref);
        Point3 ou = cu;
        if (std::abs(cu.x - ru.x) >= std::abs(cu.y - ru.y)) ou.y = ru.y;
        else                                                ou.x = ru.x;
        const Point3 w = m_doc->ucsToWorld(ou);
        wx = w.x; wy = w.y;
        return;
    }
    // Mantém o eixo de maior deslocamento; zera o outro (horizontal ou vertical).
    if (std::abs(wx - ref.x) >= std::abs(wy - ref.y)) wy = ref.y;
    else                                              wx = ref.x;
}

void ViewportWidget::applyFence() {
    // Para cada entidade cuja geometria a linha-cerca cruza, apara/estende no
    // ponto de cruzamento. Coleta primeiro (o aparo altera ids), aplica depois.
    if (m_fencePts.size() < 2 || !m_doc) { m_fenceMode = false; m_fencePts.clear(); return; }
    const ToolKind tk = m_tools.tool();

    AABB fb;
    for (const Point3& p : m_fencePts) fb.expand(p);
    fb.expand(Point3{fb.min.x - 1.0, fb.min.y - 1.0, 0.0});   // folga p/ a query
    fb.expand(Point3{fb.max.x + 1.0, fb.max.y + 1.0, 0.0});

    std::vector<std::pair<EntityId, Point3>> hits;
    for (const EntityId id : m_doc->query(fb)) {
        const Entity* ent = m_doc->getEntity(id);
        if (!ent) continue;
        RenderBatch rb; ent->emitTo(rb);
        const auto& lv = rb.lineVertices;
        bool found = false;
        for (std::size_t i = 0; i + 1 < lv.size() && !found; i += 2)
            for (std::size_t j = 0; j + 1 < m_fencePts.size() && !found; ++j) {
                Point3 at{};
                if (segmentIntersect(lv[i], lv[i + 1], m_fencePts[j], m_fencePts[j + 1], at)) {
                    hits.emplace_back(id, at);   // 1 cruzamento por entidade
                    found = true;
                }
            }
    }
    for (const auto& [id, at] : hits) {
        if (tk == ToolKind::Extend) m_tools.extendClick(id, at);
        else                        m_tools.trimClick(id, at);
    }
    m_fenceMode = false;
    m_fencePts.clear();
}

bool ViewportWidget::applyGridSnap(double& wx, double& wy) const {
    if (!m_gridSnapOn || m_gridSpacing <= 0.0) return false;
    if (m_doc && m_doc->ucsActive()) {   // grade acompanha o UCS
        Point3 u = m_doc->worldToUcs(Point3{wx, wy, 0.0});
        u.x = std::round(u.x / m_gridSpacing) * m_gridSpacing;
        u.y = std::round(u.y / m_gridSpacing) * m_gridSpacing;
        const Point3 w = m_doc->ucsToWorld(u);
        wx = w.x; wy = w.y;
        return true;
    }
    wx = std::round(wx / m_gridSpacing) * m_gridSpacing;   // gruda na grade (base 0,0)
    wy = std::round(wy / m_gridSpacing) * m_gridSpacing;
    return true;
}

bool ViewportWidget::applyPolar(double& wx, double& wy) {
    if (!m_polarOn) return false;
    Point3 ref;
    if (!m_tools.referencePoint(ref)) return false;
    // UCS ativo: os ângulos do polar valem no FRAME DO UCS — roda tudo pra lá,
    // aplica a lógica normal e devolve o resultado ao mundo no fim.
    const bool ucs = m_doc && m_doc->ucsActive();
    if (ucs) {
        const Point3 cu = m_doc->worldToUcs(Point3{wx, wy, 0.0});
        ref = m_doc->worldToUcs(ref);
        wx = cu.x; wy = cu.y;
    }
    constexpr double kTolDeg = 4.0;
    constexpr double kRadToDeg = 57.29577951308232;
    const double dx = wx - ref.x, dy = wy - ref.y;
    if (std::hypot(dx, dy) < 1e-9) return false;
    const double ang = std::atan2(dy, dx) * kRadToDeg;
    auto delta = [](double a, double b) {
        double d = std::fmod(std::fabs(a - b), 360.0);
        return d > 180.0 ? 360.0 - d : d;
    };

    // MULTI-incrementos (ex.: 45 E 90 marcados no menu) + ângulos ADICIONAIS
    // absolutos: vence o raio mais próximo da direção do cursor.
    PolarResult best;
    double bestDelta = kTolDeg + 1.0;
    for (const double inc : m_polarIncs) {
        const PolarResult pr = polarSnap(ref, wx, wy, inc, kTolDeg);
        if (!pr.active) continue;
        const double d = delta(ang, pr.angleDeg);
        if (d < bestDelta) { best = pr; bestDelta = d; }
    }
    for (const double a : m_polarExtraDeg) {
        const double d = delta(ang, a);
        if (d > kTolDeg || d >= bestDelta) continue;
        const double ar = a / kRadToDeg;
        const double c = std::cos(ar), s = std::sin(ar);
        const double dist = dx * c + dy * s;
        best.active = true;
        best.point = Point3{ref.x + dist * c, ref.y + dist * s, 0.0};
        best.angleDeg = a;
        bestDelta = d;
    }
    if (!best.active) return false;
    Point3 pw = best.point, rw = ref;
    if (ucs) { pw = m_doc->ucsToWorld(best.point); rw = m_doc->ucsToWorld(ref); }
    wx = pw.x; wy = pw.y;
    m_trackGuides.clear();                       // guia tracejada do raio polar
    m_trackGuides.push_back(rw);
    m_trackGuides.push_back(Point3{wx, wy, 0.0});
    return true;
}

void ViewportWidget::setGridOn(bool on)      { m_gridOn = on; update(); }
void ViewportWidget::setSnapEnabled(bool on) { m_snapEnabled = on; if (!on) m_snapResult = SnapResult{}; update(); }
void ViewportWidget::setOrthoOn(bool on)     { m_orthoOn = on; if (on) m_polarOn = false; update(); }
void ViewportWidget::setOtrackEnabled(bool on) { m_otrackEnabled = on; if (!on) m_trackGuides.clear(); update(); }
void ViewportWidget::setPolarOn(bool on)     { m_polarOn = on; if (on) m_orthoOn = false; if (!on) m_trackGuides.clear(); update(); }

void ViewportWidget::enterEvent(QEnterEvent*) { m_mouseInside = true; setFocus(Qt::MouseFocusReason); update(); }
void ViewportWidget::leaveEvent(QEvent*) {
    m_mouseInside = false;
    m_trackGuides.clear();          // não deixa guia OTRACK/polar presa ao sair do canvas
    m_snapResult = SnapResult{};    // nem o marcador de snap
    update();
}

void ViewportWidget::cancelAndDeselect() {
    if (m_viewOp != ViewOp::None) { endViewOp(); return; }
    m_fenceMode = false; m_fencePts.clear();
    m_boxActive = false;
    m_tools.cancel();
    m_tools.clearSelection();
    m_gripDrag = -1;
    m_trackPts.clear();
    m_trackGuides.clear();
    m_dynInput.clear();
    if (m_tools.tool() != ToolKind::None) setTool(ToolKind::None);
    updateGrips();
    updateAutocomplete();
    setFocus(Qt::OtherFocusReason);   // devolve o foco de teclado ao canvas
    emitPrompt();
    update();
}

void ViewportWidget::inputPoint(const Point3& p) {
    m_tools.onPoint(p);
    m_lastPoint = p;
    rebuild();
    emitPrompt();
}

void ViewportWidget::inputDimensions(double w, double h) {
    if (m_tools.onDimensions(w, h)) { rebuild(); emitPrompt(); }
}

void ViewportWidget::inputDistance(double dist) {
    if (m_tools.wantsValue()) {     // raio/ângulo digitado (Arco SER/SEA)
        if (m_tools.onValue(dist)) { rebuild(); emitPrompt(); }
        return;
    }
    Point3 ref;
    if (!m_tools.referencePoint(ref)) {
        // Sem ponto-base: se o cursor está alinhado a uma guia OTRACK, a distância
        // digitada coloca o ponto a essa distância da REFERÊNCIA rastreada (estilo
        // AutoCAD: aponte uma extremidade, siga a guia, digite o valor).
        if (m_trackLocked) {
            const Point3 p{m_trackLockRef.x + m_trackLockDir.x * dist,
                           m_trackLockRef.y + m_trackLockDir.y * dist, 0.0};
            m_tools.onPoint(p);
            m_lastPoint = p;
            m_trackPts.clear(); m_trackPts.push_back(p);
            m_trackGuides.clear();
            m_trackLocked = false;
            rebuild();
            emitPrompt();
            return;
        }
        // Fora das guias H/V: direção LIVRE a partir do último ponto ADQUIRIDO
        // (pairado). Cobre "seguir a própria linha" em qualquer ângulo — com o
        // snap Nearest na linha, a direção é exata (extension à la AutoCAD).
        if (!m_trackPts.empty()) {
            const Point3 r = m_trackPts.back();
            const double dx = m_curX - r.x, dy = m_curY - r.y;
            const double len = std::hypot(dx, dy);
            if (len >= 1e-9) {
                const Point3 p{r.x + dx / len * dist, r.y + dy / len * dist, 0.0};
                m_tools.onPoint(p);
                m_lastPoint = p;
                m_trackPts.clear(); m_trackPts.push_back(p);
                m_trackGuides.clear();
                rebuild();
                emitPrompt();
                return;
            }
        }
        // Sem NENHUMA referência: avisa em vez de falhar em silêncio.
        emit prompt("Distância: pare o mouse sobre um ponto de referência (ou clique "
                    "o 1º ponto) e aponte a direção antes de digitar o valor.");
        return;
    }
    const double dx = m_curX - ref.x, dy = m_curY - ref.y;
    const double len = std::hypot(dx, dy);
    if (len < 1e-9) {
        emit prompt("Distância: afaste o cursor do ponto-base para indicar a direção.");
        return;
    }
    const Point3 p{ref.x + dx / len * dist, ref.y + dy / len * dist, 0.0};
    m_tools.onPoint(p);
    m_lastPoint = p;
    rebuild();
    emitPrompt();
}

void ViewportWidget::setCurrentLayer(const std::string& name) {
    m_tools.setCurrentLayer(name);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* e) {
    // Modo papel: solta o arrasto de viewport (mover/grip).
    if (m_paperMode && m_paperVpDragging && e->button() == Qt::LeftButton) {
        m_paperVpDragging = false;
        m_paperVpGrip = -1;
        update();
        return;
    }
    // Caixa de texto: o release fecha o arrasto da largura e abre o editor.
    if (m_textDragActive && e->button() == Qt::LeftButton) {
        m_textDragActive = false;
        double wx, wy; m_cam.screenToWorld(e->position().x(), e->position().y(), wx, wy);
        const double minW = 12.0 / std::max(m_cam.scale(), 1e-9);   // ~12px
        const double dx = std::abs(wx - m_textDragP1.x);
        if (dx <= minW) {
            // Clique simples: texto LIVRE no ponto (sem quebra automática).
            beginTextBoxEdit(m_textDragP1, 0.0, kInvalidId);
        } else {
            // Caixa: inserção = 1ª linha, no topo-esquerda da caixa arrastada.
            const double h = m_tools.annotationHeight();
            beginTextBoxEdit(Point3{std::min(m_textDragP1.x, wx),
                                    std::max(m_textDragP1.y, wy) - h, 0.0},
                             dx, kInvalidId);
        }
        update();
        return;
    }
    // Operação de VISTA: encerra Janela (one-shot) ou solta o arrasto de Pan/tempo-real.
    if (m_viewDragging && e->button() == Qt::LeftButton) {
        m_viewDragging = false;
        if (m_viewOp == ViewOp::ZoomWindow && m_boxActive) {
            m_boxActive = false;
            const double minx = std::min(m_boxStartWX, m_boxCurWX);
            const double maxx = std::max(m_boxStartWX, m_boxCurWX);
            const double miny = std::min(m_boxStartWY, m_boxCurWY);
            const double maxy = std::max(m_boxStartWY, m_boxCurWY);
            if (maxx - minx > 1e-9 && maxy - miny > 1e-9) m_cam.fit(minx, miny, maxx, maxy);
            endViewOp();                       // Janela é one-shot: volta ao normal
        } else {
            if (m_viewOp == ViewOp::Pan) setCursor(Qt::OpenHandCursor);   // Pan/tempo-real seguem ativos
        }
        emitZoomPercent(); update();
        return;
    }
    // Soltou um grip arrastado: aplica a edição (entra no undo/redo).
    if (e->button() == Qt::LeftButton && m_gripDrag >= 0) {
        if (m_gripEntity != kInvalidId)
            m_doc->execute(std::make_unique<GripEditCmd>(
                m_gripEntity, m_gripDrag, Point3{m_curX, m_curY, 0.0}));
        m_gripDrag = -1;
        rebuild();
        updateGrips();
        emitPrompt();
        return;
    }
    if (e->button() != Qt::LeftButton || !m_boxActive) return;
    m_boxActive = false;

    const QPoint p = e->position().toPoint();
    const int dpix = std::abs(p.x() - m_boxStartScreen.x()) +
                     std::abs(p.y() - m_boxStartScreen.y());

    // Trim: a caixa serve para APARAR, não para selecionar.
    if (m_tools.tool() == ToolKind::Trim) {
        // Limiar generoso (10px): clique com leve tremida ainda conta como clique,
        // não como janela minúscula (que não pegaria a linha).
        auto pickTrimAtStart = [&] {
            Ray r; r.origin = Point3{m_boxStartWX, m_boxStartWY, 0.0};
            const EntityId id = m_doc->pick(r, snapTolWorld());
            if (id != kInvalidId) m_tools.trimClick(id, Point3{m_boxStartWX, m_boxStartWY, 0.0});
        };
        if (dpix < 10) {
            pickTrimAtStart();   // clique simples: apara o trecho clicado
        } else {
            // Janela de aparo: apara toda entidade cuja geometria cruza o retângulo.
            AABB box;
            box.expand(Point3{m_boxStartWX, m_boxStartWY, 0.0});
            box.expand(Point3{m_boxCurWX, m_boxCurWY, 0.0});
            bool any = false;
            for (const EntityId id : m_doc->query(box)) {
                const Entity* ent = m_doc->getEntity(id);
                if (!ent) continue;
                RenderBatch rb;
                ent->emitTo(rb);
                Point3 at{};
                const auto& lv = rb.lineVertices;
                for (std::size_t i = 0; i + 1 < lv.size(); i += 2)
                    if (clippedMidpointInBox(lv[i], lv[i + 1], box, at)) {
                        m_tools.trimClick(id, at);   // apara contra as outras entidades
                        any = true;
                        break;
                    }
            }
            if (!any) pickTrimAtStart();   // janela não pegou nada -> tenta clique
        }
        rebuild();
        update();
        emitPrompt();
        return;
    }

    if (dpix < 4) {
        // Arrasto desprezível => clique simples (pick por proximidade).
        m_tools.selectAt(Point3{m_boxStartWX, m_boxStartWY, 0.0}, snapTolWorld(), m_boxAdd);
    } else {
        // Sentido do arrasto define o modo: D->E (cur < start) = Crossing.
        const bool crossing = (m_boxCurWX < m_boxStartWX);
        AABB box;
        box.expand(Point3{m_boxStartWX, m_boxStartWY, 0.0});
        box.expand(Point3{m_boxCurWX, m_boxCurWY, 0.0});
        m_tools.selectInBox(box, crossing, m_boxAdd);
    }
    // Offset: ao escolher o objeto, já avança para o lado (não exige Enter).
    if (m_tools.tool() == ToolKind::Offset &&
        m_tools.phase() == ToolController::EditPhase::Selecting &&
        !m_tools.selection().empty()) {
        m_tools.confirmSelection();
        emit prompt("Offset: clique o lado para onde deslocar.");
    }
    updateGrips();
    update();
    emitPrompt();   // atualiza a contagem na fase Selecing
}

void ViewportWidget::wheelEvent(QWheelEvent* e) {
    const double factor = std::pow(1.0015, e->angleDelta().y());
    const QPointF p = e->position();
    // MSPACE: a roda dá zoom na VISTA do viewport ativo (mantendo o ponto do
    // modelo sob o cursor), não na folha.
    if (m_paperMode && m_mspaceIdx >= 0 && m_paperLayout &&
        m_mspaceIdx < int(m_paperLayout->viewports.size())) {
        double px, py; m_cam.screenToWorld(p.x(), p.y(), px, py);
        SheetViewport& v = m_paperLayout->viewports[std::size_t(m_mspaceIdx)];
        if (v.contains(px, py)) {
            if (v.locked) {                        // travado: a escala não mexe
                emit prompt("Viewport TRAVADO — destrave pelo botão direito para alterar a vista.");
                return;
            }
            const Point3 mp = v.toModel(px, py);   // ponto do modelo sob o cursor
            v.mmPerUnit *= factor;
            if (v.scaleDenom > 0.0) v.scaleDenom /= factor;   // rótulo 1:N acompanha
            v.modelCx = mp.x - (px - v.cxMm()) / v.mmPerUnit;
            v.modelCy = mp.y - (py - v.cyMm()) / v.mmPerUnit;
            update();
            return;
        }
    }
    m_cam.zoomAt(factor, p.x(), p.y());
    if (m_baseScale > 0.0)
        emit zoomChanged(static_cast<int>(std::lround(m_cam.scale() / m_baseScale * 100.0)));
    update();
}

void ViewportWidget::keyPressEvent(QKeyEvent* e) {
    const int k = e->key();

    const bool listOpen = m_cmdList && m_cmdList->isVisible() && m_cmdList->count() > 0;

    // Navegação na lista de autocomplete.
    if (listOpen && (k == Qt::Key_Down || k == Qt::Key_Up)) {
        int row = m_cmdList->currentRow() + (k == Qt::Key_Down ? 1 : -1);
        if (row < 0) row = m_cmdList->count() - 1;
        if (row >= m_cmdList->count()) row = 0;
        m_cmdList->setCurrentRow(row);
        return;
    }
    if (k == Qt::Key_Tab && listOpen && m_cmdList->currentItem()) {
        m_dynInput = m_cmdList->currentItem()->text();   // completa para a sugestão
        updateAutocomplete();
        update();
        return;
    }
    if (k == Qt::Key_Tab) {
        bool isNum = false; m_dynInput.toDouble(&isNum);
        // Retângulo (e variantes): LARGURA -> Tab -> ALTURA. Vira "@L,A" (canto
        // RELATIVO ao 1º canto) — a linha de comando trata "x,y" com vírgula como
        // canto ABSOLUTO (estilo AutoCAD), então dimensões usam o prefixo '@'.
        if (isNum && !m_dynInput.contains(',') && m_tools.wantsDimensions()) {
            m_dynInput = "@" + m_dynInput + ",";
            emit prompt("Dynamic input: digite a ALTURA + Enter.");
            update();
            return;
        }
        // Dynamic input estilo AutoCAD: digitou a DISTÂNCIA -> Tab passa para o
        // campo ÂNGULO (vira entrada polar @dist<ang, resolvida no Enter).
        Point3 ref;
        if (isNum && !m_dynInput.startsWith('@') && m_tools.referencePoint(ref)) {
            m_dynInput = "@" + m_dynInput + "<";          // próximo dígito = ângulo
            emit prompt("Dynamic input: digite o ÂNGULO (graus) + Enter (Tab volta).");
            update();
            return;
        }
        if (m_dynInput.startsWith('@') && m_dynInput.contains('<')) {   // Tab de volta p/ distância
            m_dynInput = m_dynInput.mid(1, m_dynInput.indexOf('<') - 1);
            emit prompt("Dynamic input: digite a DISTÂNCIA (Tab = ângulo).");
            update();
            return;
        }
    }

    if (k == Qt::Key_Escape) {
        if (m_paperVpCreate) { cancelPaperViewport(); return; }  // cancela criação de viewport
        if (m_textDragActive) { m_textDragActive = false; update(); return; }  // caixa de texto
        if (m_selPolyActive) {                                   // cancela WP/CP
            m_selPolyActive = false;
            m_selPolyPts.clear();
            emit prompt("Polígono de seleção cancelado.");
            update();
            return;
        }
        if (m_ucsMode != 0) {                                    // cancela UCS por cliques
            m_ucsMode = 0;
            emit prompt("UCS cancelado.");
            update();
            return;
        }
        if (m_m2pActive) {                                       // cancela M2P
            m_m2pActive = false;
            m_m2pHasFirst = false;
            emit prompt("M2P cancelado.");
            return;
        }
        if (m_paperMode && m_mspaceIdx >= 0) {                   // MSPACE -> papel
            m_mspaceIdx = -1;
            emit prompt("PSPACE: de volta ao papel.");
            update();
            return;
        }
        if (m_paperMode && m_paperVpSel >= 0) {                  // desmarca o viewport
            m_paperVpSel = -1;
            update();
            return;
        }
        if (m_viewOp != ViewOp::None) { endViewOp(); return; }   // sai do modo de vista (Pan/zoom)
        if (!m_dynInput.isEmpty()) { m_dynInput.clear(); updateAutocomplete(); update(); return; }
        if (!m_areaPts.empty()) { m_areaPts.clear(); emitPrompt(); update(); return; }   // cancela AREA
        if (m_fenceMode) { m_fenceMode = false; m_fencePts.clear(); emitPrompt(); update(); return; }
        // Cancela uma janela de seleção em curso (não deixa o retângulo na tela).
        if (m_boxActive) { m_boxActive = false; update(); return; }
        // Esc 1: cancela a operação em curso (pontos pendentes / grip em arrasto),
        // mantendo a ferramenta armada para um novo traço.
        if (m_tools.hasPending() || m_tools.strokeInProgress() || m_gripDrag >= 0) {
            m_tools.cancel();
            m_gripDrag = -1;
            m_trackPts.clear();
            m_trackGuides.clear();
            emitPrompt();
            update();
            return;
        }
        // Esc 2 (nada em curso): volta para Selecionar e limpa seleção/grips.
        m_tools.clearSelection();
        m_trackPts.clear();
        m_trackGuides.clear();
        m_gripDrag = -1;
        if (m_tools.tool() != ToolKind::None) setTool(ToolKind::None);
        updateGrips();
        emitPrompt();
        update();
        return;
    }
    if (k == Qt::Key_Backspace) {
        if (!m_dynInput.isEmpty()) { m_dynInput.chop(1); updateAutocomplete(); update(); }
        return;
    }
    if (k == Qt::Key_Delete) {
        // Modo papel: Delete exclui o viewport selecionado na prancha.
        if (m_paperMode) {
            if (m_paperLayout && m_paperVpSel >= 0 &&
                m_paperVpSel < int(m_paperLayout->viewports.size())) {
                m_paperLayout->viewports.erase(
                    m_paperLayout->viewports.begin() + m_paperVpSel);
                m_paperVpSel = -1; m_mspaceIdx = -1;
                emit prompt("Viewport excluído.");
                update();
            }
            return;
        }
        // PEDIT: vértice de polilinha sob o cursor -> remove só o vértice (não a entidade).
        if (m_gripEntity != kInvalidId) {
            const int gi = gripUnderCursor(m_curX, m_curY);
            if (gi >= 0)
                if (const Entity* e = m_doc->getEntity(m_gripEntity))
                    if (auto neu = withVertexRemoved(*e, gi)) {
                        m_doc->execute(std::make_unique<ReplaceCmd>(m_gripEntity, std::move(neu)));
                        updateGrips(); rebuild();
                        emit prompt("Vértice removido.");
                        return;
                    }
        }
        eraseSelected();
        return;
    }
    // PEDIT: 'I' arma a inserção de vértice na polilinha selecionada (próximo clique).
    if (k == Qt::Key_I && m_dynInput.isEmpty() && m_gripEntity != kInvalidId) {
        m_peditInsert = true;
        emit prompt("PEDIT: clique sobre a polilinha para inserir um vértice.");
        return;
    }

    if (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space) {
        // Polígono de seleção (WP/CP): Enter fecha e seleciona.
        if (m_selPolyActive && m_dynInput.isEmpty()) {
            m_selPolyActive = false;
            if (m_selPolyPts.size() >= 3) {
                m_tools.selectInPolygon(m_selPolyPts, m_selPolyCrossing, false);
                emit prompt(QString("%1 entidade(s) selecionada(s).")
                                .arg(m_tools.selection().size()));
            } else {
                emit prompt("Polígono de seleção precisa de 3+ vértices — cancelado.");
            }
            m_selPolyPts.clear();
            afterSelectionChange();
            return;
        }
        // Comando selecionado na lista tem prioridade; senão o texto digitado.
        QString cmd;
        if (listOpen && m_cmdList->currentItem()) cmd = m_cmdList->currentItem()->text();
        else if (!m_dynInput.isEmpty())           cmd = m_dynInput;

        if (!cmd.isEmpty()) {
            m_dynInput.clear();
            updateAutocomplete();
            emit commandEntered(cmd);
            update();
        } else if (m_tools.tool() == ToolKind::Area && m_areaPts.size() >= 3) {
            // AREA: Enter fecha o contorno e reporta o resultado final.
            emit prompt(QString("AREA = %1 u²  |  Perímetro = %2  (%3 pontos)")
                            .arg(polygonArea(m_areaPts), 0, 'f', 2)
                            .arg(polygonPerimeter(m_areaPts, true), 0, 'f', 2)
                            .arg(m_areaPts.size()));
            m_areaPts.clear();
            update();
        } else if (m_fenceMode) {
            applyFence();             // aplica Trim/Extend por cerca
            rebuild();
            emitPrompt();
            update();
        } else if (m_tools.editLoopActive()) {
            m_tools.cancel();         // encerra o Copy múltiplo
            rebuild();
            emitPrompt();
            update();
        } else if (m_tools.strokeInProgress() || m_tools.mleaderPending()) {
            // Polilinha/Spline concluem; no Multileader, cada Enter acumula uma
            // chamada e o Enter "vazio" (sem traço) conclui com todas.
            m_tools.finishStroke();
            rebuild();
            emitPrompt();
        } else if (m_tools.selectingObjects() && !m_tools.selection().empty()) {
            m_tools.confirmSelection();   // confirma a seleção (Verb-Noun)
            emitPrompt();
            update();
        } else if (m_tools.tool() == ToolKind::None && m_lastTool != ToolKind::None) {
            setTool(m_lastTool);          // Enter/Espaço ocioso = repete último comando
        }
        return;
    }

    // Fence (F): durante Trim/Extend, liga o modo cerca (clica pontos, Enter aplica).
    if (k == Qt::Key_F && m_dynInput.isEmpty() && !m_fenceMode &&
        (m_tools.tool() == ToolKind::Trim || m_tools.tool() == ToolKind::Extend)) {
        m_fenceMode = true; m_fencePts.clear();
        emit prompt("Fence: clique pontos cruzando o que aparar/estender; Enter aplica, Esc cancela.");
        update();
        return;
    }

    // Opções da Polilinha: A=arco, L=linha, C=fechar — SÓ durante o traço em
    // andamento. Com a polilinha ociosa (armada, sem traço), a letra cai no
    // autocomplete/comando (ex.: "A"+Enter troca para a ferramenta Arco), em vez
    // de virar modo-arco da PLINE (era o bug do "arco preso" ao encadear comandos).
    if (m_tools.tool() == ToolKind::Polyline && m_dynInput.isEmpty() && m_tools.strokeInProgress()) {
        if (k == Qt::Key_A) { m_tools.setPolyArc(true);  emitPrompt(); update(); return; }
        if (k == Qt::Key_L) { m_tools.setPolyArc(false); emitPrompt(); update(); return; }
        if (k == Qt::Key_C) {
            m_tools.finishStroke(/*closed=*/true);
            rebuild();
            emitPrompt();
            return;
        }
    }
    // PAREDE: C fecha o anel (o eixo volta ao 1º ponto com esquadria no canto).
    if (m_tools.tool() == ToolKind::WallTool && m_dynInput.isEmpty() &&
        m_tools.strokeInProgress() && k == Qt::Key_C) {
        m_tools.finishStroke(/*closed=*/true);
        rebuild();
        emitPrompt();
        return;
    }

    // Entrada dinâmica no canvas: letras (comando, com autocomplete) e
    // dígitos/pontuação de coordenada.
    const QString t = e->text();
    if (t.size() == 1) {
        const QChar ch = t.at(0);
        if (ch.isLetterOrNumber() || ch == '.' || ch == ',' || ch == '-' ||
            ch == '@' || ch == '<') {
            m_dynInput += ch.toUpper();
            updateAutocomplete();
            update();
            return;
        }
    }
    QOpenGLWidget::keyPressEvent(e);
}

void ViewportWidget::updateAutocomplete() {
    if (!m_cmdList) return;
    // Modo comando: o buffer começa com letra -> sugere comandos por prefixo.
    if (m_dynInput.isEmpty() || !m_dynInput.at(0).isLetter()) { m_cmdList->hide(); return; }

    const QString pref = m_dynInput.toUpper();
    m_cmdList->clear();
    // Ordem por relevância p/ troca rápida de comando (estilo AutoCAD):
    //   1) ALIAS EXATO (digitou "M" -> MOVE vem primeiro, mesmo havendo MLINE);
    //   2) nome canônico que começa com o prefixo;
    //   3) qualquer alias que começa com o prefixo.
    QStringList exact, byName, byAlias;
    for (const CommandDef& d : commandTable()) {
        bool isExact = false, aliasPrefix = false;
        for (const QString& a : d.aliases) {
            if (a == pref) isExact = true;
            else if (a.startsWith(pref)) aliasPrefix = true;
        }
        if (isExact)                       exact   << d.canonical;
        else if (d.canonical.startsWith(pref)) byName  << d.canonical;
        else if (aliasPrefix)              byAlias << d.canonical;
    }
    // Ações especiais (tratadas no runCommand, FORA da CommandTable) COM seus
    // aliases, para que o alias EXATO tenha prioridade — senão "X" cairia em
    // XLINE, "E" em ELLIPSE/EXTEND, etc.
    struct ActionAlias { const char* name; const char* alias; };
    static const ActionAlias kActions[] = {
        {"EXPLODE", "X"}, {"ERASE", "E"}, {"UNDO", "U"}, {"SELECT", ""}, {"REDO", ""},
        {"QSELECT", "QSE"}, {"LTSCALE", "LTS"}, {"ATTEDIT", "ATE"},
        {"ZOOM", "Z"}, {"ZE", ""}, {"ZP", ""}, {"PAN", "P"},
        {"WP", ""}, {"CP", ""}, {"ALL", ""}, {"PREVIOUS", "PRE"}, {"LAST", ""},
        {"GROUP", "G"}, {"UNGROUP", ""}, {"FIND", ""},
        {"PURGE", "PU"}, {"QDIM", ""}, {"DIMJOG", "DJO"}, {"M2P", "MTP"},
        {"UM", ""}, {"UB", ""}, {"OOPS", ""},
        {"OVERKILL", ""}, {"REVERSE", "REV"}, {"BLEND", "BLE"}, {"PUBLISH", "PUB"},
        {"PREVIEW", ""}, {"PLOTSTYLE", "CTB"}, {"ANNOSCALE", "ESCANOT"},
        {"XREF", "XR"}, {"BVIS", "VISBLOCO"}, {"UCS", ""}
    };
    for (const ActionAlias& a : kActions) {
        const QString name(a.name);
        const QString alias(a.alias);
        if (!alias.isEmpty() && alias == pref)               exact   << name;
        else if (name.startsWith(pref))                      byName  << name;
        else if (!alias.isEmpty() && alias.startsWith(pref)) byAlias << name;
    }
    for (const QString& s : exact)   m_cmdList->addItem(s);
    for (const QString& s : byName)  if (!exact.contains(s)) m_cmdList->addItem(s);
    for (const QString& s : byAlias) if (!exact.contains(s) && !byName.contains(s)) m_cmdList->addItem(s);
    if (m_cmdList->count() == 0) { m_cmdList->hide(); return; }

    m_cmdList->setCurrentRow(0);
    const int rows = std::min(m_cmdList->count(), 8);
    m_cmdList->resize(150, rows * 20 + 4);
    m_cmdList->move(m_cursorScreen.x() + 16, m_cursorScreen.y() + 18);
    m_cmdList->show();
    m_cmdList->raise();
}

void ViewportWidget::afterSelectionChange() {
    updateGrips();
    emit selectionChanged();
    update();
}

void ViewportWidget::eraseSelected() {
    if (m_tools.eraseSelected()) rebuild();
}

void ViewportWidget::explodeSelected() {
    if (m_tools.explodeSelected()) rebuild();
}

void ViewportWidget::arrayRectangular(int rows, int cols, double dx, double dy) {
    if (m_tools.arrayRectangular(rows, cols, dx, dy)) rebuild();
}

void ViewportWidget::arrayPolar(int count, double totalAngleDeg) {
    if (m_tools.arrayPolar(count, totalAngleDeg * 3.14159265358979323846 / 180.0)) rebuild();
}

// ---------------------------------------------------------------------------
// Caixa de texto IN-PLACE (MTEXT estilo caixa de texto): o editor é um
// QPlainTextEdit sobreposto ao canvas, na posição/na fonte/no tamanho do texto.
// Ctrl+Enter ou clicar fora confirma; Esc cancela.
// ---------------------------------------------------------------------------
void ViewportWidget::beginTextBoxEdit(const Point3& insert, double boxWidthWorld,
                                      EntityId editId) {
    if (!m_textBox) {
        m_textBox = new QPlainTextEdit(this);
        m_textBox->setObjectName("inplaceText");
        m_textBox->setStyleSheet(
            "QPlainTextEdit#inplaceText{background:rgba(13,15,18,235); color:#e8eaed;"
            " border:1px solid #c2a063; border-radius:3px; padding:2px;}");
        m_textBox->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textBox->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textBox->installEventFilter(this);
        m_textBox->hide();
    }
    m_textEditId = editId;
    m_textInsert = insert;
    double height = m_tools.annotationHeight();
    QString family = QString::fromStdString(m_tools.annotationFont());
    bool bold = m_tools.annotationBold(), italic = m_tools.annotationItalic();
    QString content;
    if (editId != kInvalidId) {
        if (const auto* mt = dynamic_cast<const MText*>(m_doc->getEntity(editId))) {
            height  = mt->height();
            family  = QString::fromStdString(mt->font());
            bold    = mt->bold();
            italic  = mt->italic();
            content = QString::fromStdString(mt->text());
            m_textBoxW = mt->boxWidth();
        } else {
            m_textEditId = kInvalidId;   // seleção mudou por baixo: cria novo
            m_textBoxW = boxWidthWorld;
        }
    } else {
        m_textBoxW = boxWidthWorld;
    }

    // Fonte do editor ~ fonte/altura reais do texto na tela.
    const double scale = std::max(m_cam.scale(), 1e-9);
    QFont f(family.isEmpty() ? QStringLiteral("Consolas") : family);
    f.setPixelSize(std::clamp(int(std::lround(height * scale)), 8, 220));
    f.setBold(bold);
    f.setItalic(italic);
    m_textBox->setFont(f);
    m_textBox->setPlainText(content);

    // Topo-esquerda do editor = topo da 1ª linha (inserção + altura).
    double sx, sy;
    m_cam.worldToScreen(m_textInsert.x, m_textInsert.y + height, sx, sy);
    const int wPx = (m_textBoxW > 0.0)
        ? std::max(120, int(std::lround(m_textBoxW * scale)) + 12)
        : 340;
    const int hPx = std::max(int(f.pixelSize() * 1.5) * 3 + 10, 60);
    m_textBox->setGeometry(int(sx) - 4, int(sy) - 4, wPx, hPx);
    m_textBox->show();
    m_textBox->raise();
    m_textBox->setFocus();
    auto cur = m_textBox->textCursor();
    cur.movePosition(QTextCursor::End);
    m_textBox->setTextCursor(cur);
    emit prompt("Texto: digite na caixa — Ctrl+Enter (ou clicar fora) confirma, Esc cancela.");
}

void ViewportWidget::commitTextEdit() {
    if (!m_textBox || !m_textBox->isVisible() || m_textCommitting) return;
    m_textCommitting = true;
    const QString s = m_textBox->toPlainText();
    const EntityId id = m_textEditId;
    const Point3 ins = m_textInsert;
    const double bw = m_textBoxW;
    m_textBox->hide();
    m_textEditId = kInvalidId;
    if (id == kInvalidId) {
        if (!s.trimmed().isEmpty()) {
            m_tools.addText(ins, s.toStdString(), bw);
            rebuild();
            emit prompt("Texto criado.");
        }
    } else if (const auto* mt = dynamic_cast<const MText*>(m_doc->getEntity(id))) {
        if (!s.trimmed().isEmpty() && s.toStdString() != mt->text()) {
            auto neu = std::make_unique<MText>(mt->position(), s.toStdString(),
                                               mt->height(), mt->rotation());
            neu->setJustify(mt->justify());
            neu->setFont(mt->font());
            neu->setBold(mt->bold());
            neu->setItalic(mt->italic());
            neu->setBoxWidth(mt->boxWidth());
            neu->setLayer(mt->layer());        neu->setColor(mt->color());
            neu->setLineType(mt->lineType());  neu->setLineWeight(mt->lineWeight());
            m_doc->execute(std::make_unique<ReplaceCmd>(id, std::move(neu)));
            rebuild();
            emit prompt("Texto atualizado.");
        }
    }
    m_textCommitting = false;
    setFocus();
    update();
}

void ViewportWidget::cancelTextEdit() {
    if (!m_textBox || !m_textBox->isVisible()) return;
    m_textCommitting = true;    // o hide dispara focusOut; não confirmar
    m_textBox->hide();
    m_textEditId = kInvalidId;
    m_textCommitting = false;
    setFocus();
    emit prompt("Edição de texto cancelada.");
    update();
}

bool ViewportWidget::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_textBox) {
        if (ev->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if (ke->key() == Qt::Key_Escape) { cancelTextEdit(); return true; }
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
                (ke->modifiers() & Qt::ControlModifier)) {
                commitTextEdit();
                return true;
            }
        } else if (ev->type() == QEvent::FocusOut) {
            commitTextEdit();   // clicar fora = confirmar (estilo PowerPoint)
        }
    }
    return QOpenGLWidget::eventFilter(obj, ev);
}

} // namespace cad
