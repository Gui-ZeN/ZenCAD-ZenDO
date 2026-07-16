// src/core/geometry/Leader.cpp
#include "core/geometry/Leader.hpp"
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

// Emite o texto (traços) ao batch usando a MESMA fonte de traços da Dimension/
// MText (strokeText -> pares de pontos = segmentos GL_LINES). Não reimplementa
// fonte: reusa exatamente o pipeline de texto do resto da geometria.
void emitText(RenderBatch& batch, const std::string& s, const Point3& pos,
              double height, double rotation) {
    const std::vector<Point3> pts = strokeText(s, pos, height, rotation);
    for (std::size_t i = 0; i + 1 < pts.size(); i += 2)
        batch.addSegment(pts[i], pts[i + 1]);
}

// Cabeça de seta como um "V" de 2 segmentos (duas linhas curtas formando a
// ponta), apontando de `from` para `tip`. Copiado da construção de seta da
// Dimension (emitArrow) para manter a consistência visual entre cota e chamada.
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

void Leader::emitTo(RenderBatch& batch) const {
    if (m_points.size() < 2) return;  // chamada precisa de ao menos 2 pontos

    const double h     = m_textHeight;
    // Tamanho da seta: automático (< 0) cai para a altura do texto — mesma
    // regra usada pela Dimension.
    const double arrow = (m_arrowSize >= 0.0) ? m_arrowSize : h;

    // 1) Segmentos da polilinha (m_points[0] -> [1] -> ...).
    for (std::size_t i = 1; i < m_points.size(); ++i)
        batch.addSegment(m_points[i - 1], m_points[i]);

    // 2) Ponta de seta no primeiro ponto, apontando ao longo de [0] -> [1].
    //    tip = m_points[0]; from = m_points[1] (a seta aponta para fora, para a
    //    ponta da chamada), igual ao emitArrow da Dimension.
    emitArrow(batch, m_points[0], m_points[1], arrow);

    // 3) Texto opcional começando perto do ÚLTIMO ponto (canto inferior-esquerdo
    //    deslocado por uma pequena folga, como a Dimension faz no Radius/Diameter).
    if (!m_text.empty()) {
        const Point3& last = m_points.back();
        const Point3 tpos{last.x + h * 0.3, last.y + h * 0.3, last.z};
        emitText(batch, m_text, tpos, h, 0.0);
    }
}

AABB Leader::boundingBox() const {
    // Reaproveita a emissão: a caixa envolve TODOS os vértices emitidos
    // (polilinha, seta e texto) — exato e sem duplicar a lógica de layout.
    RenderBatch tmp;
    emitTo(tmp);
    AABB b;
    for (const Point3& p : tmp.lineVertices) b.expand(p);
    if (!b.valid())
        for (const Point3& p : m_points) b.expand(p);
    return b;
}

HitResult Leader::hitTest(const Ray& pickRay, double tol) const {
    HitResult r;
    if (m_points.size() < 2) return r;

    const Point3 p = pickRay.origin;
    double bestDist = std::numeric_limits<double>::max();
    Point3 bestPt;
    // Menor distância de p aos segmentos da polilinha (plano XY).
    for (std::size_t i = 1; i < m_points.size(); ++i) {
        const Point3 c  = closestPointOnSegment2D(p, m_points[i - 1], m_points[i]);
        const double dx = p.x - c.x, dy = p.y - c.y;
        const double d  = std::sqrt(dx * dx + dy * dy);
        if (d < bestDist) { bestDist = d; bestPt = c; }
    }
    if (bestDist <= tol) { r.hit = true; r.distance = bestDist; r.point = bestPt; }
    return r;
}

void Leader::transform(const Matrix4& m) {
    for (Point3& v : m_points)
        v = m.transformPoint(v);
    // Escala a altura do texto pela escala do eixo X (mesma abordagem da Dimension).
    const Vec3 xAxis = m.transformVector(Vec3{1.0, 0.0, 0.0});
    m_textHeight *= std::sqrt(xAxis.x * xAxis.x + xAxis.y * xAxis.y);
}

void Leader::appendSnapPoints(std::vector<SnapPoint>& out) const {
    // Cada vértice da chamada vira um ponto de captura de extremidade.
    for (const Point3& v : m_points)
        out.push_back({v, SnapType::Endpoint});
}

std::unique_ptr<Entity> Leader::clone() const {
    return std::make_unique<Leader>(*this);
}

} // namespace cad
