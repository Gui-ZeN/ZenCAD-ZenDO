// src/core/geometry/Table.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include <string>
#include <vector>

namespace cad {

// Tabela simples: grade de `linhas` × `colunas` células de tamanho fixo
// (larguraCol × alturaLin), desenhada como uma malha de segmentos de reta
// (linhas horizontais e verticais) mais o texto de cada célula via StrokeFont
// (mesmo pipeline de geometria do MText — pares de pontos -> GL_LINES).
//
// `m_origem` é o canto SUPERIOR-ESQUERDO da tabela: a grade cresce em +X
// (colunas) e DESCE em Y (linhas), seguindo a convenção de leitura. O texto
// de cada célula é desenhado com altura ~0.6×alturaLin e uma pequena margem
// interna a partir do canto inferior-esquerdo da célula.
class Table final : public Entity {
public:
    Table() = default;
    // Construtor: origem (canto superior-esquerdo), nº de linhas/colunas e o
    // tamanho fixo de cada célula. O armazenamento de conteúdo já nasce com
    // linhas*colunas strings vazias.
    Table(Point3 origem, int linhas, int colunas, double larguraCol, double alturaLin);

    int    rows()    const { return m_rows; }
    int    cols()    const { return m_cols; }
    double colWidth()  const { return m_colWidth; }
    double rowHeight() const { return m_rowHeight; }
    const Point3& origin() const { return m_origem; }
    // Eixos locais acumulados por transform() — necessários para persistir
    // tabelas rotacionadas/escaladas com fidelidade (ProjectIo).
    const Vec3& colDir() const { return m_colDir; }
    const Vec3& rowDir() const { return m_rowDir; }

    // Define o texto da célula (row, col). Fora dos limites: ignora (no-op).
    void setCell(int row, int col, std::string texto);
    // Lê o texto da célula (row, col). Fora dos limites: string vazia.
    const std::string& cell(int row, int col) const;

    // --- virtuais de Entity (assinaturas EXATAS de Entity.hpp) ------------
    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;

    // accept: o EntityVisitor (Entity.hpp) NÃO possui visit(const Table&) e o
    // escopo proíbe editá-lo. MText/Polyline fazem `v.visit(*this)` — isso só
    // compila porque existe o overload correspondente. Como não há overload de
    // Table, replicamos o MESMO padrão de double-dispatch porém resolvendo para
    // o método base genérico do visitor: aqui mantemos um corpo no-op (não há
    // método específico a chamar). Assim a entidade participa do contrato de
    // Entity e COMPILA sem tocar no EntityVisitor. Quando/se um
    // visit(const Table&) for adicionado ao visitor, basta trocar por
    // `v.visit(*this);` — idêntico a MText.
    void      accept(EntityVisitor& v) const override { v.visit(*this); }   // visit(const Table&) já existe
    const char* typeName() const noexcept override { return "TABLE"; }

private:
    // Gera os pontos da GRADE (pares consecutivos = um segmento), já no mundo.
    std::vector<Point3> buildGridPoints() const;
    // Índice linear na matriz de conteúdo (sem checagem de limites).
    int index(int row, int col) const { return row * m_cols + col; }

    Point3 m_origem{};            // canto superior-esquerdo
    int    m_rows{0};
    int    m_cols{0};
    double m_colWidth{0.0};
    double m_rowHeight{0.0};
    std::vector<std::string> m_cells;  // tamanho rows*cols (linha-maior)

    // Eixos locais da tabela: começam como +X (colunas) e -Y (linhas) e são
    // rotacionados/escalados por transform(). Mantê-los explícitos permite que
    // a tabela acompanhe rotação afim sem perder a métrica das células.
    Vec3 m_colDir{1.0, 0.0, 0.0};   // direção de avanço de coluna (uma unidade)
    Vec3 m_rowDir{0.0, -1.0, 0.0};  // direção de avanço de linha (uma unidade)
};

} // namespace cad
