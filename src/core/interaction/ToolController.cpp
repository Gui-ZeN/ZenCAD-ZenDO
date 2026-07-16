// src/core/interaction/ToolController.cpp
#include "core/interaction/ToolController.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "core/document/DrawingManager.hpp"
#include "core/document/Layer.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/Wipeout.hpp"
#include "core/geometry/Region.hpp"
#include "core/geometry/Table.hpp"
#include "core/geometry/MText.hpp"
#include "core/geometry/Dimension.hpp"
#include "core/geometry/Hatch.hpp"
#include "core/geometry/Ellipse.hpp"
#include "core/geometry/PointEntity.hpp"
#include "core/geometry/Spline.hpp"
#include "core/geometry/XLine.hpp"
#include "core/geometry/RayLine.hpp"
#include "core/geometry/MLine.hpp"
#include "core/geometry/Leader.hpp"
#include "core/geometry/MLeader.hpp"
#include "core/edit/ReviseCloud.hpp"
#include "core/geometry/Entity.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/math/Matrix4.hpp"
#include "core/math/Ray.hpp"
#include "core/edit/ConstructOps.hpp"
#include "core/command/commands/AddEntityCmd.hpp"
#include "core/command/commands/MoveCmd.hpp"
#include "core/command/commands/CopyCmd.hpp"
#include "core/command/commands/EraseCmd.hpp"
#include "core/command/commands/TransformCmd.hpp"
#include "core/command/commands/MirrorCmd.hpp"
#include "core/command/commands/OffsetCmd.hpp"
#include "core/command/commands/TrimCmd.hpp"
#include "core/command/commands/FilletCmd.hpp"
#include "core/command/commands/ChamferCmd.hpp"
#include "core/command/commands/ExtendCmd.hpp"
#include "core/command/commands/ExplodeCmd.hpp"
#include "core/command/commands/ArrayCmd.hpp"
#include "core/command/commands/MacroCmd.hpp"
#include "core/edit/ArrayPathOps.hpp"
#include "core/command/commands/MakeBlockCmd.hpp"
#include "core/command/commands/StretchCmd.hpp"
#include "core/command/commands/ReplaceCmd.hpp"
#include "core/command/commands/TransformCopyCmd.hpp"
#include "core/command/commands/BreakCmd.hpp"
#include "core/command/commands/JoinCmd.hpp"
#include "core/edit/ModifyOps.hpp"
#include "core/edit/TrimOps.hpp"
#include "core/edit/BoundaryTrace.hpp"
#include "core/edit/LoopExtract.hpp"
#include "core/edit/IntersectOps.hpp"
#include "core/edit/GeometryOps.hpp"
#include "core/geometry/Wall.hpp"
#include "core/edit/DivideOps.hpp"
#include "core/edit/CleanupOps.hpp"
#include "core/edit/TangentOps.hpp"
#include "core/edit/Tangent3Ops.hpp"
#include "core/math/Constants.hpp"

namespace cad {
namespace {

bool isEditTool(ToolKind t) {
    return t == ToolKind::Move || t == ToolKind::Copy || t == ToolKind::Rotate ||
           t == ToolKind::Scale || t == ToolKind::Mirror || t == ToolKind::Offset ||
           t == ToolKind::Stretch || t == ToolKind::Block || t == ToolKind::Align;
}

// Transformação de similaridade (translada+rotaciona+escala uniforme) que leva
// s1->d1 e s2->d2. Usada pelo comando ALIGN.
Matrix4 alignMatrix(const Point3& s1, const Point3& d1,
                    const Point3& s2, const Point3& d2) {
    const double sx = s2.x - s1.x, sy = s2.y - s1.y, slen = std::hypot(sx, sy);
    const double dx = d2.x - d1.x, dy = d2.y - d1.y, dlen = std::hypot(dx, dy);
    const double k   = slen > 1e-9 ? dlen / slen : 1.0;
    const double ang = std::atan2(dy, dx) - std::atan2(sy, sx);
    // P -> d1 + k*R(ang)*(P - s1)
    return Matrix4::translation(d1) * Matrix4::rotationZ(ang) *
           Matrix4::scale(Vec3{k, k, 1.0}) * Matrix4::translation(Vec3{-s1.x, -s1.y, 0.0});
}

Matrix4 rotAbout(const Point3& b, double a) {
    return Matrix4::translation(b) * Matrix4::rotationZ(a) *
           Matrix4::translation(Vec3{-b.x, -b.y, -b.z});
}
Matrix4 scaleAbout(const Point3& b, double s) {
    return Matrix4::translation(b) * Matrix4::scale(Vec3{s, s, 1.0}) *
           Matrix4::translation(Vec3{-b.x, -b.y, -b.z});
}
Matrix4 reflectLine(const Point3& p1, const Point3& p2) {
    const double th = std::atan2(p2.y - p1.y, p2.x - p1.x);
    const double c2 = std::cos(2 * th), s2 = std::sin(2 * th);
    Matrix4 R = Matrix4::identity();
    R.m[0] = c2; R.m[1] = s2; R.m[4] = s2; R.m[5] = -c2;
    return Matrix4::translation(p1) * R * Matrix4::translation(Vec3{-p1.x, -p1.y, 0.0});
}

// --- Interseção segmento × retângulo (no plano XY) -----------------------
int orient(const Point3& a, const Point3& b, const Point3& c) {
    const double v = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (v > 1e-12) return 1;
    if (v < -1e-12) return -1;
    return 0;
}
bool onSeg(const Point3& a, const Point3& b, const Point3& c) {
    return std::min(a.x, b.x) - 1e-9 <= c.x && c.x <= std::max(a.x, b.x) + 1e-9 &&
           std::min(a.y, b.y) - 1e-9 <= c.y && c.y <= std::max(a.y, b.y) + 1e-9;
}
bool segSeg(const Point3& p1, const Point3& p2, const Point3& p3, const Point3& p4) {
    const int o1 = orient(p1, p2, p3), o2 = orient(p1, p2, p4);
    const int o3 = orient(p3, p4, p1), o4 = orient(p3, p4, p2);
    if (o1 != o2 && o3 != o4) return true;
    if (o1 == 0 && onSeg(p1, p2, p3)) return true;
    if (o2 == 0 && onSeg(p1, p2, p4)) return true;
    if (o3 == 0 && onSeg(p3, p4, p1)) return true;
    if (o4 == 0 && onSeg(p3, p4, p2)) return true;
    return false;
}
bool segIntersectsRect(const Point3& a, const Point3& b, const AABB& box) {
    if (box.contains(a) || box.contains(b)) return true;
    const Point3 c0{box.min.x, box.min.y, 0.0}, c1{box.max.x, box.min.y, 0.0},
                 c2{box.max.x, box.max.y, 0.0}, c3{box.min.x, box.max.y, 0.0};
    return segSeg(a, b, c0, c1) || segSeg(a, b, c1, c2) ||
           segSeg(a, b, c2, c3) || segSeg(a, b, c3, c0);
}

} // namespace

void ToolController::setTool(ToolKind k) {
    m_tool = k;
    m_pending.clear();
    m_trimCutId = kInvalidId;
    m_filletFirstId = kInvalidId;
    m_chamferFirstId = kInvalidId;
    m_ttrFirstId = kInvalidId;
    m_tttIds.clear();
    m_breakId = kInvalidId;
    m_breakHasP1 = false;
    m_joinFirstId = kInvalidId;
    m_matchSrcId = kInvalidId;
    m_polyBulges.clear();
    m_polyArc = false;
    m_mleaderLeaders.clear();
    m_extendBoundId = kInvalidId;
    m_dimHasCircle = false;
    m_dimSrcId = kInvalidId;
    m_nextAnchor = DimAnchor{};
    m_dimAnch[0] = DimAnchor{}; m_dimAnch[1] = DimAnchor{};
    if (isEditTool(k))
        m_phase = m_selection.empty() ? EditPhase::Selecting : EditPhase::Base;  // Verb/Noun
    else
        m_phase = EditPhase::Idle;
}

void ToolController::cancel() {
    m_pending.clear();
    m_doorStage = 0;  m_doorWallId = kInvalidId;   // porta-na-parede
    m_winHas = false; m_winWallId = kInvalidId;    // janela
    m_trimCutId = kInvalidId;
    m_filletFirstId = kInvalidId;
    m_chamferFirstId = kInvalidId;
    m_ttrFirstId = kInvalidId;
    m_tttIds.clear();
    m_breakId = kInvalidId;
    m_breakHasP1 = false;
    m_joinFirstId = kInvalidId;
    m_matchSrcId = kInvalidId;
    m_polyBulges.clear();
    m_polyArc = false;
    m_mleaderLeaders.clear();
    m_extendBoundId = kInvalidId;
    if (isEditTool(m_tool)) m_phase = EditPhase::Selecting;
    m_dimHasCircle = false;
    m_dimSrcId = kInvalidId;
    m_nextAnchor = DimAnchor{};
    m_dimAnch[0] = DimAnchor{}; m_dimAnch[1] = DimAnchor{};
}

void ToolController::dimCirclePick(EntityId id) {
    const Entity* e = m_doc.getEntity(id);
    if (const auto* c = dynamic_cast<const Circle*>(e)) {
        m_dimCenter = c->center(); m_dimRadius = c->radius();
    } else if (const auto* a = dynamic_cast<const Arc*>(e)) {
        m_dimCenter = a->center(); m_dimRadius = a->radius();
    } else {
        return;   // clicou fora de um círculo/arco: ignora
    }
    m_dimHasCircle = true;
    m_dimSrcId = id;                    // associatividade: a cota segue a entidade
    m_pending.clear();
    m_pending.push_back(m_dimCenter);   // p1 = centro real da entidade
}

// Deriva a âncora (entidade + QUAL ponto) a partir do OSNAP que grudou o
// clique: Endpoint numa Line vira Start/End (o extremo mais próximo), Center
// num círculo vira Center, vértice de polilinha vira Vertex+índice, etc.
void ToolController::setNextPointAnchor(EntityId id, int snapType, const Point3& pt) {
    m_nextAnchor = DimAnchor{};
    if (id == kInvalidId) return;
    const Entity* e = m_doc.getEntity(id);
    if (!e) return;
    const auto st = static_cast<SnapType>(snapType);
    using W = DimAnchor::Which;
    auto dist = [&](const Point3& a) { return std::hypot(a.x - pt.x, a.y - pt.y); };
    if (const auto* l = dynamic_cast<const Line*>(e)) {
        if (st == SnapType::Endpoint)
            m_nextAnchor = DimAnchor{id, dist(l->start()) <= dist(l->end())
                                             ? W::Start : W::End, 0};
    } else if (const auto* c = dynamic_cast<const Circle*>(e)) {
        if (st == SnapType::Center)        m_nextAnchor = DimAnchor{id, W::Center, 0};
        else if (st == SnapType::Quadrant || st == SnapType::Nearest)
            m_nextAnchor = DimAnchor{id, W::OnCurve, 0};
        (void)c;
    } else if (const auto* ar = dynamic_cast<const Arc*>(e)) {
        if (st == SnapType::Center) {
            m_nextAnchor = DimAnchor{id, W::Center, 0};
        } else if (st == SnapType::Endpoint) {
            const Point3 s{ar->center().x + ar->radius() * std::cos(ar->startAngle()),
                           ar->center().y + ar->radius() * std::sin(ar->startAngle()), 0.0};
            const Point3 en{ar->center().x + ar->radius() * std::cos(ar->endAngle()),
                            ar->center().y + ar->radius() * std::sin(ar->endAngle()), 0.0};
            m_nextAnchor = DimAnchor{id, dist(s) <= dist(en) ? W::Start : W::End, 0};
        }
    } else if (const auto* pl = dynamic_cast<const Polyline*>(e)) {
        if (st == SnapType::Endpoint && !pl->vertices().empty()) {
            int best = 0;
            double bd = dist(pl->vertices()[0]);
            for (std::size_t i = 1; i < pl->vertices().size(); ++i) {
                const double d = dist(pl->vertices()[i]);
                if (d < bd) { bd = d; best = static_cast<int>(i); }
            }
            m_nextAnchor = DimAnchor{id, W::Vertex, best};
        }
    } else if (dynamic_cast<const PointEntity*>(e)) {
        if (st == SnapType::Node) m_nextAnchor = DimAnchor{id, W::Node, 0};
    }
}

// Retângulo por 2 cantos ALINHADO AO UCS: com o frame girado, os cantos são
// interpretados no UCS e o resultado volta ao mundo (polilinha rotacionada).
Polyline ToolController::ucsRectFrom2(const Point3& a, const Point3& b) const {
    if (!m_doc.ucsActive()) return rectangleFrom2Points(a, b);
    const Polyline r = rectangleFrom2Points(m_doc.worldToUcs(a), m_doc.worldToUcs(b));
    std::vector<Point3> v;
    v.reserve(r.vertices().size());
    for (const Point3& p : r.vertices()) v.push_back(m_doc.ucsToWorld(p));
    return Polyline(std::move(v), r.closed());
}

void ToolController::emitNew(std::unique_ptr<Entity> e) {
    e->setLayer(m_currentLayer);
    e->setColor(m_curColor);                       // props correntes (ByLayer por padrão)
    e->setLineType(LineType{m_curLineType});
    e->setLineWeight(LineWeight{m_curLineWeight});
    if (auto* d = dynamic_cast<Dimension*>(e.get())) {   // aplica o estilo de cota corrente
        d->setDecimals(m_dimDecimals);
        d->setSuffix(m_dimSuffix);
        d->setArrowSize(m_dimArrow);
        d->setArrowType(static_cast<Dimension::ArrowType>(m_dimArrowType));
        d->setFont(m_annotFont);   // texto da cota na fonte do estilo de texto
        d->setTolerance(m_dimTolPlus, m_dimTolMinus);
        if (d->kind() == Dimension::DimKind::Radius && m_dimJogged) {
            d->setJogged(true);
            m_dimJogged = false;   // one-shot: só a cota armada pelo DIMJOG
        }
        // Estilo ANOTATIVO: os valores do estilo estão em MM DE PAPEL —
        // converte p/ unidades de modelo pela escala de anotação corrente.
        if (m_dimAnnotative) {
            const double k = m_doc.annoMmPerUnit();
            d->setAnnotative(true);
            d->setTextHeight(d->textHeight() / k);
            if (d->arrowSize() > 0.0) d->setArrowSize(d->arrowSize() / k);
        }
    } else if (auto* t = dynamic_cast<MText*>(e.get())) {
        if (m_textAnnotative) {    // idem para TEXTO anotativo
            t->setAnnotative(true);
            t->setHeight(t->height() / m_doc.annoMmPerUnit());
        }
    }
    m_doc.execute(std::make_unique<AddEntityCmd>(std::move(e)));
}

void ToolController::trimClick(EntityId picked, const Point3& at) {
    // Modelo AutoCAD moderno: TODAS as outras entidades são arestas de corte.
    // Um clique apara a entidade escolhida no trecho clicado, contra tudo o que
    // a cruza — sem precisar escolher a aresta de corte antes.
    if (picked == kInvalidId) return;
    const Entity* tgt = m_doc.getEntity(picked);
    if (!tgt) return;
    std::vector<Point3> cuts;
    m_doc.forEach([&](const Entity& other) {
        if (other.id() == picked) return;
        const std::vector<Point3> xs = intersectEntities(*tgt, other);
        cuts.insert(cuts.end(), xs.begin(), xs.end());
    });
    if (cuts.empty()) return;   // nada cruza o alvo: inerte
    m_doc.execute(std::make_unique<TrimCmd>(picked, std::move(cuts), at));
}

void ToolController::filletClick(EntityId picked, const Point3& at) {
    if (picked == kInvalidId) return;
    if (m_filletFirstId == kInvalidId) {
        m_filletFirstId = picked;        // 1ª entidade
        m_filletFirstPick = at;          // guarda o lado clicado
    } else {
        m_doc.execute(std::make_unique<FilletCmd>(
            m_filletFirstId, picked, m_filletRadius, m_filletFirstPick, at));
        m_filletFirstId = kInvalidId;
    }
}

bool ToolController::onValue(double v) {
    if (m_tool == ToolKind::ArcSER && m_pending.size() == 2) {
        const Arc3Result r = arcStartEndRadius(m_pending[0], m_pending[1], v);
        m_pending.clear();
        if (r.ok) { emitNew(std::make_unique<Arc>(r.arc)); return true; }
    } else if (m_tool == ToolKind::ArcSEA && m_pending.size() == 2) {
        const Arc3Result r = arcStartEndAngle(m_pending[0], m_pending[1], v * kPi / 180.0);
        m_pending.clear();
        if (r.ok) { emitNew(std::make_unique<Arc>(r.arc)); return true; }
    } else if (m_tool == ToolKind::Circle && m_pending.size() == 1) {
        const Point3 c = m_pending[0];
        m_pending.clear();
        if (v > 0.0) { emitNew(std::make_unique<Circle>(c, v)); return true; }   // raio digitado
    } else if (m_tool == ToolKind::Polygon && m_pending.size() == 1) {
        const Point3 c = m_pending[0];
        m_pending.clear();
        if (v > 0.0 && m_polygonSides >= 3) {
            const double rr = m_polygonInscribed ? v : v / std::cos(kPi / m_polygonSides);
            emitNew(std::make_unique<Polyline>(regularPolygon(c, m_polygonSides, rr, 0.0)));
            return true;
        }
    } else if (m_tool == ToolKind::Ellipse && m_pending.size() == 2) {
        const Point3 c = m_pending[0];
        const Vec3   major{m_pending[1].x - c.x, m_pending[1].y - c.y, 0.0};
        m_pending.clear();
        if (std::hypot(major.x, major.y) > 1e-9 && v > 0.0) {   // v = meio-eixo menor
            emitNew(std::make_unique<Ellipse>(Ellipse::fromCenterAxes(c, major, v)));
            return true;
        }
    }
    return false;
}

void ToolController::ttrClick(EntityId picked, const Point3& at) {
    if (picked == kInvalidId) return;
    if (m_ttrFirstId == kInvalidId) { m_ttrFirstId = picked; return; }  // 1ª entidade
    const Entity* a = m_doc.getEntity(m_ttrFirstId);
    const Entity* b = m_doc.getEntity(picked);
    if (a && b) {
        const std::optional<Circle> c = circleTanTanRadius(*a, *b, m_ttrRadius, at);
        if (c) emitNew(std::make_unique<Circle>(*c));
    }
    m_ttrFirstId = kInvalidId;
}

void ToolController::tttClick(EntityId picked, const Point3& at) {
    if (picked == kInvalidId) return;
    m_tttIds.push_back(picked);
    if (m_tttIds.size() < 3) return;        // junta 3 entidades
    const Entity* a = m_doc.getEntity(m_tttIds[0]);
    const Entity* b = m_doc.getEntity(m_tttIds[1]);
    const Entity* c = m_doc.getEntity(m_tttIds[2]);
    if (a && b && c) {
        const std::optional<Circle> circ = circleTanTanTan(*a, *b, *c, at);
        if (circ) emitNew(std::make_unique<Circle>(*circ));
    }
    m_tttIds.clear();
}

void ToolController::matchPropsClick(EntityId picked) {
    if (picked == kInvalidId) return;
    if (m_matchSrcId == kInvalidId) { m_matchSrcId = picked; return; }   // 1ª: fonte
    if (picked == m_matchSrcId) return;
    const Entity* src = m_doc.getEntity(m_matchSrcId);
    const Entity* tgt = m_doc.getEntity(picked);
    if (!src || !tgt) return;
    EntityPtr neu = tgt->clone();                       // copia cor/camada/tipo/espessura
    neu->setColor(src->color());
    neu->setLayer(src->layer());
    neu->setLineType(src->lineType());
    neu->setLineWeight(src->lineWeight());
    m_doc.execute(std::make_unique<ReplaceCmd>(picked, std::move(neu)));
    // mantém a fonte ativa: permite pintar vários alvos em sequência
}

void ToolController::breakClick(EntityId picked, const Point3& at) {
    if (m_breakId == kInvalidId) { if (picked != kInvalidId) m_breakId = picked; return; }  // 1ª: linha
    if (!m_breakHasP1) { m_breakP1 = at; m_breakHasP1 = true; return; }                       // 2ª: 1º ponto
    if (const Entity* e = m_doc.getEntity(m_breakId)) {                                       // 3ª: 2º ponto
        const Point3 mid{(m_breakP1.x + at.x) * 0.5, (m_breakP1.y + at.y) * 0.5, 0.0};        // trecho removido
        std::vector<EntityPtr> parts = splitEntityAt(*e, {m_breakP1, at}, mid);               // Line/Circle/Arc
        for (auto& p : parts) {                                                               // herda props
            p->setLayer(e->layer()); p->setColor(e->color());
            p->setLineType(e->lineType()); p->setLineWeight(e->lineWeight());
        }
        if (!parts.empty()) m_doc.execute(std::make_unique<BreakCmd>(m_breakId, std::move(parts)));
    }
    m_breakId = kInvalidId; m_breakHasP1 = false;
}

void ToolController::joinClick(EntityId picked, const Point3&) {
    if (picked == kInvalidId) return;
    if (m_joinFirstId == kInvalidId) { m_joinFirstId = picked; return; }
    if (picked != m_joinFirstId) {
        const Entity* a = m_doc.getEntity(m_joinFirstId);
        const Entity* b = m_doc.getEntity(picked);
        if (a && b) {
            EntityPtr joined = joinEntities(*a, *b);   // colineares -> Line; tocando -> Polyline
            if (joined) {
                joined->setLayer(a->layer());
                joined->setColor(a->color());
                joined->setLineType(a->lineType());
                joined->setLineWeight(a->lineWeight());
                m_doc.execute(std::make_unique<JoinCmd>(m_joinFirstId, picked, std::move(joined)));
            }
        }
    }
    m_joinFirstId = kInvalidId;
}

void ToolController::lengthenClick(EntityId picked, const Point3& at) {
    const Entity* e = m_doc.getEntity(picked);
    if (const auto* l = dynamic_cast<const Line*>(e)) {
        const double de = std::hypot(at.x - l->end().x, at.y - l->end().y);
        const double ds = std::hypot(at.x - l->start().x, at.y - l->start().y);
        const Line nl = lengthenLine(*l, m_lengthenDelta, /*fromEnd=*/de <= ds);
        m_doc.execute(std::make_unique<ReplaceCmd>(picked, std::make_unique<Line>(nl)));
    } else if (const auto* ar = dynamic_cast<const Arc*>(e)) {
        const Point3 c = ar->center(); const double r = ar->radius();
        const Point3 ps{c.x + r * std::cos(ar->startAngle()), c.y + r * std::sin(ar->startAngle()), 0.0};
        const Point3 pe{c.x + r * std::cos(ar->endAngle()),   c.y + r * std::sin(ar->endAngle()),   0.0};
        const bool fromEnd = std::hypot(at.x - pe.x, at.y - pe.y) <= std::hypot(at.x - ps.x, at.y - ps.y);
        const Arc na = lengthenArc(*ar, m_lengthenDelta, fromEnd);
        m_doc.execute(std::make_unique<ReplaceCmd>(picked, std::make_unique<Arc>(na)));
    }
}

void ToolController::divideClick(EntityId picked) {
    const Entity* e = m_doc.getEntity(picked);
    if (!e) return;
    placeDivideMarks(divideMarks(*e, m_divideN));
}

void ToolController::measureClick(EntityId picked) {
    const Entity* e = m_doc.getEntity(picked);
    if (!e) return;
    placeDivideMarks(measureMarks(*e, m_measureSpacing));
}

// DIVIDE/MEASURE: cada marca vira um PointEntity ou, se houver bloco
// configurado, uma inserção (girada pela tangente local quando align).
void ToolController::placeDivideMarks(const std::vector<DivMark>& marks) {
    const BlockDefinition* def = m_divideBlockName.empty()
                               ? nullptr : m_doc.blocks().find(m_divideBlockName);
    for (const DivMark& m : marks) {
        if (def) {
            const Matrix4 x = Matrix4::translation(Vec3{m.p.x, m.p.y, 0.0})
                            * Matrix4::rotationZ(m_divideAlign ? m.angleRad : 0.0);
            emitNew(BlockRef::fromDefinition(*def, x));
        } else {
            emitNew(std::make_unique<PointEntity>(m.p));
        }
    }
}

void ToolController::chamferClick(EntityId picked, const Point3& at) {
    (void)at;
    if (picked == kInvalidId) return;
    if (m_chamferFirstId == kInvalidId) {
        m_chamferFirstId = picked;       // 1ª linha
    } else {
        m_doc.execute(std::make_unique<ChamferCmd>(
            m_chamferFirstId, picked, m_chamferDist, m_chamferDist));
        m_chamferFirstId = kInvalidId;
    }
}

void ToolController::extendClick(EntityId picked, const Point3& at) {
    if (picked == kInvalidId) return;
    if (m_extendBoundId == kInvalidId) {
        m_extendBoundId = picked;        // 1º clique: contorno (limite)
    } else {
        m_doc.execute(std::make_unique<ExtendCmd>(picked, m_extendBoundId, at));  // alvos
    }
}

bool ToolController::explodeSelected() {
    if (m_selection.empty()) return false;
    for (const EntityId id : m_selection)
        m_doc.execute(std::make_unique<ExplodeCmd>(id));
    m_selection.clear();
    return true;
}

bool ToolController::arrayRectangular(int rows, int cols, double dx, double dy) {
    if (m_selection.empty() || rows < 1 || cols < 1) return false;
    m_doc.execute(std::make_unique<ArrayCmd>(
        ArrayCmd::rectangular(m_selection, rows, cols, dx, dy)));
    return true;
}

namespace {
// Pontos ordenados de uma entidade (contorno fechado p/ booleanas OU caminho p/
// array): polilinha -> sampledPoints; demais -> extrai do outline tesselado.
// Remove o ponto de fechamento duplicado quando o contorno é fechado.
std::vector<Point3> orderedPointsOf(const Entity& e) {
    std::vector<Point3> v;
    if (const auto* pl = dynamic_cast<const Polyline*>(&e)) {
        v = pl->sampledPoints();
    } else {
        RenderBatch b; e.emitTo(b);
        const auto& lv = b.lineVertices;
        if (lv.size() >= 2) {
            v.push_back(lv[0]);
            for (std::size_t i = 1; i < lv.size(); i += 2) v.push_back(lv[i]);
        }
    }
    if (v.size() >= 2) {
        const Point3 a = v.front(), z = v.back();
        if (std::hypot(z.x - a.x, z.y - a.y) < 1e-6) v.pop_back();
    }
    return v;
}
double shoelaceArea(const std::vector<Point3>& p) {
    double a = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Point3& u = p[i]; const Point3& v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return std::fabs(a) * 0.5;
}
} // namespace

bool ToolController::booleanSelected(BoolOp op) {
    if (m_selection.size() != 2) return false;
    const Entity* ea = m_doc.getEntity(m_selection[0]);
    const Entity* eb = m_doc.getEntity(m_selection[1]);
    if (!ea || !eb) return false;
    const std::vector<Point3> A = orderedPointsOf(*ea);
    const std::vector<Point3> B = orderedPointsOf(*eb);
    if (A.size() < 3 || B.size() < 3) return false;
    const auto result = polygonBoolean(A, B, op);
    if (result.empty()) return false;
    auto macro = std::make_unique<MacroCmd>("BOOLEAN");
    macro->add(std::make_unique<EraseCmd>(m_selection));
    for (const auto& loop : result)
        if (loop.size() >= 3)
            macro->add(std::make_unique<AddEntityCmd>(std::make_unique<Polyline>(loop, true)));
    m_doc.execute(std::move(macro));
    m_selection.clear();
    return true;
}

bool ToolController::regionFromSelection() {
    if (m_selection.empty()) return false;
    std::vector<std::vector<Point3>> loops;
    for (const EntityId id : m_selection) {
        const Entity* e = m_doc.getEntity(id);
        if (!e) continue;
        std::vector<Point3> loop = orderedPointsOf(*e);
        if (loop.size() >= 3) loops.push_back(std::move(loop));
    }
    if (loops.empty()) return false;
    std::sort(loops.begin(), loops.end(), [](const auto& a, const auto& b) {
        return shoelaceArea(a) > shoelaceArea(b); });   // maior = externo; demais = furos
    auto macro = std::make_unique<MacroCmd>("REGION");
    macro->add(std::make_unique<EraseCmd>(m_selection));
    macro->add(std::make_unique<AddEntityCmd>(std::make_unique<Region>(std::move(loops))));
    m_doc.execute(std::move(macro));
    m_selection.clear();
    return true;
}

void ToolController::beginArrayPath(int count, bool align) {
    m_arrayPathSrc   = m_selection;        // fontes = seleção corrente
    m_arrayPathCount = count;
    m_arrayPathAlign = align;
    m_tool  = ToolKind::ArrayPath;
    m_phase = EditPhase::Idle;
    m_pending.clear();
    m_selection.clear();
}

bool ToolController::arrayPathClick(EntityId pathId) {
    const Entity* pe = m_doc.getEntity(pathId);
    if (!pe || m_arrayPathSrc.empty()) return false;
    const std::vector<Point3> path = orderedPointsOf(*pe);
    if (path.size() < 2) return false;
    auto macro = std::make_unique<MacroCmd>("ARRAYPATH");
    bool any = false;
    for (const EntityId sid : m_arrayPathSrc) {
        const Entity* se = m_doc.getEntity(sid);
        if (!se) continue;
        for (auto& copy : arrayAlongPath(*se, path, m_arrayPathCount, m_arrayPathAlign)) {
            macro->add(std::make_unique<AddEntityCmd>(std::move(copy)));
            any = true;
        }
    }
    if (any) m_doc.execute(std::move(macro));
    m_arrayPathSrc.clear();
    return any;
}

bool ToolController::arrayPolar(int count, double totalAngleRad) {
    if (m_selection.empty() || count < 2) return false;
    AABB bb;
    for (const EntityId id : m_selection)
        if (const Entity* e = m_doc.getEntity(id)) bb.expand(e->boundingBox());
    const Point3 center = bb.valid() ? bb.center() : Point3{};
    m_doc.execute(std::make_unique<ArrayCmd>(
        ArrayCmd::polar(m_selection, center, count, totalAngleRad)));
    return true;
}

bool ToolController::selectingObjects() const {
    return m_tool == ToolKind::None ||
           (isEditTool(m_tool) && m_phase == EditPhase::Selecting);
}

void ToolController::confirmSelection() {
    if (isEditTool(m_tool) && m_phase == EditPhase::Selecting && !m_selection.empty())
        m_phase = EditPhase::Base;
}

bool ToolController::onPoint(const Point3& p) {
    // --- Edição ---
    if (isEditTool(m_tool)) {
        // Offset: após a seleção, um ÚNICO ponto-lado define distância e direção.
        if (m_tool == ToolKind::Offset) {
            if (m_phase == EditPhase::Base) {
                for (const EntityId id : m_selection)
                    if (const Entity* e = m_doc.getEntity(id)) {
                        Ray r; r.origin = p;
                        // Distância digitada (m_offsetDist>0) OU a do clique; o ponto
                        // p sempre define o LADO. O clique fica só p/ a direção.
                        const double dist = m_offsetDist > 0.0
                                          ? m_offsetDist : e->hitTest(r, 1e12).distance;
                        m_doc.execute(std::make_unique<OffsetCmd>(id, dist, p));
                    }
                m_selection.clear();
                m_phase = EditPhase::Selecting;
                return true;
            }
            return false;
        }
        // Align: 4 pontos (origem1->destino1, origem2->destino2) -> similaridade.
        if (m_tool == ToolKind::Align) {
            if (m_phase == EditPhase::Base) {
                m_pending.push_back(p);
                if (m_pending.size() == 4) {
                    const Matrix4 M  = alignMatrix(m_pending[0], m_pending[1],
                                                   m_pending[2], m_pending[3]);
                    const Matrix4 Mi = alignMatrix(m_pending[1], m_pending[0],
                                                   m_pending[3], m_pending[2]);   // inversa
                    m_doc.execute(std::make_unique<TransformCmd>(m_selection, M, Mi));
                    m_pending.clear();
                    m_selection.clear();
                    m_phase = EditPhase::Selecting;
                    return true;
                }
                return false;
            }
            return false;
        }
        // Block: após a seleção, um único ponto define o ponto-base do bloco.
        if (m_tool == ToolKind::Block) {
            if (m_phase == EditPhase::Base) {
                m_doc.execute(std::make_unique<MakeBlockCmd>(m_selection, p, m_pendingBlockName));
                m_selection.clear();
                m_phase = EditPhase::Selecting;
                return true;
            }
            return false;
        }
        // Demais edições: ponto-base e destino (fases Base/Target).
        if (m_phase == EditPhase::Base) {
            m_pending = {p};
            m_phase = EditPhase::Target;
            return false;
        }
        if (m_phase == EditPhase::Target) {
            // Referência (Rotate/Scale): o 2º clique é o ponto de REFERÊNCIA; o
            // 3º (destino) define o ângulo/comprimento final.
            if (m_editRef && (m_tool == ToolKind::Rotate || m_tool == ToolKind::Scale) &&
                m_pending.size() == 1) {
                m_pending.push_back(p);
                return false;
            }
            commitEdit(m_pending[0], p);
            if (m_tool == ToolKind::Copy) {
                // Copy múltiplo: mantém o ponto-base e a seleção; cada clique
                // seguinte cola outra cópia (deslocamento sempre a partir do base).
                return true;
            }
            m_pending.clear();
            m_selection.clear();
            m_phase = EditPhase::Selecting;
            return true;
        }
        return false;  // fase Selecting: cliques selecionam (não chegam aqui)
    }

    // --- Criação ---
    if (m_tool == ToolKind::None) return false;
    if (m_tool == ToolKind::Point) {           // ponto: 1 clique cria o nó
        emitNew(std::make_unique<PointEntity>(p));
        return true;
    }
    if (m_tool == ToolKind::TableTool) {       // tabela: 1 clique = canto superior-esquerdo
        emitNew(std::make_unique<Table>(p, m_tableRows, m_tableCols, m_tableCW, m_tableRH));
        return true;
    }
    if (m_tool == ToolKind::Insert) {          // insere um bloco da biblioteca no clique
        if (const BlockDefinition* def = m_doc.blocks().find(m_insertName)) {
            const double rad = m_insertRot * 0.017453292519943295;   // graus -> rad
            const Matrix4 x = Matrix4::translation(Vec3{p.x, p.y, 0.0})
                            * Matrix4::rotationZ(rad)
                            * Matrix4::scale(Vec3{m_insertScale, m_insertScale, 1.0});
            auto ins = BlockRef::fromDefinition(*def, x);
            for (const auto& kv : m_insertValues)         // valores respondidos no diálogo
                ins->setAttValue(kv.first, kv.second);
            emitNew(std::move(ins));
        }
        return true;
    }
    if (m_tool == ToolKind::AttDefTool) {      // ATTDEF: 1 clique posiciona o atributo
        emitNew(std::make_unique<AttDef>(p, m_attdefTag, m_attdefPrompt,
                                         m_attdefDefault, m_annotHeight));
        return true;
    }
    m_pending.push_back(p);   // Polyline/Spline acumulam até finishStroke()

    // Cotas ASSOCIATIVAS: os 2 primeiros pontos de Linear/Alinhada guardam a
    // âncora sugerida pelo OSNAP do clique (setNextPointAnchor). A dica vale
    // só para este ponto — consumida (ou descartada) aqui.
    if ((m_tool == ToolKind::DimLinear || m_tool == ToolKind::DimAligned) &&
        m_pending.size() <= 2)
        m_dimAnch[m_pending.size() - 1] = m_nextAnchor;
    m_nextAnchor = DimAnchor{};

    // Polilinha: calcula o bulge do trecho recém-criado (arco tangente se modo Arco).
    if (m_tool == ToolKind::Polyline && m_pending.size() >= 2) {
        double bulge = 0.0;
        if (m_polyArc && m_pending.size() >= 3) {
            const Point3 prev2 = m_pending[m_pending.size() - 3];
            const Point3 s = m_pending[m_pending.size() - 2];
            const Point3 e = m_pending[m_pending.size() - 1];
            double tx = s.x - prev2.x, ty = s.y - prev2.y;   // tangente = trecho anterior
            double cx = e.x - s.x, cy = e.y - s.y;            // corda do novo trecho
            const double tl = std::hypot(tx, ty), cl = std::hypot(cx, cy);
            if (tl > 1e-9 && cl > 1e-9) {
                tx /= tl; ty /= tl; cx /= cl; cy /= cl;
                const double phi = std::atan2(cx * ty - cy * tx, cx * tx + cy * ty);
                bulge = std::tan(phi * 0.5);   // ângulo corda-tangente = sweep/2
            }
        }
        m_polyBulges.push_back(bulge);
    }

    if (m_tool == ToolKind::Line && m_pending.size() == 2) {
        emitNew(std::make_unique<Line>(m_pending[0], m_pending[1]));
        const Point3 last = m_pending[1];
        m_pending.clear();
        m_pending.push_back(last);   // encadeia
        return true;
    }
    if (m_tool == ToolKind::Circle && m_pending.size() == 2) {
        const Point3  c = m_pending[0];
        const double  r = std::hypot(p.x - c.x, p.y - c.y);
        m_pending.clear();
        if (r > 0.0) {
            emitNew(std::make_unique<Circle>(c, r));
            return true;
        }
    }
    if (m_tool == ToolKind::XLine && m_pending.size() == 2) {
        emitNew(std::make_unique<XLine>(XLine::fromTwoPoints(m_pending[0], m_pending[1])));
        m_pending.clear();
        return true;
    }
    if (m_tool == ToolKind::Ray && m_pending.size() == 2) {
        emitNew(std::make_unique<RayLine>(RayLine::fromTwoPoints(m_pending[0], m_pending[1])));
        m_pending.clear();
        return true;
    }
    if (m_tool == ToolKind::RevCloud && m_pending.size() == 2) {
        emitNew(std::make_unique<Polyline>(
            revisionCloudRect(m_pending[0], m_pending[1], m_revCloudRadius)));
        m_pending.clear();
        return true;
    }
    if (m_tool == ToolKind::Rectangle && m_pending.size() == 2) {
        emitNew(std::make_unique<Polyline>(ucsRectFrom2(m_pending[0], m_pending[1])));
        m_pending.clear();
        return true;
    }
    if (m_tool == ToolKind::Arc3 && m_pending.size() == 3) {
        const Arc3Result a = arcFrom3Points(m_pending[0], m_pending[1], m_pending[2]);
        m_pending.clear();
        if (a.ok) {
            emitNew(std::make_unique<Arc>(a.arc));
            return true;
        }
    }
    if (m_tool == ToolKind::Ellipse && m_pending.size() == 3) {
        const Point3 c = m_pending[0];
        const Vec3   major{m_pending[1].x - c.x, m_pending[1].y - c.y, 0.0};
        const double a = std::hypot(major.x, major.y);
        const Point3 p3 = m_pending[2];
        m_pending.clear();
        if (a > 1e-9) {
            const Vec3 perp{-major.y / a, major.x / a, 0.0};
            const double minorLen = std::abs((p3.x - c.x) * perp.x + (p3.y - c.y) * perp.y);
            if (minorLen > 1e-9)
                emitNew(std::make_unique<Ellipse>(Ellipse::fromCenterAxes(c, major, minorLen)));
            return true;
        }
    }
    if (m_tool == ToolKind::EllipseArc && m_pending.size() == 5) {
        const Point3 c = m_pending[0];
        const Vec3   major{m_pending[1].x - c.x, m_pending[1].y - c.y, 0.0};
        const double a = std::hypot(major.x, major.y);
        const Point3 p2 = m_pending[2], p3 = m_pending[3], p4 = m_pending[4];
        m_pending.clear();
        if (a > 1e-9) {
            const Vec3 perp{-major.y / a, major.x / a, 0.0};
            const double minorLen = std::abs((p2.x - c.x) * perp.x + (p2.y - c.y) * perp.y);
            if (minorLen > 1e-9) {
                auto paramOf = [&](const Point3& P) {
                    const double u = ((P.x - c.x) * major.x + (P.y - c.y) * major.y) / (a * a);
                    const double v = ((P.x - c.x) * perp.x + (P.y - c.y) * perp.y) / minorLen;
                    return std::atan2(v, u);
                };
                emitNew(std::make_unique<Ellipse>(
                    Ellipse::fromCenterAxesArc(c, major, minorLen, paramOf(p3), paramOf(p4))));
            }
        }
        return true;
    }
    if (m_tool == ToolKind::Circle2P && m_pending.size() == 2) {
        emitNew(std::make_unique<Circle>(circle2Points(m_pending[0], m_pending[1])));
        m_pending.clear();
        return true;
    }
    if (m_tool == ToolKind::Circle3P && m_pending.size() == 3) {
        bool ok = false;
        const Circle c = circle3Points(m_pending[0], m_pending[1], m_pending[2], ok);
        m_pending.clear();
        if (ok) { emitNew(std::make_unique<Circle>(c)); return true; }
    }
    if (m_tool == ToolKind::ArcSCE && m_pending.size() == 3) {
        const Point3 s = m_pending[0], ctr = m_pending[1], e = m_pending[2];
        const double r  = std::hypot(s.x - ctr.x, s.y - ctr.y);
        const double a0 = std::atan2(s.y - ctr.y, s.x - ctr.x);
        const double a1 = std::atan2(e.y - ctr.y, e.x - ctr.x);
        m_pending.clear();
        if (r > 1e-9) { emitNew(std::make_unique<Arc>(ctr, r, a0, a1)); return true; }
    }
    if (m_tool == ToolKind::ArcCSE && m_pending.size() == 3) {
        const Point3 ctr = m_pending[0], s = m_pending[1], e = m_pending[2];
        const double r = std::hypot(s.x - ctr.x, s.y - ctr.y);
        m_pending.clear();
        if (r > 1e-9) {
            emitNew(std::make_unique<Arc>(ctr, r, std::atan2(s.y - ctr.y, s.x - ctr.x),
                                          std::atan2(e.y - ctr.y, e.x - ctr.x)));
            return true;
        }
    }
    if (m_tool == ToolKind::ArcSED && m_pending.size() == 3) {
        const Arc3Result r = arcStartEndDirection(m_pending[0], m_pending[1], m_pending[2]);
        m_pending.clear();
        if (r.ok) { emitNew(std::make_unique<Arc>(r.arc)); return true; }
    }
    // SER/SEA: raio/ângulo digitado (onValue) OU — clicando um 3º ponto — o
    // arco passa por ele (fallback de 3 pontos; antes o 3º clique TRAVAVA a
    // ferramenta acumulando pontos mortos).
    if ((m_tool == ToolKind::ArcSER || m_tool == ToolKind::ArcSEA) &&
        m_pending.size() == 3) {
        const Arc3Result r = arcFrom3Points(m_pending[0], m_pending[2], m_pending[1]);
        m_pending.clear();
        if (r.ok) { emitNew(std::make_unique<Arc>(r.arc)); return true; }
    }
    // PORTA: dobradiça -> outro batente -> lado de abertura. Cria a FOLHA
    // (linha da dobradiça, aberta a 90°) + o ARCO de giro (batente -> ponta).
    if (m_tool == ToolKind::Door && m_pending.size() == 3) {
        const Point3 h = m_pending[0], b = m_pending[1], lado = m_pending[2];
        m_pending.clear();
        const double vx = b.x - h.x, vy = b.y - h.y;
        const double r = std::hypot(vx, vy);
        if (r > 1e-9) {
            // Perpendicular ao vão apontando para o LADO clicado.
            const double cross = vx * (lado.y - h.y) - vy * (lado.x - h.x);
            const double s = (cross >= 0.0) ? 1.0 : -1.0;
            const Point3 tip{h.x - vy * s, h.y + vx * s, 0.0};   // dobradiça + perp(vão)·lado
            const double aB = std::atan2(vy, vx);
            const double aT = std::atan2(tip.y - h.y, tip.x - h.x);
            auto folha = std::make_unique<Line>(h, tip);
            // Arco sempre CCW: escolhe a ordem que dá o QUARTO de volta certo.
            auto giro = (s > 0.0) ? std::make_unique<Arc>(h, r, aB, aT)
                                  : std::make_unique<Arc>(h, r, aT, aB);
            auto aplica = [&](Entity& e) {
                e.setLayer(m_currentLayer);
                e.setColor(m_curColor);
                e.setLineType(LineType{m_curLineType});
                e.setLineWeight(LineWeight{m_curLineWeight});
            };
            aplica(*folha); aplica(*giro);
            auto macro = std::make_unique<MacroCmd>("PORTA");   // 1 undo só
            macro->add(std::make_unique<AddEntityCmd>(std::move(folha)));
            macro->add(std::make_unique<AddEntityCmd>(std::move(giro)));
            m_doc.execute(std::move(macro));
        }
        return true;
    }
    if (m_tool == ToolKind::Polygon && m_pending.size() == 2) {
        const Point3 ctr = m_pending[0];
        const double r   = std::hypot(m_pending[1].x - ctr.x, m_pending[1].y - ctr.y);
        const double rot = std::atan2(m_pending[1].y - ctr.y, m_pending[1].x - ctr.x);
        m_pending.clear();
        if (r > 1e-9 && m_polygonSides >= 3) {
            const double rr = m_polygonInscribed ? r : r / std::cos(kPi / m_polygonSides);
            emitNew(std::make_unique<Polyline>(regularPolygon(ctr, m_polygonSides, rr, rot)));
            return true;
        }
    }
    if (m_tool == ToolKind::RectChamfer && m_pending.size() == 2) {
        emitNew(std::make_unique<Polyline>(
            rectangleChamfer(m_pending[0], m_pending[1], m_chamferDist)));
        m_pending.clear();
        return true;
    }
    if (m_tool == ToolKind::RectFillet && m_pending.size() == 2) {
        emitNew(std::make_unique<Polyline>(
            rectangleFillet(m_pending[0], m_pending[1], m_filletRadius)));
        m_pending.clear();
        return true;
    }

    // --- Cotas (dimensions) ---
    if ((m_tool == ToolKind::DimLinear || m_tool == ToolKind::DimAligned) &&
        m_pending.size() == 3) {
        Dimension d = (m_tool == ToolKind::DimLinear)
            ? Dimension::linear(m_pending[0], m_pending[1], m_pending[2], m_annotHeight)
            : Dimension::aligned(m_pending[0], m_pending[1], m_pending[2], m_annotHeight);
        d.setAnchors(m_dimAnch[0], m_dimAnch[1]);   // associativa se veio de OSNAP
        m_dimAnch[0] = DimAnchor{}; m_dimAnch[1] = DimAnchor{};
        if (m_tool == ToolKind::DimLinear) {   // memoriza p/ Contínua / Linha-base
            m_lastDimP1 = m_pending[0]; m_lastDimP2 = m_pending[1]; m_lastDimP3 = m_pending[2];
            m_hasLastDim = true;
        }
        emitNew(std::make_unique<Dimension>(d));
        m_pending.clear();
        return true;
    }
    if (m_tool == ToolKind::DimContinue && m_pending.size() == 1 && m_hasLastDim) {
        const Point3 np = m_pending[0];
        m_pending.clear();
        const Dimension prev = Dimension::linear(m_lastDimP1, m_lastDimP2, m_lastDimP3, m_annotHeight);
        const Dimension d = Dimension::continueLinear(prev, np, m_annotHeight);
        m_lastDimP1 = d.p1(); m_lastDimP2 = d.p2(); m_lastDimP3 = d.p3();  // encadeia
        emitNew(std::make_unique<Dimension>(d));
        return true;
    }
    if (m_tool == ToolKind::DimBaseline && m_pending.size() == 1 && m_hasLastDim) {
        const Point3 np = m_pending[0];
        m_pending.clear();
        const Dimension prev = Dimension::linear(m_lastDimP1, m_lastDimP2, m_lastDimP3, m_annotHeight);
        const Dimension d = Dimension::baselineLinear(prev, np, m_annotHeight, m_annotHeight * 3.0);
        m_lastDimP1 = d.p1(); m_lastDimP2 = d.p2(); m_lastDimP3 = d.p3();  // empilha
        emitNew(std::make_unique<Dimension>(d));
        return true;
    }
    if ((m_tool == ToolKind::DimRadius || m_tool == ToolKind::DimDiameter) &&
        m_pending.size() == 2) {
        Point3 p1 = m_pending[0], p2 = m_pending[1];
        if (m_dimHasCircle) {   // usa o RAIO REAL: projeta a direção do clique no círculo
            const double dx = p2.x - p1.x, dy = p2.y - p1.y, len = std::hypot(dx, dy);
            p2 = (len > 1e-9)
               ? Point3{p1.x + dx / len * m_dimRadius, p1.y + dy / len * m_dimRadius, p1.z}
               : Point3{p1.x + m_dimRadius, p1.y, p1.z};
        }
        Dimension d = (m_tool == ToolKind::DimRadius)
            ? Dimension::radius(p1, p2, m_annotHeight)
            : Dimension::diameter(p1, p2, m_annotHeight);
        if (m_dimSrcId != kInvalidId)   // segue o círculo/arco cotado
            d.setAnchors(DimAnchor{m_dimSrcId, DimAnchor::Which::Center, 0},
                         DimAnchor{m_dimSrcId, DimAnchor::Which::OnCurve, 0});
        emitNew(std::make_unique<Dimension>(d));
        m_pending.clear();
        m_dimHasCircle = false;   // pronto p/ cotar a próxima
        m_dimSrcId = kInvalidId;
        return true;
    }
    if (m_tool == ToolKind::DimOrdinate && m_pending.size() == 2) {
        emitNew(std::make_unique<Dimension>(
            Dimension::ordinate(m_pending[0], m_pending[1], m_annotHeight)));
        m_pending.clear();
        return true;
    }
    if (m_tool == ToolKind::DimAngular && m_pending.size() == 3) {
        emitNew(std::make_unique<Dimension>(
            Dimension::angular(m_pending[0], m_pending[1], m_pending[2], m_annotHeight)));
        m_pending.clear();
        return true;
    }
    return false;
}

int ToolController::qdim(const Point3& linePos) {
    if (m_selection.empty()) return 0;
    // Endpoints notáveis da seleção + bbox (decide o eixo pelo lado do clique).
    std::vector<Point3> pts;
    std::vector<SnapPoint> sps;
    AABB bb;
    for (const EntityId id : m_selection) {
        const Entity* e = m_doc.getEntity(id);
        if (!e) continue;
        sps.clear();
        e->appendSnapPoints(sps);
        for (const SnapPoint& sp : sps)
            if (sp.type == SnapType::Endpoint) {
                pts.push_back(sp.point);
                bb.expand(sp.point);
            }
    }
    if (pts.size() < 2) return 0;
    const bool horizontal = (linePos.y < bb.min.y || linePos.y > bb.max.y) ? true
                          : (linePos.x < bb.min.x || linePos.x > bb.max.x) ? false
                          : (bb.max.x - bb.min.x) >= (bb.max.y - bb.min.y);
    // Coordenadas únicas ao longo do eixo (tolerância curta) + ponto de cada.
    auto key = [&](const Point3& p) { return horizontal ? p.x : p.y; };
    std::sort(pts.begin(), pts.end(), [&](const Point3& a, const Point3& b) {
        return key(a) < key(b);
    });
    std::vector<Point3> uniq;
    for (const Point3& p : pts)
        if (uniq.empty() || std::abs(key(p) - key(uniq.back())) > 1e-6)
            uniq.push_back(p);
    if (uniq.size() < 2) return 0;
    // Cadeia de cotas LINEARES contínuas sobre a linha do clique.
    int n = 0;
    for (std::size_t i = 0; i + 1 < uniq.size(); ++i) {
        emitNew(std::make_unique<Dimension>(Dimension::linear(
            uniq[i], uniq[i + 1], linePos, m_annotHeight)));
        ++n;
    }
    return n;
}

void ToolController::addText(const Point3& pos, const std::string& text,
                             double boxWidth) {
    if (text.empty()) return;
    auto t = std::make_unique<MText>(pos, text, m_annotHeight);
    t->setFont(m_annotFont);       // fonte TTF do estilo de texto corrente
    t->setBold(m_annotBold);
    t->setItalic(m_annotItalic);
    t->setBoxWidth(boxWidth);      // >0 = caixa com quebra automática
    emitNew(std::move(t));
}

bool ToolController::finishStroke(bool closed) {
    bool created = false;
    if (m_tool == ToolKind::WallTool && m_pending.size() >= 2) {
        // PAREDE: os cliques são o EIXO; fechar (C) repete o 1º ponto para a
        // esquadria do canto final resolver no próprio vértice.
        std::vector<Point3> ax = m_pending;
        if (closed && ax.size() >= 3) ax.push_back(ax.front());
        emitNew(std::make_unique<Wall>(std::move(ax), m_wallThickness));
        created = true;
    } else if (m_tool == ToolKind::Polyline && m_pending.size() >= 2) {
        auto pl = std::make_unique<Polyline>(m_pending, m_polyBulges, closed);  // honra arcos
        pl->setWidth(m_polyWidth);                                              // e largura global
        emitNew(std::move(pl));
        created = true;
    } else if (m_tool == ToolKind::Spline && m_pending.size() >= 2) {
        emitNew(std::make_unique<Spline>(m_pending));
        created = true;
    } else if (m_tool == ToolKind::SplineCV && m_pending.size() >= 2) {
        emitNew(std::make_unique<Spline>(m_pending, /*cv=*/true));
        created = true;
    } else if (m_tool == ToolKind::MLine && m_pending.size() >= 2) {
        emitNew(std::make_unique<MLine>(m_pending, m_mlineWidth, closed));
        created = true;
    } else if (m_tool == ToolKind::Leader && m_pending.size() >= 2) {
        emitNew(std::make_unique<Leader>(m_pending, m_leaderText, m_annotHeight));
        created = true;
    } else if (m_tool == ToolKind::Wipeout && m_pending.size() >= 3) {
        emitNew(std::make_unique<Wipeout>(m_pending));   // máscara opaca pelo contorno
        created = true;
    } else if (m_tool == ToolKind::MLeaderTool) {
        // Multileader multi-chamada: cada traço (>=2 pts) ACUMULA uma chamada; o
        // Enter "vazio" (sem traço novo) conclui criando 1 MLeader com TODAS.
        if (m_pending.size() >= 2) {
            m_mleaderLeaders.push_back(m_pending);
            m_pending.clear();
            return false;                 // segue armado p/ outra chamada ou conclusão
        }
        if (!m_mleaderLeaders.empty()) {
            const Point3 textPos = m_mleaderLeaders.back().back();
            emitNew(std::make_unique<MLeader>(m_mleaderLeaders, textPos, m_mleaderText, m_annotHeight));
            m_mleaderLeaders.clear();
            created = true;
        }
    }
    m_pending.clear();
    m_polyBulges.clear();
    return created;
}

void ToolController::hatchPick(EntityId picked) {
    const Entity* e = m_doc.getEntity(picked);
    if (!e) return;
    // Contorno fechado extraído pelo helper compartilhado (o mesmo que o regen
    // associativo usa) — a hachura criada fica ANCORADA na entidade clicada.
    std::vector<Point3> bound = extractClosedLoop(*e);
    if (bound.size() >= 3) {
        std::vector<std::vector<Point3>> loops{std::move(bound)};
        emitHatchFrom(std::move(loops), {picked});
    }
}

void ToolController::hatchAtPoint(const Point3& inside) {
    // Clique numa área vazia: traça o contorno fechado que envolve o ponto a
    // partir das entidades vizinhas (à la BOUNDARY do AutoCAD) e hachura.
    // Sem âncora: o contorno é composto de várias entidades (não associativa).
    std::vector<Point3> bound = traceBoundary(m_doc, inside);
    if (bound.size() >= 3) {
        std::vector<std::vector<Point3>> loops{std::move(bound)};
        emitHatchFrom(std::move(loops), {});
    }
}

void ToolController::emitHatchFrom(std::vector<std::vector<Point3>> loops,
                                   std::vector<EntityId> srcIds) {
    auto h = std::make_unique<Hatch>(std::move(loops),
        static_cast<HatchPattern>(m_hatchPattern), m_hatchAngle, m_hatchScale);
    if (static_cast<HatchPattern>(m_hatchPattern) == HatchPattern::Gradient) {
        h->setGradientColor1(Rgba{std::uint8_t(m_grad1[0]), std::uint8_t(m_grad1[1]),
                                  std::uint8_t(m_grad1[2]), 255});
        h->setGradientColor2(Rgba{std::uint8_t(m_grad2[0]), std::uint8_t(m_grad2[1]),
                                  std::uint8_t(m_grad2[2]), 255});
    }
    h->setSrcIds(std::move(srcIds));   // hachura ASSOCIATIVA (sessão)
    emitNew(std::move(h));
}

void ToolController::commitEdit(const Point3& base, const Point3& target) {
    switch (m_tool) {
        case ToolKind::Move: {
            const Vec3 d{target.x - base.x, target.y - base.y, 0.0};
            m_doc.execute(std::make_unique<MoveCmd>(m_selection, d));
            break;
        }
        case ToolKind::Copy: {
            const Vec3 d{target.x - base.x, target.y - base.y, 0.0};
            m_doc.execute(std::make_unique<CopyCmd>(m_selection, d));
            break;
        }
        case ToolKind::Rotate: {
            double a = std::atan2(target.y - base.y, target.x - base.x);
            if (m_editRef && m_pending.size() >= 2)   // ângulo relativo à referência
                a -= std::atan2(m_pending[1].y - base.y, m_pending[1].x - base.x);
            if (m_editCopy)
                m_doc.execute(std::make_unique<TransformCopyCmd>(m_selection, rotAbout(base, a)));
            else
                m_doc.execute(std::make_unique<TransformCmd>(
                    m_selection, rotAbout(base, a), rotAbout(base, -a)));
            break;
        }
        case ToolKind::Scale: {
            double s = std::hypot(target.x - base.x, target.y - base.y);
            if (m_editRef && m_pending.size() >= 2) {  // fator = comprimento/referência
                const double refLen = std::hypot(m_pending[1].x - base.x, m_pending[1].y - base.y);
                if (refLen < 1e-9) return;
                s /= refLen;
            }
            if (s < 1e-9) return;
            if (m_editCopy)
                m_doc.execute(std::make_unique<TransformCopyCmd>(m_selection, scaleAbout(base, s)));
            else
                m_doc.execute(std::make_unique<TransformCmd>(
                    m_selection, scaleAbout(base, s), scaleAbout(base, 1.0 / s)));
            break;
        }
        case ToolKind::Mirror: {
            m_doc.execute(std::make_unique<MirrorCmd>(m_selection, reflectLine(base, target)));
            break;
        }
        case ToolKind::Stretch: {
            const Vec3 d{target.x - base.x, target.y - base.y, 0.0};
            m_doc.execute(std::make_unique<StretchCmd>(m_selection, m_lastBox, d));
            break;
        }
        default: break;
    }
}

Matrix4 ToolController::previewMatrix(const Point3& base, const Point3& cursor) const {
    switch (m_tool) {
        case ToolKind::Move:
        case ToolKind::Copy:
            return Matrix4::translation(Vec3{cursor.x - base.x, cursor.y - base.y, 0.0});
        case ToolKind::Rotate: {
            double a = std::atan2(cursor.y - base.y, cursor.x - base.x);
            if (m_editRef && m_pending.size() >= 2)
                a -= std::atan2(m_pending[1].y - base.y, m_pending[1].x - base.x);
            return rotAbout(base, a);
        }
        case ToolKind::Scale: {
            double s = std::hypot(cursor.x - base.x, cursor.y - base.y);
            if (m_editRef && m_pending.size() >= 2) {
                const double refLen = std::hypot(m_pending[1].x - base.x, m_pending[1].y - base.y);
                s = refLen < 1e-9 ? 1.0 : s / refLen;
            }
            if (s < 1e-9) s = 1e-9;
            return scaleAbout(base, s);
        }
        case ToolKind::Mirror:
            return reflectLine(base, cursor);
        default:
            return Matrix4::identity();
    }
}

void ToolController::buildPreview(const Point3& cursor, RenderBatch& out) const {
    if (m_tool == ToolKind::Line && !m_pending.empty()) {
        out.addSegment(m_pending[0], cursor);
    } else if (m_tool == ToolKind::XLine && !m_pending.empty()) {
        XLine::fromTwoPoints(m_pending[0], cursor).emitTo(out);
    } else if (m_tool == ToolKind::Ray && !m_pending.empty()) {
        RayLine::fromTwoPoints(m_pending[0], cursor).emitTo(out);
    } else if (m_tool == ToolKind::Polyline && !m_pending.empty()) {
        if (m_pending.size() >= 2)               // trechos já definidos (com arcos)
            Polyline(m_pending, m_polyBulges, false).emitTo(out);
        const Point3 s = m_pending.back();
        double bulge = 0.0;                       // trecho atual até o cursor
        if (m_polyArc && m_pending.size() >= 2) {
            const Point3 prev2 = m_pending[m_pending.size() - 2];
            double tx = s.x - prev2.x, ty = s.y - prev2.y;
            double cx = cursor.x - s.x, cy = cursor.y - s.y;
            const double tl = std::hypot(tx, ty), cl = std::hypot(cx, cy);
            if (tl > 1e-9 && cl > 1e-9) {
                tx /= tl; ty /= tl; cx /= cl; cy /= cl;
                bulge = std::tan(std::atan2(cx * ty - cy * tx, cx * tx + cy * ty) * 0.5);
            }
        }
        if (std::fabs(bulge) > 1e-9)
            Polyline(std::vector<Point3>{s, cursor}, std::vector<double>{bulge}, false).emitTo(out);
        else
            out.addSegment(s, cursor);
    } else if (m_tool == ToolKind::Spline && !m_pending.empty()) {
        std::vector<Point3> pts = m_pending;
        pts.push_back(cursor);
        if (pts.size() >= 2) Spline(pts).emitTo(out);
    } else if (m_tool == ToolKind::SplineCV && !m_pending.empty()) {
        std::vector<Point3> pts = m_pending;
        pts.push_back(cursor);
        if (pts.size() >= 2) Spline(pts, /*cv=*/true).emitTo(out);
    } else if (m_tool == ToolKind::MLine && !m_pending.empty()) {
        std::vector<Point3> pts = m_pending;
        pts.push_back(cursor);
        if (pts.size() >= 2) MLine(pts, m_mlineWidth, false).emitTo(out);
    } else if ((m_tool == ToolKind::Leader || m_tool == ToolKind::MLeaderTool) && !m_pending.empty()) {
        for (std::size_t i = 1; i < m_pending.size(); ++i)
            out.addSegment(m_pending[i - 1], m_pending[i]);
        out.addSegment(m_pending.back(), cursor);
    } else if (m_tool == ToolKind::RevCloud && m_pending.size() == 1) {
        revisionCloudRect(m_pending[0], cursor, m_revCloudRadius).emitTo(out);
    } else if (m_tool == ToolKind::Circle && !m_pending.empty()) {
        const double r = std::hypot(cursor.x - m_pending[0].x, cursor.y - m_pending[0].y);
        Circle(m_pending[0], r).emitTo(out);
        out.addSegment(m_pending[0], cursor);
    } else if (m_tool == ToolKind::Rectangle && !m_pending.empty()) {
        ucsRectFrom2(m_pending[0], cursor).emitTo(out);
    } else if (m_tool == ToolKind::Arc3 && m_pending.size() >= 2) {
        const Arc3Result a = arcFrom3Points(m_pending[0], m_pending[1], cursor);
        if (a.ok) a.arc.emitTo(out);
        else { out.addSegment(m_pending[0], m_pending[1]); out.addSegment(m_pending[1], cursor); }
    } else if (m_tool == ToolKind::Arc3 && m_pending.size() == 1) {
        out.addSegment(m_pending[0], cursor);
    } else if (m_tool == ToolKind::Ellipse && m_pending.size() == 2) {
        const Point3 c = m_pending[0];
        const Vec3   major{m_pending[1].x - c.x, m_pending[1].y - c.y, 0.0};
        const double a = std::hypot(major.x, major.y);
        if (a > 1e-9) {
            const Vec3 perp{-major.y / a, major.x / a, 0.0};
            const double minorLen = std::abs((cursor.x - c.x) * perp.x + (cursor.y - c.y) * perp.y);
            if (minorLen > 1e-9) Ellipse::fromCenterAxes(c, major, minorLen).emitTo(out);
        }
    } else if (m_tool == ToolKind::Ellipse && m_pending.size() == 1) {
        out.addSegment(m_pending[0], cursor);
    } else if (m_tool == ToolKind::EllipseArc && !m_pending.empty()) {
        const Point3 c = m_pending[0];
        if (m_pending.size() == 1) {
            out.addSegment(c, cursor);
        } else {
            const Vec3 major{m_pending[1].x - c.x, m_pending[1].y - c.y, 0.0};
            const double a = std::hypot(major.x, major.y);
            if (a > 1e-9) {
                const Vec3 perp{-major.y / a, major.x / a, 0.0};
                const Point3 mp = (m_pending.size() >= 3) ? m_pending[2] : cursor;
                const double minorLen = std::abs((mp.x - c.x) * perp.x + (mp.y - c.y) * perp.y);
                if (minorLen > 1e-9) {
                    if (m_pending.size() <= 3) {
                        Ellipse::fromCenterAxes(c, major, minorLen).emitTo(out);  // elipse cheia
                    } else {
                        auto paramOf = [&](const Point3& P) {
                            const double u = ((P.x - c.x) * major.x + (P.y - c.y) * major.y) / (a * a);
                            const double v = ((P.x - c.x) * perp.x + (P.y - c.y) * perp.y) / minorLen;
                            return std::atan2(v, u);
                        };
                        Ellipse::fromCenterAxesArc(c, major, minorLen,
                                                   paramOf(m_pending[3]), paramOf(cursor)).emitTo(out);
                    }
                }
            }
        }
    } else if (m_tool == ToolKind::Circle2P && m_pending.size() == 1) {
        circle2Points(m_pending[0], cursor).emitTo(out);
        out.addSegment(m_pending[0], cursor);
    } else if (m_tool == ToolKind::Circle3P && m_pending.size() == 2) {
        bool ok = false;
        const Circle c = circle3Points(m_pending[0], m_pending[1], cursor, ok);
        if (ok) c.emitTo(out);
        else { out.addSegment(m_pending[0], m_pending[1]); out.addSegment(m_pending[1], cursor); }
    } else if (m_tool == ToolKind::Circle3P && m_pending.size() == 1) {
        out.addSegment(m_pending[0], cursor);
    } else if (m_tool == ToolKind::ArcSCE && m_pending.size() == 2) {
        const Point3 s = m_pending[0], ctr = m_pending[1];
        const double r = std::hypot(s.x - ctr.x, s.y - ctr.y);
        if (r > 1e-9)
            Arc(ctr, r, std::atan2(s.y - ctr.y, s.x - ctr.x),
                std::atan2(cursor.y - ctr.y, cursor.x - ctr.x)).emitTo(out);
    } else if (m_tool == ToolKind::ArcSCE && m_pending.size() == 1) {
        out.addSegment(m_pending[0], cursor);
    } else if (m_tool == ToolKind::ArcCSE && m_pending.size() == 2) {
        const Point3 ctr = m_pending[0], s = m_pending[1];
        const double r = std::hypot(s.x - ctr.x, s.y - ctr.y);
        if (r > 1e-9)
            Arc(ctr, r, std::atan2(s.y - ctr.y, s.x - ctr.x),
                std::atan2(cursor.y - ctr.y, cursor.x - ctr.x)).emitTo(out);
    } else if (m_tool == ToolKind::ArcCSE && m_pending.size() == 1) {
        out.addSegment(m_pending[0], cursor);
    } else if (m_tool == ToolKind::ArcSED && m_pending.size() == 2) {
        const Arc3Result r = arcStartEndDirection(m_pending[0], m_pending[1], cursor);
        if (r.ok) r.arc.emitTo(out);
        else out.addSegment(m_pending[0], m_pending[1]);
    } else if (m_tool == ToolKind::ArcSED && m_pending.size() == 1) {
        out.addSegment(m_pending[0], cursor);
    } else if ((m_tool == ToolKind::ArcSER || m_tool == ToolKind::ArcSEA) &&
               m_pending.size() == 1) {
        out.addSegment(m_pending[0], cursor);   // 1ª aresta; o valor vem digitado
    } else if ((m_tool == ToolKind::ArcSER || m_tool == ToolKind::ArcSEA) &&
               m_pending.size() == 2) {
        out.addSegment(m_pending[0], m_pending[1]);  // corda; aguardando raio/ângulo
    } else if (m_tool == ToolKind::Polygon && m_pending.size() == 1) {
        const Point3 ctr = m_pending[0];
        const double r = std::hypot(cursor.x - ctr.x, cursor.y - ctr.y);
        const double rot = std::atan2(cursor.y - ctr.y, cursor.x - ctr.x);
        if (r > 1e-9 && m_polygonSides >= 3) {
            const double rr = m_polygonInscribed ? r : r / std::cos(kPi / m_polygonSides);
            regularPolygon(ctr, m_polygonSides, rr, rot).emitTo(out);
        }
    } else if ((m_tool == ToolKind::ArcSER || m_tool == ToolKind::ArcSEA) &&
               m_pending.size() == 2) {
        // Fantasma do arco passando pelo cursor (3º clique = ponto de passagem).
        const Arc3Result r = arcFrom3Points(m_pending[0], cursor, m_pending[1]);
        if (r.ok) r.arc.emitTo(out);
        else { out.addSegment(m_pending[0], cursor); out.addSegment(cursor, m_pending[1]); }
    } else if (m_tool == ToolKind::WallTool && !m_pending.empty()) {
        // Fantasma da PAREDE inteira até o cursor.
        std::vector<Point3> ax = m_pending;
        ax.push_back(cursor);
        Wall(std::move(ax), m_wallThickness).emitTo(out);
    } else if (m_tool == ToolKind::Door && m_pending.size() == 1) {
        out.addSegment(m_pending[0], cursor);              // vão: dobradiça -> batente
    } else if (m_tool == ToolKind::Door && m_pending.size() == 2) {
        // Fantasma da porta completa (folha + arco), com o lado pelo cursor.
        const Point3 h = m_pending[0], b = m_pending[1];
        const double vx = b.x - h.x, vy = b.y - h.y;
        const double r = std::hypot(vx, vy);
        if (r > 1e-9) {
            const double cross = vx * (cursor.y - h.y) - vy * (cursor.x - h.x);
            const double s = (cross >= 0.0) ? 1.0 : -1.0;
            const Point3 tip{h.x - vy * s, h.y + vx * s, 0.0};
            const double aB = std::atan2(vy, vx);
            const double aT = std::atan2(tip.y - h.y, tip.x - h.x);
            out.addSegment(h, tip);
            ((s > 0.0) ? Arc(h, r, aB, aT) : Arc(h, r, aT, aB)).emitTo(out);
        }
    } else if (m_tool == ToolKind::RectChamfer && m_pending.size() == 1) {
        rectangleChamfer(m_pending[0], cursor, m_chamferDist).emitTo(out);
    } else if (m_tool == ToolKind::RectFillet && m_pending.size() == 1) {
        rectangleFillet(m_pending[0], cursor, m_filletRadius).emitTo(out);
    } else if ((m_tool == ToolKind::DimLinear || m_tool == ToolKind::DimAligned) &&
               m_pending.size() == 2) {
        Dimension d = (m_tool == ToolKind::DimLinear)
            ? Dimension::linear(m_pending[0], m_pending[1], cursor, m_annotHeight)
            : Dimension::aligned(m_pending[0], m_pending[1], cursor, m_annotHeight);
        d.emitTo(out);
    } else if ((m_tool == ToolKind::DimRadius || m_tool == ToolKind::DimDiameter) &&
               m_pending.size() == 1) {
        Point3 edge = cursor;
        if (m_dimHasCircle) {   // fantasma já no raio real
            const Point3 c = m_pending[0];
            const double dx = cursor.x - c.x, dy = cursor.y - c.y, len = std::hypot(dx, dy);
            edge = (len > 1e-9)
                 ? Point3{c.x + dx / len * m_dimRadius, c.y + dy / len * m_dimRadius, c.z}
                 : Point3{c.x + m_dimRadius, c.y, c.z};
        }
        Dimension d = (m_tool == ToolKind::DimRadius)
            ? Dimension::radius(m_pending[0], edge, m_annotHeight)
            : Dimension::diameter(m_pending[0], edge, m_annotHeight);
        d.emitTo(out);
    } else if (m_tool == ToolKind::DimAngular && m_pending.size() == 2) {
        Dimension::angular(m_pending[0], m_pending[1], cursor, m_annotHeight).emitTo(out);
    } else if (m_tool == ToolKind::Offset && m_phase == EditPhase::Base &&
               !m_selection.empty()) {
        // Fantasma do offset à distância do cursor à geometria.
        for (const EntityId id : m_selection)
            if (const Entity* e = m_doc.getEntity(id)) {
                Ray r; r.origin = cursor;
                const double dist = m_offsetDist > 0.0
                                  ? m_offsetDist : e->hitTest(r, 1e12).distance;
                if (const auto* ln = dynamic_cast<const Line*>(e))
                    offsetLine(*ln, dist, cursor).emitTo(out);
                else if (const auto* ci = dynamic_cast<const Circle*>(e))
                    offsetCircle(*ci, dist, cursor).emitTo(out);
                else if (const auto* ar = dynamic_cast<const Arc*>(e))
                    offsetArc(*ar, dist, cursor).emitTo(out);
                else if (const auto* pl = dynamic_cast<const Polyline*>(e))
                    offsetPolyline(*pl, dist, cursor).emitTo(out);
            }
    } else if (m_tool == ToolKind::Align && m_phase == EditPhase::Base) {
        if (m_pending.size() == 1) {
            out.addSegment(m_pending[0], cursor);          // origem1 -> destino1
        } else if (m_pending.size() == 3) {
            const Matrix4 m = alignMatrix(m_pending[0], m_pending[1], m_pending[2], cursor);
            for (const EntityId id : m_selection)
                if (const Entity* e = m_doc.getEntity(id)) {
                    EntityPtr c = e->clone();
                    c->transform(m);
                    c->emitTo(out);
                }
        }
    } else if (m_editRef && (m_tool == ToolKind::Rotate || m_tool == ToolKind::Scale) &&
               m_phase == EditPhase::Target && m_pending.size() == 1) {
        out.addSegment(m_pending[0], cursor);   // linha de referência (base -> cursor)
    } else if (isEditTool(m_tool) && m_phase == EditPhase::Target && !m_pending.empty()) {
        const Matrix4 m = previewMatrix(m_pending[0], cursor);
        for (const EntityId id : m_selection)
            if (const Entity* e = m_doc.getEntity(id)) {
                EntityPtr c = e->clone();
                c->transform(m);
                c->emitTo(out);
            }
    }
}

void ToolController::selectAt(const Point3& p, double tol, bool add) {
    // Candidatos: toda entidade (não travada/oculta) cuja geometria está a <= tol
    // do clique, ordenados pelo mais próximo. Clique repetido no mesmo ponto CICLA
    // entre os sobrepostos (selection cycling, estilo AutoCAD).
    const AABB box = AABB::fromCenterHalf(p, Vec3{tol, tol, tol});
    Ray r; r.origin = p;
    std::vector<std::pair<double, EntityId>> cands;
    for (const EntityId id : m_doc.query(box)) {
        const Entity* e = m_doc.getEntity(id);
        if (!e || !e->visible()) continue;
        const Layer* lay = m_doc.layers().find(e->layer());
        if (lay && (!lay->on || lay->frozen || lay->locked)) continue;   // desligada/congelada/travada não seleciona
        const HitResult h = e->hitTest(r, tol);
        if (h.hit) cands.emplace_back(h.distance, id);
    }
    if (cands.empty()) {
        if (!add) m_selection.clear();
        m_hasCycle = false;
        return;
    }
    std::sort(cands.begin(), cands.end());
    const bool sameSpot = m_hasCycle &&
                          std::hypot(p.x - m_cyclePoint.x, p.y - m_cyclePoint.y) < tol;
    if (sameSpot && !add) m_cycleIndex = (m_cycleIndex + 1) % cands.size();
    else                  m_cycleIndex = 0;
    m_cyclePoint = p; m_hasCycle = true;

    const EntityId id = cands[m_cycleIndex].second;
    if (!add) m_selection.clear();
    auto it = std::find(m_selection.begin(), m_selection.end(), id);
    if (it == m_selection.end()) m_selection.push_back(id);
    else if (add) m_selection.erase(it);

    // GRUPOS: clicar num membro seleciona o grupo inteiro (estilo AutoCAD).
    if (const std::vector<EntityId>* grp = m_doc.groupOf(id))
        for (const EntityId g : *grp)
            if (m_doc.getEntity(g) &&
                std::find(m_selection.begin(), m_selection.end(), g) == m_selection.end())
                m_selection.push_back(g);

    if (!m_selection.empty()) m_prevSelection = m_selection;
}

void ToolController::selectInBox(const AABB& box, bool crossing, bool add) {
    m_lastBox = box;   // memorizado para o Stretch (janela de esticamento)
    if (!add) m_selection.clear();
    const std::vector<EntityId> ids = m_doc.query(box);
    RenderBatch tmp;
    for (const EntityId id : ids) {
        const Entity* e = m_doc.getEntity(id);
        if (!e || !e->visible()) continue;
        const Layer* lay = m_doc.layers().find(e->layer());
        if (lay && (!lay->on || lay->frozen || lay->locked)) continue;   // desligada/congelada/travada não seleciona

        bool hit = false;
        if (crossing) {
            tmp.clear();
            e->emitTo(tmp);
            const std::vector<Point3>& v = tmp.lineVertices;
            for (std::size_t i = 0; i + 1 < v.size(); i += 2)
                if (segIntersectsRect(v[i], v[i + 1], box)) { hit = true; break; }
        } else {
            hit = box.contains(e->boundingBox());
        }
        if (hit && std::find(m_selection.begin(), m_selection.end(), id) == m_selection.end())
            m_selection.push_back(id);
    }
    if (!m_selection.empty()) m_prevSelection = m_selection;
}

// --- Seleção avançada (WPolygon/CPolygon/ALL/Previous/Last) ----------------

void ToolController::selectInPolygon(const std::vector<Point3>& poly,
                                     bool crossing, bool add) {
    if (!add) m_selection.clear();
    if (poly.size() < 3) return;

    // Ponto-em-polígono (par-ímpar) e interseção de segmentos, sobre os
    // VÉRTICES EMITIDOS da entidade (fiel à geometria real, não só ao bbox).
    auto inside = [&](const Point3& p) {
        bool in = false;
        for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
            const Point3& a = poly[i];
            const Point3& b = poly[j];
            if ((a.y > p.y) != (b.y > p.y) &&
                p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                in = !in;
        }
        return in;
    };
    auto segX = [](const Point3& a, const Point3& b,
                   const Point3& c, const Point3& d) {
        auto cross = [](double ax, double ay, double bx, double by) {
            return ax * by - ay * bx;
        };
        const double d1 = cross(d.x - c.x, d.y - c.y, a.x - c.x, a.y - c.y);
        const double d2 = cross(d.x - c.x, d.y - c.y, b.x - c.x, b.y - c.y);
        const double d3 = cross(b.x - a.x, b.y - a.y, c.x - a.x, c.y - a.y);
        const double d4 = cross(b.x - a.x, b.y - a.y, d.x - a.x, d.y - a.y);
        return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
    };

    AABB pb;
    for (const Point3& p : poly) pb.expand(p);
    RenderBatch tmp;
    for (const EntityId id : m_doc.query(pb)) {
        const Entity* e = m_doc.getEntity(id);
        if (!e || !e->visible()) continue;
        const Layer* lay = m_doc.layers().find(e->layer());
        if (lay && (!lay->on || lay->frozen || lay->locked)) continue;

        tmp.lineVertices.clear();
        tmp.fillVertices.clear();
        e->emitTo(tmp);
        const std::vector<Point3>& v = tmp.lineVertices;
        if (v.empty()) continue;

        bool hit;
        if (crossing) {
            hit = false;
            for (std::size_t i = 0; i + 1 < v.size() && !hit; i += 2) {
                if (inside(v[i]) || inside(v[i + 1])) { hit = true; break; }
                for (std::size_t k = 0, j = poly.size() - 1; k < poly.size(); j = k++)
                    if (segX(v[i], v[i + 1], poly[j], poly[k])) { hit = true; break; }
            }
        } else {
            hit = true;                       // Window: TODOS os vértices dentro
            for (std::size_t i = 0; i < v.size() && hit; ++i)
                if (!inside(v[i])) hit = false;
            if (hit) {                        // ... e nenhuma aresta cruzando
                for (std::size_t i = 0; i + 1 < v.size() && hit; i += 2)
                    for (std::size_t k = 0, j = poly.size() - 1; k < poly.size(); j = k++)
                        if (segX(v[i], v[i + 1], poly[j], poly[k])) { hit = false; break; }
            }
        }
        if (hit && std::find(m_selection.begin(), m_selection.end(), id) == m_selection.end())
            m_selection.push_back(id);
    }
    if (!m_selection.empty()) m_prevSelection = m_selection;
}

int ToolController::selectAllVisible() {
    m_selection.clear();
    m_doc.forEach([&](const Entity& e) {
        if (!e.visible()) return;
        const Layer* lay = m_doc.layers().find(e.layer());
        if (lay && (!lay->on || lay->frozen || lay->locked)) return;
        m_selection.push_back(e.id());
    });
    if (!m_selection.empty()) m_prevSelection = m_selection;
    return static_cast<int>(m_selection.size());
}

int ToolController::selectPrevious() {
    m_selection.clear();
    for (const EntityId id : m_prevSelection)
        if (m_doc.getEntity(id)) m_selection.push_back(id);   // sobreviventes
    return static_cast<int>(m_selection.size());
}

bool ToolController::selectLastCreated() {
    const EntityId id = m_doc.lastAssignedId();
    if (id == kInvalidId || !m_doc.getEntity(id)) return false;
    m_selection.clear();
    m_selection.push_back(id);
    m_prevSelection = m_selection;
    return true;
}

void ToolController::selectIds(const std::vector<EntityId>& ids, bool add) {
    if (!add) m_selection.clear();
    for (const EntityId id : ids)
        if (m_doc.getEntity(id) &&
            std::find(m_selection.begin(), m_selection.end(), id) == m_selection.end())
            m_selection.push_back(id);
    if (!m_selection.empty()) m_prevSelection = m_selection;
}

bool ToolController::joinSelected() {
    if (m_selection.size() < 2) return false;
    // Coleta os segmentos (v1: só LINHAS; outras entidades abortam o join).
    struct Seg { Point3 a, b; bool used{false}; };
    std::vector<Seg> segs;
    segs.reserve(m_selection.size());
    for (const EntityId id : m_selection) {
        const auto* ln = dynamic_cast<const Line*>(m_doc.getEntity(id));
        if (!ln) return false;
        segs.push_back({ln->start(), ln->end(), false});
    }
    const double tol = 1e-6;
    auto same = [&](const Point3& p, const Point3& q) {
        return std::abs(p.x - q.x) <= tol && std::abs(p.y - q.y) <= tol;
    };
    // Encadeia a partir do 1º segmento, crescendo pelas duas pontas.
    std::vector<Point3> chain{segs[0].a, segs[0].b};
    segs[0].used = true;
    std::size_t used = 1;
    bool progress = true;
    while (progress && used < segs.size()) {
        progress = false;
        for (Seg& s : segs) {
            if (s.used) continue;
            if      (same(chain.back(),  s.a)) chain.push_back(s.b);
            else if (same(chain.back(),  s.b)) chain.push_back(s.a);
            else if (same(chain.front(), s.a)) chain.insert(chain.begin(), s.b);
            else if (same(chain.front(), s.b)) chain.insert(chain.begin(), s.a);
            else continue;
            s.used = true; ++used; progress = true;
        }
    }
    if (used != segs.size()) return false;   // não formam UMA corrente conectada
    // Loop fechado: último ponto == primeiro -> vira polilinha FECHADA.
    bool closed = false;
    if (chain.size() >= 4 && same(chain.front(), chain.back())) {
        chain.pop_back();
        closed = true;
    }
    if (chain.size() < 2) return false;
    auto macro = std::make_unique<MacroCmd>("JOIN");
    macro->add(std::make_unique<EraseCmd>(m_selection));
    macro->add(std::make_unique<AddEntityCmd>(
        std::make_unique<Polyline>(std::move(chain), closed)));
    m_doc.execute(std::move(macro));
    m_selection.clear();
    return true;
}

// PORTA sensível à PAREDE: clicando numa Wall, os 2 primeiros cliques marcam
// as bordas do vão SOBRE o eixo e o 3º escolhe o lado de abertura — a porta
// vira um Opening da própria parede (vão + folha + arco embutidos, undo via
// ReplaceCmd). Fora de parede, cai no fluxo antigo de 3 pontos (onPoint).
void ToolController::doorClick(EntityId picked, const Point3& p) {
    const auto* w = dynamic_cast<const Wall*>(m_doc.getEntity(
        m_doorStage == 0 ? picked : m_doorWallId));
    if (m_doorStage == 0) {
        if (!w) { onPoint(p); return; }          // fluxo legado (porta solta)
        m_pending.clear();                       // abandona pontos soltos
        m_doorWallId = picked;
        m_doorS0 = w->stationOf(p);
        m_doorStage = 1;
        return;
    }
    if (!w) { m_doorStage = 0; return; }          // parede sumiu: recomeça
    if (m_doorStage == 1) {
        m_doorS1 = w->stationOf(p);
        if (std::abs(m_doorS1 - m_doorS0) < 1e-6) return;   // largura nula
        m_doorStage = 2;
        return;
    }
    // 3º clique: LADO de abertura = sinal do ponto em relação ao eixo local.
    const double s0 = std::min(m_doorS0, m_doorS1);
    const double s1 = std::max(m_doorS0, m_doorS1);
    // acha o segmento do meio do vão p/ medir o lado
    const double sm = (s0 + s1) * 0.5;
    const auto& ax = w->axis();
    double acc = 0.0;
    int side = 1;
    bool hingeAtEnd = (m_doorS0 > m_doorS1);      // dobradiça = 1º clique
    for (std::size_t i = 1; i < ax.size(); ++i) {
        const double lk = std::hypot(ax[i].x - ax[i - 1].x, ax[i].y - ax[i - 1].y);
        if (sm <= acc + lk + 1e-9) {
            const double dx = ax[i].x - ax[i - 1].x, dy = ax[i].y - ax[i - 1].y;
            const double cross = dx * (p.y - ax[i - 1].y) - dy * (p.x - ax[i - 1].x);
            side = (cross >= 0.0) ? 1 : -1;
            break;
        }
        acc += lk;
    }
    auto neu = std::unique_ptr<Wall>(static_cast<Wall*>(w->clone().release()));
    neu->addOpening({s0, s1 - s0, /*kind=*/1, side, hingeAtEnd});
    m_doc.execute(std::make_unique<ReplaceCmd>(m_doorWallId, std::move(neu)));
    m_doorStage = 0;
    m_doorWallId = kInvalidId;
}

// JANELA: 2 cliques sobre a MESMA parede definem o vão (símbolo de 3 linhas).
void ToolController::windowClick(EntityId picked, const Point3& p) {
    const auto* w = dynamic_cast<const Wall*>(m_doc.getEntity(
        m_winHas ? m_winWallId : picked));
    if (!m_winHas) {
        if (!w) return;                           // janela SÓ existe em parede
        m_winWallId = picked;
        m_winS0 = w->stationOf(p);
        m_winHas = true;
        return;
    }
    m_winHas = false;
    if (!w) return;
    const double s1 = w->stationOf(p);
    const double a = std::min(m_winS0, s1), b = std::max(m_winS0, s1);
    if (b - a < 1e-6) return;
    auto neu = std::unique_ptr<Wall>(static_cast<Wall*>(w->clone().release()));
    neu->addOpening({a, b - a, /*kind=*/2, 1, false});
    m_doc.execute(std::make_unique<ReplaceCmd>(m_winWallId, std::move(neu)));
    m_winWallId = kInvalidId;
}

bool ToolController::overkillRun(double tol, int& duplicates, int& merged) {
    duplicates = merged = 0;
    std::vector<std::pair<EntityId, const Entity*>> items;
    if (!m_selection.empty()) {
        for (const EntityId id : m_selection)
            if (const Entity* e = m_doc.getEntity(id)) items.push_back({id, e});
    } else {
        m_doc.forEach([&](const Entity& e) { items.push_back({e.id(), &e}); });
    }
    cleanup::OverkillResult res = cleanup::overkill(items, tol);
    duplicates = res.duplicates;
    merged = res.mergedLines;
    if (res.remove.empty()) return false;
    auto macro = std::make_unique<MacroCmd>("OVERKILL");
    macro->add(std::make_unique<EraseCmd>(res.remove));
    for (auto& e : res.add) macro->add(std::make_unique<AddEntityCmd>(std::move(e)));
    m_doc.execute(std::move(macro));
    m_selection.clear();
    return true;
}

int ToolController::reverseSelected() {
    if (m_selection.empty()) return 0;
    auto macro = std::make_unique<MacroCmd>("REVERSE");
    int n = 0;
    for (const EntityId id : m_selection) {
        const Entity* e = m_doc.getEntity(id);
        if (!e) continue;
        std::unique_ptr<Entity> r = cleanup::reversed(*e);
        if (!r) continue;
        r->setLayer(e->layer());
        r->setColor(e->color());
        r->setLineType(e->lineType());
        r->setLineWeight(e->lineWeight());
        macro->add(std::make_unique<ReplaceCmd>(id, std::move(r)));
        ++n;
    }
    if (n > 0) m_doc.execute(std::move(macro));
    return n;
}

bool ToolController::blendSelected() {
    if (m_selection.size() != 2) return false;
    const Entity* a = m_doc.getEntity(m_selection[0]);
    const Entity* b = m_doc.getEntity(m_selection[1]);
    if (!a || !b) return false;
    std::unique_ptr<Spline> sp = cleanup::blend(*a, *b);
    if (!sp) return false;
    sp->setLayer(a->layer());          // a emenda herda da 1ª (como o Fillet)
    sp->setColor(a->color());
    sp->setLineType(a->lineType());
    sp->setLineWeight(a->lineWeight());
    m_doc.execute(std::make_unique<AddEntityCmd>(std::move(sp)));
    m_selection.clear();
    return true;
}

int ToolController::selectByFilter(const std::string& typeName, const std::string& layer) {
    m_selection.clear();
    m_doc.forEach([&](const Entity& e) {
        if (!e.visible()) return;
        const Layer* lay = m_doc.layers().find(e.layer());
        if (lay && (!lay->on || lay->frozen || lay->locked)) return;
        if (!typeName.empty() && typeName != e.typeName()) return;
        if (!layer.empty() && layer != e.layer()) return;
        m_selection.push_back(e.id());
    });
    return static_cast<int>(m_selection.size());
}

bool ToolController::eraseSelected() {
    if (m_selection.empty()) return false;
    m_doc.execute(std::make_unique<EraseCmd>(m_selection));
    m_selection.clear();
    return true;
}

} // namespace cad
