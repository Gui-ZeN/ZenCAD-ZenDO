// src/core/geometry/Wall.cpp
#include "core/geometry/Wall.hpp"
#include "core/geometry/RenderBatch.hpp"
#include <algorithm>
#include <cmath>

namespace cad {
namespace {

struct V2 { double x, y; };
inline V2 dirOf(const Point3& a, const Point3& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double l = std::hypot(dx, dy);
    return l > 1e-12 ? V2{dx / l, dy / l} : V2{1.0, 0.0};
}
inline V2 perp(const V2& d) { return V2{-d.y, d.x}; }   // "esquerda" do eixo
inline Point3 off(const Point3& p, const V2& n, double t) {
    return {p.x + n.x * t, p.y + n.y * t, 0.0};
}
inline Point3 lerp(const Point3& a, const Point3& b, double u) {
    return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, 0.0};
}

} // namespace

double Wall::axisLength() const {
    double L = 0.0;
    for (std::size_t i = 1; i < m_axis.size(); ++i)
        L += std::hypot(m_axis[i].x - m_axis[i - 1].x, m_axis[i].y - m_axis[i - 1].y);
    return L;
}

double Wall::stationOf(const Point3& p) const {
    double best = 1e300, bestS = 0.0, acc = 0.0;
    for (std::size_t i = 1; i < m_axis.size(); ++i) {
        const Point3& a = m_axis[i - 1];
        const Point3& b = m_axis[i];
        const double lx = b.x - a.x, ly = b.y - a.y;
        const double ll = lx * lx + ly * ly;
        double u = ll > 1e-18 ? ((p.x - a.x) * lx + (p.y - a.y) * ly) / ll : 0.0;
        u = std::clamp(u, 0.0, 1.0);
        const double qx = a.x + lx * u, qy = a.y + ly * u;
        const double d = std::hypot(p.x - qx, p.y - qy);
        if (d < best) { best = d; bestS = acc + std::sqrt(ll) * u; }
        acc += std::sqrt(ll);
    }
    return bestS;
}

// Cantos das faces com junção em ESQUADRIA. O comprimento do bico é limitado
// (ângulos muito rasos não geram espinhos quilométricos).
void Wall::faceCorners(std::vector<Point3>& L, std::vector<Point3>& R) const {
    const std::size_t n = m_axis.size();
    L.resize(n); R.resize(n);
    if (n < 2) return;
    const double h = m_thickness * 0.5;
    for (std::size_t i = 0; i < n; ++i) {
        V2 nrm;
        if (i == 0) {
            nrm = perp(dirOf(m_axis[0], m_axis[1]));
            L[i] = off(m_axis[i], nrm, h);
            R[i] = off(m_axis[i], nrm, -h);
        } else if (i == n - 1) {
            nrm = perp(dirOf(m_axis[n - 2], m_axis[n - 1]));
            L[i] = off(m_axis[i], nrm, h);
            R[i] = off(m_axis[i], nrm, -h);
        } else {
            const V2 n1 = perp(dirOf(m_axis[i - 1], m_axis[i]));
            const V2 n2 = perp(dirOf(m_axis[i], m_axis[i + 1]));
            double mx = n1.x + n2.x, my = n1.y + n2.y;
            const double ml = std::hypot(mx, my);
            if (ml < 1e-9) {           // 180°: segue a normal do 1º trecho
                L[i] = off(m_axis[i], n1, h);
                R[i] = off(m_axis[i], n1, -h);
                continue;
            }
            mx /= ml; my /= ml;
            const double cosHalf = mx * n1.x + my * n1.y;
            const double len = h / std::max(cosHalf, 0.2);   // limita o bico (5x)
            L[i] = {m_axis[i].x + mx * len, m_axis[i].y + my * len, 0.0};
            R[i] = {m_axis[i].x - mx * len, m_axis[i].y - my * len, 0.0};
        }
    }
}

void Wall::emitTo(RenderBatch& batch) const {
    if (m_axis.size() < 2) return;
    std::vector<Point3> L, R;
    faceCorners(L, R);

    // Pontas: arremate reto.
    batch.addSegment(L.front(), R.front());
    batch.addSegment(L.back(), R.back());

    double acc = 0.0;
    for (std::size_t k = 0; k + 1 < m_axis.size(); ++k) {
        const Point3& a = m_axis[k];
        const Point3& b = m_axis[k + 1];
        const double lk = std::hypot(b.x - a.x, b.y - a.y);
        if (lk < 1e-12) continue;
        const V2 d = dirOf(a, b);
        const V2 nn = perp(d);

        // Vãos DESTE segmento (inteiramente contidos), em u local [0..1].
        struct Cut { double u0, u1; const Opening* op; };
        std::vector<Cut> cuts;
        for (const Opening& op : m_openings) {
            const double s0 = op.station, s1 = op.station + op.width;
            if (s0 >= acc - 1e-9 && s1 <= acc + lk + 1e-9 && op.width > 1e-9)
                cuts.push_back({(s0 - acc) / lk, (s1 - acc) / lk, &op});
        }
        std::sort(cuts.begin(), cuts.end(),
                  [](const Cut& x, const Cut& y) { return x.u0 < y.u0; });

        // Trechos SÓLIDOS entre vãos: faces + preenchimento.
        auto solid = [&](double u0, double u1) {
            if (u1 - u0 < 1e-9) return;
            const Point3 l0 = lerp(L[k], L[k + 1], u0), l1 = lerp(L[k], L[k + 1], u1);
            const Point3 r0 = lerp(R[k], R[k + 1], u0), r1 = lerp(R[k], R[k + 1], u1);
            batch.addSegment(l0, l1);
            batch.addSegment(r0, r1);
            batch.fillVertices.push_back(l0);   // 2 triângulos do quadrilátero
            batch.fillVertices.push_back(l1);
            batch.fillVertices.push_back(r1);
            batch.fillVertices.push_back(l0);
            batch.fillVertices.push_back(r1);
            batch.fillVertices.push_back(r0);
        };
        double cur = 0.0;
        for (const Cut& c : cuts) {
            solid(cur, c.u0);
            // ombreiras (jambas) nas duas bordas do vão
            batch.addSegment(lerp(L[k], L[k + 1], c.u0), lerp(R[k], R[k + 1], c.u0));
            batch.addSegment(lerp(L[k], L[k + 1], c.u1), lerp(R[k], R[k + 1], c.u1));
            const Point3 p0 = lerp(a, b, c.u0), p1 = lerp(a, b, c.u1);
            if (c.op->kind == 2) {
                // JANELA: 3 linhas ao longo do vão (faces + eixo)
                const double h = m_thickness * 0.5;
                batch.addSegment(off(p0, nn, h), off(p1, nn, h));
                batch.addSegment(p0, p1);
                batch.addSegment(off(p0, nn, -h), off(p1, nn, -h));
            } else if (c.op->kind == 1) {
                // PORTA: folha aberta a 90° + arco de giro, na FACE do lado
                // de abertura, articulada na dobradiça escolhida.
                const double h = m_thickness * 0.5;
                const double sd = (c.op->side >= 0) ? 1.0 : -1.0;
                const Point3 fh = off(c.op->hingeAtEnd ? p1 : p0, nn, h * sd);
                const Point3 fo = off(c.op->hingeAtEnd ? p0 : p1, nn, h * sd);
                const double w = std::hypot(fo.x - fh.x, fo.y - fh.y);
                if (w > 1e-9) {
                    const Point3 tip{fh.x + nn.x * sd * w, fh.y + nn.y * sd * w, 0.0};
                    batch.addSegment(fh, tip);
                    const double aB = std::atan2(fo.y - fh.y, fo.x - fh.x);
                    const double aT = std::atan2(tip.y - fh.y, tip.x - fh.x);
                    const double cross = (fo.x - fh.x) * (tip.y - fh.y) -
                                         (fo.y - fh.y) * (tip.x - fh.x);
                    const double s0 = (cross >= 0.0) ? aB : aT;
                    const double s1 = (cross >= 0.0) ? aT : aB;
                    const int N = 24;                    // quarto de volta
                    double sweep = s1 - s0;
                    while (sweep < 0.0) sweep += 6.283185307179586;
                    for (int i = 0; i < N; ++i) {
                        const double t0 = s0 + sweep * i / N;
                        const double t1 = s0 + sweep * (i + 1) / N;
                        batch.addSegment({fh.x + w * std::cos(t0), fh.y + w * std::sin(t0), 0.0},
                                         {fh.x + w * std::cos(t1), fh.y + w * std::sin(t1), 0.0});
                    }
                }
            }
            cur = c.u1;
        }
        solid(cur, 1.0);
        acc += lk;
    }
}

AABB Wall::boundingBox() const {
    AABB bb;
    if (m_axis.size() < 2) {
        for (const Point3& p : m_axis) bb.expand(p);
        return bb;
    }
    std::vector<Point3> L, R;
    faceCorners(L, R);
    for (const Point3& p : L) bb.expand(p);
    for (const Point3& p : R) bb.expand(p);
    return bb;
}

HitResult Wall::hitTest(const Ray& pickRay, double tol) const {
    HitResult res;
    const Point3& p = pickRay.origin;
    double best = 1e300;
    for (std::size_t i = 1; i < m_axis.size(); ++i) {
        const Point3& a = m_axis[i - 1];
        const Point3& b = m_axis[i];
        const double lx = b.x - a.x, ly = b.y - a.y;
        const double ll = lx * lx + ly * ly;
        double u = ll > 1e-18 ? ((p.x - a.x) * lx + (p.y - a.y) * ly) / ll : 0.0;
        u = std::clamp(u, 0.0, 1.0);
        const double d = std::hypot(p.x - (a.x + lx * u), p.y - (a.y + ly * u));
        best = std::min(best, d);
    }
    if (best <= m_thickness * 0.5 + tol) {
        res.hit = true;
        res.distance = best;
    }
    return res;
}

void Wall::transform(const Matrix4& m) {
    for (Point3& p : m_axis) p = m.transformPoint(p);
    // Escala uniforme: espessura e vãos acompanham o comprimento do eixo.
    const Point3 o = m.transformPoint(Point3{0, 0, 0});
    const Point3 e = m.transformPoint(Point3{1, 0, 0});
    const double s = std::hypot(e.x - o.x, e.y - o.y);
    if (std::abs(s - 1.0) > 1e-9 && s > 1e-12) {
        m_thickness *= s;
        for (Opening& op : m_openings) { op.station *= s; op.width *= s; }
    }
}

void Wall::appendSnapPoints(std::vector<SnapPoint>& out) const {
    for (std::size_t i = 0; i < m_axis.size(); ++i) {
        out.push_back({m_axis[i], SnapType::Endpoint});
        if (i + 1 < m_axis.size())
            out.push_back({lerp(m_axis[i], m_axis[i + 1], 0.5), SnapType::Midpoint});
    }
    // bordas dos vãos (para cotar/emendar): pontos correspondentes no eixo
    for (const Opening& op : m_openings) {
        for (const double s : {op.station, op.station + op.width}) {
            double rem = s;
            for (std::size_t i = 1; i < m_axis.size(); ++i) {
                const double lk = std::hypot(m_axis[i].x - m_axis[i - 1].x,
                                             m_axis[i].y - m_axis[i - 1].y);
                if (rem <= lk + 1e-9) {
                    out.push_back({lerp(m_axis[i - 1], m_axis[i],
                                        lk > 1e-12 ? rem / lk : 0.0),
                                   SnapType::Endpoint});
                    break;
                }
                rem -= lk;
            }
        }
    }
}

std::unique_ptr<Entity> Wall::clone() const {
    return std::make_unique<Wall>(*this);   // copia também as props da base
}

} // namespace cad
