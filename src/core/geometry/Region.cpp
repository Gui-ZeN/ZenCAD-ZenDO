// src/core/geometry/Region.cpp
#include "core/geometry/Region.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/math/Segment.hpp"
#include "core/math/Vec.hpp"
#include "core/math/AABB.hpp"
#include <vector>
#include <cmath>

namespace cad {

namespace {

// Tolerância geométrica (mesma usada no Hatch/Wipeout) para casos degenerados.
constexpr double kTol = 1e-9;

// Teste ponto-em-polígono pela regra par-ímpar (ray casting horizontal) no XY.
// Replicado do padrão do Hatch/Wipeout — usado pelo hitTest para detectar clique
// no interior. (Os helpers do Hatch vivem num namespace anônimo, não exportados.)
bool pointInPolygon2D(const std::vector<Point3>& poly, double px, double py) {
    bool inside = false;
    const std::size_t n = poly.size();
    if (n < 3) return false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double yi = poly[i].y, yj = poly[j].y;
        const double xi = poly[i].x, xj = poly[j].x;
        if (((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

// Área 2x COM sinal (shoelace) de um loop no plano XY. >0 => CCW, <0 => CW.
// Mesma fórmula que o Hatch usa internamente (signedArea2D).
double signedArea2D(const std::vector<Point3>& p) {
    double a = 0.0;
    const std::size_t n = p.size();
    if (n < 3) return 0.0;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++)
        a += (p[j].x * p[i].y) - (p[i].x * p[j].y);
    return a; // = 2 * área orientada
}

} // namespace

AABB Region::boundingBox() const {
    // Caixa envolvente de TODOS os loops (externo + furos), igual ao Hatch.
    AABB b;
    for (const auto& loop : m_loops)
        for (const auto& v : loop)
            b.expand(v);
    return b;
}

void Region::emitTo(RenderBatch& batch) const {
    // A REGION desenha apenas o CONTORNO de cada loop (externo e furos) no canal de
    // linhas — SEM preenchimento por padrão. Cada loop é fechado implicitamente
    // (aresta final último->primeiro), exatamente como no contorno do Hatch.
    for (const auto& loop : m_loops) {
        const std::size_t n = loop.size();
        if (n < 2) continue;
        for (std::size_t i = 0; i < n; ++i) {
            const Point3& a = loop[i];
            const Point3& b = loop[(i + 1) % n];
            batch.addSegment(a, b);
        }
    }
}

HitResult Region::hitTest(const Ray& pickRay, double tol) const {
    const Point3 p = pickRay.origin;

    // (a) Perto de alguma aresta de algum loop? (mesmo padrão do Hatch/Wipeout)
    HitResult best;
    for (const auto& loop : m_loops) {
        const std::size_t n = loop.size();
        if (n < 2) continue;
        for (std::size_t i = 0; i < n; ++i) {
            const Point3& a = loop[i];
            const Point3& b = loop[(i + 1) % n];
            const Point3  c    = closestPointOnSegment2D(p, a, b);
            const double  dx   = p.x - c.x, dy = p.y - c.y;
            const double  dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= tol && (!best.hit || dist < best.distance)) {
                best.hit = true; best.distance = dist; best.point = c;
            }
        }
    }
    if (best.hit) return best;

    // (b) Dentro da área? Considera furos: pega se está dentro do contorno externo
    //     (loops[0]) E fora de TODOS os furos (loops[1..]).
    if (!m_loops.empty() && pointInPolygon2D(m_loops[0], p.x, p.y)) {
        bool emFuro = false;
        for (std::size_t k = 1; k < m_loops.size(); ++k) {
            if (pointInPolygon2D(m_loops[k], p.x, p.y)) { emFuro = true; break; }
        }
        if (!emFuro) {
            HitResult r;
            r.hit = true; r.distance = 0.0; r.point = p;
            return r;
        }
    }
    return {};
}

void Region::transform(const Matrix4& m) {
    // Transformação afim in-place de todos os vértices de todos os loops.
    for (auto& loop : m_loops)
        for (auto& v : loop)
            v = m.transformPoint(v);
}

void Region::appendSnapPoints(std::vector<SnapPoint>& out) const {
    // OSNAP: vértices de todos os loops como Endpoint (mesmo critério do Hatch).
    for (const auto& loop : m_loops)
        for (const auto& v : loop)
            out.push_back({v, SnapType::Endpoint});
}

std::unique_ptr<Entity> Region::clone() const {
    // Cópia profunda: m_loops é std::vector<std::vector<Point3>> (valores), então o
    // copy-ctor gerado já copia tudo — mesmo padrão de Hatch::clone()/Wipeout::clone().
    return std::make_unique<Region>(*this);
}

void Region::accept(EntityVisitor& v) const { v.visit(*this); }   // visit(const Region&) já existe

double Region::area() const {
    // Área = |área(externo)| - soma(|área(furo_k)|). Cada loop é medido por shoelace
    // em valor absoluto, então a orientação (CW/CCW) de cada loop é irrelevante.
    if (m_loops.empty()) return 0.0;

    const double externo = std::fabs(signedArea2D(m_loops[0])) * 0.5;
    double furos = 0.0;
    for (std::size_t k = 1; k < m_loops.size(); ++k)
        furos += std::fabs(signedArea2D(m_loops[k])) * 0.5;

    const double a = externo - furos;
    return a > 0.0 ? a : 0.0; // nunca negativa (furos maiores que o externo => 0)
}

} // namespace cad
