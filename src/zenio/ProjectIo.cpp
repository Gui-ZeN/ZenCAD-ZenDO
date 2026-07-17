// src/zenio/ProjectIo.cpp
// Implementação do formato .zencad: UM arquivo JSON com todo o documento —
// entidades, camadas, blocos (com atributos), estilos, pranchas e configurações.
// Round-trip fiel: cada tipo concreto de Entity tem serializador e fábrica.
#include "zenio/ProjectIo.hpp"

#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <utility>

#include "core/geometry/Arc.hpp"
#include "core/geometry/AttDef.hpp"
#include "core/geometry/BlockRef.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Dimension.hpp"
#include "core/geometry/Ellipse.hpp"
#include "core/geometry/Hatch.hpp"
#include "core/geometry/Leader.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/MLeader.hpp"
#include "core/geometry/MLine.hpp"
#include "core/geometry/MText.hpp"
#include "core/geometry/PointEntity.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/RayLine.hpp"
#include "core/geometry/Region.hpp"
#include "core/geometry/Spline.hpp"
#include "core/geometry/Table.hpp"
#include "core/geometry/Wall.hpp"
#include "core/geometry/Wipeout.hpp"
#include "core/geometry/XLine.hpp"

namespace cad {

// A versão do formato .zencad que ESTE binário sabe ler. Anda junto com o
// `root["version"]` do saveProject — subir uma sem a outra é o bug que a
// checagem no loadProject existe para pegar.
constexpr int kFormatVersion = 1;

namespace {

// ============================================================================
//  Helpers de conversão básica (Point3/Vec3, Rgba, strings, loops)
// ============================================================================

QJsonArray toJson(const Point3& p) { return QJsonArray{p.x, p.y, p.z}; }

Point3 pointFrom(const QJsonValue& v, const Point3& def = {}) {
    const QJsonArray a = v.toArray();
    if (a.size() < 3) return def;
    return {a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble()};
}

QJsonArray toJson(const Rgba& c) {
    return QJsonArray{int(c.r), int(c.g), int(c.b), int(c.a)};
}

Rgba rgbaFrom(const QJsonValue& v, const Rgba& def = {}) {
    const QJsonArray a = v.toArray();
    if (a.size() < 4) return def;
    auto u8 = [](const QJsonValue& x) {
        return std::uint8_t(std::clamp(x.toInt(255), 0, 255));
    };
    return {u8(a.at(0)), u8(a.at(1)), u8(a.at(2)), u8(a.at(3))};
}

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QJsonValue& v) { return v.toString().toStdString(); }

QJsonArray pointsToJson(const std::vector<Point3>& pts) {
    QJsonArray a;
    for (const Point3& p : pts) a.append(toJson(p));
    return a;
}

std::vector<Point3> pointsFrom(const QJsonValue& v) {
    std::vector<Point3> out;
    const QJsonArray a = v.toArray();
    out.reserve(std::size_t(a.size()));
    for (const QJsonValue& e : a) out.push_back(pointFrom(e));
    return out;
}

QJsonArray loopsToJson(const std::vector<std::vector<Point3>>& loops) {
    QJsonArray a;
    for (const auto& loop : loops) a.append(pointsToJson(loop));
    return a;
}

std::vector<std::vector<Point3>> loopsFrom(const QJsonValue& v) {
    std::vector<std::vector<Point3>> out;
    const QJsonArray a = v.toArray();
    out.reserve(std::size_t(a.size()));
    for (const QJsonValue& e : a) out.push_back(pointsFrom(e));
    return out;
}

// ============================================================================
//  Entidades — serialização
// ============================================================================

// `deepInsert`: dentro de definições de bloco, INSERTs nomeados também gravam
// os membros — garante reconstrução mesmo se a definição referida ainda não
// tiver sido lida (blocos aninhados) ou tiver sumido.
QJsonObject entityToJson(const Entity& e, bool deepInsert);

QJsonArray membersToJson(const std::vector<EntityPtr>& members, bool deepInsert) {
    QJsonArray a;
    for (const EntityPtr& m : members) a.append(entityToJson(*m, deepInsert));
    return a;
}

// Propriedades comuns a toda entidade (camada, cor, tipo/espessura de linha).
void writeCommon(const Entity& e, QJsonObject& o) {
    o["type"]  = e.typeName();
    o["layer"] = qs(e.layer());
    switch (e.color().mode) {
        case ColorRef::Mode::ByLayer: o["colorMode"] = "byLayer"; break;
        case ColorRef::Mode::ByBlock: o["colorMode"] = "byBlock"; break;
        case ColorRef::Mode::Explicit:
            o["colorMode"] = "rgb";
            o["color"]     = toJson(e.color().value);
            break;
    }
    o["lineType"]     = qs(e.lineType().name);
    o["lineWeightMm"] = e.lineWeight().mm;
    o["visible"]      = e.visible();
}

QJsonObject entityToJson(const Entity& e, bool deepInsert) {
    QJsonObject o;
    writeCommon(e, o);

    if (auto* p = dynamic_cast<const Line*>(&e)) {
        o["start"] = toJson(p->start());
        o["end"]   = toJson(p->end());
    } else if (auto* p = dynamic_cast<const Circle*>(&e)) {
        o["center"] = toJson(p->center());
        o["radius"] = p->radius();
    } else if (auto* p = dynamic_cast<const Arc*>(&e)) {
        o["center"]     = toJson(p->center());
        o["radius"]     = p->radius();
        o["startAngle"] = p->startAngle();
        o["endAngle"]   = p->endAngle();
    } else if (auto* p = dynamic_cast<const Polyline*>(&e)) {
        o["vertices"] = pointsToJson(p->vertices());
        QJsonArray bulges;
        for (double b : p->bulges()) bulges.append(b);
        o["bulges"] = bulges;
        o["closed"] = p->closed();
        o["width"]  = p->width();
    } else if (auto* p = dynamic_cast<const MText*>(&e)) {
        o["position"] = toJson(p->position());
        o["text"]     = qs(p->text());
        o["height"]   = p->height();
        o["rotation"] = p->rotation();
        o["justify"]  = int(p->justify());
        if (!p->font().empty()) o["font"] = qs(p->font());
        if (p->boxWidth() > 0.0) o["boxWidth"] = p->boxWidth();
        if (p->bold())   o["bold"]   = true;
        if (p->italic()) o["italic"] = true;
        if (p->annotative()) o["annotative"] = true;
    } else if (auto* p = dynamic_cast<const Ellipse*>(&e)) {
        o["center"] = toJson(p->center());
        o["major"]  = toJson(p->major());
        o["ratio"]  = p->ratio();
        o["t0"]     = p->startParam();
        o["t1"]     = p->endParam();
    } else if (auto* p = dynamic_cast<const Dimension*>(&e)) {
        o["kind"]       = int(p->kind());
        o["p1"]         = toJson(p->p1());
        o["p2"]         = toJson(p->p2());
        o["p3"]         = toJson(p->p3());
        o["textHeight"] = p->textHeight();
        o["arrowSize"]  = p->arrowSize();
        o["decimals"]   = p->decimals();
        o["suffix"]     = qs(p->suffix());
        o["arrowType"]  = int(p->arrowType());
        if (!p->font().empty()) o["font"] = qs(p->font());
        if (!p->textOverride().empty()) o["textOverride"] = qs(p->textOverride());
        if (p->textOffsetX() != 0.0 || p->textOffsetY() != 0.0) {
            o["textOffX"] = p->textOffsetX();
            o["textOffY"] = p->textOffsetY();
        }
        if (p->tolPlus() > 0.0 || p->tolMinus() > 0.0) {
            o["tolPlus"]  = p->tolPlus();
            o["tolMinus"] = p->tolMinus();
        }
        if (p->jogged()) o["jogged"] = true;
        if (p->annotative()) o["annotative"] = true;
    } else if (auto* p = dynamic_cast<const Hatch*>(&e)) {
        o["loops"]          = loopsToJson(p->loops());
        o["pattern"]        = int(p->pattern());
        o["angleDeg"]       = p->angleDeg();
        o["scale"]          = p->scale();
        o["spacing"]        = p->spacing();
        o["gradientColor1"] = toJson(p->gradientColor1());
        o["gradientColor2"] = toJson(p->gradientColor2());
    } else if (auto* p = dynamic_cast<const PointEntity*>(&e)) {
        o["position"] = toJson(p->position());
    } else if (auto* p = dynamic_cast<const Spline*>(&e)) {
        o["points"] = pointsToJson(p->controlPoints());
        o["cv"]     = p->isCV();
    } else if (auto* p = dynamic_cast<const XLine*>(&e)) {
        o["base"] = toJson(p->base());
        o["dir"]  = toJson(p->dir());
    } else if (auto* p = dynamic_cast<const RayLine*>(&e)) {
        o["base"] = toJson(p->base());
        o["dir"]  = toJson(p->dir());
    } else if (auto* p = dynamic_cast<const MLine*>(&e)) {
        o["vertices"] = pointsToJson(p->vertices());
        o["width"]    = p->width();
        o["closed"]   = p->closed();
    } else if (auto* p = dynamic_cast<const Leader*>(&e)) {
        o["points"]     = pointsToJson(p->points());
        o["text"]       = qs(p->text());
        o["textHeight"] = p->textHeight();
    } else if (auto* p = dynamic_cast<const MLeader*>(&e)) {
        o["leaders"]    = loopsToJson(p->leaders());
        o["textPos"]    = toJson(p->textPos());
        o["text"]       = qs(p->text());
        o["textHeight"] = p->textHeight();
    } else if (auto* p = dynamic_cast<const Wipeout*>(&e)) {
        o["contour"] = pointsToJson(p->contour());
    } else if (auto* p = dynamic_cast<const Region*>(&e)) {
        o["loops"] = loopsToJson(p->loops());
    } else if (auto* p = dynamic_cast<const Table*>(&e)) {
        o["origin"]    = toJson(p->origin());
        o["rows"]      = p->rows();
        o["cols"]      = p->cols();
        o["colWidth"]  = p->colWidth();
        o["rowHeight"] = p->rowHeight();
        o["colDir"]    = toJson(p->colDir());
        o["rowDir"]    = toJson(p->rowDir());
        QJsonArray cells;   // linha-maior (rows*cols)
        for (int r = 0; r < p->rows(); ++r)
            for (int c = 0; c < p->cols(); ++c)
                cells.append(qs(p->cell(r, c)));
        o["cells"] = cells;
    } else if (auto* p = dynamic_cast<const AttDef*>(&e)) {
        o["position"] = toJson(p->position());
        o["tag"]      = qs(p->tag());
        o["prompt"]   = qs(p->prompt());
        o["defValue"] = qs(p->defValue());
        o["height"]   = p->height();
    } else if (auto* p = dynamic_cast<const Wall*>(&e)) {
        o["axis"]      = pointsToJson(p->axis());
        o["thickness"] = p->thickness();
        QJsonArray ops;
        for (const Wall::Opening& op : p->openings()) {
            QJsonObject oo;
            oo["station"] = op.station;
            oo["width"]   = op.width;
            oo["kind"]    = op.kind;
            oo["side"]    = op.side;
            if (op.hingeAtEnd) oo["hingeAtEnd"] = true;
            ops.append(oo);
        }
        o["openings"] = ops;
    } else if (auto* p = dynamic_cast<const BlockRef*>(&e)) {
        o["blockName"] = qs(p->blockName());
        QJsonArray xf;
        for (double d : p->xform().m) xf.append(d);
        o["xform"] = xf;
        QJsonArray atts;
        for (const BlockRef::AttValue& a : p->attValues()) {
            QJsonObject ao;
            ao["tag"]    = qs(a.tag);
            ao["value"]  = qs(a.value);
            ao["pos"]    = toJson(a.pos);
            ao["height"] = a.height;
            atts.append(ao);
        }
        o["attValues"] = atts;
        if (!p->hiddenLayers().empty()) {          // estados de visibilidade
            QJsonArray hl;
            for (const std::string& n : p->hiddenLayers()) hl.append(qs(n));
            o["hiddenLayers"] = hl;
        }
        // Anônimo: membros SEMPRE (única fonte). Nomeado: só em deepInsert
        // (membros de definição de bloco), como redundância à prova de ordem.
        if (p->blockName().empty() || deepInsert)
            o["members"] = membersToJson(p->members(), deepInsert);
    }
    // Tipos desconhecidos ficam só com as propriedades comuns (não deveria
    // ocorrer: todos os tipos concretos do kernel estão cobertos acima).
    return o;
}

// ============================================================================
//  Entidades — fábrica (load)
// ============================================================================

EntityPtr entityFromJson(const QJsonObject& o, const BlockTable& blocks);

std::vector<EntityPtr> membersFrom(const QJsonValue& v, const BlockTable& blocks) {
    std::vector<EntityPtr> out;
    const QJsonArray a = v.toArray();
    out.reserve(std::size_t(a.size()));
    for (const QJsonValue& e : a)
        if (EntityPtr m = entityFromJson(e.toObject(), blocks))
            out.push_back(std::move(m));
    return out;
}

void applyCommon(Entity& e, const QJsonObject& o) {
    e.setLayer(ss(o.value("layer")));
    const QString mode = o.value("colorMode").toString("byLayer");
    if (mode == "rgb")
        e.setColor(ColorRef::explicitColor(rgbaFrom(o.value("color"))));
    else if (mode == "byBlock")
        e.setColor(ColorRef{ColorRef::Mode::ByBlock, {}});
    else
        e.setColor(ColorRef::byLayer());
    e.setLineType(LineType{ss(o.value("lineType"))});
    e.setLineWeight(LineWeight{o.value("lineWeightMm").toDouble(-1.0)});
    e.setVisible(o.value("visible").toBool(true));
}

EntityPtr entityFromJson(const QJsonObject& o, const BlockTable& blocks) {
    const QString type = o.value("type").toString();
    EntityPtr ent;

    if (type == "LINE") {
        ent = std::make_unique<Line>(pointFrom(o.value("start")),
                                     pointFrom(o.value("end")));
    } else if (type == "CIRCLE") {
        ent = std::make_unique<Circle>(pointFrom(o.value("center")),
                                       o.value("radius").toDouble());
    } else if (type == "ARC") {
        ent = std::make_unique<Arc>(pointFrom(o.value("center")),
                                    o.value("radius").toDouble(),
                                    o.value("startAngle").toDouble(),
                                    o.value("endAngle").toDouble());
    } else if (type == "LWPOLYLINE") {
        std::vector<double> bulges;
        for (const QJsonValue& b : o.value("bulges").toArray())
            bulges.push_back(b.toDouble());
        auto p = std::make_unique<Polyline>(pointsFrom(o.value("vertices")),
                                            std::move(bulges),
                                            o.value("closed").toBool(false));
        p->setWidth(o.value("width").toDouble(0.0));
        ent = std::move(p);
    } else if (type == "MTEXT") {
        auto p = std::make_unique<MText>(pointFrom(o.value("position")),
                                         ss(o.value("text")),
                                         o.value("height").toDouble(2.5),
                                         o.value("rotation").toDouble(0.0));
        p->setJustify(MTextJustify(std::clamp(o.value("justify").toInt(0), 0, 2)));
        p->setFont(ss(o.value("font")));   // ausente = traços (compat.)
        p->setBoxWidth(o.value("boxWidth").toDouble(0.0));
        p->setBold(o.value("bold").toBool(false));
        p->setItalic(o.value("italic").toBool(false));
        p->setAnnotative(o.value("annotative").toBool(false));
        ent = std::move(p);
    } else if (type == "ELLIPSE") {
        const Vec3   major = pointFrom(o.value("major"), Vec3{1.0, 0.0, 0.0});
        const double ratio = o.value("ratio").toDouble(1.0);
        // Reconstrução via fromCenterAxesArc (único caminho com t0/t1):
        // minorLen = ratio * |major|; o ratio é recalculado internamente.
        ent = std::make_unique<Ellipse>(Ellipse::fromCenterAxesArc(
            pointFrom(o.value("center")), major, ratio * major.length(),
            o.value("t0").toDouble(0.0), o.value("t1").toDouble(kTwoPi)));
    } else if (type == "DIMENSION") {
        auto p = std::make_unique<Dimension>(
            Dimension::DimKind(std::clamp(o.value("kind").toInt(0), 0, 5)),
            pointFrom(o.value("p1")), pointFrom(o.value("p2")),
            pointFrom(o.value("p3")), o.value("textHeight").toDouble(2.5));
        p->setArrowSize(o.value("arrowSize").toDouble(-1.0));
        p->setDecimals(o.value("decimals").toInt(2));
        p->setSuffix(ss(o.value("suffix")));
        p->setArrowType(Dimension::ArrowType(
            std::clamp(o.value("arrowType").toInt(0), 0, 2)));
        p->setFont(ss(o.value("font")));
        p->setTextOverride(ss(o.value("textOverride")));
        p->setTextOffset(o.value("textOffX").toDouble(0.0),
                         o.value("textOffY").toDouble(0.0));
        p->setTolerance(o.value("tolPlus").toDouble(0.0),
                        o.value("tolMinus").toDouble(0.0));
        p->setJogged(o.value("jogged").toBool(false));
        p->setAnnotative(o.value("annotative").toBool(false));
        ent = std::move(p);
    } else if (type == "HATCH") {
        auto p = std::make_unique<Hatch>(
            loopsFrom(o.value("loops")),
            HatchPattern(std::clamp(o.value("pattern").toInt(0), 0, 9)),
            o.value("angleDeg").toDouble(45.0),
            o.value("scale").toDouble(1.0),
            o.value("spacing").toDouble(3.0));
        p->setGradientColor1(rgbaFrom(o.value("gradientColor1"), {0, 0, 0, 255}));
        p->setGradientColor2(rgbaFrom(o.value("gradientColor2"), {255, 255, 255, 255}));
        ent = std::move(p);
    } else if (type == "POINT") {
        ent = std::make_unique<PointEntity>(pointFrom(o.value("position")));
    } else if (type == "SPLINE") {
        ent = std::make_unique<Spline>(pointsFrom(o.value("points")),
                                       o.value("cv").toBool(false));
    } else if (type == "XLINE") {
        ent = std::make_unique<XLine>(pointFrom(o.value("base")),
                                      pointFrom(o.value("dir"), Vec3{1.0, 0.0, 0.0}));
    } else if (type == "RAY") {
        ent = std::make_unique<RayLine>(pointFrom(o.value("base")),
                                        pointFrom(o.value("dir"), Vec3{1.0, 0.0, 0.0}));
    } else if (type == "MLINE") {
        ent = std::make_unique<MLine>(pointsFrom(o.value("vertices")),
                                      o.value("width").toDouble(0.0),
                                      o.value("closed").toBool(false));
    } else if (type == "LEADER") {
        ent = std::make_unique<Leader>(pointsFrom(o.value("points")),
                                       ss(o.value("text")),
                                       o.value("textHeight").toDouble(2.5));
    } else if (type == "MULTILEADER") {
        ent = std::make_unique<MLeader>(loopsFrom(o.value("leaders")),
                                        pointFrom(o.value("textPos")),
                                        ss(o.value("text")),
                                        o.value("textHeight").toDouble(2.5));
    } else if (type == "WIPEOUT") {
        ent = std::make_unique<Wipeout>(pointsFrom(o.value("contour")));
    } else if (type == "REGION") {
        ent = std::make_unique<Region>(loopsFrom(o.value("loops")));
    } else if (type == "TABLE") {
        const Point3 origin = pointFrom(o.value("origin"));
        const int    rows   = o.value("rows").toInt(0);
        const int    cols   = o.value("cols").toInt(0);
        auto p = std::make_unique<Table>(origin, rows, cols,
                                         o.value("colWidth").toDouble(0.0),
                                         o.value("rowHeight").toDouble(0.0));
        const QJsonArray cells = o.value("cells").toArray();
        int i = 0;
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c, ++i)
                if (i < cells.size())
                    p->setCell(r, c, cells.at(i).toString().toStdString());
        // Reaplica os eixos locais acumulados por transform() (rotação/escala),
        // mantendo a origem fixa: t = origem - L*origem.
        const Vec3 cd = pointFrom(o.value("colDir"), Vec3{1.0, 0.0, 0.0});
        const Vec3 rd = pointFrom(o.value("rowDir"), Vec3{0.0, -1.0, 0.0});
        if (cd.x != 1.0 || cd.y != 0.0 || cd.z != 0.0 ||
            rd.x != 0.0 || rd.y != -1.0 || rd.z != 0.0) {
            Matrix4 m = Matrix4::identity();
            m.m[0] = cd.x;  m.m[1] = cd.y;  m.m[2] = cd.z;   // X -> colDir
            m.m[4] = -rd.x; m.m[5] = -rd.y; m.m[6] = -rd.z;  // -Y -> rowDir
            const Vec3 lo = m.transformVector(origin);
            m.m[12] = origin.x - lo.x;
            m.m[13] = origin.y - lo.y;
            m.m[14] = origin.z - lo.z;
            p->transform(m);
        }
        ent = std::move(p);
    } else if (type == "ATTDEF") {
        ent = std::make_unique<AttDef>(pointFrom(o.value("position")),
                                       ss(o.value("tag")),
                                       ss(o.value("prompt")),
                                       ss(o.value("defValue")),
                                       o.value("height").toDouble(2.5));
    } else if (type == "WALL") {
        auto p = std::make_unique<Wall>(pointsFrom(o.value("axis")),
                                        o.value("thickness").toDouble(0.15));
        std::vector<Wall::Opening> ops;
        for (const QJsonValue& ov : o.value("openings").toArray()) {
            const QJsonObject oo = ov.toObject();
            ops.push_back({oo.value("station").toDouble(0.0),
                           oo.value("width").toDouble(0.9),
                           std::clamp(oo.value("kind").toInt(0), 0, 2),
                           oo.value("side").toInt(1) >= 0 ? 1 : -1,
                           oo.value("hingeAtEnd").toBool(false)});
        }
        p->setOpenings(std::move(ops));
        ent = std::move(p);
    } else if (type == "INSERT") {
        Matrix4 xf = Matrix4::identity();
        const QJsonArray xa = o.value("xform").toArray();
        if (xa.size() == 16)
            for (int i = 0; i < 16; ++i) xf.m[std::size_t(i)] = xa.at(i).toDouble();
        std::vector<BlockRef::AttValue> atts;
        for (const QJsonValue& av : o.value("attValues").toArray()) {
            const QJsonObject ao = av.toObject();
            atts.push_back({ss(ao.value("tag")), ss(ao.value("value")),
                            pointFrom(ao.value("pos")),
                            ao.value("height").toDouble(2.5)});
        }
        const std::string name = ss(o.value("blockName"));
        std::unique_ptr<BlockRef> p;
        const BlockDefinition* def = name.empty() ? nullptr : blocks.find(name);
        if (def) {
            p = BlockRef::fromDefinition(*def, xf);
        } else {
            // Anônimo — ou nomeado sem definição resolvida (usa os membros
            // embutidos gravados em deepInsert; sem eles fica vazio).
            p = std::make_unique<BlockRef>(membersFrom(o.value("members"), blocks), xf);
            p->setBlockName(name);
        }
        // Sobrescreve os valores padrão que fromDefinition aplicou.
        p->setAttValues(std::move(atts));
        std::set<std::string> hidden;              // estados de visibilidade
        for (const QJsonValue& hv : o.value("hiddenLayers").toArray())
            hidden.insert(hv.toString().toStdString());
        if (!hidden.empty()) p->setHiddenLayers(std::move(hidden));
        ent = std::move(p);
    }

    if (ent) applyCommon(*ent, o);
    return ent;   // nullptr = tipo desconhecido (ignorado)
}

// ============================================================================
//  Tabelas do documento (camadas, estilos, blocos, pranchas)
// ============================================================================

QJsonObject layerToJson(const Layer& l) {
    QJsonObject o;
    o["name"]         = qs(l.name);
    o["color"]        = toJson(l.color);
    o["lineType"]     = qs(l.lineType.name);
    o["lineWeightMm"] = l.lineWeight.mm;
    o["on"]           = l.on;
    o["frozen"]       = l.frozen;
    o["locked"]       = l.locked;
    if (l.transparency > 0) o["transparency"] = l.transparency;
    return o;
}

Layer layerFromJson(const QJsonObject& o) {
    Layer l;
    l.name = ss(o.value("name"));
    l.color         = rgbaFrom(o.value("color"), l.color);
    l.lineType.name = ss(o.value("lineType"));
    l.lineWeight.mm = o.value("lineWeightMm").toDouble(-1.0);
    l.on     = o.value("on").toBool(true);
    l.frozen = o.value("frozen").toBool(false);
    l.locked = o.value("locked").toBool(false);
    l.transparency = std::clamp(o.value("transparency").toInt(0), 0, 90);
    return l;
}

QJsonArray layersToJson(const LayerTable& layers) {
    QJsonArray a;
    for (const Layer& l : layers.all()) a.append(layerToJson(l));
    return a;
}

void layersFromJson(const QJsonValue& v, LayerTable& layers) {
    for (const QJsonValue& e : v.toArray()) {
        const Layer l = layerFromJson(e.toObject());
        if (l.name.empty()) continue;
        layers.add(l);   // insere ou atualiza (a "0" é atualizada in-place)
    }
    layers.ensureDefaultLayer();
}

QJsonArray blocksToJson(const BlockTable& blocks) {
    QJsonArray a;
    for (const std::string& name : blocks.names()) {
        const BlockDefinition* def = blocks.find(name);
        if (!def) continue;
        QJsonObject o;
        o["name"] = qs(def->name);
        o["base"] = toJson(def->base);
        QJsonArray atts;
        for (const AttDefSpec& ad : def->attdefs) {
            QJsonObject ao;
            ao["tag"]      = qs(ad.tag);
            ao["prompt"]   = qs(ad.prompt);
            ao["defValue"] = qs(ad.defValue);
            ao["pos"]      = toJson(ad.pos);
            ao["height"]   = ad.height;
            atts.append(ao);
        }
        o["attdefs"] = atts;
        o["members"] = membersToJson(def->members, /*deepInsert=*/true);
        a.append(o);
    }
    return a;
}

void blocksFromJson(const QJsonValue& v, BlockTable& blocks) {
    for (const QJsonValue& e : v.toArray()) {
        const QJsonObject o = e.toObject();
        BlockDefinition def;
        def.name = ss(o.value("name"));
        if (def.name.empty()) continue;
        def.base = pointFrom(o.value("base"));
        for (const QJsonValue& av : o.value("attdefs").toArray()) {
            const QJsonObject ao = av.toObject();
            def.attdefs.push_back({ss(ao.value("tag")), ss(ao.value("prompt")),
                                   ss(ao.value("defValue")),
                                   pointFrom(ao.value("pos")),
                                   ao.value("height").toDouble(2.5)});
        }
        def.members = membersFrom(o.value("members"), blocks);
        blocks.add(std::move(def));
    }
}

QJsonArray dimStylesToJson(const StyleTable& styles) {
    QJsonArray a;
    for (const DimStyle& s : styles.allDim()) {
        QJsonObject o;
        o["name"]       = qs(s.name);
        o["textHeight"] = s.textHeight;
        o["arrowSize"]  = s.arrowSize;
        o["decimals"]   = s.decimals;
        o["suffix"]     = qs(s.suffix);
        o["arrowType"]  = s.arrowType;
        o["tolPlus"]    = s.tolPlus;
        o["tolMinus"]   = s.tolMinus;
        o["annotative"] = s.annotative;
        a.append(o);
    }
    return a;
}

QJsonArray textStylesToJson(const StyleTable& styles) {
    QJsonArray a;
    for (const TextStyle& s : styles.allText()) {
        QJsonObject o;
        o["name"]   = qs(s.name);
        o["height"] = s.height;
        if (!s.font.empty()) o["font"] = qs(s.font);
        o["annotative"] = s.annotative;
        a.append(o);
    }
    return a;
}

QJsonArray layoutsToJson(const LayoutTable& layouts) {
    QJsonArray a;
    for (const Layout& l : layouts.all()) {
        QJsonObject o;
        o["name"]       = qs(l.name);
        o["paper"]      = int(l.paper);
        o["landscape"]  = l.landscape;
        o["marginMm"]   = l.marginMm;
        o["title"]      = qs(l.title);
        o["project"]    = qs(l.project);
        o["author"]     = qs(l.author);
        o["date"]       = qs(l.date);
        o["scaleLabel"] = qs(l.scaleLabel);
        QJsonArray vps;
        for (const SheetViewport& vp : l.viewports) {
            QJsonObject vo;
            vo["xMm"] = vp.xMm; vo["yMm"] = vp.yMm;
            vo["wMm"] = vp.wMm; vo["hMm"] = vp.hMm;
            vo["modelCx"]    = vp.modelCx;
            vo["modelCy"]    = vp.modelCy;
            vo["mmPerUnit"]  = vp.mmPerUnit;
            vo["scaleDenom"] = vp.scaleDenom;
            if (vp.locked) vo["locked"] = true;
            if (!vp.frozenLayers.empty()) {
                QJsonArray fl;
                for (const std::string& n : vp.frozenLayers) fl.append(qs(n));
                vo["frozenLayers"] = fl;
            }
            vps.append(vo);
        }
        o["viewports"] = vps;
        a.append(o);
    }
    return a;
}

void layoutsFromJson(const QJsonObject& root, LayoutTable& layouts) {
    layouts = LayoutTable{};   // zera (inclusive a corrente)
    for (const QJsonValue& e : root.value("layouts").toArray()) {
        const QJsonObject o = e.toObject();
        Layout l;
        l.name       = ss(o.value("name"));
        l.paper      = PaperSize(std::clamp(o.value("paper").toInt(1), 0, 4));
        l.landscape  = o.value("landscape").toBool(true);
        l.marginMm   = o.value("marginMm").toDouble(10.0);
        l.title      = ss(o.value("title"));
        l.project    = ss(o.value("project"));
        l.author     = ss(o.value("author"));
        l.date       = ss(o.value("date"));
        l.scaleLabel = ss(o.value("scaleLabel"));
        for (const QJsonValue& ve : o.value("viewports").toArray()) {
            const QJsonObject vo = ve.toObject();
            SheetViewport vp;
            vp.xMm = vo.value("xMm").toDouble(vp.xMm);
            vp.yMm = vo.value("yMm").toDouble(vp.yMm);
            vp.wMm = vo.value("wMm").toDouble(vp.wMm);
            vp.hMm = vo.value("hMm").toDouble(vp.hMm);
            vp.modelCx    = vo.value("modelCx").toDouble(0.0);
            vp.modelCy    = vo.value("modelCy").toDouble(0.0);
            vp.mmPerUnit  = vo.value("mmPerUnit").toDouble(1.0);
            vp.scaleDenom = vo.value("scaleDenom").toDouble(1.0);
            vp.locked     = vo.value("locked").toBool(false);
            for (const QJsonValue& fl : vo.value("frozenLayers").toArray())
                vp.frozenLayers.push_back(ss(fl));
            l.viewports.push_back(vp);
        }
        layouts.add(l);
    }
    layouts.setCurrent(std::size_t(std::max(0, root.value("currentLayout").toInt(0))));
}

void setErr(QString* err, const QString& msg) { if (err) *err = msg; }

} // namespace

// ============================================================================
//  API pública
// ============================================================================

bool saveProject(const QString& path,
                 const DrawingManager& doc,
                 const LayoutTable& layouts,
                 const StyleTable& styles,
                 const ProjectSettings& settings,
                 QString* err,
                 const QImage* thumbnail) {
    QJsonObject root;
    root["app"]     = "ZenCAD";
    root["version"] = kFormatVersion;

    QJsonObject s;
    s["ltScale"]      = settings.ltScale;
    s["unitIndex"]    = settings.unitIndex;
    s["unitDecimals"] = settings.unitDecimals;
    s["unitSuffix"]   = settings.unitSuffix;
    s["currentLayer"] = settings.currentLayer;
    s["annoMmPerUnit"] = doc.annoMmPerUnit();   // escala de anotação (CANNOSCALE)
    s["ucsOx"]    = doc.ucsOrigin().x;          // UCS 2D de trabalho
    s["ucsOy"]    = doc.ucsOrigin().y;
    s["ucsAngle"] = doc.ucsAngleRad();
    root["settings"] = s;
    if (settings.plotStyle.active || !settings.plotStyle.entries.empty())
        root["plotStyle"] = plotStyleToJson(settings.plotStyle);

    root["layers"]      = layersToJson(doc.layers());
    {   // Estados de camada nomeados (snapshots completos).
        QJsonArray states;
        for (const auto& kv : doc.layerStates()) {
            QJsonObject so;
            so["name"] = qs(kv.first);
            QJsonArray ls;
            for (const Layer& l : kv.second) ls.append(layerToJson(l));
            so["layers"] = ls;
            states.append(so);
        }
        root["layerStates"] = states;
    }
    root["dimStyles"]   = dimStylesToJson(styles);
    root["currentDim"]  = qs(styles.currentDimName());
    root["textStyles"]  = textStylesToJson(styles);
    root["currentText"] = qs(styles.currentTextName());
    root["blocks"]      = blocksToJson(doc.blocks());
    if (!doc.xrefs().empty()) {   // XREF: vínculo bloco -> arquivo externo
        QJsonArray xr;
        for (const auto& kv : doc.xrefs()) {
            QJsonObject xo;
            xo["name"] = qs(kv.first);
            xo["path"] = qs(kv.second);
            xr.append(xo);
        }
        root["xrefs"] = xr;
    }
    root["layouts"]       = layoutsToJson(layouts);
    root["currentLayout"] = int(layouts.current());

    QJsonArray ents;
    QHash<EntityId, int> idToIndex;   // p/ grupos: ids não sobrevivem ao load
    int entIdx = 0;
    doc.forEach([&](const Entity& e) {
        idToIndex.insert(e.id(), entIdx++);
        ents.append(entityToJson(e, /*deepInsert=*/false));
    });
    root["entities"] = ents;

    // Grupos nomeados: membros gravados como ÍNDICES no array de entidades.
    QJsonArray grps;
    for (const auto& kv : doc.groups()) {
        QJsonArray mi;
        for (const EntityId id : kv.second) {
            const auto it = idToIndex.constFind(id);
            if (it != idToIndex.constEnd()) mi.append(it.value());
        }
        if (mi.size() >= 2) {
            QJsonObject go;
            go["name"] = qs(kv.first);
            go["members"] = mi;
            grps.append(go);
        }
    }
    root["groups"] = grps;

    // Associatividade PERSISTENTE (cotas/hachuras): os vínculos são EntityIds
    // de sessão, que não sobrevivem ao load — gravados como ÍNDICES no array
    // de entidades, no mesmo padrão dos grupos.
    QJsonArray dimLinks, hatchLinks;
    doc.forEach([&](const Entity& e) {
        const int self = idToIndex.value(e.id(), -1);
        if (self < 0) return;
        if (const auto* d = dynamic_cast<const Dimension*>(&e)) {
            if (!d->associative()) return;
            auto anchorJson = [&](const DimAnchor& a) {
                QJsonObject ao;
                const int src = a.valid() ? idToIndex.value(a.id, -1) : -1;
                if (src < 0) return ao;              // objeto vazio = sem âncora
                ao["src"]   = src;
                ao["which"] = int(a.which);
                ao["index"] = a.index;
                return ao;
            };
            const QJsonObject a = anchorJson(d->anchorA());
            const QJsonObject b = anchorJson(d->anchorB());
            if (a.isEmpty() && b.isEmpty()) return;
            QJsonObject o;
            o["ent"] = self;
            if (!a.isEmpty()) o["a"] = a;
            if (!b.isEmpty()) o["b"] = b;
            dimLinks.append(o);
        } else if (const auto* h = dynamic_cast<const Hatch*>(&e)) {
            if (!h->associative()) return;
            QJsonArray src;
            for (const EntityId id : h->srcIds()) {
                const int i = idToIndex.value(id, -1);
                if (i >= 0) src.append(i);
            }
            if (src.isEmpty()) return;
            QJsonObject o;
            o["ent"] = self;
            o["src"] = src;
            hatchLinks.append(o);
        }
    });
    if (!dimLinks.isEmpty())   root["dimLinks"]   = dimLinks;
    if (!hatchLinks.isEmpty()) root["hatchLinks"] = hatchLinks;

    // Miniatura embutida (PNG/base64) — cards da tela inicial.
    if (thumbnail && !thumbnail->isNull()) {
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        thumbnail->save(&buf, "PNG");
        root["thumbnail"] = QString::fromLatin1(png.toBase64());
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setErr(err, QStringLiteral("Não foi possível gravar \"%1\": %2")
                        .arg(path, f.errorString()));
        return false;
    }
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (f.write(json) != json.size()) {
        setErr(err, QStringLiteral("Falha de escrita em \"%1\": %2")
                        .arg(path, f.errorString()));
        return false;
    }
    return true;
}

bool loadProject(const QString& path,
                 DrawingManager& doc,
                 LayoutTable& layouts,
                 StyleTable& styles,
                 ProjectSettings& settings,
                 QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setErr(err, QStringLiteral("Não foi possível abrir \"%1\": %2")
                        .arg(path, f.errorString()));
        return false;
    }
    QJsonParseError perr{};
    const QJsonDocument jdoc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !jdoc.isObject()) {
        setErr(err, QStringLiteral("JSON inválido em \"%1\": %2")
                        .arg(path, perr.errorString()));
        return false;
    }
    const QJsonObject root = jdoc.object();
    if (root.value("app").toString() != QLatin1String("ZenCAD")) {
        setErr(err, QStringLiteral("\"%1\" não é um projeto ZenCAD.").arg(path));
        return false;
    }

    // O formato se APRESENTA antes de ser lido.
    //
    // O saveProject grava `version` desde o primeiro dia e este load nunca o
    // lia: o campo era enfeite. Com um instalador único ninguém sentia — os
    // dois apps sempre saíam da mesma leva, então a versão era a mesma por
    // construção. Com produtos de release SEPARADO (instaladores próprios) a
    // premissa morre, e o silêncio vira perda de dados: o irmão novo grava um
    // campo que o irmão velho desconhece, o velho carrega ignorando o aviso
    // que ninguém leu, o usuário salva por cima — e o trabalho evapora sem uma
    // única mensagem de erro. É o modo de falha mais caro que existe.
    //
    // Recusar é a única resposta honesta: "não sei ler" é sempre melhor que
    // "li errado e não te contei". Ausência do campo = v1 (formato original),
    // que é o que os arquivos de antes desta checagem realmente são.
    // `toDouble` e não `toInt`: o JSON não tem tipo inteiro, e `toInt` devolve
    // o DEFAULT (=1, "pode abrir") quando o valor é fracionário ou estoura o
    // int — ou seja, erra pro lado permissivo justamente onde deveria
    // desconfiar. Hoje isso é inalcançável (o único escritor é o saveProject
    // logo acima), mas um gate que falha aberto não é um gate.
    const double ver = root.value("version").toDouble(1.0);
    if (ver > double(kFormatVersion)) {
        setErr(err, QStringLiteral(
                        "\"%1\" foi salvo num formato mais novo (v%2) do que "
                        "este aplicativo sabe ler (v%3). Atualize o Zen para "
                        "abrir este projeto.")
                        .arg(path).arg(ver).arg(kFormatVersion));
        return false;
    }

    // Validação OK: a partir daqui o documento é substituído.
    doc.clearAll();

    // 1) Camadas (antes das entidades, que as referenciam) + estados nomeados.
    layersFromJson(root.value("layers"), doc.layers());
    for (const QJsonValue& sv : root.value("layerStates").toArray()) {
        const QJsonObject so = sv.toObject();
        std::vector<Layer> ls;
        for (const QJsonValue& lv : so.value("layers").toArray())
            ls.push_back(layerFromJson(lv.toObject()));
        doc.addLayerState(ss(so.value("name")), std::move(ls));
    }

    // 2) Blocos (antes das entidades, para os INSERTs resolverem).
    blocksFromJson(root.value("blocks"), doc.blocks());
    for (const QJsonValue& xv : root.value("xrefs").toArray()) {   // vínculos XREF
        const QJsonObject xo = xv.toObject();
        doc.addXref(ss(xo.value("name")), ss(xo.value("path")));
    }

    // 3) Entidades do Model Space. `loadedIds[i]` = id novo da i-ésima entrada
    //    do array (kInvalidId p/ tipos ignorados) — usado pelos grupos abaixo.
    std::vector<EntityId> loadedIds;
    for (const QJsonValue& e : root.value("entities").toArray()) {
        EntityPtr ent = entityFromJson(e.toObject(), doc.blocks());
        if (!ent) { loadedIds.push_back(kInvalidId); continue; }   // tipo desconhecido
        if (!doc.layers().contains(ent->layer())) {
            Layer l; l.name = ent->layer();
            doc.layers().add(l);   // camada órfã: cria com padrões
        }
        loadedIds.push_back(doc.addEntity(std::move(ent)));
    }

    // 3b) Grupos nomeados (membros por índice -> ids novos).
    for (const QJsonValue& g : root.value("groups").toArray()) {
        const QJsonObject go = g.toObject();
        std::vector<EntityId> ids;
        for (const QJsonValue& m : go.value("members").toArray()) {
            const int i = m.toInt(-1);
            if (i >= 0 && i < int(loadedIds.size()) &&
                loadedIds[std::size_t(i)] != kInvalidId)
                ids.push_back(loadedIds[std::size_t(i)]);
        }
        if (ids.size() >= 2) doc.addGroup(ss(go.value("name")), std::move(ids));
    }

    // 3c) Associatividade persistente: religa âncoras de cota e fontes de
    //     hachura pelos índices gravados. Vínculo para entidade que não
    //     carregou é descartado (a cota/hachura segue válida, só perde a
    //     regeneração automática — mesmo comportamento de apagar a fonte).
    const auto idAt = [&](int i) {
        return (i >= 0 && i < int(loadedIds.size())) ? loadedIds[std::size_t(i)]
                                                     : kInvalidId;
    };
    for (const QJsonValue& v : root.value("dimLinks").toArray()) {
        const QJsonObject o = v.toObject();
        auto* d = dynamic_cast<Dimension*>(doc.getEntity(idAt(o.value("ent").toInt(-1))));
        if (!d) continue;
        const auto anchorFrom = [&](const QJsonValue& av) {
            const QJsonObject ao = av.toObject();
            DimAnchor a;
            if (ao.isEmpty()) return a;
            a.id = idAt(ao.value("src").toInt(-1));
            if (a.id == kInvalidId) return DimAnchor{};
            a.which = DimAnchor::Which(std::clamp(ao.value("which").toInt(0), 0, 6));
            a.index = ao.value("index").toInt(0);
            return a;
        };
        d->setAnchors(anchorFrom(o.value("a")), anchorFrom(o.value("b")));
    }
    for (const QJsonValue& v : root.value("hatchLinks").toArray()) {
        const QJsonObject o = v.toObject();
        auto* h = dynamic_cast<Hatch*>(doc.getEntity(idAt(o.value("ent").toInt(-1))));
        if (!h) continue;
        std::vector<EntityId> src;
        for (const QJsonValue& sv : o.value("src").toArray()) {
            const EntityId id = idAt(sv.toInt(-1));
            if (id != kInvalidId) src.push_back(id);
        }
        if (!src.empty()) h->setSrcIds(std::move(src));
    }
    // Regen imediato: arquivo consistente é no-op; geometria defasada em
    // relação às fontes se auto-corrige já na abertura.
    doc.regenAssociativeNow();

    // 4) Pranchas e estilos.
    layoutsFromJson(root, layouts);
    styles = StyleTable{};
    for (const QJsonValue& e : root.value("dimStyles").toArray()) {
        const QJsonObject o = e.toObject();
        DimStyle s;
        s.name = ss(o.value("name"));
        if (s.name.empty()) continue;
        s.textHeight = o.value("textHeight").toDouble(2.5);
        s.arrowSize  = o.value("arrowSize").toDouble(-1.0);
        s.decimals   = o.value("decimals").toInt(2);
        s.suffix     = ss(o.value("suffix"));
        s.arrowType  = o.value("arrowType").toInt(0);
        s.tolPlus    = o.value("tolPlus").toDouble(0.0);
        s.tolMinus   = o.value("tolMinus").toDouble(0.0);
        s.annotative = o.value("annotative").toBool(false);
        styles.addDim(s);
    }
    for (const QJsonValue& e : root.value("textStyles").toArray()) {
        const QJsonObject o = e.toObject();
        TextStyle s;
        s.name = ss(o.value("name"));
        if (s.name.empty()) continue;
        s.height = o.value("height").toDouble(2.5);
        s.font   = ss(o.value("font"));
        s.annotative = o.value("annotative").toBool(false);
        styles.addText(s);
    }
    styles.setCurrentDim(ss(root.value("currentDim")));
    styles.setCurrentText(ss(root.value("currentText")));

    // 5) Configurações.
    const QJsonObject s = root.value("settings").toObject();
    const ProjectSettings defaults{};
    settings.ltScale      = s.value("ltScale").toDouble(defaults.ltScale);
    settings.unitIndex    = s.value("unitIndex").toInt(defaults.unitIndex);
    settings.unitDecimals = s.value("unitDecimals").toInt(defaults.unitDecimals);
    settings.unitSuffix   = s.value("unitSuffix").toString(defaults.unitSuffix);
    settings.currentLayer = s.value("currentLayer").toString(defaults.currentLayer);
    if (!doc.layers().contains(settings.currentLayer.toStdString()))
        settings.currentLayer = QStringLiteral("0");
    settings.plotStyle = plotStyleFromJson(root.value("plotStyle"));
    // Escala de anotação (CANNOSCALE) e UCS vivem no documento.
    doc.setAnnoMmPerUnit(s.value("annoMmPerUnit").toDouble(1.0));
    doc.setUcs(Point3{s.value("ucsOx").toDouble(0.0),
                      s.value("ucsOy").toDouble(0.0), 0.0},
               s.value("ucsAngle").toDouble(0.0));

    return true;
}

QImage projectThumbnail(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument jdoc = QJsonDocument::fromJson(f.readAll());
    if (!jdoc.isObject()) return {};
    const QString b64 = jdoc.object().value("thumbnail").toString();
    if (b64.isEmpty()) return {};
    QImage img;
    img.loadFromData(QByteArray::fromBase64(b64.toLatin1()), "PNG");
    return img;
}

// ============================================================================
//  Estilos de plotagem (CTB) <-> JSON — .zencad (seção "plotStyle") e .zctb
// ============================================================================

QJsonObject plotStyleToJson(const PlotStyleTable& t) {
    QJsonObject o;
    o["name"]   = qs(t.name);
    o["active"] = t.active;
    QJsonArray entries;
    for (const PlotLayerStyle& e : t.entries) {
        QJsonObject eo;
        eo["layer"]     = qs(e.layer);
        eo["plot"]      = e.plot;
        eo["colorMode"] = e.colorMode;
        if (e.colorMode == 3) eo["color"] = toJson(e.color);
        if (e.lineWeightMm >= 0.0) eo["lineWeightMm"] = e.lineWeightMm;
        entries.append(eo);
    }
    o["entries"] = entries;
    return o;
}

PlotStyleTable plotStyleFromJson(const QJsonValue& v) {
    PlotStyleTable t;
    const QJsonObject o = v.toObject();
    if (o.isEmpty()) return t;
    t.name   = ss(o.value("name"));
    if (t.name.empty()) t.name = "Padrao";
    t.active = o.value("active").toBool(false);
    for (const QJsonValue& ev : o.value("entries").toArray()) {
        const QJsonObject eo = ev.toObject();
        PlotLayerStyle e;
        e.layer = ss(eo.value("layer"));
        if (e.layer.empty()) continue;
        e.plot         = eo.value("plot").toBool(true);
        e.colorMode    = std::clamp(eo.value("colorMode").toInt(0), 0, 3);
        e.color        = rgbaFrom(eo.value("color"), {0, 0, 0, 255});
        e.lineWeightMm = eo.value("lineWeightMm").toDouble(-1.0);
        t.entries.push_back(std::move(e));
    }
    return t;
}

} // namespace cad
