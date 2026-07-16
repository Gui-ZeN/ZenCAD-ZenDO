// src/app/CursorHud.hpp
#pragma once

#include <vector>
#include <cmath>
#include <QString>

#include "core/math/Vec.hpp"
#include "core/math/Constants.hpp"
#include "core/geometry/SnapPoint.hpp"

namespace cad {

// Marcador de OSNAP por tipo, no estilo AutoCAD (AutoSnap): Endpoint = quadrado,
// Midpoint = triângulo, Center = círculo, Quadrant = losango, Intersection = X.
// cx,cy = centro (em coords RELATIVAS à origem de rebase); h = meia-dimensão.
// Retorna pares (x,y) prontos para GL_LINES.
inline std::vector<float> buildSnapMarker(SnapType type, float cx, float cy, float h) {
    std::vector<float> v;
    auto seg = [&](float ax, float ay, float bx, float by) {
        v.push_back(cx + ax); v.push_back(cy + ay);
        v.push_back(cx + bx); v.push_back(cy + by);
    };
    switch (type) {
        case SnapType::Midpoint:                       // triângulo
            seg(-h, -h,  h, -h);  seg(h, -h, 0, h);  seg(0, h, -h, -h);
            break;
        case SnapType::Center: {                       // círculo
            const int N = 16;
            float px = h, py = 0.0f;
            for (int i = 1; i <= N; ++i) {
                const float a = static_cast<float>(i) / N * 6.2831853f;
                const float nx = h * std::cos(a), ny = h * std::sin(a);
                seg(px, py, nx, ny);
                px = nx; py = ny;
            }
            break;
        }
        case SnapType::Quadrant:                       // losango
            seg(0, h, h, 0);  seg(h, 0, 0, -h);  seg(0, -h, -h, 0);  seg(-h, 0, 0, h);
            break;
        case SnapType::Intersection:                   // X
            seg(-h, -h, h, h);  seg(-h, h, h, -h);
            break;
        case SnapType::Nearest:                        // ampulheta (bowtie)
            seg(-h, h, h, h);  seg(h, h, -h, -h);  seg(-h, -h, h, -h);  seg(h, -h, -h, h);
            break;
        case SnapType::Perpendicular:                  // símbolo de ângulo reto
            seg(-h, h, -h, -h);  seg(-h, -h, h, -h);   // lados (L)
            seg(-h, 0, 0, 0);    seg(0, 0, 0, -h);     // marca interna do ângulo reto
            break;
        case SnapType::Tangent: {                       // círculo + tangente no topo
            const int N = 12;
            float px = h, py = 0.0f;
            for (int i = 1; i <= N; ++i) {
                const float a = static_cast<float>(i) / N * 6.2831853f;
                const float nx = h * std::cos(a), ny = h * std::sin(a);
                seg(px, py, nx, ny);
                px = nx; py = ny;
            }
            seg(-h, h, h, h);                          // reta tangente acima do círculo
            break;
        }
        case SnapType::Node: {                         // círculo com X (⊗)
            const int N = 12;
            float px = h, py = 0.0f;
            for (int i = 1; i <= N; ++i) {
                const float a = static_cast<float>(i) / N * 6.2831853f;
                const float nx = h * std::cos(a), ny = h * std::sin(a);
                seg(px, py, nx, ny);
                px = nx; py = ny;
            }
            const float k = h * 0.6f;
            seg(-k, -k, k, k);  seg(-k, k, k, -k);
            break;
        }
        case SnapType::Extension:                      // reticências (tracinhos)
            seg(-h, 0, -h * 0.4f, 0);  seg(-h * 0.15f, 0, h * 0.15f, 0);
            seg(h * 0.4f, 0, h, 0);
            break;
        case SnapType::Parallel:                       // duas barras paralelas (∥)
            seg(-h * 0.7f, -h, h * 0.1f, h);  seg(-h * 0.1f, -h, h * 0.7f, h);
            break;
        case SnapType::Insertion:                      // dois quadrados deslocados
            seg(-h, -h, 0, -h);  seg(0, -h, 0, 0);  seg(0, 0, -h, 0);  seg(-h, 0, -h, -h);
            seg(0, 0, h, 0);     seg(h, 0, h, h);   seg(h, h, 0, h);   seg(0, h, 0, 0);
            break;
        case SnapType::GeomCenter: {                   // círculo + cruzinha central
            const int N = 12;
            float px = h, py = 0.0f;
            for (int i = 1; i <= N; ++i) {
                const float a = static_cast<float>(i) / N * 6.2831853f;
                const float nx = h * std::cos(a), ny = h * std::sin(a);
                seg(px, py, nx, ny);
                px = nx; py = ny;
            }
            seg(-h * 0.4f, 0, h * 0.4f, 0);  seg(0, -h * 0.4f, 0, h * 0.4f);
            break;
        }
        case SnapType::AppInt:                         // X dentro de um quadrado
            seg(-h, -h, h, h);  seg(-h, h, h, -h);
            seg(-h, -h, h, -h); seg(h, -h, h, h);  seg(h, h, -h, h);  seg(-h, h, -h, -h);
            break;
        case SnapType::Endpoint:
        default:                                       // quadrado
            seg(-h, -h, h, -h);  seg(h, -h, h, h);  seg(h, h, -h, h);  seg(-h, h, -h, -h);
            break;
    }
    return v;
}

// HUD do cursor: utilitários para desenhar a MIRA (crosshair) estilo AutoCAD
// e o rótulo de "entrada dinâmica" (dynamic input).

// Monta os vértices da MIRA (crosshair) para desenho com GL_LINES.
//
// Gera duas linhas que se cruzam no cursor:
//   - HORIZONTAL: de (minX, curY) até (maxX, curY)
//   - VERTICAL:   de (curX, minY) até (curX, maxY)
//
// Retorna 8 floats = 4 vértices (pares x,y), prontos para GL_LINES.
//
// IMPORTANTE: todos os valores chegam já em coordenadas RELATIVAS à origem
// de rebase (quem chama subtrai a origem). Aqui apenas montamos os vértices
// com os números recebidos.
// Mira curta: cada braço tem comprimento `half` (em mundo) — tipicamente o
// tamanho de uma célula da grade, para uma cruz discreta (não atravessa a tela).
inline std::vector<float> buildCrosshair(double curX, double curY, double half) {
    const float cx = static_cast<float>(curX), cy = static_cast<float>(curY);
    const float h  = static_cast<float>(half);
    return {
        cx - h, cy,  cx + h, cy,   // braço horizontal
        cx, cy - h,  cx, cy + h,   // braço vertical
    };
}

// Monta o rótulo de "entrada dinâmica" (dynamic input) do AutoCAD para o
// segmento base -> cursor: comprimento, ângulo (graus, 0–360 CCW a partir
// de +X) e o delta (dx, dy).
inline QString dynamicInputText(const Point3& base, const Point3& cursor) {
    const double dx = cursor.x - base.x;
    const double dy = cursor.y - base.y;

    const double len = std::hypot(dx, dy);

    // atan2 retorna em rad no intervalo (-pi, pi]; converte para graus
    // e normaliza para [0, 360).
    double ang = std::atan2(dy, dx) * 180.0 / kPi;
    if (ang < 0.0) {
        ang += 360.0;
    }

    return QString::asprintf("L %.2f   ang %.1f°   dx %.2f  dy %.2f",
                             len, ang, dx, dy);
}

} // namespace cad
