// src/core/edit/BooleanOps.hpp
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

#include "core/math/Vec.hpp"

// =============================================================================
// Operações booleanas de polígonos (base de REGION / booleanas de CAD).
//
// Algoritmo: Greiner–Hormann (1998) — "Efficient clipping of arbitrary
// polygons". Constrói duas listas duplamente ligadas (uma por polígono),
// insere os pontos de interseção em ambas, marca cada interseção como
// "entrada" ou "saída" (travessia) usando teste ponto-em-polígono no 1º
// vértice de cada loop e, por fim, percorre as listas alternando entre os dois
// polígonos a cada interseção para extrair os loops resultantes.
//
// Os polígonos são dados como loops de vértices (CCW ou CW; o algoritmo não
// exige orientação), SEM repetir o 1º vértice no fim. Retorna 0+ loops; cada
// loop também vem sem o 1º vértice repetido no fim.
//
// LIMITAÇÕES (documentadas de propósito):
//  - Assume polígonos SIMPLES (sem auto-interseção).
//  - NÃO trata buracos (apenas contornos externos; sem furos aninhados).
//  - Casos DEGENERADOS de tangência exata — vértice de um polígono exatamente
//    sobre uma aresta do outro, ou arestas colineares sobrepostas — não são
//    tratados de forma robusta (limitação clássica do Greiner–Hormann puro).
//    Para esses casos, perturbe levemente a entrada. Sobreposições "limpas"
//    (cruzamentos transversais) e os casos sem interseção são tratados ok.
//  - Trabalha no plano XY (z é ignorado nos cálculos; saída tem z = 0).
// =============================================================================

namespace cad {

// Tolerância geométrica padrão (coordenadas ~unitárias a milhares).
inline constexpr double kBoolEps = 1e-7;

enum class BoolOp { Union, Intersection, Difference };  // A∪B, A∩B, A−B

// -----------------------------------------------------------------------------
// Teste ponto-em-polígono por ray casting (par/ímpar) no plano XY.
// Retorna true se `p` está estritamente dentro do polígono `poly`.
// Pontos exatamente sobre a borda têm resultado não especificado (limitação).
// -----------------------------------------------------------------------------
inline bool pointInPolygon(const std::vector<Point3>& poly, const Point3& p) {
    const std::size_t n = poly.size();
    if (n < 3) return false;
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = poly[i].x, yi = poly[i].y;
        const double xj = poly[j].x, yj = poly[j].y;
        // A aresta j->i cruza a horizontal em y = p.y?
        const bool intersectsY = (yi > p.y) != (yj > p.y);
        if (intersectsY) {
            // x da interseção da aresta com a reta y = p.y.
            const double xCross = (xj - xi) * (p.y - yi) / (yj - yi) + xi;
            if (p.x < xCross) inside = !inside;
        }
    }
    return inside;
}

// =============================================================================
// Implementação interna do Greiner–Hormann.
// =============================================================================
namespace detail {

// Nó da lista duplamente ligada que representa um vértice (original ou
// interseção) do polígono. As listas são circulares.
struct GHVertex {
    Point3 p;                 // coordenada
    GHVertex* next = nullptr; // próximo na cadeia deste polígono
    GHVertex* prev = nullptr;
    GHVertex* neighbor = nullptr; // mesma interseção na lista do OUTRO polígono
    double    alpha = 0.0;    // parâmetro [0,1] ao longo da aresta (p/ ordenar)
    bool      intersect = false; // é um ponto de interseção?
    bool      entry = false;  // true = "entrada", false = "saída" (travessia)
    bool      visited = false;// usado na fase de extração
};

inline bool nearlyEqual(const Point3& a, const Point3& b) {
    return std::fabs(a.x - b.x) < kBoolEps && std::fabs(a.y - b.y) < kBoolEps;
}

// Constrói uma cadeia circular de GHVertex a partir de um loop de pontos.
// Retorna o ponteiro para o 1º nó (dono dos nós via o vetor `pool`).
inline GHVertex* buildChain(const std::vector<Point3>& poly,
                            std::vector<GHVertex*>& pool) {
    const std::size_t n = poly.size();
    GHVertex* first = nullptr;
    GHVertex* prev = nullptr;
    for (std::size_t i = 0; i < n; ++i) {
        auto* v = new GHVertex();
        v->p = poly[i];
        pool.push_back(v);
        if (!first) first = v;
        if (prev) { prev->next = v; v->prev = prev; }
        prev = v;
    }
    // Fecha o ciclo.
    if (first && prev) { prev->next = first; first->prev = prev; }
    return first;
}

// Próximo nó "original" (não-interseção) a partir de v (inclui o caso de v ser
// o último original antes de voltar ao início). Usado p/ varrer arestas.
inline GHVertex* nextOriginal(GHVertex* v) {
    GHVertex* c = v->next;
    while (c->intersect) c = c->next;
    return c;
}

// Interseção de dois segmentos AB e CD no plano XY. Em caso de cruzamento
// transversal dentro de ambos, devolve true e preenche o ponto e os parâmetros
// alphaA (em AB) e alphaB (em CD). Paralelos/colineares -> false.
inline bool segIntersectAlpha(const Point3& a, const Point3& b,
                              const Point3& c, const Point3& d,
                              Point3& out, double& alphaA, double& alphaB) {
    const double rx = b.x - a.x, ry = b.y - a.y;
    const double sx = d.x - c.x, sy = d.y - c.y;
    const double denom = rx * sy - ry * sx;
    if (std::fabs(denom) < 1e-12) return false; // paralelos/colineares
    const double t = ((c.x - a.x) * sy - (c.y - a.y) * sx) / denom; // em AB
    const double u = ((c.x - a.x) * ry - (c.y - a.y) * rx) / denom; // em CD
    // Estritamente dentro (evita tangências de vértice — limitação assumida).
    if (t <= kBoolEps || t >= 1.0 - kBoolEps) return false;
    if (u <= kBoolEps || u >= 1.0 - kBoolEps) return false;
    out = Point3{a.x + t * rx, a.y + t * ry, 0.0};
    alphaA = t;
    alphaB = u;
    return true;
}

// Insere um nó de interseção `ins` na cadeia entre `start` e o próximo original,
// mantendo a ordem por alpha (p/ múltiplas interseções na mesma aresta).
inline void insertByAlpha(GHVertex* start, GHVertex* ins) {
    GHVertex* curr = start;
    // Avança enquanto o próximo for interseção com alpha menor.
    while (curr->next->intersect && curr->next->alpha < ins->alpha)
        curr = curr->next;
    ins->next = curr->next;
    ins->prev = curr;
    curr->next->prev = ins;
    curr->next = ins;
}

} // namespace detail

// -----------------------------------------------------------------------------
// Operação booleana principal.
// -----------------------------------------------------------------------------
inline std::vector<std::vector<Point3>> polygonBoolean(
    const std::vector<Point3>& A, const std::vector<Point3>& B, BoolOp op) {
    using namespace detail;

    std::vector<std::vector<Point3>> result;
    if (A.size() < 3 || B.size() < 3) {
        // Entradas degeneradas: trata como "sem área".
        if (op == BoolOp::Union) {
            if (A.size() >= 3) result.push_back(A);
            if (B.size() >= 3) result.push_back(B);
        } else if (op == BoolOp::Difference) {
            if (A.size() >= 3) result.push_back(A);
        }
        return result;
    }

    // Vetor "dono" dos nós alocados; liberado no fim.
    std::vector<GHVertex*> pool;
    auto cleanup = [&pool]() { for (auto* v : pool) delete v; };

    GHVertex* chainA = buildChain(A, pool);
    GHVertex* chainB = buildChain(B, pool);

    // -------------------------------------------------------------------------
    // FASE 1: achar e inserir todas as interseções em ambas as cadeias.
    // -------------------------------------------------------------------------
    int interCount = 0;
    // Para cada aresta original de A x cada aresta original de B.
    // Coletamos primeiro os "originais" de cada cadeia (a estrutura não muda
    // de originais durante a inserção; só insere interseções entre eles).
    std::vector<GHVertex*> origA, origB;
    {
        GHVertex* c = chainA;
        do { origA.push_back(c); c = c->next; } while (c != chainA);
        c = chainB;
        do { origB.push_back(c); c = c->next; } while (c != chainB);
    }

    for (GHVertex* a0 : origA) {
        GHVertex* a1 = nextOriginal(a0);
        for (GHVertex* b0 : origB) {
            GHVertex* b1 = nextOriginal(b0);
            Point3 ip;
            double aA, aB;
            if (segIntersectAlpha(a0->p, a1->p, b0->p, b1->p, ip, aA, aB)) {
                auto* va = new GHVertex();
                auto* vb = new GHVertex();
                pool.push_back(va);
                pool.push_back(vb);
                va->p = ip; va->intersect = true; va->alpha = aA;
                vb->p = ip; vb->intersect = true; vb->alpha = aB;
                va->neighbor = vb; vb->neighbor = va;
                insertByAlpha(a0, va);
                insertByAlpha(b0, vb);
                ++interCount;
            }
        }
    }

    // -------------------------------------------------------------------------
    // CASO DEGENERADO: nenhuma interseção (disjuntos ou um contido no outro).
    // -------------------------------------------------------------------------
    if (interCount == 0) {
        const bool aInB = pointInPolygon(B, A[0]); // A dentro de B?
        const bool bInA = pointInPolygon(A, B[0]); // B dentro de A?
        cleanup();
        switch (op) {
            case BoolOp::Union:
                if (aInB)      result.push_back(B);          // A some dentro de B
                else if (bInA) result.push_back(A);          // B some dentro de A
                else { result.push_back(A); result.push_back(B); } // disjuntos
                break;
            case BoolOp::Intersection:
                if (aInB)      result.push_back(A);          // A∩B = A
                else if (bInA) result.push_back(B);          // A∩B = B
                // disjuntos -> vazio
                break;
            case BoolOp::Difference:                         // A − B
                if (aInB) {}                                 // A todo dentro de B -> vazio
                else if (bInA) {                             // B é um furo em A
                    // Sem suporte a furos: devolve A (contorno externo).
                    result.push_back(A);
                } else result.push_back(A);                  // disjuntos -> A
                break;
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // FASE 2: marcar cada interseção como entrada/saída (status de travessia).
    // Regra de Greiner–Hormann: ao percorrer um polígono, alterna-se entry/exit
    // a cada interseção; o status inicial depende de o 1º vértice estar dentro
    // do OUTRO polígono. O flag `invert` escolhe qual "fatia" será extraída e,
    // combinado por operação (ver abaixo), seleciona ∩, ∪ ou −.
    // -------------------------------------------------------------------------
    auto markEntryExit = [](GHVertex* chain, const std::vector<Point3>& other,
                            bool invert) {
        bool inside = pointInPolygon(other, chain->p);
        if (invert) inside = !inside;
        GHVertex* c = chain;
        do {
            if (c->intersect) {
                c->entry = !inside; // se estava fora, esta interseção é entrada
                inside = !inside;   // alterna
            }
            c = c->next;
        } while (c != chain);
    };

    // Convenções por operação (verificadas empiricamente; extração usa
    // forward = entry). Pares (invertA, invertB):
    //  Intersection: (false, false)  -> mantém a fatia comum aos dois.
    //  Union:        (true,  true)   -> mantém a fatia externa aos dois.
    //  Difference:   (true,  false)  -> A − B (parte de A fora de B).
    switch (op) {
        case BoolOp::Intersection:
            markEntryExit(chainA, B, false);
            markEntryExit(chainB, A, false);
            break;
        case BoolOp::Union:
            markEntryExit(chainA, B, true);
            markEntryExit(chainB, A, true);
            break;
        case BoolOp::Difference:
            markEntryExit(chainA, B, true);
            markEntryExit(chainB, A, false);
            break;
    }

    // -------------------------------------------------------------------------
    // FASE 3: extrair loops. A partir de cada interseção não visitada, anda-se
    // pela cadeia atual até a próxima interseção; troca-se para o vizinho (outro
    // polígono) e continua, fechando quando se retorna ao ponto inicial.
    // A direção de caminhada depende do status entry/exit.
    // -------------------------------------------------------------------------
    for (GHVertex* startNode : pool) {
        if (!startNode->intersect || startNode->visited) continue;

        std::vector<Point3> loop;
        GHVertex* curr = startNode;

        do {
            curr->visited = true;
            if (curr->neighbor) curr->neighbor->visited = true;

            // Direção: se é "entrada", avança (next); senão recua (prev).
            const bool forward = curr->entry;
            // Adiciona vértices ao longo da cadeia até a próxima interseção.
            do {
                curr = forward ? curr->next : curr->prev;
                loop.push_back(curr->p);
            } while (!curr->intersect);

            // Chegou numa interseção: marca e salta para o vizinho.
            curr->visited = true;
            if (curr->neighbor) curr->neighbor->visited = true;
            curr = curr->neighbor;
        } while (curr != startNode && curr != nullptr);

        // Remove vértice final duplicado (fechamento) se igual ao 1º.
        if (loop.size() >= 2 && nearlyEqual(loop.front(), loop.back()))
            loop.pop_back();
        if (loop.size() >= 3) result.push_back(std::move(loop));
    }

    cleanup();
    return result;
}

} // namespace cad
