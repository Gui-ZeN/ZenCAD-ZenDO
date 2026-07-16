// src/core/geometry/MLeader.cpp
#include "core/geometry/MLeader.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/text/StrokeFont.hpp"
#include "core/math/Segment.hpp"
#include "core/math/Vec.hpp"
#include "core/math/Matrix4.hpp"
#include <cmath>
#include <limits>
#include <vector>

namespace cad {
namespace {

// Emite o texto (traços) ao batch usando a MESMA fonte de traços do Leader/
// Dimension (strokeText -> pares de pontos = segmentos GL_LINES). Idêntico ao
// emitText do Leader, para manter o mesmo visual de anotação.
void emitText(RenderBatch& batch, const std::string& s, const Point3& pos,
              double height, double rotation) {
    const std::vector<Point3> pts = strokeText(s, pos, height, rotation);
    for (std::size_t i = 0; i + 1 < pts.size(); i += 2)
        batch.addSegment(pts[i], pts[i + 1]);
}

// Cabeça de seta como um "V" de 2 segmentos, apontando de `from` para `tip`.
// COPIADO do emitArrow do Leader (que por sua vez veio da Dimension) — mesma
// geometria de ponta para coerência visual entre chamada simples e multileader.
void emitArrow(RenderBatch& batch, const Point3& tip, const Point3& from, double size) {
    Vec3 dir = (tip - from);
    const double len = dir.length();
    if (len <= 0.0) return;
    dir = dir * (1.0 / len);                 // unitário from->tip = direção da ponta
    const Vec3 perp{-dir.y, dir.x, 0.0};
    const double back = size;                // recuo ao longo do eixo
    const double half = size * 0.35;         // meia-abertura da seta
    const Point3 base = tip - dir * back;
    const Point3 a{base.x + perp.x * half, base.y + perp.y * half, base.z};
    const Point3 b{base.x - perp.x * half, base.y - perp.y * half, base.z};
    batch.addSegment(tip, a);
    batch.addSegment(tip, b);
}

} // namespace

void MLeader::emitTo(RenderBatch& batch) const {
    const double h = m_textHeight;
    // Tamanho da seta: automático (< 0) cai para a altura do texto — mesma regra
    // do Leader/Dimension.
    const double arrow = (m_arrowSize >= 0.0) ? m_arrowSize : h;

    // 1) Para CADA chamada: segmentos da polilinha + ponta de seta no 1º ponto.
    for (const std::vector<Point3>& leader : m_leaders) {
        if (leader.empty()) continue;

        // Segmentos da polilinha (leader[0] -> [1] -> ...).
        for (std::size_t i = 1; i < leader.size(); ++i)
            batch.addSegment(leader[i - 1], leader[i]);

        // Seta no primeiro ponto, apontando ao longo de [0] -> [1]. Precisa de
        // >=2 pontos para haver direção; com 1 ponto, pula a seta.
        if (leader.size() >= 2)
            emitArrow(batch, leader[0], leader[1], arrow);
    }

    // 2) Texto único na âncora m_textPos (mesmo helper de texto do Leader).
    if (!m_text.empty())
        emitText(batch, m_text, m_textPos, h, 0.0);
}

AABB MLeader::boundingBox() const {
    // Reaproveita a emissão: a caixa envolve TODOS os vértices emitidos
    // (polilinhas, setas e texto) — exato e sem duplicar a lógica de layout.
    RenderBatch tmp;
    emitTo(tmp);
    AABB b;
    for (const Point3& p : tmp.lineVertices) b.expand(p);
    if (!b.valid()) {
        // Fallback: ao menos os pontos brutos de todas as chamadas + a âncora.
        for (const std::vector<Point3>& leader : m_leaders)
            for (const Point3& p : leader) b.expand(p);
        b.expand(m_textPos);
    }
    return b;
}

HitResult MLeader::hitTest(const Ray& pickRay, double tol) const {
    HitResult r;

    const Point3 p = pickRay.origin;
    double bestDist = std::numeric_limits<double>::max();
    Point3 bestPt;
    // Menor distância de p a QUALQUER segmento de QUALQUER chamada (plano XY).
    for (const std::vector<Point3>& leader : m_leaders) {
        for (std::size_t i = 1; i < leader.size(); ++i) {
            const Point3 c  = closestPointOnSegment2D(p, leader[i - 1], leader[i]);
            const double dx = p.x - c.x, dy = p.y - c.y;
            const double d  = std::sqrt(dx * dx + dy * dy);
            if (d < bestDist) { bestDist = d; bestPt = c; }
        }
    }
    if (bestDist <= tol) { r.hit = true; r.distance = bestDist; r.point = bestPt; }
    return r;
}

void MLeader::transform(const Matrix4& m) {
    for (std::vector<Point3>& leader : m_leaders)
        for (Point3& v : leader)
            v = m.transformPoint(v);
    m_textPos = m.transformPoint(m_textPos);
    // Escala a altura do texto pela escala do eixo X (mesma abordagem do Leader).
    const Vec3 xAxis = m.transformVector(Vec3{1.0, 0.0, 0.0});
    m_textHeight *= std::sqrt(xAxis.x * xAxis.x + xAxis.y * xAxis.y);
}

void MLeader::appendSnapPoints(std::vector<SnapPoint>& out) const {
    // Cada vértice de cada chamada vira um ponto de captura de extremidade...
    for (const std::vector<Point3>& leader : m_leaders)
        for (const Point3& v : leader)
            out.push_back({v, SnapType::Endpoint});
    // ...e a âncora do texto também.
    out.push_back({m_textPos, SnapType::Endpoint});
}

std::unique_ptr<Entity> MLeader::clone() const {
    return std::make_unique<MLeader>(*this);
}

} // namespace cad
