// src/io/DxfWriter.cpp
//
// Implementação do exportador DXF ASCII mínimo (ver DxfWriter.hpp).
//
// Estratégia: um EntityVisitor interno (DxfEntityVisitor) percorre cada
// entidade do documento via double-dispatch e emite os pares de código de
// grupo DXF (código numa linha, valor na linha seguinte) num std::ofstream.
//
#include "io/DxfWriter.hpp"

#include "core/document/DrawingManager.hpp"
#include "core/document/LayerTable.hpp"
#include "core/document/Layer.hpp"
#include "core/geometry/Entity.hpp"
#include "core/geometry/Line.hpp"
#include "core/geometry/Circle.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Polyline.hpp"
#include "core/geometry/MText.hpp"
#include "core/geometry/Ellipse.hpp"
#include "core/geometry/Dimension.hpp"
#include "core/geometry/Hatch.hpp"
#include "core/geometry/BlockRef.hpp"
#include "core/document/BlockTable.hpp"
#include "core/math/AABB.hpp"
#include "core/math/Constants.hpp"

#include <fstream>
#include <string>
#include <cmath>

namespace cad {
namespace {

// Converte radianos -> graus (o DXF expressa ângulos de ARC/TEXT em graus).
inline double toDegrees(double rad) { return rad * (180.0 / kPi); }

// ----------------------------------------------------------------------------
//  Mapeamento RGB -> índice de cor do AutoCAD (ACI), aproximado.
//  Cobre as 7 cores básicas e o branco/preto; qualquer outra cai em 7 (padrão).
//  Implementação propositalmente simples — fidelidade total de cor exigiria
//  cores verdadeiras (códigos 420/430), fora do escopo deste DXF mínimo.
// ----------------------------------------------------------------------------
int rgbToAci(const Rgba& c) {
    struct Entry { std::uint8_t r, g, b; int aci; };
    static const Entry kTable[] = {
        {255,   0,   0, 1},  // vermelho
        {255, 255,   0, 2},  // amarelo
        {  0, 255,   0, 3},  // verde
        {  0, 255, 255, 4},  // ciano
        {  0,   0, 255, 5},  // azul
        {255,   0, 255, 6},  // magenta
        {255, 255, 255, 7},  // branco
        {  0,   0,   0, 7},  // preto -> 7 (cor de "papel": branco/preto)
    };

    int best = 7;                 // padrão: branco/preto
    long bestDist = -1;
    for (const auto& e : kTable) {
        const long dr = static_cast<long>(c.r) - e.r;
        const long dg = static_cast<long>(c.g) - e.g;
        const long db = static_cast<long>(c.b) - e.b;
        const long dist = dr * dr + dg * dg + db * db;
        if (bestDist < 0 || dist < bestDist) {
            bestDist = dist;
            best = e.aci;
        }
    }
    return best;
}

// ----------------------------------------------------------------------------
//  Emissor de pares de grupo DXF.
//  Cada par ocupa DUAS linhas: o código de grupo e, em seguida, o valor.
//  Os reais são formatados com precisão alta (15 dígitos significativos), o
//  bastante para preservar coordenadas em double.
// ----------------------------------------------------------------------------
class DxfStream {
public:
    explicit DxfStream(std::ofstream& os) : m_os(os) {
        m_os.setf(std::ios::fixed);
        m_os.precision(6);
    }

    // Par com valor inteiro (ex.: 0/SECTION usa string; aqui para 62, 70, 90...).
    void pair(int code, int value) {
        m_os << code << '\n' << value << '\n';
    }

    // Par com valor string (ex.: 0/LINE, 2/nome, 8/camada, 1/texto).
    void pair(int code, const std::string& value) {
        m_os << code << '\n' << value << '\n';
    }
    void pair(int code, const char* value) {
        m_os << code << '\n' << value << '\n';
    }

    // Par com valor real (coordenadas, raios, ângulos...).
    void pairReal(int code, double value) {
        m_os << code << '\n' << value << '\n';
    }

private:
    std::ofstream& m_os;
};

// ----------------------------------------------------------------------------
//  Visitor que serializa cada entidade concreta para a seção ENTITIES.
//  O código 8 (nome da camada) é emitido para TODAS as entidades.
// ----------------------------------------------------------------------------
class DxfEntityVisitor final : public EntityVisitor {
public:
    explicit DxfEntityVisitor(DxfStream& dxf) : m_dxf(dxf) {}

    void visit(const Line& e) override {
        m_dxf.pair(0, "LINE");
        emitLayer(e);
        m_dxf.pairReal(10, e.start().x);
        m_dxf.pairReal(20, e.start().y);
        m_dxf.pairReal(11, e.end().x);
        m_dxf.pairReal(21, e.end().y);
    }

    void visit(const Circle& e) override {
        m_dxf.pair(0, "CIRCLE");
        emitLayer(e);
        m_dxf.pairReal(10, e.center().x);
        m_dxf.pairReal(20, e.center().y);
        m_dxf.pairReal(40, e.radius());
    }

    void visit(const Arc& e) override {
        m_dxf.pair(0, "ARC");
        emitLayer(e);
        m_dxf.pairReal(10, e.center().x);
        m_dxf.pairReal(20, e.center().y);
        m_dxf.pairReal(40, e.radius());
        // Ângulos do ARC em GRAUS no DXF.
        m_dxf.pairReal(50, toDegrees(e.startAngle()));
        m_dxf.pairReal(51, toDegrees(e.endAngle()));
    }

    void visit(const Polyline& e) override {
        const auto& verts = e.vertices();
        m_dxf.pair(0, "LWPOLYLINE");
        emitLayer(e);
        m_dxf.pair(90, static_cast<int>(verts.size()));  // nº de vértices
        m_dxf.pair(70, e.closed() ? 1 : 0);              // bit 1 = fechada
        for (std::size_t i = 0; i < verts.size(); ++i) {
            m_dxf.pairReal(10, verts[i].x);
            m_dxf.pairReal(20, verts[i].y);
            const double bu = e.bulgeAt(i);
            if (std::fabs(bu) > 1e-12) m_dxf.pairReal(42, bu);  // arco no trecho
        }
    }

    void visit(const MText& e) override {
        // Exportada como TEXT (entidade de texto de linha única do DXF).
        m_dxf.pair(0, "TEXT");
        emitLayer(e);
        m_dxf.pairReal(10, e.position().x);
        m_dxf.pairReal(20, e.position().y);
        m_dxf.pairReal(40, e.height());
        m_dxf.pair(1, e.text());
        m_dxf.pairReal(50, toDegrees(e.rotation()));  // rotação em GRAUS
    }

    void visit(const Ellipse& e) override {
        m_dxf.pair(0, "ELLIPSE");
        emitLayer(e);
        m_dxf.pairReal(10, e.center().x);
        m_dxf.pairReal(20, e.center().y);
        // 11/21: vetor do eixo maior, relativo ao centro (major()).
        m_dxf.pairReal(11, e.major().x);
        m_dxf.pairReal(21, e.major().y);
        m_dxf.pairReal(40, e.ratio());  // razão eixo menor/maior
        m_dxf.pairReal(41, 0.0);        // ângulo inicial do paramétrico
        m_dxf.pairReal(42, kTwoPi);     // ângulo final (elipse completa)
    }

    // ------------------------------------------------------------------------
    //  DIMENSION — round-trip CadCore.
    //
    //  Mapeamento de códigos (lido de volta IDÊNTICO pelo DxfReader):
    //    0  = DIMENSION
    //    8  = camada
    //    10/20 = p1   (definição 1)
    //    11/21 = p2   (definição 2)
    //    12/22 = p3   (definição 3 — posição da linha de cota / 2º lado angular)
    //    40    = altura do texto (textHeight)
    //    70    = DimKind como inteiro, na ORDEM do enum:
    //            0=Linear, 1=Aligned, 2=Radius, 3=Diameter, 4=Angular
    //
    //  Obs.: não usamos os códigos "ricos" da DIMENSION real do AutoCAD
    //  (10/20=defpoint, 13/14=extension lines etc.) porque o objetivo é
    //  fidelidade ao reabrir no próprio CadCore — reconstruímos a MESMA cota
    //  chamando o estático correspondente de Dimension. Diâmetro/raio/angular
    //  ignoram p3 / partes não usadas, coerente com os estáticos.
    // ------------------------------------------------------------------------
    void visit(const Dimension& e) override {
        m_dxf.pair(0, "DIMENSION");
        emitLayer(e);
        m_dxf.pairReal(10, e.p1().x);
        m_dxf.pairReal(20, e.p1().y);
        m_dxf.pairReal(11, e.p2().x);
        m_dxf.pairReal(21, e.p2().y);
        m_dxf.pairReal(12, e.p3().x);
        m_dxf.pairReal(22, e.p3().y);
        m_dxf.pairReal(40, e.textHeight());
        m_dxf.pair(70, static_cast<int>(e.kind()));  // 0..4 = ordem do enum DimKind
    }

    // ------------------------------------------------------------------------
    //  HATCH — round-trip CadCore.
    //
    //  Mapeamento de códigos (lido de volta IDÊNTICO pelo DxfReader):
    //    0  = HATCH
    //    8  = camada
    //    70 = índice do padrão (ordem do enum HatchPattern):
    //         0=Lines, 1=ANSI31, 2=ANSI37, 3=Grid, 4=Solid
    //    52 = ângulo do padrão em GRAUS (angleDeg)
    //    41 = escala (scale)
    //    91 = nº de loops (boundary paths)
    //    para CADA loop:
    //      92 = nº de vértices do loop
    //      10/20 (repetidos) = vértices do loop, na ordem
    //
    //  Codificação simplificada: cada loop é uma sequência de vértices (polígono
    //  fechado implícito), sem dados de bulge/arco — o Hatch do CadCore guarda os
    //  loops como vetores de Point3. Reconstruído via Hatch(loops, pattern,
    //  angleDeg, scale).
    // ------------------------------------------------------------------------
    void visit(const Hatch& e) override {
        const auto& loops = e.loops();
        m_dxf.pair(0, "HATCH");
        emitLayer(e);
        m_dxf.pair(70, static_cast<int>(e.pattern()));  // índice do padrão
        m_dxf.pairReal(52, e.angleDeg());
        m_dxf.pairReal(41, e.scale());
        m_dxf.pair(91, static_cast<int>(loops.size()));  // nº de loops
        for (const auto& loop : loops) {
            m_dxf.pair(92, static_cast<int>(loop.size()));  // vértices deste loop
            for (const auto& v : loop) {
                m_dxf.pairReal(10, v.x);
                m_dxf.pairReal(20, v.y);
            }
        }
    }

    // ------------------------------------------------------------------------
    //  INSERT — inserção de bloco nomeado.
    //    0  = INSERT
    //    2  = nome do bloco (referencia uma BLOCK da seção BLOCKS)
    //    10/20/30 = ponto de inserção
    //    41/42/43 = escala X/Y/Z
    //    50 = rotação (graus)
    //  A escala/rotação/translação são extraídas da matriz de inserção
    //  (m_xform = T·Rz·S). Blocos ANÔNIMOS (sem nome) são "achatados" na sua
    //  geometria, pois não há BLOCK correspondente para referenciar.
    // ------------------------------------------------------------------------
    void visit(const BlockRef& e) override {
        if (e.blockName().empty()) {
            for (const auto& part : e.explodedClones()) part->accept(*this);
            return;
        }
        const auto& m = e.xform().m;   // column-major
        const double sx = std::sqrt(m[0] * m[0] + m[1] * m[1]);
        const double sy = std::sqrt(m[4] * m[4] + m[5] * m[5]);
        const double rot = std::atan2(m[1], m[0]);
        m_dxf.pair(0, "INSERT");
        emitLayer(e);
        m_dxf.pair(2, e.blockName());
        m_dxf.pairReal(10, m[12]);
        m_dxf.pairReal(20, m[13]);
        m_dxf.pairReal(30, 0.0);
        m_dxf.pairReal(41, sx);
        m_dxf.pairReal(42, sy);
        m_dxf.pairReal(43, 1.0);
        m_dxf.pairReal(50, toDegrees(rot));
    }

private:
    // Emite o código 8 (camada) comum a todas as entidades.
    void emitLayer(const Entity& e) {
        m_dxf.pair(8, e.layer());
    }

    DxfStream& m_dxf;
};

// ----------------------------------------------------------------------------
//  Seção TABLES com uma tabela LAYER: uma entrada por camada do documento.
//  Estrutura mínima exigida pelo formato (TABLE/LAYER + entradas + ENDTAB).
// ----------------------------------------------------------------------------
void writeLayerTable(DxfStream& dxf, const DrawingManager& doc) {
    const std::vector<Layer> layers = doc.layers().all();

    dxf.pair(0, "SECTION");
    dxf.pair(2, "TABLES");

    dxf.pair(0, "TABLE");
    dxf.pair(2, "LAYER");
    dxf.pair(70, static_cast<int>(layers.size()));  // nº de entradas

    for (const auto& layer : layers) {
        dxf.pair(0, "LAYER");
        dxf.pair(2, layer.name);          // nome da camada
        dxf.pair(70, 0);                  // flags (0 = sem restrições)
        dxf.pair(62, rgbToAci(layer.color));  // cor ACI aproximada
        dxf.pair(6, "CONTINUOUS");        // tipo de linha
    }

    dxf.pair(0, "ENDTAB");
    dxf.pair(0, "ENDSEC");
}

// ----------------------------------------------------------------------------
//  Seção HEADER mínima: versão, base de inserção e extensões do desenho.
//  O AutoCAD usa $EXTMIN/$EXTMAX para o Zoom Extents inicial ao abrir.
// ----------------------------------------------------------------------------
void writeHeader(DxfStream& dxf, const DrawingManager& doc) {
    AABB bb;
    doc.forEach([&](const Entity& e) {
        const AABB eb = e.boundingBox();
        if (eb.valid()) { bb.expand(eb.min); bb.expand(eb.max); }
    });
    const bool ok = bb.valid();
    dxf.pair(0, "SECTION");
    dxf.pair(2, "HEADER");
    dxf.pair(9, "$ACADVER");  dxf.pair(1, "AC1015");        // AutoCAD 2000
    dxf.pair(9, "$INSBASE");  dxf.pairReal(10, 0.0); dxf.pairReal(20, 0.0); dxf.pairReal(30, 0.0);
    dxf.pair(9, "$EXTMIN");
    dxf.pairReal(10, ok ? bb.min.x : 0.0); dxf.pairReal(20, ok ? bb.min.y : 0.0); dxf.pairReal(30, 0.0);
    dxf.pair(9, "$EXTMAX");
    dxf.pairReal(10, ok ? bb.max.x : 0.0); dxf.pairReal(20, ok ? bb.max.y : 0.0); dxf.pairReal(30, 0.0);
    dxf.pair(0, "ENDSEC");
}

// ----------------------------------------------------------------------------
//  Seção BLOCKS: uma BLOCK...ENDBLK por definição da biblioteca. A geometria
//  dos membros é emitida em coords LOCAIS (base do bloco na origem); cada
//  INSERT na seção ENTITIES referencia a BLOCK pelo nome (código 2).
// ----------------------------------------------------------------------------
void writeBlocks(DxfStream& dxf, const DrawingManager& doc) {
    dxf.pair(0, "SECTION");
    dxf.pair(2, "BLOCKS");

    DxfEntityVisitor visitor(dxf);
    for (const std::string& name : doc.blocks().names()) {
        const BlockDefinition* def = doc.blocks().find(name);
        if (!def) continue;
        dxf.pair(0, "BLOCK");
        dxf.pair(8, "0");
        dxf.pair(2, def->name);
        dxf.pair(70, 0);
        dxf.pairReal(10, 0.0); dxf.pairReal(20, 0.0); dxf.pairReal(30, 0.0);  // base = origem local
        for (const auto& m : def->members) m->accept(visitor);
        dxf.pair(0, "ENDBLK");
        dxf.pair(8, "0");
    }

    dxf.pair(0, "ENDSEC");
}

// ----------------------------------------------------------------------------
//  Seção ENTITIES: percorre o documento e serializa cada entidade.
// ----------------------------------------------------------------------------
void writeEntities(DxfStream& dxf, const DrawingManager& doc) {
    dxf.pair(0, "SECTION");
    dxf.pair(2, "ENTITIES");

    DxfEntityVisitor visitor(dxf);
    doc.forEach([&](const Entity& e) { e.accept(visitor); });

    dxf.pair(0, "ENDSEC");
}

} // namespace

// ============================================================================
//  Ponto de entrada público.
// ============================================================================
bool writeDxf(const DrawingManager& doc, const std::string& path) {
    std::ofstream os(path, std::ios::out | std::ios::trunc);
    if (!os) {
        return false;  // não foi possível abrir o arquivo para escrita
    }

    DxfStream dxf(os);

    writeHeader(dxf, doc);
    writeLayerTable(dxf, doc);
    writeBlocks(dxf, doc);
    writeEntities(dxf, doc);

    // Encerramento obrigatório do arquivo DXF.
    dxf.pair(0, "EOF");

    return static_cast<bool>(os);
}

} // namespace cad
