// src/core/geometry/BlockRef.cpp
#include "core/geometry/BlockRef.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/geometry/MText.hpp"
#include "core/document/BlockTable.hpp"

namespace cad {

std::unique_ptr<BlockRef> BlockRef::fromDefinition(const BlockDefinition& def,
                                                   const Matrix4& insertXform) {
    auto b = std::make_unique<BlockRef>(def.cloneMembers(), insertXform);
    b->m_blockName = def.name;
    // Atributos: cada inserção nasce com os VALORES PADRÃO da definição.
    b->m_attValues.reserve(def.attdefs.size());
    for (const AttDefSpec& a : def.attdefs)
        b->m_attValues.push_back({a.tag, a.defValue, a.pos, a.height});
    return b;
}

std::unique_ptr<BlockRef> BlockRef::fromEntities(const std::vector<const Entity*>& ents,
                                                 const Point3& base) {
    std::vector<EntityPtr> members;
    const Matrix4 toLocal = Matrix4::translation(Vec3{-base.x, -base.y, -base.z});
    for (const Entity* e : ents) {
        if (!e) continue;
        EntityPtr c = e->clone();
        c->transform(toLocal);   // ponto-base vira a origem local do bloco
        members.push_back(std::move(c));
    }
    return std::make_unique<BlockRef>(std::move(members),
                                      Matrix4::translation(Vec3{base.x, base.y, base.z}));
}

std::vector<EntityPtr> BlockRef::explodedClones() const {
    std::vector<EntityPtr> out;
    out.reserve(m_members.size() + m_attValues.size());
    for (const EntityPtr& m : m_members) {
        if (!memberVisible(*m)) continue;   // estado de visibilidade da inserção
        EntityPtr c = m->clone();
        c->transform(m_xform);
        out.push_back(std::move(c));
    }
    // Explodir materializa os valores de atributo como TEXTO comum.
    for (const AttValue& a : m_attValues) {
        if (a.value.empty()) continue;
        auto t = std::make_unique<MText>(a.pos, a.value, a.height);
        t->transform(m_xform);
        out.push_back(std::move(t));
    }
    return out;
}

AABB BlockRef::boundingBox() const {
    // Sem clone: transforma os 4 cantos do bbox LOCAL de cada membro pela
    // inserção e expande. Conservador (AABB de AABB rotacionada) e barato —
    // este método roda a cada reindexação do documento.
    AABB bb;
    auto expandLocal = [&](const AABB& lb) {
        if (!lb.valid()) return;
        const Point3 corners[4] = { {lb.min.x, lb.min.y, 0.0}, {lb.max.x, lb.min.y, 0.0},
                                    {lb.max.x, lb.max.y, 0.0}, {lb.min.x, lb.max.y, 0.0} };
        for (const Point3& c : corners) bb.expand(m_xform.transformPoint(c));
    };
    for (const EntityPtr& m : m_members)
        if (memberVisible(*m)) expandLocal(m->boundingBox());
    for (const AttValue& a : m_attValues) {
        if (a.value.empty()) continue;
        expandLocal(MText(a.pos, a.value, a.height).boundingBox());
    }
    return bb;
}

void BlockRef::emitTo(RenderBatch& batch) const {
    // Sem clone: emite os membros em coords LOCAIS num batch de trabalho e
    // transforma os VÉRTICES pela inserção. Evita o deep-clone de todos os
    // membros a cada regen (multiplicador O(instâncias × membros) no render).
    RenderBatch local;
    for (const EntityPtr& m : m_members)
        if (memberVisible(*m)) m->emitTo(local);
    // Atributos: cada valor vira texto em coords locais (mesma transformação).
    for (const AttValue& a : m_attValues) {
        if (a.value.empty()) continue;
        MText t(a.pos, a.value, a.height);
        t.emitTo(local);
    }
    batch.lineVertices.reserve(batch.lineVertices.size() + local.lineVertices.size());
    for (const Point3& v : local.lineVertices) batch.lineVertices.push_back(m_xform.transformPoint(v));
    batch.fillVertices.reserve(batch.fillVertices.size() + local.fillVertices.size());
    for (const Point3& v : local.fillVertices) batch.fillVertices.push_back(m_xform.transformPoint(v));
}

HitResult BlockRef::hitTest(const Ray& pickRay, double tol) const {
    HitResult best;
    for (const EntityPtr& m : m_members) {
        if (!memberVisible(*m)) continue;
        EntityPtr c = m->clone();
        c->transform(m_xform);
        const HitResult h = c->hitTest(pickRay, tol);
        if (h.hit && (!best.hit || h.distance < best.distance)) best = h;
    }
    return best;
}

void BlockRef::transform(const Matrix4& m) {
    m_xform = m * m_xform;   // compõe a transformação de inserção
}

void BlockRef::appendSnapPoints(std::vector<SnapPoint>& out) const {
    // Ponto de INSERÇÃO do bloco (OSNAP Insertion); também Endpoint para
    // continuar capturável com o padrão curado.
    const Point3 ins = m_xform.transformPoint(Point3{0, 0, 0});
    out.push_back({ins, SnapType::Endpoint});
    out.push_back({ins, SnapType::Insertion});
}

std::unique_ptr<Entity> BlockRef::clone() const {
    std::vector<EntityPtr> cm;
    cm.reserve(m_members.size());
    for (const EntityPtr& m : m_members) cm.push_back(m->clone());
    auto c = std::make_unique<BlockRef>(std::move(cm), m_xform);
    c->m_blockName = m_blockName;
    c->m_attValues = m_attValues;
    c->m_hidden    = m_hidden;
    return c;
}

} // namespace cad
