// src/core/geometry/Entity.hpp
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "core/geometry/Properties.hpp"
#include "core/geometry/SnapPoint.hpp"
#include "core/math/Vec.hpp"
#include "core/math/AABB.hpp"
#include "core/math/Ray.hpp"
#include "core/math/Matrix4.hpp"

namespace cad {

// Resultado de teste de proximidade (picking/seleção).
struct HitResult {
    bool   hit{false};
    double distance{0.0};   // distância em unidades de mundo até a geometria
    Point3 point{};         // ponto mais próximo sobre a entidade
    explicit operator bool() const { return hit; }
};

class LayerTable;     // fwd — resolução de ByLayer
struct RenderBatch;   // fwd — destino de emissão (sem incluir o header GL aqui)
class EntityVisitor;  // fwd — double-dispatch

// ============================================================================
//  Entity — base abstrata de TODA primitiva geométrica.
//  Invariantes:
//   * coordenadas sempre em precisão dupla (double);
//   * NÃO conhece OpenGL/Qt — só emite para RenderBatch;
//   * clone() produz cópia profunda independente (suporte a Memento/undo).
// ============================================================================
class Entity {
public:
    virtual ~Entity() = default;

    // --- Identidade & propriedades comuns ---------------------------------
    EntityId           id()    const noexcept { return m_id; }
    const std::string& layer() const noexcept { return m_layer; }
    void setLayer(std::string name) { m_layer = std::move(name); }

    const ColorRef&   color()      const noexcept { return m_color; }
    const LineType&   lineType()   const noexcept { return m_lineType; }
    const LineWeight& lineWeight() const noexcept { return m_lineWeight; }
    bool              visible()    const noexcept { return m_visible; }

    void setColor(ColorRef c)        { m_color = c; }
    void setLineType(LineType lt)    { m_lineType = std::move(lt); }
    void setLineWeight(LineWeight w) { m_lineWeight = w; }
    void setVisible(bool v)          { m_visible = v; }

    // Resolve a cor efetiva considerando ByLayer/ByBlock contra a tabela.
    Rgba resolveColor(const LayerTable& layers) const;

    // --- Contrato geométrico (cada primitiva implementa) ------------------
    virtual AABB      boundingBox() const = 0;                       // p/ índice espacial
    virtual void      emitTo(RenderBatch& batch) const = 0;          // sem chamadas GL (Qt usa 'emit')
    virtual HitResult hitTest(const Ray& pickRay, double tol) const = 0;
    virtual void      transform(const Matrix4& m) = 0;               // afim in-place
    virtual void      appendSnapPoints(std::vector<SnapPoint>& out) const = 0;  // OSNAP
    virtual std::unique_ptr<Entity> clone() const = 0;              // cópia profunda
    virtual void      accept(EntityVisitor& v) const = 0;            // double-dispatch
    virtual const char* typeName() const noexcept = 0;               // "LINE", "CIRCLE"...

protected:
    Entity() = default;
    Entity(const Entity&) = default;  // usado pelas implementações de clone()

    friend class DrawingManager;      // atribui o id ao inserir
    void setId(EntityId id) noexcept { m_id = id; }

    EntityId    m_id{kInvalidId};
    std::string m_layer{"0"};
    ColorRef    m_color{ColorRef::byLayer()};
    LineType    m_lineType{};
    LineWeight  m_lineWeight{};
    bool        m_visible{true};
};

using EntityPtr = std::unique_ptr<Entity>;

// Visitor — um método por primitiva concreta (DXF writer, área, exportadores...).
class EntityVisitor {
public:
    virtual ~EntityVisitor() = default;
    virtual void visit(const class Line&)         {}
    virtual void visit(const class Polyline&)     {}
    virtual void visit(const class Circle&)       {}
    virtual void visit(const class Arc&)          {}
    virtual void visit(const class Ellipse&)      {}
    virtual void visit(const class MText&)        {}
    virtual void visit(const class Hatch&)        {}
    virtual void visit(const class Dimension&)    {}
    virtual void visit(const class PointEntity&)  {}
    virtual void visit(const class Spline&)       {}
    virtual void visit(const class BlockRef&)     {}
    virtual void visit(const class XLine&)        {}
    virtual void visit(const class RayLine&)      {}
    virtual void visit(const class MLine&)        {}
    virtual void visit(const class Leader&)       {}
    virtual void visit(const class MLeader&)      {}
    virtual void visit(const class Solid3D&)      {}
    virtual void visit(const class NurbsSurface&) {}
    virtual void visit(const class PolyMesh&)     {}
    virtual void visit(const class Wipeout&)      {}
    virtual void visit(const class Region&)       {}
    virtual void visit(const class Wall&)         {}
    virtual void visit(const class Table&)        {}
    virtual void visit(const class AttDef&)       {}
};

} // namespace cad
