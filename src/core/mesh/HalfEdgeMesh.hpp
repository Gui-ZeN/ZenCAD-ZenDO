// src/core/mesh/HalfEdgeMesh.hpp
// MALHA HALF-EDGE — a fundação 3D do Zendo (F1 do roadmap).
// Estrutura de adjacência plena: cada aresta vira DUAS meias-arestas gêmeas
// (twin), cada uma com next/prev no ciclo da própria face. É o que torna
// baratas as operações do modelador estilo SketchUp:
//   * splitEdge   — inserir vértice numa aresta (preparo p/ desenhar sobre);
//   * splitFace   — diagonal entre 2 vértices da face → 2 faces (o operador
//                   de "desenhar sobre a face divide a face");
//   * extrudeFace — o coração do EMPURRAR/PUXAR: a face vira tampa, nascem
//                   os quads laterais, os vínculos são recosturados.
// Convenções: faces em CCW vistas DE FORA (normal p/ fora); malha fechada ou
// aberta (twin = kNone na borda). Faces tratadas como CONVEXAS na triangulação
// e no picking (leque) — suficiente p/ caixas/extrusões; ear-clipping fica
// para quando houver furo em face. Sem Qt/OpenGL — testável headless.
#pragma once
#include "core/math/Vec.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cad {

class HalfEdgeMesh {
public:
    using Idx = std::uint32_t;
    static constexpr Idx kNone = 0xFFFFFFFFu;

    struct Vertex   { Point3 p; Idx he{kNone}; };
    struct HalfEdge { Idx origin{kNone}, twin{kNone}, next{kNone},
                      prev{kNone}, face{kNone}; };
    struct Face     { Idx he{kNone}; };

    // --- construção (fase de build; addFace liga twins pelo mapa de arestas)
    Idx  addVertex(const Point3& p);
    // Loop CCW visto de fora. Recusa (kNone) laço < 3, vértice inválido ou
    // aresta dirigida repetida (não-manifold).
    Idx  addFace(const std::vector<Idx>& loop);

    // --- consulta ----------------------------------------------------------
    std::size_t vertexCount()   const { return m_verts.size(); }
    std::size_t faceCount()     const { return m_faces.size(); }
    std::size_t halfEdgeCount() const { return m_hes.size(); }

    const Vertex&   vertex(Idx v)   const { return m_verts[v]; }
    const HalfEdge& halfEdge(Idx h) const { return m_hes[h]; }
    const Face&     face(Idx f)     const { return m_faces[f]; }

    std::vector<Idx> faceHalfEdges(Idx f) const;   // ciclo a partir de face.he
    std::vector<Idx> faceVertices(Idx f) const;
    Vec3   faceNormal(Idx f) const;                // Newell, normalizada
    double faceArea(Idx f) const;
    Point3 faceCentroid(Idx f) const;

    // --- operadores (invalidam o mapa de build; addFace não vale depois) ---
    // Insere vértice em `he` (e no twin). Retorna o vértice novo.
    Idx splitEdge(Idx he, const Point3& p);
    // Diagonal vA->vB dentro da face f (não adjacentes). Retorna a face nova.
    Idx splitFace(Idx f, Idx vA, Idx vB);
    // Empurrar/puxar: tampa sobe `dist` pela normal (negativo = para dentro),
    // nascem os quads laterais. A PRÓPRIA f vira a tampa (id preservado —
    // seleção sobrevive à operação). Retorna f, ou kNone se inválida.
    Idx extrudeFace(Idx f, double dist);
    // DESENHAR SOBRE A FACE: insere o laço `inner` (>=3 pontos, no plano de f,
    // inteiramente dentro dela) como FACE NOVA plugável; f vira o ANEL em
    // volta, costurado por uma PONTE (par de gêmeas na própria f — técnica
    // keyhole, sem furo real). A face nova extruda normalmente (push/pull).
    // Orientação do laço é ajustada sozinha. Retorna a face interna ou kNone.
    Idx insetFace(Idx f, std::vector<Point3> inner);
    // Aresta-ponte do keyhole (gêmea na MESMA face): não é desenhada.
    bool isBridge(Idx he) const {
        const Idx t = m_hes[he].twin;
        return t != kNone && m_hes[t].face == m_hes[he].face;
    }
    // Translada a malha inteira (mover/copiar sólido).
    void translate(const Vec3& d) {
        for (Vertex& v : m_verts) v.p = v.p + d;
    }
    // Move UM vértice (G2: sticky move — vizinhos esticam porque compartilham
    // o vértice; refazer a planaridade das faces é papel do AUTOFOLD, no app).
    void moveVertex(Idx v, const Point3& p) { m_verts[v].p = p; }
    // R8: rotação em torno de um EIXO unitário arbitrário (Rodrigues) —
    // o transferidor no plano de qualquer face.
    void rotateAxis(const Point3& pivot, const Vec3& axis, double rad) {
        const double c = std::cos(rad), s = std::sin(rad);
        for (Vertex& v : m_verts) {
            const Vec3 p{v.p.x - pivot.x, v.p.y - pivot.y, v.p.z - pivot.z};
            const Vec3 r = p * c + axis.cross(p) * s +
                           axis * (axis.dot(p) * (1.0 - c));
            v.p = {pivot.x + r.x, pivot.y + r.y, pivot.z + r.z};
        }
    }
    // Rotação em torno de Z passando pelo pivô (graus CCW vistos de cima).
    void rotateZ(const Point3& pivot, double rad) {
        const double c = std::cos(rad), s = std::sin(rad);
        for (Vertex& v : m_verts) {
            const double dx = v.p.x - pivot.x, dy = v.p.y - pivot.y;
            v.p.x = pivot.x + dx * c - dy * s;
            v.p.y = pivot.y + dx * s + dy * c;
        }
    }
    // Escala uniforme em torno do pivô.
    void scaleAbout(const Point3& pivot, double f) {
        for (Vertex& v : m_verts) v.p = pivot + (v.p - pivot) * f;
    }
    // Centro do bounding box (pivô padrão de rotação/escala).
    Point3 bboxCenter() const {
        Point3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
        for (const Vertex& v : m_verts) {
            lo.x = std::min(lo.x, v.p.x); hi.x = std::max(hi.x, v.p.x);
            lo.y = std::min(lo.y, v.p.y); hi.y = std::max(hi.y, v.p.y);
            lo.z = std::min(lo.z, v.p.z); hi.z = std::max(hi.z, v.p.z);
        }
        return (lo + hi) * 0.5;
    }
    // DISSOLVE a aresta (apagar a linha desenhada): funde as duas faces
    // adjacentes numa só, reconstruindo a malha por sopa de polígonos (a
    // ordem das faces é preservada, com a segunda removida — `removedFace`
    // devolve o índice dela p/ o chamador remapear cores). Recusa borda,
    // ponte de keyhole e casos que quebrem a integridade.
    bool dissolveEdge(Idx he, Idx* removedFace = nullptr);
    // UNIÃO POR COSTURA ("geometria que gruda"): se A e B se tocam por UMA
    // interface face-a-face COPLANAR — faces iguais, ou a de um contida na do
    // outro — funde os dois num único sólido manifold: a face maior recebe um
    // inset com o laço da menor, o par de interface é removido, os vértices
    // coincidentes são SOLDADOS por posição e a malha é recosturada por sopa.
    // Retorna false se não há interface assim (ou a costura quebraria a
    // integridade) — nesse caso `out` não é tocado.
    static bool weldUnion(const HalfEdgeMesh& A, const HalfEdgeMesh& B,
                          HalfEdgeMesh& out);

    // --- integridade (twins simétricos, ciclos fechados, faces coerentes) --
    bool checkIntegrity(std::string* why = nullptr) const;

    // --- render/picking ----------------------------------------------------
    // Triangulação de UMA face por EAR-CLIPPING (aguenta o ciclo keyhole do
    // insetFace e faces côncavas; convexa degenera em leque).
    void triangulateFace(Idx f, std::vector<Point3>& tris) const;
    // Todas as faces (p/ GPU); faceOfTri (opcional) = face de cada tri.
    void triangulate(std::vector<Point3>& tris,
                     std::vector<Idx>* faceOfTri = nullptr) const;
    // Cada aresta UMA vez (pares p/ GL_LINES).
    void edgeLines(std::vector<Point3>& lines) const;

    struct RayHit {
        bool   hit{false};
        double t{0.0};
        Idx    face{kNone};
        Point3 point{};
    };
    // Möller–Trumbore no leque de cada face; devolve o t>eps mais próximo.
    RayHit pickRay(const Point3& orig, const Vec3& dir) const;

private:
    Idx newHalfEdge() { m_hes.push_back({}); return Idx(m_hes.size() - 1); }
    Idx newFace()     { m_faces.push_back({}); return Idx(m_faces.size() - 1); }
    Idx halfEdgeFromIn(Idx f, Idx v) const;   // he com origin v no ciclo de f

    std::vector<Vertex>   m_verts;
    std::vector<HalfEdge> m_hes;
    std::vector<Face>     m_faces;
    // (a,b) dirigido -> he; só vale na FASE DE BUILD (ops limpam o mapa).
    std::map<std::pair<Idx, Idx>, Idx> m_edgeMap;
    bool m_building{true};
};

} // namespace cad
