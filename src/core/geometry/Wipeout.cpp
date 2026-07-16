// src/core/geometry/Wipeout.cpp
#include "core/geometry/Wipeout.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/math/Segment.hpp"
#include "core/math/Vec.hpp"
#include "core/math/AABB.hpp"
#include <vector>
#include <algorithm>
#include <cmath>

namespace cad {

namespace {

// Tolerância geométrica (igual à usada no Hatch) para descartar casos degenerados.
constexpr double kTol = 1e-9;

// Teste ponto-em-polígono pela regra par-ímpar (ray casting horizontal) no XY.
// Copiado do padrão do Hatch — usado pelo hitTest para detectar clique no interior.
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

// --- Ear clipping (triangulação de polígono SIMPLES no plano XY) --------------
// Replicado fielmente da triangulação que o Hatch::Solid usa (Hatch.cpp). Os
// helpers vivem no Hatch num namespace anônimo (não exportados), então duplicamos
// aqui para manter o escopo restrito a estes dois arquivos.

// Área 2x com sinal (shoelace). >0 => CCW, <0 => CW.
double signedArea2D(const std::vector<Point3>& p) {
    double a = 0.0;
    const std::size_t n = p.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++)
        a += (p[j].x * p[i].y) - (p[i].x * p[j].y);
    return a; // = 2 * área orientada
}

// Z da componente do produto vetorial (b-a) x (c-a). Em CCW, >0 => convexo.
double crossZ(const Point3& a, const Point3& b, const Point3& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Ponto p dentro (ou na borda) do triângulo a,b,c? (sinais dos cross products).
bool pointInTriangle2D(const Point3& p, const Point3& a, const Point3& b, const Point3& c) {
    const double d1 = crossZ(a, b, p);
    const double d2 = crossZ(b, c, p);
    const double d3 = crossZ(c, a, p);
    const bool hasNeg = (d1 < -kTol) || (d2 < -kTol) || (d3 < -kTol);
    const bool hasPos = (d1 > kTol) || (d2 > kTol) || (d3 > kTol);
    return !(hasNeg && hasPos);
}

// Triangula o contorno `poly` (XY) por ear clipping e emite as orelhas no batch.
void triangulateEarClipping(const std::vector<Point3>& poly, RenderBatch& batch) {
    const std::size_t n = poly.size();
    if (n < 3) return; // nada a preencher

    // Anel de índices normalizado para CCW (assim "convexo" sempre é crossZ>0).
    std::vector<std::size_t> idx(n);
    if (signedArea2D(poly) >= 0.0) {
        for (std::size_t i = 0; i < n; ++i) idx[i] = i;            // já CCW
    } else {
        for (std::size_t i = 0; i < n; ++i) idx[i] = n - 1 - i;   // CW -> CCW
    }

    while (idx.size() > 3) {
        const std::size_t m = idx.size();
        bool earFound = false;

        for (std::size_t i = 0; i < m; ++i) {
            const std::size_t i0 = idx[(i + m - 1) % m];
            const std::size_t i1 = idx[i];
            const std::size_t i2 = idx[(i + 1) % m];
            const Point3& a = poly[i0];
            const Point3& b = poly[i1];
            const Point3& c = poly[i2];

            if (crossZ(a, b, c) <= kTol) continue;  // reflexo/colinear não é orelha

            bool contains = false;
            for (std::size_t k = 0; k < m; ++k) {
                const std::size_t vk = idx[k];
                if (vk == i0 || vk == i1 || vk == i2) continue;
                if (pointInTriangle2D(poly[vk], a, b, c)) { contains = true; break; }
            }
            if (contains) continue;

            batch.addTriangle(a, b, c);     // orelha válida
            idx.erase(idx.begin() + i);
            earFound = true;
            break;
        }

        if (!earFound) return; // fallback: evita laço infinito em casos degenerados
    }

    batch.addTriangle(poly[idx[0]], poly[idx[1]], poly[idx[2]]); // último triângulo
}

} // namespace

AABB Wipeout::boundingBox() const {
    AABB b;
    for (const auto& v : m_contour)
        b.expand(v);
    return b;
}

void Wipeout::emitTo(RenderBatch& batch) const {
    // Máscara opaca: preenche a área (triângulos no canal de fill) cobrindo o que
    // estiver atrás + desenha a borda (canal de linhas). Não há padrão de hachura.
    const std::size_t n = m_contour.size();

    // 1) Preenchimento sólido: tritura o contorno em triângulos (ear clipping),
    //    exatamente como o Hatch::Solid faz com cada loop.
    triangulateEarClipping(m_contour, batch);

    // 2) Borda: arestas consecutivas + fechamento último->primeiro.
    if (n < 2) return;
    for (std::size_t i = 0; i < n; ++i) {
        const Point3& a = m_contour[i];
        const Point3& b = m_contour[(i + 1) % n];
        batch.addSegment(a, b);
    }
}

HitResult Wipeout::hitTest(const Ray& pickRay, double tol) const {
    const Point3 p = pickRay.origin;

    // (a) Perto de alguma aresta da borda? (mesmo padrão do Hatch)
    HitResult best;
    const std::size_t n = m_contour.size();
    if (n >= 2) {
        for (std::size_t i = 0; i < n; ++i) {
            const Point3& a = m_contour[i];
            const Point3& b = m_contour[(i + 1) % n];
            const Point3  c    = closestPointOnSegment2D(p, a, b);
            const double  dx   = p.x - c.x, dy = p.y - c.y;
            const double  dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= tol && (!best.hit || dist < best.distance)) {
                best.hit = true; best.distance = dist; best.point = c;
            }
        }
    }
    if (best.hit) return best;

    // (b) Dentro do contorno (par-ímpar)? Como é máscara opaca, o interior pega.
    if (pointInPolygon2D(m_contour, p.x, p.y)) {
        HitResult r;
        r.hit = true; r.distance = 0.0; r.point = p;
        return r;
    }
    return {};
}

void Wipeout::transform(const Matrix4& m) {
    for (auto& v : m_contour)
        v = m.transformPoint(v);
}

void Wipeout::appendSnapPoints(std::vector<SnapPoint>& out) const {
    for (const auto& v : m_contour)
        out.push_back({v, SnapType::Endpoint});
}

std::unique_ptr<Entity> Wipeout::clone() const {
    // Cópia profunda: m_contour é std::vector<Point3> (valores), então o copy-ctor
    // gerado já copia tudo — mesmo padrão de Hatch::clone()/Polyline::clone().
    return std::make_unique<Wipeout>(*this);
}

void Wipeout::accept(EntityVisitor& v) const { v.visit(*this); }   // visit(const Wipeout&) já existe

} // namespace cad
