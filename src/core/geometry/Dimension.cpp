// src/core/geometry/Dimension.cpp
#include "core/geometry/Dimension.hpp"
#include "core/geometry/RenderBatch.hpp"
#include "core/text/StrokeFont.hpp"
#include "core/text/TtfFont.hpp"
#include "core/math/Vec.hpp"
#include "core/math/Matrix4.hpp"
#include "core/math/Constants.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace cad {
namespace {

// Formata um número com `decimals` casas decimais, removendo zeros/ponto à
// direita, e anexa `suffix` ao final (ex.: " mm"). Os defaults (2 casas,
// sufixo vazio) reproduzem o comportamento original do antigo fmt("%.2f").
// `decimals` é limitado a [0, 15] para manter o buffer seguro e o resultado
// numericamente sensato (precisão dupla).
std::string fmt(double v, int decimals, const std::string& suffix) {
    if (decimals < 0)  decimals = 0;
    if (decimals > 15) decimals = 15;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    std::string s{buf};
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    s += suffix;  // sufixo de unidade (vazio = como antes)
    return s;
}

// Largura do texto da cota: TTF (quando a cota tem fonte E há provider
// registrado) ou fonte de traços — mesmo critério do MText.
double textWidth(const std::string& s, double h, const std::string& font) {
    if (!font.empty() && ttfMeasurer())
        return ttfMeasurer()(TtfLine{s, font, Point3{}, h, 0.0, false, false});
    return strokeTextWidth(s, h);
}

// Emite o texto ao batch (TTF via hook ou traços).
void emitText(RenderBatch& batch, const std::string& s, const Point3& pos,
              double height, double rotation, const std::string& font) {
    const std::vector<Point3> pts = (!font.empty() && ttfTessellator())
        ? ttfTessellator()(TtfLine{s, font, pos, height, rotation, false, false})
        : strokeText(s, pos, height, rotation);
    for (std::size_t i = 0; i + 1 < pts.size(); i += 2)
        batch.addSegment(pts[i], pts[i + 1]);
}

// Cabeça de seta como um "V" de 2 segmentos, apontando de `from` para `tip`.
// `size` é o comprimento aproximado das hastes da seta.
// type: 0 = seta (2 traços), 1 = tique arquitetônico (barra a 45°), 2 = ponto (cruz).
void emitArrow(RenderBatch& batch, const Point3& tip, const Point3& from, double size, int type) {
    Vec3 dir = (tip - from);
    const double len = dir.length();
    if (len <= 0.0) return;
    dir = dir * (1.0 / len);                 // unitário tip<-from -> direção da ponta
    const Vec3 perp{-dir.y, dir.x, 0.0};
    if (type == 1) {                         // TIQUE arquitetônico: barra a 45° na ponta
        Vec3 t{dir.x + perp.x, dir.y + perp.y, 0.0};
        const double tl = t.length();
        if (tl > 0.0) t = t * (1.0 / tl);
        const double half = size * 0.7;
        batch.addSegment(Point3{tip.x - t.x * half, tip.y - t.y * half, tip.z},
                         Point3{tip.x + t.x * half, tip.y + t.y * half, tip.z});
        return;
    }
    if (type == 2) {                         // PONTO: cruz curta na ponta
        const double r = size * 0.3;
        batch.addSegment(Point3{tip.x - perp.x * r, tip.y - perp.y * r, tip.z},
                         Point3{tip.x + perp.x * r, tip.y + perp.y * r, tip.z});
        batch.addSegment(Point3{tip.x - dir.x * r, tip.y - dir.y * r, tip.z},
                         Point3{tip.x + dir.x * r, tip.y + dir.y * r, tip.z});
        return;
    }
    const double back = size;                // seta padrão: recuo ao longo do eixo
    const double half = size * 0.35;         // meia-abertura da seta
    const Point3 base = tip - dir * back;
    const Point3 a{base.x + perp.x * half, base.y + perp.y * half, base.z};
    const Point3 b{base.x - perp.x * half, base.y - perp.y * half, base.z};
    batch.addSegment(tip, a);
    batch.addSegment(tip, b);
}

double dist2D(const Point3& a, const Point3& b) {
    return std::hypot(b.x - a.x, b.y - a.y);
}

// Distância (no plano XY) de p ao segmento a-b.
double distPointSeg2D(const Point3& p, const Point3& a, const Point3& b) {
    const double vx = b.x - a.x, vy = b.y - a.y;
    const double wx = p.x - a.x, wy = p.y - a.y;
    const double vv = vx * vx + vy * vy;
    double t = vv > 0.0 ? (wx * vx + wy * vy) / vv : 0.0;
    if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
    const double cx = a.x + vx * t, cy = a.y + vy * t;
    return std::hypot(p.x - cx, p.y - cy);
}

} // namespace

// ============================================================================
//  Encadeamento de cotas LINEARES (continue / baseline) — estilo AutoCAD.
//
//  Estratégia comum: ambas reaproveitam Dimension::linear(p1,p2,linePos,h).
//  A orientação (horizontal/vertical) e a posição da linha de cota são
//  herdadas de `prev`. Como linear() recalcula a orientação pelo eixo
//  dominante de (p1,p2), montamos `linePos` na coordenada correta do eixo
//  perpendicular à medida de `prev` e forçamos o ponto da linha a respeitar a
//  orientação de `prev`: usamos o Y da linha quando `prev` é horizontal e o X
//  da linha quando `prev` é vertical, preenchendo o outro componente com o
//  valor do próprio ponto correspondente (irrelevante para esse eixo, mas
//  mantém a geometria limpa).
// ============================================================================
namespace {

// Constrói a cota linear filha a partir de (startPt -> nextPoint), mantendo a
// orientação de `prev` e colocando a linha de cota na coordenada `lineCoord`
// (Y se horizontal, X se vertical).
Dimension makeChildLinear(bool horizontal, double lineCoord,
                          const Point3& startPt, const Point3& nextPoint,
                          double textH) {
    Point3 linePos;
    if (horizontal) {
        // Orientação horizontal: o que importa em m_p3 é o Y (lineCoord).
        // Garante eixo dominante X clonando o Y nos dois pontos medidos não é
        // necessário — linear() decide pelo eixo dominante de (startPt,nextPoint).
        linePos = Point3{nextPoint.x, lineCoord, startPt.z};
    } else {
        // Orientação vertical: o que importa em m_p3 é o X (lineCoord).
        linePos = Point3{lineCoord, nextPoint.y, startPt.z};
    }
    return Dimension::linear(startPt, nextPoint, linePos, textH);
}

} // namespace

Dimension Dimension::continueLinear(const Dimension& prev, const Point3& nextPoint,
                                    double textH) {
    if (prev.kind() != DimKind::Linear) {
        // Fallback seguro: cota linear simples de prev.p2() até nextPoint,
        // com a linha de cota passando pelo próprio nextPoint (offset zero).
        return Dimension::linear(prev.p2(), nextPoint, nextPoint, textH);
    }
    const bool horizontal = prev.isLinearHorizontal();
    const double lineCoord = prev.linearLineCoord();   // mesma linha de cota
    // Continua a partir de onde `prev` terminou (2º ponto de prev).
    return makeChildLinear(horizontal, lineCoord, prev.p2(), nextPoint, textH);
}

Dimension Dimension::baselineLinear(const Dimension& prev, const Point3& nextPoint,
                                    double textH, double offsetStep) {
    if (prev.kind() != DimKind::Linear) {
        // Fallback seguro: cota linear simples de prev.p1() até nextPoint,
        // com a linha de cota passando pelo próprio nextPoint (offset zero).
        return Dimension::linear(prev.p1(), nextPoint, nextPoint, textH);
    }
    const bool horizontal = prev.isLinearHorizontal();
    // Empilha: linha de cota de `prev` + um passo adicional, no MESMO sentido
    // em que a linha de cota de `prev` já está deslocada dos pontos medidos.
    const double prevOffset = prev.linearOffset();            // assinado
    const double sign = (prevOffset >= 0.0) ? 1.0 : -1.0;     // sentido do afastamento
    const double lineCoord = prev.linearLineCoord() + sign * offsetStep;
    // Parte sempre do MESMO 1º ponto de `prev` (base comum).
    return makeChildLinear(horizontal, lineCoord, prev.p1(), nextPoint, textH);
}

void Dimension::emitTo(RenderBatch& batch) const {
    const double h = m_textHeight;
    // Tamanho da seta: usa o estilo configurado; se m_arrowSize < 0 (default
    // "automático"), cai para a altura do texto — o comportamento original.
    const double arrow = (m_arrowSize >= 0.0) ? m_arrowSize : h;
    const int    atype = static_cast<int>(m_arrowType);   // seta/tique/ponto
    // Estilo de número aplicado em todos os ramos do switch abaixo.
    const int          dec = m_decimals;
    const std::string& suf = m_suffix;

    // Sufixo de TOLERÂNCIA: "±x" (simétrica) ou "+a/-b" (deposição). A fonte
    // de traços não tem o glifo ±, então cai em "+/-".
    auto tol = [&](std::string s) {
        if (m_tolPlus <= 0.0 && m_tolMinus <= 0.0) return s;
        if (std::abs(m_tolPlus - m_tolMinus) < 1e-12) {
            const std::string pm = m_font.empty() ? std::string("+/-")
                                                  : std::string("\xC2\xB1");
            return s + " " + pm + fmt(m_tolPlus, dec, std::string{});
        }
        return s + " +" + fmt(m_tolPlus, dec, std::string{}) +
               "/-" + fmt(m_tolMinus, dec, std::string{});
    };

    switch (m_kind) {
        case DimKind::Linear: {
            // Eixo dominante define se a cota é horizontal ou vertical.
            const double dx = std::abs(m_p2.x - m_p1.x);
            const double dy = std::abs(m_p2.y - m_p1.y);
            const bool horizontal = dx >= dy;

            // Projeta os pontos medidos na linha de cota (altura/coluna de m_p3).
            Point3 d1, d2;
            double measure;
            if (horizontal) {
                const double yLine = m_p3.y;
                d1 = Point3{m_p1.x, yLine, m_p1.z};
                d2 = Point3{m_p2.x, yLine, m_p2.z};
                measure = std::abs(m_p2.x - m_p1.x);
            } else {
                const double xLine = m_p3.x;
                d1 = Point3{xLine, m_p1.y, m_p1.z};
                d2 = Point3{xLine, m_p2.y, m_p2.z};
                measure = std::abs(m_p2.y - m_p1.y);
            }

            // Linhas de chamada (extension) dos pontos medidos até a linha de cota.
            batch.addSegment(m_p1, d1);
            batch.addSegment(m_p2, d2);
            // Linha de cota e setas (apontando para fora, em direção a d1/d2).
            batch.addSegment(d1, d2);
            emitArrow(batch, d1, d2, arrow, atype);
            emitArrow(batch, d2, d1, arrow, atype);

            // Texto centralizado sobre o meio da linha de cota. Cota VERTICAL
            // tem o texto GIRADO 90° (lendo de baixo p/ cima, ao lado esquerdo
            // da linha) — convenção ABNT/AutoCAD; horizontal fica deitado.
            // Override substitui o texto inteiro; offset = DIMTEDIT.
            const std::string txt = m_textOverride.empty()
                                  ? tol(fmt(measure, dec, suf)) : m_textOverride;
            const double tw = textWidth(txt, h, m_font);
            const Point3 mid{(d1.x + d2.x) * 0.5 + m_textOffX,
                             (d1.y + d2.y) * 0.5 + m_textOffY, d1.z};
            if (horizontal) {
                const Point3 tpos{mid.x - tw * 0.5, mid.y + h * 0.3, mid.z};
                emitText(batch, txt, tpos, h, 0.0, m_font);
            } else {
                // Rotação +90°: o texto corre em +Y a partir da âncora e o
                // "topo" das letras aponta para -X (fica à esquerda da linha).
                const Point3 tpos{mid.x - h * 0.3, mid.y - tw * 0.5, mid.z};
                emitText(batch, txt, tpos, h, kPi * 0.5, m_font);
            }
            break;
        }

        case DimKind::Aligned: {
            // Linha de cota paralela ao segmento p1-p2, deslocada para o lado de m_p3.
            Vec3 axis = (m_p2 - m_p1);
            const double measure = axis.length();
            if (measure <= 0.0) break;
            axis = axis * (1.0 / measure);
            const Vec3 perp{-axis.y, axis.x, 0.0};
            // Offset assinado: projeta (m_p3 - m_p1) na perpendicular.
            const Vec3 toLine = m_p3 - m_p1;
            const double off = toLine.dot(perp);

            const Point3 d1{m_p1.x + perp.x * off, m_p1.y + perp.y * off, m_p1.z};
            const Point3 d2{m_p2.x + perp.x * off, m_p2.y + perp.y * off, m_p2.z};

            batch.addSegment(m_p1, d1);
            batch.addSegment(m_p2, d2);
            batch.addSegment(d1, d2);
            emitArrow(batch, d1, d2, arrow, atype);
            emitArrow(batch, d2, d1, arrow, atype);

            const std::string txt = m_textOverride.empty()
                                  ? tol(fmt(measure, dec, suf)) : m_textOverride;
            const double tw  = textWidth(txt, h, m_font);
            // Direção de leitura legível: nunca de cabeça para baixo. Se o eixo
            // apontar para a esquerda (ou para baixo, na vertical), inverte.
            Vec3 tdir = axis;
            if (tdir.x < -1e-9 || (std::abs(tdir.x) <= 1e-9 && tdir.y < 0.0))
                tdir = tdir * -1.0;
            const double rot = std::atan2(tdir.y, tdir.x);   // em (-90°, 90°]
            const Vec3 up{-std::sin(rot), std::cos(rot), 0.0}; // "cima" do texto
            const Point3 mid{(d1.x + d2.x) * 0.5 + m_textOffX,
                             (d1.y + d2.y) * 0.5 + m_textOffY, d1.z};
            // Âncora = meio - tdir*(largura/2) + cima*(folga): centraliza e levanta.
            const Point3 tpos{
                mid.x - tdir.x * (tw * 0.5) + up.x * (h * 0.4),
                mid.y - tdir.y * (tw * 0.5) + up.y * (h * 0.4),
                mid.z};
            emitText(batch, txt, tpos, h, rot, m_font);
            break;
        }

        case DimKind::Radius: {
            const double r = dist2D(m_p1, m_p2);
            if (!m_jogged) {
                batch.addSegment(m_p1, m_p2);   // centro -> ponto no círculo
            } else {
                // JOGGED: leader com zigue-zague (centro implícito longe).
                const Vec3 dvec = m_p2 - m_p1;
                const double L = dvec.length();
                if (L > 1e-9) {
                    const Vec3 u = dvec * (1.0 / L);
                    const Vec3 pp{-u.y, u.x, 0.0};
                    auto at = [&](double t) {
                        return Point3{m_p1.x + u.x * L * t, m_p1.y + u.y * L * t, 0.0};
                    };
                    const double j = h * 0.9;
                    const Point3 a = at(0.40);
                    const Point3 b{at(0.50).x + pp.x * j, at(0.50).y + pp.y * j, 0.0};
                    const Point3 c2{at(0.60).x - pp.x * j, at(0.60).y - pp.y * j, 0.0};
                    const Point3 e2 = at(0.70);
                    batch.addSegment(m_p1, a);
                    batch.addSegment(a, b);
                    batch.addSegment(b, c2);
                    batch.addSegment(c2, e2);
                    batch.addSegment(e2, m_p2);
                } else {
                    batch.addSegment(m_p1, m_p2);
                }
            }
            emitArrow(batch, m_p2, m_p1, arrow, atype);

            const std::string txt = m_textOverride.empty()
                                  ? tol("R" + fmt(r, dec, suf)) : m_textOverride;
            const Point3 tpos{m_p2.x + h * 0.3 + m_textOffX,
                              m_p2.y + h * 0.3 + m_textOffY, m_p2.z};
            emitText(batch, txt, tpos, h, 0.0, m_font);
            break;
        }

        case DimKind::Diameter: {
            const double r = dist2D(m_p1, m_p2);
            // Ponto diametralmente oposto: reflexão de m_p2 pelo centro m_p1.
            const Point3 opp{2.0 * m_p1.x - m_p2.x, 2.0 * m_p1.y - m_p2.y, m_p1.z};
            batch.addSegment(opp, m_p2);    // linha passando pelo centro
            emitArrow(batch, m_p2, m_p1, arrow, atype);
            emitArrow(batch, opp, m_p1, arrow, atype);

            // Ø: a StrokeFont lê o byte Latin-1 (0xD8); o provider TTF lê UTF-8.
            const std::string oSym = m_font.empty() ? std::string("\xD8")
                                                    : std::string("\xC3\x98");
            const std::string txt = m_textOverride.empty()
                                  ? tol(oSym + fmt(2.0 * r, dec, suf)) : m_textOverride;
            const Point3 tpos{m_p2.x + h * 0.3 + m_textOffX,
                              m_p2.y + h * 0.3 + m_textOffY, m_p2.z};
            emitText(batch, txt, tpos, h, 0.0, m_font);
            break;
        }

        case DimKind::Ordinate: {
            // p1 = ponto medido; p3 = fim do leader (com cotovelo). Leader
            // predominantemente VERTICAL mede X (X-datum); horizontal mede Y.
            const bool xdatum = std::abs(m_p3.x - m_p1.x) < std::abs(m_p3.y - m_p1.y);
            const double val = xdatum ? m_p1.x : m_p1.y;
            const Point3 elbow = xdatum ? Point3{m_p1.x, m_p3.y, 0.0}
                                        : Point3{m_p3.x, m_p1.y, 0.0};
            batch.addSegment(m_p1, elbow);
            batch.addSegment(elbow, m_p3);
            const std::string txt = m_textOverride.empty()
                                  ? tol(fmt(val, dec, suf)) : m_textOverride;
            const Point3 tpos{m_p3.x + h * 0.3 + m_textOffX,
                              m_p3.y + h * 0.3 + m_textOffY, 0.0};
            emitText(batch, txt, tpos, h, xdatum ? kPi * 0.5 : 0.0, m_font);
            break;
        }

        case DimKind::Angular: {
            // Ângulos dos dois lados a partir do vértice.
            const double a1 = std::atan2(m_p2.y - m_p1.y, m_p2.x - m_p1.x);
            const double a2 = std::atan2(m_p3.y - m_p1.y, m_p3.x - m_p1.x);
            double sweep = a2 - a1;
            // Normaliza para (-pi, pi] para escolher o menor arco.
            while (sweep <= -kPi) sweep += kTwoPi;
            while (sweep   > kPi) sweep -= kTwoPi;

            // Raio do arco = menor das duas distâncias dos lados ao vértice.
            const double r1 = dist2D(m_p1, m_p2);
            const double r2 = dist2D(m_p1, m_p3);
            const double r  = (r1 < r2 ? r1 : r2) * 0.7;

            const int n = 24;
            Point3 prev{m_p1.x + r * std::cos(a1), m_p1.y + r * std::sin(a1), m_p1.z};
            for (int i = 1; i <= n; ++i) {
                const double a = a1 + sweep * (double(i) / n);
                Point3 cur{m_p1.x + r * std::cos(a), m_p1.y + r * std::sin(a), m_p1.z};
                batch.addSegment(prev, cur);
                prev = cur;
            }

            const double deg = std::abs(sweep) * 180.0 / kPi;
            // Angular: aplica só as casas decimais; o símbolo de unidade já é o
            // grau (°), então NÃO anexamos m_suffix aqui (sufixo de unidade
            // linear não faz sentido em ângulo). Mantém o ° como antes.
            // °: byte Latin-1 p/ a StrokeFont; UTF-8 p/ o provider TTF.
            const std::string degSym = m_font.empty() ? std::string("\xB0")
                                                      : std::string("\xC2\xB0");
            const std::string txt = m_textOverride.empty()
                                  ? fmt(deg, dec, std::string{}) + degSym
                                  : m_textOverride;
            const double amid = a1 + sweep * 0.5;
            const double tr   = r + h * 0.5;
            const Point3 tpos{m_p1.x + tr * std::cos(amid) + m_textOffX,
                              m_p1.y + tr * std::sin(amid) + m_textOffY, m_p1.z};
            emitText(batch, txt, tpos, h, 0.0, m_font);
            break;
        }
    }
}

AABB Dimension::boundingBox() const {
    // Reaproveita a emissão: a caixa envolve TODOS os vértices emitidos
    // (linhas, setas e texto) — exato e sem duplicar a lógica de layout.
    RenderBatch tmp;
    emitTo(tmp);
    AABB b;
    for (const Point3& p : tmp.lineVertices) b.expand(p);
    if (!b.valid()) { b.expand(m_p1); b.expand(m_p2); b.expand(m_p3); }
    return b;
}

HitResult Dimension::hitTest(const Ray& pickRay, double tol) const {
    const Point3 p = pickRay.origin;

    // Reconstrói a linha de cota conforme o tipo e testa distância ponto-segmento.
    Point3 a, b;
    switch (m_kind) {
        case DimKind::Linear: {
            const bool horizontal = std::abs(m_p2.x - m_p1.x) >= std::abs(m_p2.y - m_p1.y);
            if (horizontal) { a = {m_p1.x, m_p3.y, m_p1.z}; b = {m_p2.x, m_p3.y, m_p2.z}; }
            else            { a = {m_p3.x, m_p1.y, m_p1.z}; b = {m_p3.x, m_p2.y, m_p2.z}; }
            break;
        }
        case DimKind::Aligned: {
            Vec3 axis = (m_p2 - m_p1);
            const double len = axis.length();
            if (len <= 0.0) { a = m_p1; b = m_p2; break; }
            axis = axis * (1.0 / len);
            const Vec3 perp{-axis.y, axis.x, 0.0};
            const double off = (m_p3 - m_p1).dot(perp);
            a = {m_p1.x + perp.x * off, m_p1.y + perp.y * off, m_p1.z};
            b = {m_p2.x + perp.x * off, m_p2.y + perp.y * off, m_p2.z};
            break;
        }
        case DimKind::Radius:
            a = m_p1; b = m_p2;
            break;
        case DimKind::Diameter:
            a = {2.0 * m_p1.x - m_p2.x, 2.0 * m_p1.y - m_p2.y, m_p1.z};
            b = m_p2;
            break;
        case DimKind::Angular:
            a = m_p2; b = m_p3;  // aproximação: perto de um dos lados
            break;
    }

    const double d = distPointSeg2D(p, a, b);
    HitResult r;
    if (d <= tol) { r.hit = true; r.distance = d; r.point = p; }
    return r;
}

void Dimension::transform(const Matrix4& m) {
    m_p1 = m.transformPoint(m_p1);
    m_p2 = m.transformPoint(m_p2);
    m_p3 = m.transformPoint(m_p3);
    const Vec3 xAxis = m.transformVector(Vec3{1.0, 0.0, 0.0});
    m_textHeight *= std::sqrt(xAxis.x * xAxis.x + xAxis.y * xAxis.y);
}

void Dimension::appendSnapPoints(std::vector<SnapPoint>&) const {
    // Cotas não oferecem pontos de captura por padrão.
}

std::unique_ptr<Entity> Dimension::clone() const {
    return std::make_unique<Dimension>(*this);
}

Point3 Dimension::textBasePoint() const {
    switch (m_kind) {
        case DimKind::Linear: {
            const bool horizontal =
                std::abs(m_p2.x - m_p1.x) >= std::abs(m_p2.y - m_p1.y);
            if (horizontal) return {(m_p1.x + m_p2.x) * 0.5, m_p3.y, 0.0};
            return {m_p3.x, (m_p1.y + m_p2.y) * 0.5, 0.0};
        }
        case DimKind::Aligned: {
            Vec3 axis = m_p2 - m_p1;
            const double len = axis.length();
            if (len <= 0.0) return m_p1;
            axis = axis * (1.0 / len);
            const Vec3 perp{-axis.y, axis.x, 0.0};
            const double off = (m_p3 - m_p1).dot(perp);
            return {(m_p1.x + m_p2.x) * 0.5 + perp.x * off,
                    (m_p1.y + m_p2.y) * 0.5 + perp.y * off, 0.0};
        }
        case DimKind::Radius:
        case DimKind::Diameter:
            return m_p2;
        case DimKind::Angular:
            return {(m_p2.x + m_p3.x) * 0.5, (m_p2.y + m_p3.y) * 0.5, 0.0};
        case DimKind::Ordinate:
            return m_p3;
    }
    return m_p1;
}

} // namespace cad
