// src/core/mesh/HalfEdgeMesh.cpp
#include "core/mesh/HalfEdgeMesh.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>

namespace cad {

// ---------------------------------------------------------------- build ----
HalfEdgeMesh::Idx HalfEdgeMesh::addVertex(const Point3& p) {
    m_verts.push_back({p, kNone});
    return Idx(m_verts.size() - 1);
}

HalfEdgeMesh::Idx HalfEdgeMesh::addFace(const std::vector<Idx>& loop) {
    const std::size_t n = loop.size();
    if (!m_building || n < 3) return kNone;
    for (const Idx v : loop)
        if (v >= m_verts.size()) return kNone;
    for (std::size_t i = 0; i < n; ++i)   // aresta dirigida repetida?
        if (m_edgeMap.count({loop[i], loop[(i + 1) % n]})) return kNone;

    const Idx f = newFace();
    const Idx base = Idx(m_hes.size());
    for (std::size_t i = 0; i < n; ++i) newHalfEdge();
    for (std::size_t i = 0; i < n; ++i) {
        const Idx h = base + Idx(i);
        HalfEdge& he = m_hes[h];
        he.origin = loop[i];
        he.next   = base + Idx((i + 1) % n);
        he.prev   = base + Idx((i + n - 1) % n);
        he.face   = f;
        m_verts[loop[i]].he = h;
        m_edgeMap[{loop[i], loop[(i + 1) % n]}] = h;
        // gêmea já existe? costura.
        const auto it = m_edgeMap.find({loop[(i + 1) % n], loop[i]});
        if (it != m_edgeMap.end()) {
            he.twin = it->second;
            m_hes[it->second].twin = h;
        }
    }
    m_faces[f].he = base;
    return f;
}

// -------------------------------------------------------------- consulta ---
std::vector<HalfEdgeMesh::Idx> HalfEdgeMesh::faceHalfEdges(Idx f) const {
    std::vector<Idx> out;
    if (f >= m_faces.size()) return out;
    const Idx start = m_faces[f].he;
    Idx h = start;
    do {
        out.push_back(h);
        h = m_hes[h].next;
    } while (h != start && out.size() <= m_hes.size());
    return out;
}

std::vector<HalfEdgeMesh::Idx> HalfEdgeMesh::faceVertices(Idx f) const {
    std::vector<Idx> out;
    for (const Idx h : faceHalfEdges(f)) out.push_back(m_hes[h].origin);
    return out;
}

Vec3 HalfEdgeMesh::faceNormal(Idx f) const {
    // Newell: robusta p/ polígonos quase-planos e côncavos.
    const std::vector<Idx> vs = faceVertices(f);
    Vec3 n{};
    for (std::size_t i = 0; i < vs.size(); ++i) {
        const Point3& a = m_verts[vs[i]].p;
        const Point3& b = m_verts[vs[(i + 1) % vs.size()]].p;
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n.normalized();
}

double HalfEdgeMesh::faceArea(Idx f) const {
    const std::vector<Idx> vs = faceVertices(f);
    if (vs.size() < 3) return 0.0;
    Vec3 acc{};
    const Point3& o = m_verts[vs[0]].p;
    for (std::size_t i = 1; i + 1 < vs.size(); ++i)
        acc = acc + (m_verts[vs[i]].p - o).cross(m_verts[vs[i + 1]].p - o);
    return 0.5 * acc.length();
}

Point3 HalfEdgeMesh::faceCentroid(Idx f) const {
    const std::vector<Idx> vs = faceVertices(f);
    Point3 c{};
    if (vs.empty()) return c;
    for (const Idx v : vs) c = c + m_verts[v].p;
    return c * (1.0 / double(vs.size()));
}

HalfEdgeMesh::Idx HalfEdgeMesh::halfEdgeFromIn(Idx f, Idx v) const {
    for (const Idx h : faceHalfEdges(f))
        if (m_hes[h].origin == v) return h;
    return kNone;
}

// ------------------------------------------------------------ operadores ---
HalfEdgeMesh::Idx HalfEdgeMesh::splitEdge(Idx he, const Point3& p) {
    if (he >= m_hes.size()) return kNone;
    m_building = false;
    m_edgeMap.clear();

    const Idx t = m_hes[he].twin;
    const Idx w = addVertex(p);

    const Idx he2 = newHalfEdge();          // w -> v (continua o he)
    m_hes[he2] = {w, kNone, m_hes[he].next, he, m_hes[he].face};
    m_hes[m_hes[he].next].prev = he2;
    m_hes[he].next = he2;

    if (t != kNone) {
        const Idx t2 = newHalfEdge();       // w -> u (continua o twin)
        m_hes[t2] = {w, kNone, m_hes[t].next, t, m_hes[t].face};
        m_hes[m_hes[t].next].prev = t2;
        m_hes[t].next = t2;
        // recostura: he(u->w)~t2(w->u) · he2(w->v)~t(v->w)
        m_hes[he].twin = t2;  m_hes[t2].twin = he;
        m_hes[he2].twin = t;  m_hes[t].twin = he2;
    } else {
        m_hes[he2].twin = kNone;
    }
    m_verts[w].he = he2;
    return w;
}

HalfEdgeMesh::Idx HalfEdgeMesh::splitFace(Idx f, Idx vA, Idx vB) {
    if (f >= m_faces.size() || vA == vB) return kNone;
    const Idx heA = halfEdgeFromIn(f, vA);
    const Idx heB = halfEdgeFromIn(f, vB);
    if (heA == kNone || heB == kNone) return kNone;
    // adjacentes já compartilham aresta — nada a dividir
    if (m_hes[heA].next == heB || m_hes[heB].next == heA) return kNone;
    m_building = false;
    m_edgeMap.clear();

    const Idx pA = m_hes[heA].prev;
    const Idx pB = m_hes[heB].prev;
    const Idx e1 = newHalfEdge();   // vA -> vB (fica na face NOVA)
    const Idx e2 = newHalfEdge();   // vB -> vA (fica na f)

    m_hes[e2] = {vB, e1, heA, pB, f};
    m_hes[pB].next = e2;
    m_hes[heA].prev = e2;
    m_faces[f].he = heA;

    const Idx g = newFace();
    m_hes[e1] = {vA, e2, heB, pA, g};
    m_hes[pA].next = e1;
    m_hes[heB].prev = e1;
    m_faces[g].he = heB;
    for (Idx h = heB; ; h = m_hes[h].next) {   // ciclo novo assume a face g
        m_hes[h].face = g;
        if (m_hes[h].next == heB) break;
    }
    return g;
}

HalfEdgeMesh::Idx HalfEdgeMesh::extrudeFace(Idx f, double dist) {
    if (f >= m_faces.size()) return kNone;
    const std::vector<Idx> ring = faceHalfEdges(f);   // he_i: v_i -> v_{i+1}
    const std::size_t n = ring.size();
    if (n < 3) return kNone;
    m_building = false;
    m_edgeMap.clear();

    const Vec3 nrm = faceNormal(f);
    std::vector<Idx> vb(n), vt(n);
    for (std::size_t i = 0; i < n; ++i) vb[i] = m_hes[ring[i]].origin;
    for (std::size_t i = 0; i < n; ++i)
        vt[i] = addVertex(m_verts[vb[i]].p + nrm * dist);

    // quads laterais (b: base · r: sobe · u: topo · l: desce)
    struct Quad { Idx b, r, u, l, face; };
    std::vector<Quad> q(n);
    for (std::size_t i = 0; i < n; ++i)
        q[i] = {newHalfEdge(), newHalfEdge(), newHalfEdge(), newHalfEdge(),
                newFace()};

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        const Quad& Q = q[i];
        m_hes[Q.b] = {vb[i], kNone, Q.r, Q.l, Q.face};
        m_hes[Q.r] = {vb[j], kNone, Q.u, Q.b, Q.face};
        m_hes[Q.u] = {vt[j], kNone, Q.l, Q.r, Q.face};
        m_hes[Q.l] = {vt[i], kNone, Q.b, Q.u, Q.face};
        m_faces[Q.face].he = Q.b;

        // base assume o vínculo externo da aresta original
        const Idx t = m_hes[ring[i]].twin;
        m_hes[Q.b].twin = t;
        if (t != kNone) m_hes[t].twin = Q.b;

        // topo do quad ~ aresta da tampa (o próprio ring[i], que SOBE)
        m_hes[Q.u].twin = ring[i];
        m_hes[ring[i]].twin = Q.u;
        m_hes[ring[i]].origin = vt[i];

        m_verts[vb[i]].he = Q.b;
        m_verts[vt[i]].he = ring[i];
    }
    for (std::size_t i = 0; i < n; ++i) {      // verticais entre quads vizinhos
        const std::size_t j = (i + 1) % n;
        m_hes[q[i].r].twin = q[j].l;
        m_hes[q[j].l].twin = q[i].r;
    }
    return f;   // a própria f é a tampa (id preservado)
}

HalfEdgeMesh::Idx HalfEdgeMesh::insetFace(Idx f, std::vector<Point3> inner) {
    const std::size_t k = inner.size();
    if (f >= m_faces.size() || k < 3) return kNone;

    // orienta o laço interno IGUAL à face (Newell dos dois lados)
    const Vec3 n = faceNormal(f);
    Vec3 ni{};
    for (std::size_t i = 0; i < k; ++i) {
        const Point3& a = inner[i];
        const Point3& b = inner[(i + 1) % k];
        ni.x += (a.y - b.y) * (a.z + b.z);
        ni.y += (a.z - b.z) * (a.x + b.x);
        ni.z += (a.x - b.x) * (a.y + b.y);
    }
    if (ni.dot(n) < 0.0) std::reverse(inner.begin(), inner.end());

    m_building = false;
    m_edgeMap.clear();

    // âncora da ponte: vértice externo mais perto de inner[0]
    const std::vector<Idx> ring = faceHalfEdges(f);
    Idx heA = ring[0];
    double best = 1e300;
    for (const Idx h : ring) {
        const Point3& p = m_verts[m_hes[h].origin].p;
        const double d = (p - inner[0]).lengthSq();
        if (d < best) { best = d; heA = h; }
    }
    const Idx vA = m_hes[heA].origin;
    const Idx pA = m_hes[heA].prev;

    std::vector<Idx> w(k), ih(k), rh(k);
    for (std::size_t j = 0; j < k; ++j) w[j] = addVertex(inner[j]);
    for (std::size_t j = 0; j < k; ++j) { ih[j] = newHalfEdge(); rh[j] = newHalfEdge(); }

    // face interna g: ih[j] percorre w_j -> w_{j+1} (CCW, plugável)
    const Idx g = newFace();
    for (std::size_t j = 0; j < k; ++j) {
        const std::size_t jn = (j + 1) % k;
        m_hes[ih[j]] = {w[j], rh[j], ih[jn], ih[(j + k - 1) % k], g};
        m_hes[rh[j]] = {w[jn], ih[j], kNone, kNone, f};   // reverso, fica no anel
        m_verts[w[j]].he = ih[j];
    }
    m_faces[g].he = ih[0];

    // ponte (keyhole): b1 vA->w0 e b2 w0->vA, gêmeas entre si, AMBAS na f.
    // Ciclo do anel: ... pA -> b1 -> rh[k-1] -> ... -> rh[0] -> b2 -> heA ...
    const Idx b1 = newHalfEdge(), b2 = newHalfEdge();
    m_hes[b1] = {vA, b2, rh[k - 1], pA, f};
    m_hes[b2] = {w[0], b1, heA, rh[0], f};
    m_hes[pA].next = b1;
    m_hes[heA].prev = b2;
    for (std::size_t j = 0; j < k; ++j) {
        m_hes[rh[j]].next = (j == 0) ? b2 : rh[j - 1];
        m_hes[rh[j]].prev = (j == k - 1) ? b1 : rh[j + 1];
    }
    m_faces[f].he = heA;
    return g;
}

bool HalfEdgeMesh::dissolveEdge(Idx he, Idx* removedFace) {
    if (he >= m_hes.size()) return false;
    const Idx t = m_hes[he].twin;
    if (t == kNone || isBridge(he)) return false;
    const Idx f1 = m_hes[he].face, f2 = m_hes[t].face;

    // laço mesclado: f1 sem a aresta + travessia de f2 (sem a gêmea)
    std::vector<Idx> merged;
    for (Idx h = m_hes[he].next; h != he; h = m_hes[h].next)
        merged.push_back(m_hes[h].origin);
    for (Idx h = m_hes[t].next; h != t; h = m_hes[h].next)
        merged.push_back(m_hes[h].origin);
    if (merged.size() < 3) return false;

    // sopa: faces na ordem original (f1 -> mesclada, f2 -> removida)
    HalfEdgeMesh nm;
    for (const Vertex& v : m_verts) nm.addVertex(v.p);
    for (Idx f = 0; f < Idx(m_faces.size()); ++f) {
        if (f == f2) continue;
        const std::vector<Idx> loop = f == f1 ? merged : faceVertices(f);
        if (nm.addFace(loop) == kNone) return false;
    }
    std::string why;
    if (!nm.checkIntegrity(&why)) return false;
    if (removedFace) *removedFace = f2;
    *this = std::move(nm);
    return true;
}

namespace {
using P3 = Point3;
inline std::tuple<long long, long long, long long> qkey(const P3& p) {
    return {llround(p.x * 1e6), llround(p.y * 1e6), llround(p.z * 1e6)};
}
} // namespace

bool HalfEdgeMesh::weldUnion(const HalfEdgeMesh& A, const HalfEdgeMesh& B,
                             HalfEdgeMesh& out) {
    // laços (posições) e normais das faces dos dois sólidos
    auto loopOf = [](const HalfEdgeMesh& m, Idx f) {
        std::vector<P3> pts;
        for (const Idx v : m.faceVertices(f)) pts.push_back(m.vertex(v).p);
        return pts;
    };
    // p ESTRITAMENTE dentro do polígono `poly` (borda NÃO conta — o caso
    // encostado-na-borda é do caminho FLUSH), no plano de normal n
    auto inside = [](const std::vector<P3>& poly, const Vec3& n, const P3& p) {
        // base 2D no plano
        Vec3 U = (poly[1] - poly[0]).normalized();
        const Vec3 V = n.cross(U).normalized();
        auto uv = [&](const P3& q) {
            const Vec3 o = q - poly[0];
            return std::pair<double, double>{o.dot(U), o.dot(V)};
        };
        const auto [pu, pv] = uv(p);
        bool in = false;
        for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
            const auto [au, av] = uv(poly[i]);
            const auto [bu, bv] = uv(poly[j]);
            // na borda NÃO conta (estrito): esse caso é do caminho flush
            const double lx = bu - au, ly = bv - av, ll = lx * lx + ly * ly;
            double w = ll > 1e-18 ? ((pu - au) * lx + (pv - av) * ly) / ll : 0.0;
            w = std::clamp(w, 0.0, 1.0);
            if (std::hypot(pu - (au + lx * w), pv - (av + ly * w)) < 1e-6)
                return false;
            if ((av > pv) != (bv > pv) &&
                pu < (bu - au) * (pv - av) / (bv - av) + au)
                in = !in;
        }
        return in;
    };

    // procura a interface: normais opostas, coplanares, igual/contida
    for (Idx fa = 0; fa < Idx(A.faceCount()); ++fa) {
        const Vec3 na = A.faceNormal(fa);
        const std::vector<P3> la = loopOf(A, fa);
        for (Idx fb = 0; fb < Idx(B.faceCount()); ++fb) {
            const Vec3 nb = B.faceNormal(fb);
            if (na.dot(nb) > -0.999) continue;               // não opostas
            const std::vector<P3> lb = loopOf(B, fb);
            if (std::abs((lb[0] - la[0]).dot(na)) > 1e-6) continue;  // planos !=

            // relação: iguais (mesmo conjunto de posições) ou contida
            std::size_t common = 0;
            for (const P3& p : lb)
                for (const P3& q : la)
                    if (qkey(p) == qkey(q)) { ++common; break; }
            const bool equal = common == lb.size() && la.size() == lb.size();
            bool bInA = equal, aInB = false;
            if (!equal) {
                bInA = true;
                for (const P3& p : lb) bInA &= inside(la, na, p);
                if (!bInA) {
                    aInB = true;
                    for (const P3& p : la) aInB &= inside(lb, nb, p);
                    // nenhum contém o outro: pode ser FLUSH (tratado abaixo)
                }
            }

            // FLUSH (M2c): B dentro dos LIMITES de A mas tocando a borda —
            // corta A ao longo dos lados livres de B até nascer uma sub-face
            // IGUAL a B; daí cai na solda de iguais. Exige retângulos.
            bool flush = false;
            if (!equal && !bInA && !aInB) {
                auto isRectQuad = [](const std::vector<P3>& L) {
                    if (L.size() != 4) return false;
                    for (int i = 0; i < 4; ++i) {
                        const Vec3 e1 = (L[(i + 1) % 4] - L[i]).normalized();
                        const Vec3 e2 = (L[(i + 2) % 4] - L[(i + 1) % 4]).normalized();
                        if (std::abs(e1.dot(e2)) > 1e-6) return false;
                    }
                    return true;
                };
                if (isRectQuad(la) && isRectQuad(lb)) {
                    const Vec3 U = (la[1] - la[0]).normalized();
                    const Vec3 V = na.cross(U).normalized();
                    auto uv = [&](const P3& p) {
                        const Vec3 o = p - la[0];
                        return std::pair<double, double>{o.dot(U), o.dot(V)};
                    };
                    double aU0 = 1e300, aU1 = -1e300, aV0 = 1e300, aV1 = -1e300;
                    double bU0 = 1e300, bU1 = -1e300, bV0 = 1e300, bV1 = -1e300;
                    bool axisOk = true;
                    for (const P3& p : la) {
                        const auto [u, v] = uv(p);
                        aU0 = std::min(aU0, u); aU1 = std::max(aU1, u);
                        aV0 = std::min(aV0, v); aV1 = std::max(aV1, v);
                    }
                    for (std::size_t i = 0; i < 4 && axisOk; ++i) {
                        const Vec3 e = lb[(i + 1) % 4] - lb[i];
                        axisOk = std::abs(e.dot(U)) < 1e-6 ||
                                 std::abs(e.dot(V)) < 1e-6;
                    }
                    for (const P3& p : lb) {
                        const auto [u, v] = uv(p);
                        bU0 = std::min(bU0, u); bU1 = std::max(bU1, u);
                        bV0 = std::min(bV0, v); bV1 = std::max(bV1, v);
                    }
                    constexpr double kE = 1e-6;
                    flush = axisOk && bU0 >= aU0 - kE && bU1 <= aU1 + kE &&
                            bV0 >= aV0 - kE && bV1 <= aV1 + kE &&
                            (bU1 - bU0) > 0.01 && (bV1 - bV0) > 0.01;
                    if (flush) {
                        // executa os cortes numa cópia de A
                        HalfEdgeMesh wa2 = A;
                        Idx cur = fa;
                        auto cutAt = [&](bool alongU, double c) -> bool {
                            // acha 2 pontos do ciclo cruzando a linha
                            std::vector<Idx> hit;    // vértices nos cruzamentos
                            const auto ring = wa2.faceHalfEdges(cur);
                            for (const Idx h : ring) {
                                const P3 pa = wa2.vertex(wa2.halfEdge(h).origin).p;
                                const P3 pb = wa2.vertex(
                                    wa2.halfEdge(wa2.halfEdge(h).next).origin).p;
                                const double ca =
                                    alongU ? uv(pa).first : uv(pa).second;
                                const double cb =
                                    alongU ? uv(pb).first : uv(pb).second;
                                if (std::abs(ca - c) < 1e-9) {
                                    hit.push_back(wa2.halfEdge(h).origin);
                                } else if ((ca - c) * (cb - c) < 0.0) {
                                    const double t = (c - ca) / (cb - ca);
                                    const Idx w = wa2.splitEdge(
                                        h, pa + (pb - pa) * t);
                                    if (w == kNone) return false;
                                    hit.push_back(w);
                                }
                            }
                            // remove duplicatas mantendo 2 distintos
                            std::sort(hit.begin(), hit.end());
                            hit.erase(std::unique(hit.begin(), hit.end()),
                                      hit.end());
                            if (hit.size() != 2) return false;
                            const Idx g = wa2.splitFace(cur, hit[0], hit[1]);
                            if (g == kNone) return false;
                            // segue com a metade que contém o centro de B
                            const P3 bc{(la[0] + U * ((bU0 + bU1) * 0.5) +
                                         V * ((bV0 + bV1) * 0.5))};
                            const auto inHalf = [&](Idx f2) {
                                const P3 cen = wa2.faceCentroid(f2);
                                const auto [cu, cv] = uv(cen);
                                const auto [tu, tv] = uv(bc);
                                return alongU ? ((cu - c) * (tu - c) > 0)
                                              : ((cv - c) * (tv - c) > 0);
                            };
                            cur = inHalf(cur) ? cur : g;
                            return true;
                        };
                        bool ok2 = true;
                        if (bU0 > aU0 + kE) ok2 &= cutAt(true, bU0);
                        if (ok2 && bU1 < aU1 - kE) ok2 &= cutAt(true, bU1);
                        if (ok2 && bV0 > aV0 + kE) ok2 &= cutAt(false, bV0);
                        if (ok2 && bV1 < aV1 - kE) ok2 &= cutAt(false, bV1);
                        std::string whyC;
                        if (!ok2 || !wa2.checkIntegrity(&whyC)) continue;
                        // a sub-face `cur` deve ser IGUAL a B: solda de iguais
                        HalfEdgeMesh nm2;
                        std::map<std::tuple<long long, long long, long long>,
                                 Idx> weld2;
                        auto vid2 = [&](const P3& p) {
                            const auto k = qkey(p);
                            const auto it = weld2.find(k);
                            if (it != weld2.end()) return it->second;
                            const Idx v = nm2.addVertex(p);
                            weld2.emplace(k, v);
                            return v;
                        };
                        bool ok3 = true;
                        auto pour2 = [&](const HalfEdgeMesh& m, Idx skip) {
                            for (Idx f = 0; f < Idx(m.faceCount()) && ok3; ++f) {
                                if (f == skip) continue;
                                std::vector<Idx> loop;
                                for (const Idx v : m.faceVertices(f))
                                    loop.push_back(vid2(m.vertex(v).p));
                                ok3 &= nm2.addFace(loop) != kNone;
                            }
                        };
                        pour2(wa2, cur);
                        pour2(B, fb);
                        if (!ok3 || !nm2.checkIntegrity(&whyC)) continue;
                        out = std::move(nm2);
                        return true;
                    }
                }
                if (!flush) continue;
            }

            // prepara cópias mutáveis; a face MAIOR recebe o inset da menor
            HalfEdgeMesh wa = A, wb = B;
            Idx ma = fa, mb = fb;
            if (!equal && bInA) {
                const Idx inner = wa.insetFace(fa, lb);
                if (inner == kNone) continue;
                ma = inner;
            } else if (!equal && aInB) {
                const Idx inner = wb.insetFace(fb, la);
                if (inner == kNone) continue;
                mb = inner;
            }

            // sopa combinada sem o par de interface, com SOLDA por posição
            HalfEdgeMesh nm;
            std::map<std::tuple<long long, long long, long long>, Idx> weld;
            auto vid = [&](const P3& p) {
                const auto k = qkey(p);
                const auto it = weld.find(k);
                if (it != weld.end()) return it->second;
                const Idx v = nm.addVertex(p);
                weld.emplace(k, v);
                return v;
            };
            bool ok = true;
            auto pour = [&](const HalfEdgeMesh& m, Idx skip) {
                for (Idx f = 0; f < Idx(m.faceCount()) && ok; ++f) {
                    if (f == skip) continue;
                    std::vector<Idx> loop;
                    for (const Idx v : m.faceVertices(f))
                        loop.push_back(vid(m.vertex(v).p));
                    ok &= nm.addFace(loop) != kNone;
                }
            };
            pour(wa, ma);
            pour(wb, mb);
            std::string why;
            if (!ok || !nm.checkIntegrity(&why)) continue;
            out = std::move(nm);
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------ integridade --
bool HalfEdgeMesh::checkIntegrity(std::string* why) const {
    // mensagens incluem o índice do he/face/vértice culpado — sem isso, um
    // "geometria inválida" na malha de saída de uma sopa-reconstruída (ex.
    // subtractSelected) não dá pista de qual das N faces novas colidiu
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    const Idx H = Idx(m_hes.size());
    for (Idx h = 0; h < H; ++h) {
        const HalfEdge& e = m_hes[h];
        if (e.next >= H || e.prev >= H)
            return fail("next/prev fora da faixa (he=" + std::to_string(h) + ")");
        if (m_hes[e.next].prev != h)
            return fail("next.prev != he (he=" + std::to_string(h) + ")");
        if (m_hes[e.prev].next != h)
            return fail("prev.next != he (he=" + std::to_string(h) + ")");
        if (e.origin >= m_verts.size())
            return fail("origin invalido (he=" + std::to_string(h) + ")");
        if (e.face >= m_faces.size())
            return fail("face invalida (he=" + std::to_string(h) + ")");
        if (e.twin != kNone) {
            if (e.twin >= H || e.twin == h)
                return fail("twin invalido (he=" + std::to_string(h) + ")");
            if (m_hes[e.twin].twin != h)
                return fail("twin nao simetrico (he=" + std::to_string(h) +
                           ", twin=" + std::to_string(e.twin) + ")");
            if (m_hes[e.twin].origin != m_hes[e.next].origin)
                return fail("twin nao inverte a aresta (he=" + std::to_string(h) +
                           ", twin=" + std::to_string(e.twin) + ")");
        }
    }
    for (Idx f = 0; f < Idx(m_faces.size()); ++f) {
        const Idx start = m_faces[f].he;
        if (start >= H) return fail("face.he invalido (face=" + std::to_string(f) + ")");
        Idx h = start;
        std::size_t steps = 0;
        do {
            if (m_hes[h].face != f)
                return fail("he de outra face no ciclo (face=" + std::to_string(f) +
                           ", he=" + std::to_string(h) + ")");
            h = m_hes[h].next;
            if (++steps > m_hes.size())
                return fail("ciclo de face nao fecha (face=" + std::to_string(f) + ")");
        } while (h != start);
        if (steps < 3)
            return fail("face com menos de 3 lados (face=" + std::to_string(f) + ")");
    }
    for (Idx v = 0; v < Idx(m_verts.size()); ++v) {
        const Idx h = m_verts[v].he;
        if (h == kNone) continue;   // vértice órfão é tolerado
        if (h >= H || m_hes[h].origin != v)
            return fail("vertex.he errado (v=" + std::to_string(v) + ")");
    }
    return true;
}

// --------------------------------------------------------- render/picking --
void HalfEdgeMesh::triangulateFace(Idx f, std::vector<Point3>& out) const {
    const std::vector<Idx> vs = faceVertices(f);
    const std::size_t m = vs.size();
    if (m < 3) return;
    auto emit = [&](const Point3& a, const Point3& b, const Point3& c) {
        const double area2 = (b - a).cross(c - a).length();
        if (area2 > 1e-12) { out.push_back(a); out.push_back(b); out.push_back(c); }
    };
    if (m == 3) {
        emit(m_verts[vs[0]].p, m_verts[vs[1]].p, m_verts[vs[2]].p);
        return;
    }

    // projeta no plano dominante da normal, com u/v escolhidos p/ CCW em 2D
    const Vec3 n = faceNormal(f);
    const double ax = std::abs(n.x), ay = std::abs(n.y), az = std::abs(n.z);
    auto uv = [&](const Point3& p) -> std::pair<double, double> {
        if (az >= ax && az >= ay) return n.z >= 0 ? std::pair{p.x, p.y} : std::pair{p.y, p.x};
        if (ax >= ay)             return n.x >= 0 ? std::pair{p.y, p.z} : std::pair{p.z, p.y};
        return n.y >= 0 ? std::pair{p.z, p.x} : std::pair{p.x, p.z};
    };
    struct P2 { double u, v; Idx vi; };
    std::vector<P2> poly(m);
    for (std::size_t i = 0; i < m; ++i) {
        const auto [u, v] = uv(m_verts[vs[i]].p);
        poly[i] = {u, v, vs[i]};
    }
    auto cross2 = [](const P2& a, const P2& b, const P2& c) {
        return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
    };
    auto inTri = [&](const P2& a, const P2& b, const P2& c, const P2& p) {
        const double d1 = cross2(a, b, p), d2 = cross2(b, c, p), d3 = cross2(c, a, p);
        constexpr double e = 1e-12;
        return d1 > e && d2 > e && d3 > e;   // estritamente dentro
    };

    // EAR CLIPPING (aguenta o keyhole: orelhas degeneradas são consumidas)
    int guard = 0;
    while (poly.size() > 3 && guard <= int(poly.size())) {
        bool clipped = false;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const std::size_t ip = (i + poly.size() - 1) % poly.size();
            const std::size_t in = (i + 1) % poly.size();
            if (cross2(poly[ip], poly[i], poly[in]) < -1e-12) continue;  // côncavo
            bool blocked = false;
            for (std::size_t j = 0; j < poly.size() && !blocked; ++j) {
                if (j == ip || j == i || j == in) continue;
                blocked = inTri(poly[ip], poly[i], poly[in], poly[j]);
            }
            if (blocked) continue;
            emit(m_verts[poly[ip].vi].p, m_verts[poly[i].vi].p,
                 m_verts[poly[in].vi].p);
            poly.erase(poly.begin() + std::ptrdiff_t(i));
            clipped = true;
            guard = 0;
            break;
        }
        if (!clipped) {                       // salvaguarda: leque do resto
            for (std::size_t i = 1; i + 1 < poly.size(); ++i)
                emit(m_verts[poly[0].vi].p, m_verts[poly[i].vi].p,
                     m_verts[poly[i + 1].vi].p);
            return;
        }
        ++guard;
    }
    if (poly.size() == 3)
        emit(m_verts[poly[0].vi].p, m_verts[poly[1].vi].p, m_verts[poly[2].vi].p);
}

void HalfEdgeMesh::triangulate(std::vector<Point3>& tris,
                               std::vector<Idx>* faceOfTri) const {
    std::vector<Point3> ft;
    for (Idx f = 0; f < Idx(m_faces.size()); ++f) {
        ft.clear();
        triangulateFace(f, ft);
        tris.insert(tris.end(), ft.begin(), ft.end());
        if (faceOfTri)
            for (std::size_t i = 0; i < ft.size() / 3; ++i) faceOfTri->push_back(f);
    }
}

void HalfEdgeMesh::edgeLines(std::vector<Point3>& lines) const {
    for (Idx h = 0; h < Idx(m_hes.size()); ++h) {
        if (isBridge(h)) continue;              // ponte do keyhole: invisível
        const Idx t = m_hes[h].twin;
        if (t != kNone && t < h) continue;      // cada aresta uma vez
        lines.push_back(m_verts[m_hes[h].origin].p);
        lines.push_back(m_verts[m_hes[m_hes[h].next].origin].p);
    }
}

HalfEdgeMesh::RayHit HalfEdgeMesh::pickRay(const Point3& orig,
                                           const Vec3& dir) const {
    RayHit best;
    constexpr double kEps = 1e-9;
    std::vector<Point3> ft;
    for (Idx f = 0; f < Idx(m_faces.size()); ++f) {
        ft.clear();
        triangulateFace(f, ft);
        for (std::size_t i = 0; i * 3 < ft.size(); ++i) {
            const Point3& a = ft[i * 3];
            const Point3& b = ft[i * 3 + 1];
            const Point3& c = ft[i * 3 + 2];
            const Vec3 e1 = b - a, e2 = c - a;
            const Vec3 pv = dir.cross(e2);
            const double det = e1.dot(pv);
            if (std::abs(det) < kEps) continue;         // raio paralelo
            const double inv = 1.0 / det;
            const Vec3 tv = orig - a;
            const double u = tv.dot(pv) * inv;
            if (u < -kEps || u > 1.0 + kEps) continue;
            const Vec3 qv = tv.cross(e1);
            const double v = dir.dot(qv) * inv;
            if (v < -kEps || u + v > 1.0 + kEps) continue;
            const double t = e2.dot(qv) * inv;
            if (t <= kEps) continue;                    // atrás da câmera
            if (!best.hit || t < best.t)
                best = {true, t, f, orig + dir * t};
        }
    }
    return best;
}

} // namespace cad
