// src/core/geometry/Table.cpp
#include "core/geometry/Table.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/text/StrokeFont.hpp"
#include "core/math/Matrix4.hpp"
#include <cmath>

namespace cad {

namespace {
    // String vazia compartilhada para retorno de cell() fora de limites.
    const std::string kEmpty{};
} // namespace

Table::Table(Point3 origem, int linhas, int colunas, double larguraCol, double alturaLin)
    : m_origem(origem),
      m_rows(linhas < 0 ? 0 : linhas),
      m_cols(colunas < 0 ? 0 : colunas),
      m_colWidth(larguraCol),
      m_rowHeight(alturaLin),
      m_cells(static_cast<std::size_t>(m_rows) * static_cast<std::size_t>(m_cols)) {
    // m_cells nasce com rows*cols strings vazias (linha-maior).
}

void Table::setCell(int row, int col, std::string texto) {
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return; // fora: no-op
    m_cells[static_cast<std::size_t>(index(row, col))] = std::move(texto);
}

const std::string& Table::cell(int row, int col) const {
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return kEmpty;
    return m_cells[static_cast<std::size_t>(index(row, col))];
}

std::vector<Point3> Table::buildGridPoints() const {
    // Constrói a malha em coordenadas de mundo usando os eixos locais
    // (m_colDir avança colunas em +X; m_rowDir avança linhas, inicialmente -Y).
    // Pares consecutivos formam um segmento (mesmo contrato do RenderBatch).
    std::vector<Point3> out;
    if (m_rows <= 0 || m_cols <= 0) return out;

    const Vec3 acrossFull = m_colDir * (m_colWidth * static_cast<double>(m_cols)); // largura total
    const Vec3 downFull   = m_rowDir * (m_rowHeight * static_cast<double>(m_rows)); // altura total

    // Linhas HORIZONTAIS: rows+1 traços (de cima para baixo), cada um cruzando
    // toda a largura.
    for (int r = 0; r <= m_rows; ++r) {
        const Vec3   off = m_rowDir * (m_rowHeight * static_cast<double>(r));
        const Point3 a   = m_origem + off;
        const Point3 b   = a + acrossFull;
        out.push_back(a);
        out.push_back(b);
    }

    // Linhas VERTICAIS: cols+1 traços (da esquerda para a direita), cada um
    // cruzando toda a altura.
    for (int c = 0; c <= m_cols; ++c) {
        const Vec3   off = m_colDir * (m_colWidth * static_cast<double>(c));
        const Point3 a   = m_origem + off;
        const Point3 b   = a + downFull;
        out.push_back(a);
        out.push_back(b);
    }

    return out;
}

AABB Table::boundingBox() const {
    // Envolve a grade e os traços de texto reais — exato sob rotação/escala.
    AABB b;
    const std::vector<Point3> grid = buildGridPoints();
    for (const Point3& p : grid) b.expand(p);
    if (!b.valid()) b.expand(m_origem); // tabela degenerada: ao menos a origem
    return b;
}

void Table::emitTo(RenderBatch& batch) const {
    // 1) Grade: pares consecutivos -> um segmento cada.
    const std::vector<Point3> grid = buildGridPoints();
    for (std::size_t i = 0; i + 1 < grid.size(); i += 2)
        batch.addSegment(grid[i], grid[i + 1]);

    if (m_rows <= 0 || m_cols <= 0) return;

    // 2) Texto das células via strokeText (mesmo pipeline do MText).
    //    Altura ~0.6×alturaLin; margem interna a partir do canto
    //    inferior-esquerdo da célula. Rotação derivada de m_colDir.
    const double textHeight = 0.6 * m_rowHeight;
    const double rotation   = std::atan2(m_colDir.y, m_colDir.x);
    const double marginX    = 0.15 * m_colWidth;   // recuo horizontal
    const double marginY    = 0.20 * m_rowHeight;  // recuo da base da célula

    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            const std::string& txt = m_cells[static_cast<std::size_t>(index(r, c))];
            if (txt.empty()) continue;

            // Canto superior-esquerdo da célula (r,c) no mundo.
            const Point3 cellTL =
                m_origem
                + m_colDir * (m_colWidth * static_cast<double>(c))
                + m_rowDir * (m_rowHeight * static_cast<double>(r));

            // strokeText ancora no canto INFERIOR-esquerdo: desce uma linha
            // inteira (rowHeight) e sobe a margem da base; recua marginX em X.
            const Point3 baseLeft =
                cellTL
                + m_colDir * marginX
                + m_rowDir * (m_rowHeight - marginY);

            const std::vector<Point3> pts =
                strokeText(txt, baseLeft, textHeight, rotation);
            for (std::size_t i = 0; i + 1 < pts.size(); i += 2)
                batch.addSegment(pts[i], pts[i + 1]);
        }
    }
}

HitResult Table::hitTest(const Ray& pickRay, double tol) const {
    const Point3 p = pickRay.origin;

    // 1) Proximidade da origem (canto superior-esquerdo).
    const double dx = p.x - m_origem.x, dy = p.y - m_origem.y;
    const double dOrig = std::sqrt(dx * dx + dy * dy);
    if (dOrig <= tol) return HitResult{true, dOrig, m_origem};

    // 2) Dentro do bounding box (no plano XY).
    const AABB box = boundingBox();
    if (box.contains(p)) return HitResult{true, 0.0, p};

    return HitResult{};
}

void Table::transform(const Matrix4& m) {
    // Origem via ponto; eixos via vetor (capturam rotação/escala afins).
    // As métricas de célula permanecem 1.0 nos eixos — o comprimento real de
    // avanço passa a viver em m_colDir/m_rowDir.
    m_origem = m.transformPoint(m_origem);
    m_colDir = m.transformVector(m_colDir);
    m_rowDir = m.transformVector(m_rowDir);
}

void Table::appendSnapPoints(std::vector<SnapPoint>& out) const {
    // Origem como endpoint; e os quatro cantos da tabela como referência.
    out.push_back({m_origem, SnapType::Endpoint});
    if (m_rows <= 0 || m_cols <= 0) return;

    const Vec3 acrossFull = m_colDir * (m_colWidth * static_cast<double>(m_cols));
    const Vec3 downFull   = m_rowDir * (m_rowHeight * static_cast<double>(m_rows));
    out.push_back({m_origem + acrossFull,            SnapType::Endpoint});
    out.push_back({m_origem + downFull,              SnapType::Endpoint});
    out.push_back({m_origem + acrossFull + downFull, SnapType::Endpoint});
}

std::unique_ptr<Entity> Table::clone() const {
    // Cópia profunda: o construtor de cópia padrão copia m_cells (strings) e os
    // eixos — independência total para Memento/undo.
    return std::make_unique<Table>(*this);
}

} // namespace cad
