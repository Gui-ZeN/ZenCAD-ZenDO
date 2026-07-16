// src/core/geometry/Polyline.hpp
#pragma once
#include "core/geometry/Entity.hpp"
#include <vector>
#include <cmath>

namespace cad {

// Polilinha: sequência de vértices conectados; opcionalmente fechada. Cada trecho
// i->i+1 pode ser um ARCO via o "bulge" m_bulges[i] (tan do ¼ do ângulo; 0 = reto)
// — o mesmo modelo do LWPOLYLINE do DXF. Base para contornos e hachuras.
class Polyline final : public Entity {
public:
    Polyline() = default;
    explicit Polyline(std::vector<Point3> verts, bool closed = false)
        : m_verts(std::move(verts)), m_closed(closed) {}
    Polyline(std::vector<Point3> verts, std::vector<double> bulges, bool closed)
        : m_verts(std::move(verts)), m_bulges(std::move(bulges)), m_closed(closed) {}

    const std::vector<Point3>& vertices() const { return m_verts; }
    const std::vector<double>& bulges() const { return m_bulges; }
    bool closed() const { return m_closed; }
    void addVertex(const Point3& p) { m_verts.push_back(p); }
    void setClosed(bool c) { m_closed = c; }
    void setBulges(std::vector<double> b) { m_bulges = std::move(b); }
    // Largura global da polilinha (0 = traço fino). > 0 => desenhada preenchida.
    void   setWidth(double w) { m_width = w; }
    double width() const { return m_width; }
    // Bulge do trecho que começa no vértice i (0 se não houver).
    double bulgeAt(std::size_t i) const { return i < m_bulges.size() ? m_bulges[i] : 0.0; }
    bool hasArcs() const {
        for (double b : m_bulges) if (std::fabs(b) > 1e-12) return true;
        return false;
    }
    // Polilinha tesselada (arcos viram segmentos) — usada por emit/hitTest/bbox
    // e por consumidores externos (Divide, etc.).
    std::vector<Point3> sampledPoints() const;

    AABB      boundingBox() const override;
    void      emitTo(RenderBatch& batch) const override;
    HitResult hitTest(const Ray& pickRay, double tol) const override;
    void      transform(const Matrix4& m) override;
    void      appendSnapPoints(std::vector<SnapPoint>& out) const override;
    std::unique_ptr<Entity> clone() const override;
    void      accept(EntityVisitor& v) const override { v.visit(*this); }
    const char* typeName() const noexcept override { return "LWPOLYLINE"; }

private:
    std::vector<Point3> m_verts;
    std::vector<double> m_bulges;   // bulge por trecho (i -> i+1); vazio = tudo reto
    bool                m_closed{false};
    double              m_width{0.0};   // largura global (0 = fino)
};

} // namespace cad
