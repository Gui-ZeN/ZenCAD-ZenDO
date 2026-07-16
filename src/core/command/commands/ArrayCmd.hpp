// src/core/command/commands/ArrayCmd.hpp
#pragma once
#include "core/command/Command.hpp"
#include "core/document/DrawingManager.hpp"
#include "core/geometry/Entity.hpp"
#include "core/math/Matrix4.hpp"
#include "core/math/Vec.hpp"
#include <utility>
#include <vector>

namespace cad {

// Array (matriz) de entidades. Dois modos:
//   * Rectangular: grade de rows x cols com passos dx (horizontal) e dy
//     (vertical). Cria clones transladados para cada célula EXCETO a original
//     (i=0, j=0), que já existe na base.
//   * Polar: count instâncias distribuídas ao redor de center varrendo
//     totalAngleRad no total; cria count-1 cópias rotacionadas (a original conta
//     como a primeira instância). A rotação usa
//     translate(center) * rotationZ(ang) * translate(-center).
//
// Em ambos os modos os clones são adicionados à base e seus ids guardados;
// undo remove os clones criados (guardando-os), redo os reinsere. Espelha o
// padrão de CopyCmd. Construa via ArrayCmd::rectangular(...) ou
// ArrayCmd::polar(...).
class ArrayCmd final : public Command {
public:
    enum class ArrayKind { Rectangular, Polar };

    // Construtor estático — array retangular.
    static ArrayCmd rectangular(std::vector<EntityId> ids, int rows, int cols,
                                double dx, double dy) {
        ArrayCmd c(std::move(ids));
        c.m_kind = ArrayKind::Rectangular;
        c.m_rows = rows;
        c.m_cols = cols;
        c.m_dx   = dx;
        c.m_dy   = dy;
        return c;
    }

    // Construtor estático — array polar.
    static ArrayCmd polar(std::vector<EntityId> ids, Point3 center, int count,
                          double totalAngleRad) {
        ArrayCmd c(std::move(ids));
        c.m_kind       = ArrayKind::Polar;
        c.m_center     = center;
        c.m_count      = count;
        c.m_totalAngle = totalAngleRad;
        return c;
    }

    void execute(DrawingManager& doc) override {
        if (!m_made) {
            if (m_kind == ArrayKind::Rectangular) buildRectangular(doc);
            else                                  buildPolar(doc);
            m_made = true;
        } else {  // redo: reinsere os clones guardados
            for (auto& c : m_clones) if (c) doc.reinsert(std::move(c));
            m_clones.clear();
        }
    }

    void undo(DrawingManager& doc) override {
        m_clones.clear();
        for (const EntityId id : m_newIds) m_clones.push_back(doc.removeEntity(id));
    }

    std::string label() const override { return "ARRAY"; }

private:
    explicit ArrayCmd(std::vector<EntityId> ids) : m_srcIds(std::move(ids)) {}

    // Clona cada fonte, aplica a matriz e adiciona à base guardando o id.
    void cloneWith(DrawingManager& doc, const Matrix4& m) {
        for (const EntityId id : m_srcIds)
            if (const Entity* e = doc.getEntity(id)) {
                EntityPtr c = e->clone();
                c->transform(m);
                m_newIds.push_back(doc.addEntity(std::move(c)));
            }
    }

    void buildRectangular(DrawingManager& doc) {
        for (int i = 0; i < m_rows; ++i)
            for (int j = 0; j < m_cols; ++j) {
                if (i == 0 && j == 0) continue;  // original já existe
                const Matrix4 m =
                    Matrix4::translation(Vec3{j * m_dx, i * m_dy, 0.0});
                cloneWith(doc, m);
            }
    }

    void buildPolar(DrawingManager& doc) {
        if (m_count <= 1) return;  // só a original
        // Passo angular: distribui totalAngle entre as count instâncias. Para
        // uma volta completa (2π) o passo é totalAngle/count; usamos o mesmo
        // critério aqui (count divisões do ângulo varrido).
        const double step = m_totalAngle / static_cast<double>(m_count);
        const Matrix4 toOrigin =
            Matrix4::translation(Vec3{-m_center.x, -m_center.y, -m_center.z});
        const Matrix4 fromOrigin = Matrix4::translation(m_center);
        for (int k = 1; k < m_count; ++k) {
            const double ang = step * static_cast<double>(k);
            const Matrix4 m = fromOrigin * Matrix4::rotationZ(ang) * toOrigin;
            cloneWith(doc, m);
        }
    }

    std::vector<EntityId>  m_srcIds;
    ArrayKind              m_kind{ArrayKind::Rectangular};

    // Retangular.
    int    m_rows{1};
    int    m_cols{1};
    double m_dx{0.0};
    double m_dy{0.0};

    // Polar.
    Point3 m_center{};
    int    m_count{1};
    double m_totalAngle{0.0};

    // Estado de undo/redo.
    std::vector<EntityPtr> m_clones;
    std::vector<EntityId>  m_newIds;
    bool                   m_made{false};
};

} // namespace cad
