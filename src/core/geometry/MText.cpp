// src/core/geometry/MText.cpp
#include "core/geometry/MText.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/text/StrokeFont.hpp"
#include "core/text/TtfFont.hpp"
#include "core/math/Vec.hpp"
#include "core/math/Matrix4.hpp"
#include <cmath>

namespace cad {

std::vector<Point3> MText::buildStrokePoints() const {
    // Empilha as linhas (separadas por '\n') de cima para baixo, com entrelinha
    // de 1.4×altura, e justifica cada uma pela sua largura. Reutiliza strokeText
    // e strokeTextWidth para que o critério de avanço por caractere seja EXATO.
    std::vector<Point3> out;

    // TTF quando a entidade tem fonte E um provider está registrado (app Qt);
    // senão, StrokeFont — mantém o kernel headless bit-a-bit.
    const bool ttf = !m_font.empty() && ttfTessellator() && ttfMeasurer();
    auto makeLine = [&](const std::string& s, const Point3& pos) {
        return TtfLine{s, m_font, pos, m_height, m_rotation, m_bold, m_italic};
    };
    auto measure = [&](const std::string& s) {
        return ttf ? ttfMeasurer()(makeLine(s, Point3{}))
                   : strokeTextWidth(s, m_height);
    };

    // 1) Conteúdo -> linhas de EXIBIÇÃO: divide por '\n' e, se a caixa tem
    //    largura (m_boxWidth>0), quebra cada linha POR PALAVRA nessa largura
    //    (palavra maior que a caixa fica sozinha na linha, sem cortar).
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl  = m_text.find('\n', start);
        const bool        end = (nl == std::string::npos);
        std::string raw = m_text.substr(start, end ? std::string::npos : nl - start);
        if (m_boxWidth > 1e-9) {
            std::string cur;
            std::size_t ws = 0;
            while (ws <= raw.size()) {
                const std::size_t sp = raw.find(' ', ws);
                const bool last = (sp == std::string::npos);
                const std::string word = raw.substr(ws, last ? std::string::npos : sp - ws);
                const std::string cand = cur.empty() ? word : cur + " " + word;
                if (!cur.empty() && measure(cand) > m_boxWidth) {
                    lines.push_back(cur);
                    cur = word;
                } else {
                    cur = cand;
                }
                if (last) break;
                ws = sp + 1;
            }
            lines.push_back(cur);
        } else {
            lines.push_back(std::move(raw));
        }
        if (end) break;
        start = nl + 1;
    }

    const double lineSpacing = 1.4 * m_height; // entrelinha (descida em Y local)

    // Rotação CCW aplicada ao offset local de cada linha (mesma de strokeText).
    const double cs = std::cos(m_rotation);
    const double sn = std::sin(m_rotation);

    for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const std::string& line = lines[lineIndex];

        // Offset local da linha em relação ao ponto de inserção:
        //   X = justificação pela largura; Y = descida pela entrelinha.
        const double width = measure(line);
        double offX = 0.0;
        switch (m_justify) {
            case MTextJustify::Left:   offX = 0.0;          break;
            case MTextJustify::Center: offX = -width / 2.0; break;
            case MTextJustify::Right:  offX = -width;       break;
        }
        const double offY = -static_cast<double>(lineIndex) * lineSpacing;

        // Rotaciona o offset local e soma ao ponto de inserção: ponto de
        // partida REAL desta linha no mundo. strokeText cuida do resto.
        const double rx = offX * cs - offY * sn;
        const double ry = offX * sn + offY * cs;
        const Point3 linePos{m_pos.x + rx, m_pos.y + ry, m_pos.z};

        const std::vector<Point3> pts = ttf
            ? ttfTessellator()(makeLine(line, linePos))
            : strokeText(line, linePos, m_height, m_rotation);
        out.insert(out.end(), pts.begin(), pts.end());
    }

    return out;
}

AABB MText::boundingBox() const {
    // Envolve todos os pontos gerados pelos traços do texto — já considera
    // rotação, altura, largura e o layout multilinha reais (mais simples e exato).
    AABB b;
    const std::vector<Point3> pts = buildStrokePoints();
    for (const Point3& p : pts) b.expand(p);
    // Texto vazio: ao menos o ponto de inserção é uma caixa válida.
    if (!b.valid()) b.expand(m_pos);
    return b;
}

void MText::emitTo(RenderBatch& batch) const {
    // buildStrokePoints devolve pares consecutivos (a,b),(c,d),... = um segmento.
    const std::vector<Point3> pts = buildStrokePoints();
    for (std::size_t i = 0; i + 1 < pts.size(); i += 2)
        batch.addSegment(pts[i], pts[i + 1]);
}

HitResult MText::hitTest(const Ray& pickRay, double tol) const {
    const Point3 p = pickRay.origin;

    // 1) Proximidade do ponto de inserção.
    const double dx = p.x - m_pos.x, dy = p.y - m_pos.y;
    const double dIns = std::sqrt(dx * dx + dy * dy);
    if (dIns <= tol) {
        return HitResult{true, dIns, m_pos};
    }

    // 2) Dentro do bounding box (no plano XY).
    const AABB box = boundingBox();
    if (box.contains(p)) {
        return HitResult{true, 0.0, p};
    }
    return HitResult{};
}

void MText::transform(const Matrix4& m) {
    // Posição via ponto; rotação e escala extraídas do eixo X transformado.
    m_pos = m.transformPoint(m_pos);
    const Vec3   xAxis = m.transformVector(Vec3{1.0, 0.0, 0.0});
    m_rotation += std::atan2(xAxis.y, xAxis.x);
    m_height   *= std::sqrt(xAxis.x * xAxis.x + xAxis.y * xAxis.y);
}

void MText::appendSnapPoints(std::vector<SnapPoint>& out) const {
    out.push_back({m_pos, SnapType::Endpoint});
    out.push_back({m_pos, SnapType::Insertion});   // ponto de inserção do texto
}

std::unique_ptr<Entity> MText::clone() const {
    return std::make_unique<MText>(*this);
}

} // namespace cad
