// src/zendo/PlantImport.cpp
#include "PlantImport.hpp"

#include <algorithm>
#include <cmath>

#include "core/document/DrawingManager.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/geometry/Wall.hpp"

using cad::Point3;
using cad::Wall;

namespace {
// alturas padrão dos vãos (m) — v2: por-vão, vindas do 2D
constexpr double kDoorHead = 2.10;    // verga de porta/passagem
constexpr double kSillZ    = 0.90;    // peitoril de janela
constexpr double kWinHead  = 2.40;    // verga de janela
} // namespace

PlantScene importPlant(const cad::DrawingManager& doc, double wallHeight) {
    PlantScene out;
    const cad::LayerTable& layers = doc.layers();

    doc.forEach([&](const cad::Entity& e) {
        if (!e.visible()) return;
        const cad::Layer* ly = layers.find(e.layer());
        if (ly && (!ly->on || ly->frozen)) return;

        if (const Wall* w = dynamic_cast<const Wall*>(&e)) {
            // ---- PAREDE: extrusão por trechos sólidos + vergas/peitoris
            const auto& ax = w->axis();
            if (ax.size() < 2) return;
            const int wallNo = ++out.wallCount;
            std::vector<Point3> L, R;
            w->faceCorners(L, R);

            const double H = wallHeight;
            double acc = 0.0;
            for (std::size_t k = 0; k + 1 < ax.size(); ++k) {
                const double lk = std::hypot(ax[k + 1].x - ax[k].x,
                                             ax[k + 1].y - ax[k].y);
                if (lk < 1e-12) continue;

                struct Cut { double u0, u1; int kind; };
                std::vector<Cut> cuts;
                for (const Wall::Opening& op : w->openings()) {
                    const double s0 = op.station, s1 = op.station + op.width;
                    if (s0 >= acc - 1e-9 && s1 <= acc + lk + 1e-9 &&
                        op.width > 1e-9)
                        cuts.push_back(
                            {(s0 - acc) / lk, (s1 - acc) / lk, op.kind});
                }
                std::sort(cuts.begin(), cuts.end(),
                          [](const Cut& a, const Cut& b) {
                              return a.u0 < b.u0;
                          });

                auto lerp2 = [](const Point3& a, const Point3& b, double u,
                                double o2[2]) {
                    o2[0] = a.x + (b.x - a.x) * u;
                    o2[1] = a.y + (b.y - a.y) * u;
                };
                auto piece = [&](double u0, double u1, double z0, double z1) {
                    if (u1 - u0 < 1e-9 || z1 - z0 < 1e-6) return;
                    PlantScene::Box b;
                    lerp2(L[k], L[k + 1], u0, b.l0);
                    lerp2(L[k], L[k + 1], u1, b.l1);
                    lerp2(R[k], R[k + 1], u0, b.r0);
                    lerp2(R[k], R[k + 1], u1, b.r1);
                    b.z0 = z0;
                    b.z1 = z1;
                    b.wallNo = wallNo;
                    out.boxes.push_back(b);
                };

                double cur = 0.0;
                for (const Cut& c : cuts) {
                    piece(cur, c.u0, 0.0, H);                       // sólido
                    if (c.kind == 2) {                              // JANELA
                        piece(c.u0, c.u1, 0.0, std::min(kSillZ, H));
                        if (H > kWinHead) piece(c.u0, c.u1, kWinHead, H);
                    } else {                                        // porta
                        if (H > kDoorHead) piece(c.u0, c.u1, kDoorHead, H);
                    }
                    cur = c.u1;
                }
                piece(cur, 1.0, 0.0, H);
                acc += lk;
            }
            return;
        }
        // ---- LINEWORK: qualquer entidade 2D vira traço no chão
        cad::RenderBatch batch;
        e.emitTo(batch);
        for (const Point3& p : batch.lineVertices)
            out.groundLines.insert(out.groundLines.end(),
                                   {float(p.x), float(p.y), 0.012f});
    });
    return out;
}
