// src/core/geometry/Ellipse.cpp
#include "core/geometry/Ellipse.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/geometry/Tessellation.hpp"
#include "core/math/Segment.hpp"
#include "core/math/Constants.hpp"
#include <algorithm>
#include <cmath>

namespace cad {
namespace {

// Vetor do semi-eixo menor: perpendicular ao maior (giro de 90° CCW no plano XY:
// {-y, x}) e escalado por ratio. Comprimento resultante = b = a * ratio.
Vec3 minorVecOf(const Vec3& major, double ratio) {
    return Vec3{-major.y * ratio, major.x * ratio, 0.0};
}

// Varredura paramétrica CCW de t0 a t1, normalizada a (0, 2pi]. Uma volta completa
// (t1 - t0 múltiplo de 2pi) é mapeada para kTwoPi, não para 0.
double sweepParam(double t0, double t1) {
    double s = std::fmod(t1 - t0, kTwoPi);
    if (s <= 0.0) s += kTwoPi;
    return s;
}

// Ponto da elipse no parâmetro angular t: center + cos(t)*major + sin(t)*minor.
Point3 pointAt(const Point3& c, const Vec3& major, const Vec3& minor, double t) {
    const double ct = std::cos(t), st = std::sin(t);
    return Point3{c.x + ct * major.x + st * minor.x,
                  c.y + ct * major.y + st * minor.y,
                  c.z};
}

// Número de segmentos da tesselação para uma dada varredura paramétrica `sweep`.
// O piso de 48 vale para a volta completa; arcos recebem uma fração proporcional
// dele, garantindo qualidade equivalente em trechos parciais.
int segmentCountOf(const Vec3& major, double sweep) {
    const int n = segmentsForArc(major.length(), sweep);
    const int floorN = std::max(1, static_cast<int>(std::ceil(48.0 * sweep / kTwoPi)));
    return std::max(n, floorN);
}

} // namespace

Ellipse Ellipse::fromCenterAxes(Point3 center, Vec3 majorVec, double minorLen) {
    const double a     = majorVec.length();
    const double ratio = (a > 0.0) ? (minorLen / a) : 1.0;
    return Ellipse{center, majorVec, ratio};
}

Ellipse Ellipse::fromCenterAxesArc(Point3 center, Vec3 majorVec, double minorLen,
                                   double t0, double t1) {
    Ellipse e = fromCenterAxes(center, majorVec, minorLen);
    e.m_t0 = t0;
    e.m_t1 = t1;
    return e;
}

bool Ellipse::isArc() const {
    // Arco se a varredura (CCW, normalizada a (0, 2pi]) difere de uma volta cheia.
    const double sweep = sweepParam(m_t0, m_t1);
    return std::abs(sweep - kTwoPi) > 1e-9;
}

AABB Ellipse::boundingBox() const {
    // Envolve os próprios pontos da tesselação do trecho [t0, t1]: simples e
    // exato o suficiente. Amostra n+1 pontos (inclui ambos os extremos do arco).
    const Vec3   minor = minorVecOf(m_major, m_ratio);
    const double sweep = sweepParam(m_t0, m_t1);
    const int    n     = segmentCountOf(m_major, sweep);
    const double step  = sweep / n;
    AABB b;
    for (int i = 0; i <= n; ++i)
        b.expand(pointAt(m_center, m_major, minor, m_t0 + step * i));
    return b;
}

void Ellipse::emitTo(RenderBatch& batch) const {
    // Tessela apenas o intervalo [t0, t1] (varredura CCW). Para a elipse completa,
    // sweep == kTwoPi e o último ponto coincide com o primeiro, fechando o laço.
    const Vec3   minor = minorVecOf(m_major, m_ratio);
    const double sweep = sweepParam(m_t0, m_t1);
    const int    n     = segmentCountOf(m_major, sweep);
    const double step  = sweep / n;
    Point3 prev = pointAt(m_center, m_major, minor, m_t0);
    for (int i = 1; i <= n; ++i) {
        Point3 cur = pointAt(m_center, m_major, minor, m_t0 + step * i);
        batch.addSegment(prev, cur);
        prev = cur;
    }
}

HitResult Ellipse::hitTest(const Ray& pickRay, double tol) const {
    // Distância aproximada: amostra a tesselação e mede a menor distância
    // ponto-segmento (no plano XY) até o cursor.
    const Point3 p     = pickRay.origin;
    const Vec3   minor = minorVecOf(m_major, m_ratio);
    const double sweep = sweepParam(m_t0, m_t1);
    const int    n     = segmentCountOf(m_major, sweep);
    const double step  = sweep / n;

    HitResult r;
    double best = tol;
    Point3 prev = pointAt(m_center, m_major, minor, m_t0);
    for (int i = 1; i <= n; ++i) {
        const Point3 cur = pointAt(m_center, m_major, minor, m_t0 + step * i);
        const Point3 c   = closestPointOnSegment2D(p, prev, cur);
        const double d   = std::hypot(p.x - c.x, p.y - c.y);
        if (d <= best) {
            best = d;
            r.hit = true;
            r.distance = d;
            r.point = c;
        }
        prev = cur;
    }
    return r;
}

void Ellipse::transform(const Matrix4& m) {
    // m_center como ponto; m_major como direção. O ratio é mantido — assume-se
    // transformação conforme (translação + rotação + escala uniforme). Escala
    // não-uniforme/cisalhamento distorceriam a razão dos eixos (não tratado;
    // aceitável no escopo atual).
    m_center = m.transformPoint(m_center);
    m_major  = m.transformVector(m_major);
}

void Ellipse::appendSnapPoints(std::vector<SnapPoint>& out) const {
    const Vec3 minor = minorVecOf(m_major, m_ratio);
    out.push_back({m_center, SnapType::Center});
    if (isArc()) {
        // Arco elíptico: extremos do trecho [t0, t1] como Endpoint.
        out.push_back({pointAt(m_center, m_major, minor, m_t0), SnapType::Endpoint});
        out.push_back({pointAt(m_center, m_major, minor, m_t1), SnapType::Endpoint});
        return;
    }
    // Elipse completa: extremidades dos eixos (quadrantes).
    // center ± major (eixo maior) e center ± minor (menor).
    out.push_back({m_center + m_major, SnapType::Quadrant});
    out.push_back({m_center - m_major, SnapType::Quadrant});
    out.push_back({m_center + minor,   SnapType::Quadrant});
    out.push_back({m_center - minor,   SnapType::Quadrant});
}

std::unique_ptr<Entity> Ellipse::clone() const {
    // Cópia membro a membro (copy ctor implícito): inclui o intervalo [t0, t1].
    return std::make_unique<Ellipse>(*this);
}

} // namespace cad
