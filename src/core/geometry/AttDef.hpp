// src/core/geometry/AttDef.hpp
// Definição de ATRIBUTO de bloco (à la ATTDEF): um campo de texto nomeado
// (tag) com prompt e valor padrão. Fora de um bloco renderiza a TAG (como no
// AutoCAD); ao virar bloco (MakeBlockCmd), vira um AttDefSpec da definição e
// cada INSERT recebe um VALOR próprio (renderizado pelo BlockRef).
// Header-only (como os ops do kernel) para não tocar o CMake.
#pragma once
#include "core/geometry/Entity.hpp"
#include "core/geometry/MText.hpp"
#include <string>
#include <memory>
#include <cmath>

namespace cad {

class AttDef final : public Entity {
public:
    AttDef() = default;
    AttDef(Point3 pos, std::string tag, std::string prompt,
           std::string defValue, double height)
        : m_pos(pos), m_tag(std::move(tag)), m_prompt(std::move(prompt)),
          m_default(std::move(defValue)), m_height(height) {}

    const Point3&      position() const { return m_pos; }
    const std::string& tag()      const { return m_tag; }
    const std::string& prompt()   const { return m_prompt; }
    const std::string& defValue() const { return m_default; }
    double             height()   const { return m_height; }

    AABB boundingBox() const override { return render().boundingBox(); }
    void emitTo(RenderBatch& batch) const override { render().emitTo(batch); }
    HitResult hitTest(const Ray& pickRay, double tol) const override {
        return render().hitTest(pickRay, tol);
    }
    void transform(const Matrix4& m) override {
        m_pos = m.transformPoint(m_pos);
        // Escala uniforme aproximada: comprimento da imagem do eixo X.
        const Vec3 sx = m.transformVector(Vec3{1.0, 0.0, 0.0});
        const double k = sx.length();
        if (k > 1e-12) m_height *= k;
    }
    void appendSnapPoints(std::vector<SnapPoint>& out) const override {
        out.push_back({m_pos, SnapType::Endpoint});
    }
    std::unique_ptr<Entity> clone() const override {
        return std::make_unique<AttDef>(*this);   // copia base + campos (como Line)
    }
    void accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "ATTDEF"; }

private:
    // Renderização = a TAG como texto de traços (estilo AutoCAD pré-inserção).
    MText render() const { return MText(m_pos, m_tag, m_height); }

    Point3      m_pos{};
    std::string m_tag{"TAG"};
    std::string m_prompt{};
    std::string m_default{};
    double      m_height{2.5};
};

} // namespace cad
