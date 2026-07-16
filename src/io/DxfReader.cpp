// src/io/DxfReader.cpp
#include "io/DxfReader.hpp"

#include "core/document/DrawingManager.hpp"
#include "core/document/LayerTable.hpp"
#include "core/document/Layer.hpp"
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
#include "core/math/Vec.hpp"
#include "core/math/Matrix4.hpp"
#include "core/math/Constants.hpp"

#include <fstream>
#include <string>
#include <vector>
#include <cmath>
#include <memory>

namespace cad {

namespace {

// --------------------------------------------------------------------------
// Utilitários de parsing robusto
// --------------------------------------------------------------------------

// Remove espaços/CR/tabs do início e do fim (DXF pode trazer CR em \r\n e
// indentação variável nos valores).
std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

// Converte para double sem lançar; em caso de erro devolve 0.0.
double toDouble(const std::string& s) {
    try {
        return std::stod(s);
    } catch (...) {
        return 0.0;
    }
}

// Converte para int sem lançar; em caso de erro devolve o fallback.
int toInt(const std::string& s, int fallback = -1) {
    try {
        return std::stoi(s);
    } catch (...) {
        return fallback;
    }
}

double degToRad(double deg) { return deg * (kPi / 180.0); }

// --------------------------------------------------------------------------
// Estado acumulado da entidade sendo lida.
//
// Como os pares código/valor de uma mesma entidade aparecem em ordem variável
// (ex.: o código 70 da LWPOLYLINE pode vir antes ou depois dos vértices),
// acumulamos tudo aqui e só construímos a entidade ao encontrar o próximo
// marcador "0/<algo>".
// --------------------------------------------------------------------------
struct EntityData {
    std::string type;          // LINE, CIRCLE, ARC, LWPOLYLINE, TEXT...
    std::string layer;         // código 8
    bool        hasLayer{false};

    // Coordenadas primárias (10/20/30) e secundárias (11/21/31).
    double x0{0.0}, y0{0.0};
    double x1{0.0}, y1{0.0};
    double radius{0.0};        // código 40 (também altura de TEXT)
    double angStart{0.0};      // código 50 (ARC início / TEXT rotação)
    double angEnd{0.0};        // código 51 (ARC fim)
    int    flags70{0};         // código 70 (bit 1 = polilinha fechada)
    std::string text;          // código 1 (conteúdo de TEXT)

    // Vértices de LWPOLYLINE: cada par 10/20 vira um Point3 na ordem de leitura.
    std::vector<Point3> verts;
    std::vector<double> bulges;   // código 42 por vértice (0 = trecho reto)
    bool   pendingX{false};    // já vimos um 10 aguardando o 20 do mesmo vértice
    double pendingVx{0.0};

    // --- ELLIPSE ---------------------------------------------------------
    // 11/21 = vetor do eixo maior (reaproveita x1/y1); 40 = ratio (reaproveita
    // radius); 41/42 = intervalo paramétrico [t0, t1].
    double ellT0{0.0};         // código 41 (ângulo paramétrico inicial)
    double ellT1{kTwoPi};      // código 42 (ângulo paramétrico final)

    // --- DIMENSION -------------------------------------------------------
    // p1 = (x0,y0); p2 = (x1,y1); p3 = (x2,y2); altura = radius (cód 40);
    // kind = flags70 (cód 70, ordem do enum DimKind).
    double x2{0.0}, y2{0.0};   // código 12/22 (terceiro ponto de definição)

    // --- HATCH -----------------------------------------------------------
    // pattern = flags70 (cód 70, ordem do enum HatchPattern); ângulo/escala:
    double hatchAngleDeg{0.0}; // código 52
    double hatchScale{1.0};    // código 41
    std::vector<std::vector<Point3>> hatchLoops;  // loops já fechados (cód 92 + 10/20)

    // --- INSERT ----------------------------------------------------------
    // 2 = nome do bloco; 10/20 = ponto de inserção; 41/42 = escala; 50 = rotação.
    std::string name2;         // código 2 (nome do bloco no INSERT)
    double insSx{1.0}, insSy{1.0};  // códigos 41/42 (escala X/Y)

    void reset() { *this = EntityData{}; }
};

// Garante que a camada exista no documento; cria se necessário.
void ensureLayer(DrawingManager& doc, const std::string& name) {
    if (name.empty()) return;
    if (!doc.layers().contains(name)) {
        Layer layer;
        layer.name = name;
        doc.layers().add(layer);
    }
}

// Constrói a entidade acumulada e a adiciona ao documento.
// Retorna true se uma entidade suportada foi efetivamente adicionada.
EntityPtr buildEntity(EntityData& e, DrawingManager& doc) {
    if (e.type.empty()) return nullptr;

    EntityPtr entity;

    if (e.type == "LINE") {
        entity = std::make_unique<Line>(Point3{e.x0, e.y0}, Point3{e.x1, e.y1});
    } else if (e.type == "CIRCLE") {
        entity = std::make_unique<Circle>(Point3{e.x0, e.y0}, e.radius);
    } else if (e.type == "ARC") {
        entity = std::make_unique<Arc>(Point3{e.x0, e.y0}, e.radius,
                                       degToRad(e.angStart), degToRad(e.angEnd));
    } else if (e.type == "LWPOLYLINE") {
        const bool closed = (e.flags70 & 1) != 0;
        entity = std::make_unique<Polyline>(e.verts, e.bulges, closed);
    } else if (e.type == "TEXT") {
        // No DXF, o código 40 da TEXT é a altura; 50 é a rotação (graus).
        entity = std::make_unique<MText>(Point3{e.x0, e.y0}, e.text,
                                         e.radius, degToRad(e.angStart));
    } else if (e.type == "ELLIPSE") {
        // Espelha o writer: 10/20 = centro, 11/21 = vetor do eixo maior,
        // 40 = ratio (b/a), 41/42 = intervalo paramétrico [t0, t1].
        // O writer guarda RATIO no cód 40 (não o comprimento), então o
        // semi-eixo menor absoluto é ratio * |majorVec|.
        const Vec3 majorVec{e.x1, e.y1};
        const double minorLen = e.radius * majorVec.length();
        // Elipse completa quando a varredura cobre ~2π; senão, arco elíptico.
        if (std::fabs((e.ellT1 - e.ellT0) - kTwoPi) < 1e-9) {
            entity = std::make_unique<Ellipse>(
                Ellipse::fromCenterAxes(Point3{e.x0, e.y0}, majorVec, minorLen));
        } else {
            entity = std::make_unique<Ellipse>(
                Ellipse::fromCenterAxesArc(Point3{e.x0, e.y0}, majorVec, minorLen,
                                           e.ellT0, e.ellT1));
        }
    } else if (e.type == "DIMENSION") {
        // 10/20=p1, 11/21=p2, 12/22=p3, 40=altura, 70=DimKind (ordem do enum).
        const Point3 p1{e.x0, e.y0};
        const Point3 p2{e.x1, e.y1};
        const Point3 p3{e.x2, e.y2};
        const double h = e.radius;
        Dimension dim;
        switch (e.flags70) {
            case 0: dim = Dimension::linear(p1, p2, p3, h);   break;  // Linear
            case 1: dim = Dimension::aligned(p1, p2, p3, h);  break;  // Aligned
            case 2: dim = Dimension::radius(p1, p2, h);       break;  // Radius
            case 3: dim = Dimension::diameter(p1, p2, h);     break;  // Diameter
            case 4: dim = Dimension::angular(p1, p2, p3, h);  break;  // Angular
            default: dim = Dimension::linear(p1, p2, p3, h);  break;  // fallback seguro
        }
        entity = std::make_unique<Dimension>(dim);
    } else if (e.type == "HATCH") {
        // 70=padrão (ordem do enum), 52=ângulo (graus), 41=escala, loops via 92+10/20.
        // O índice do padrão é validado contra o intervalo conhecido; fora da
        // faixa cai em Lines (0) por robustez.
        int pidx = e.flags70;
        if (pidx < 0 || pidx > static_cast<int>(HatchPattern::Gradient)) pidx = 0;
        const auto pattern = static_cast<HatchPattern>(pidx);
        entity = std::make_unique<Hatch>(e.hatchLoops, pattern,
                                         e.hatchAngleDeg, e.hatchScale);
    } else if (e.type == "INSERT") {
        // Inserção de bloco: referencia uma definição da biblioteca pelo nome.
        const BlockDefinition* def = doc.blocks().find(e.name2);
        if (!def) return nullptr;                 // bloco não definido: ignora
        const Matrix4 x = Matrix4::translation(Vec3{e.x0, e.y0, 0.0})
                        * Matrix4::rotationZ(degToRad(e.angStart))
                        * Matrix4::scale(Vec3{e.insSx, e.insSy, 1.0});
        entity = BlockRef::fromDefinition(*def, x);
    } else {
        // Tipo desconhecido: ignorado por robustez.
        return nullptr;
    }

    if (e.hasLayer && !e.layer.empty()) {
        ensureLayer(doc, e.layer);
        entity->setLayer(e.layer);
    }
    return entity;
}

// Constrói a entidade e a adiciona ao documento (seção ENTITIES).
bool flushEntity(EntityData& e, DrawingManager& doc) {
    EntityPtr ent = buildEntity(e, doc);
    if (!ent) return false;
    doc.addEntity(std::move(ent));
    return true;
}

// Acumula um par (código, valor) na entidade em construção. Compartilhado
// entre a seção ENTITIES e os membros de um BLOCK (mesma máquina de acúmulo).
void accumulateCode(EntityData& cur, int code, const std::string& value) {
    switch (code) {
        case 8:  cur.layer = value; cur.hasLayer = true; break;
        case 2:  cur.name2 = value; break;   // nome do bloco (INSERT)
        case 10:
            if (cur.type == "LWPOLYLINE" || cur.type == "HATCH") {
                cur.pendingVx = toDouble(value); cur.pendingX = true;
            } else { cur.x0 = toDouble(value); }
            break;
        case 20:
            if (cur.type == "LWPOLYLINE") {
                const double vy = toDouble(value);
                cur.verts.emplace_back(cur.pendingX ? cur.pendingVx : 0.0, vy);
                cur.bulges.push_back(0.0);
                cur.pendingX = false;
            } else if (cur.type == "HATCH") {
                const double vy = toDouble(value);
                if (cur.hatchLoops.empty()) cur.hatchLoops.emplace_back();
                cur.hatchLoops.back().emplace_back(cur.pendingX ? cur.pendingVx : 0.0, vy);
                cur.pendingX = false;
            } else { cur.y0 = toDouble(value); }
            break;
        case 11: cur.x1 = toDouble(value); break;
        case 21: cur.y1 = toDouble(value); break;
        case 12: cur.x2 = toDouble(value); break;
        case 22: cur.y2 = toDouble(value); break;
        case 40: cur.radius = toDouble(value); break;
        case 50: cur.angStart = toDouble(value); break;
        case 51: cur.angEnd = toDouble(value); break;
        case 41:
            if (cur.type == "ELLIPSE")     cur.ellT0 = toDouble(value);
            else if (cur.type == "HATCH")  cur.hatchScale = toDouble(value);
            else if (cur.type == "INSERT") cur.insSx = toDouble(value);
            break;
        case 42:
            if (cur.type == "ELLIPSE")     cur.ellT1 = toDouble(value);
            else if (cur.type == "INSERT") cur.insSy = toDouble(value);
            else if (cur.type == "LWPOLYLINE" && !cur.bulges.empty()) cur.bulges.back() = toDouble(value);
            break;
        case 52: if (cur.type == "HATCH") cur.hatchAngleDeg = toDouble(value); break;
        case 91: break;   // nº de loops (informativo)
        case 92: if (cur.type == "HATCH") cur.hatchLoops.emplace_back(); break;
        case 70: cur.flags70 = toInt(value, 0); break;
        case 1:  cur.text = value; break;
        default: break;
    }
}

} // namespace

// ----------------------------------------------------------------------------
// readDxf — implementação
// ----------------------------------------------------------------------------
int readDxf(const std::string& path, DrawingManager& doc) {
    std::ifstream in(path);
    if (!in) return -1;

    int added = 0;

    // Máquina de estados por seção. A seção BLOCKS (definições de bloco) vem
    // ANTES de ENTITIES no arquivo, então as definições já existem quando os
    // INSERTs da seção ENTITIES as referenciam pelo nome.
    bool inEntities = false, inBlocks = false;
    bool building   = false;   // entidade em construção (ENTITIES)
    EntityData cur;

    // Estado da seção BLOCKS.
    bool inBlockDef = false, readingHeader = false, buildingMember = false;
    EntityData memberData;
    BlockDefinition curBlock;

    std::string codeLine;
    std::string valueLine;

    // Lê pares (código, valor): uma linha é o código inteiro, a próxima o valor.
    while (std::getline(in, codeLine)) {
        if (!std::getline(in, valueLine)) break;  // par incompleto no fim

        const int code = toInt(trim(codeLine));
        const std::string value = trim(valueLine);
        if (code < 0) continue;  // linha de código inválida → ignora o par

        // ---- Transições de seção (código 2 nomeia a seção) -----------------
        if (code == 2 && value == "BLOCKS")   { inBlocks = true;  inEntities = false; continue; }
        if (code == 2 && value == "ENTITIES") { inEntities = true; inBlocks = false; continue; }

        // ---- Seção BLOCKS: acumula definições de bloco ---------------------
        if (inBlocks) {
            if (code == 0) {
                if (buildingMember) {   // fecha o membro anterior do bloco
                    if (EntityPtr m = buildEntity(memberData, doc))
                        curBlock.members.push_back(std::move(m));
                    buildingMember = false;
                }
                if (value == "ENDSEC") { inBlocks = false; inBlockDef = false; continue; }
                if (value == "BLOCK")  {
                    curBlock = BlockDefinition{}; inBlockDef = true; readingHeader = true; continue;
                }
                if (value == "ENDBLK") {
                    if (inBlockDef && !curBlock.name.empty()) doc.blocks().add(std::move(curBlock));
                    inBlockDef = false; readingHeader = false; continue;
                }
                memberData.reset(); memberData.type = value;   // início de um membro
                buildingMember = true; readingHeader = false;
                continue;
            }
            if (!inBlockDef) continue;
            if (readingHeader) {                 // cabeçalho do BLOCK (nome/base)
                if      (code == 2)  curBlock.name = value;
                else if (code == 10) curBlock.base.x = toDouble(value);
                else if (code == 20) curBlock.base.y = toDouble(value);
                else if (code == 30) curBlock.base.z = toDouble(value);
                continue;
            }
            if (buildingMember) accumulateCode(memberData, code, value);
            continue;
        }

        if (!inEntities) continue;   // fora de seções conhecidas (HEADER/TABLES/…)

        // ---- Dentro da seção ENTITIES --------------------------------------

        if (code == 0) {
            // Marcador de nova entidade ou fim de seção: finaliza a anterior.
            if (building) {
                if (flushEntity(cur, doc)) ++added;
                building = false;
            }

            if (value == "ENDSEC") {
                inEntities = false;   // fim da seção ENTITIES
                continue;
            }

            // Inicia uma nova entidade (mesmo que de tipo desconhecido — será
            // descartada no flush, mas ainda delimita o bloco corretamente).
            cur.reset();
            cur.type = value;
            building = true;
            continue;
        }

        if (!building) continue;  // dados fora de qualquer entidade → ignora

        // Acumula o par código/valor na entidade em construção.
        accumulateCode(cur, code, value);
    }

    // Arquivo pode terminar sem "0/ENDSEC": finaliza a entidade pendente.
    if (building) {
        if (flushEntity(cur, doc)) ++added;
    }

    return added;
}

} // namespace cad
