// src/zendo/Viewport3D.cpp
#include "Viewport3D.hpp"
#include "ObjImport.hpp"    // R15

#include <QColor>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>


using cad::HalfEdgeMesh;
using cad::Point3;
using cad::Vec3;

namespace {

// ---- paleta Sumi & Washi, agora com céu -----------------------------------
const QVector3D kBgTop{0.129f, 0.114f, 0.094f};     // sumi quente (alto)
const QVector3D kBgBottom{0.051f, 0.043f, 0.035f};  // quase breu (horizonte)
const QVector3D kWallColor{0.918f, 0.894f, 0.839f}; // washi
const QVector4D kEdgeColor{0.078f, 0.063f, 0.047f, 1.0f};   // tinta nanquim
const QVector4D kGroundInk{0.659f, 0.624f, 0.549f, 0.85f};  // traço do chão
const QVector4D kGridInk{0.659f, 0.624f, 0.549f, 0.13f};    // grade discreta
// R5: paleta do AMANHECER — o ateliê claro (washi) para criar do zero
const QVector3D kDayTop{0.945f, 0.914f, 0.855f};
const QVector3D kDayBottom{0.769f, 0.722f, 0.635f};   // chão mais fundo:
// as faces washi criadas no plano têm CONTRASTE (senão a forma "some")
const QVector4D kDayGrid{0.30f, 0.27f, 0.22f, 0.16f};
const QVector4D kDayGround{0.33f, 0.29f, 0.24f, 0.9f};
const QVector4D kDaySketch{0.24f, 0.20f, 0.15f, 1.0f};
const QVector4D kNightSketch{0.87f, 0.83f, 0.75f, 1.0f};
const QVector4D kSelFill{0.761f, 0.627f, 0.388f, 0.62f};    // latão (seleção)
const QVector4D kHovFill{0.761f, 0.627f, 0.388f, 0.25f};    // latão (hover)
const QVector4D kGhostInk{0.761f, 0.627f, 0.388f, 1.0f};    // latão (fantasma)
const QVector4D kSnapInk{0.471f, 0.608f, 0.553f, 1.0f};     // sálvia (snap)
const QVector3D kSunDir = QVector3D(0.42f, 0.30f, 0.86f).normalized();

// alturas padrão dos vãos (m) — v2: por-vão, vindas do 2D
constexpr double kDoorHead = 2.10;    // verga de porta/passagem
constexpr double kSillZ    = 0.90;    // peitoril de janela
constexpr double kWinHead  = 2.40;    // verga de janela

constexpr double kMinRect = 0.02;     // lado mínimo do retângulo (m)
constexpr int    kMaxUndo = 32;

const char* kMeshVs = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec3 aCol;
uniform mat4 uMvp;
uniform mat4 uLightMvp;
out vec3 vN;
out vec3 vC;
out vec4 vSh;
out vec3 vP;
void main(){
    vN = aNrm; vC = aCol; vP = aPos;
    vSh = uLightMvp * vec4(aPos,1.0);
    gl_Position = uMvp * vec4(aPos,1.0);
})";

const char* kMeshFs = R"(#version 330 core
in vec3 vN;
in vec3 vC;
in vec4 vSh;
in vec3 vP;
uniform vec3 uSun;
uniform int uShadows;
uniform sampler2D uShadowMap;
uniform int uStyle;          // 0 normal · 1 monocromático · 2 raio-x
uniform int uClipCount;      // R32: até 4 seções ativas juntas
uniform vec4 uClips[4];      // nx,ny,nz,d: descarta dot(P,n) > d
uniform int uFogOn;
uniform float uFogDens;
uniform vec3 uFogCol;
uniform vec3 uEye;
out vec4 frag;
void main(){
    for (int i = 0; i < uClipCount; ++i)
        if (dot(vP, uClips[i].xyz) > uClips[i].w) discard;
    // R31: TAMPA da seção — com corte ligado, face traseira exposta pinta
    // em sumi chapado (material "cortado"), sem gerar geometria nenhuma.
    if (uClipCount > 0 && !gl_FrontFacing) {
        frag = vec4(0.164, 0.152, 0.140, 1.0);
        return;
    }
    vec3 N = normalize(vN);
    if (!gl_FrontFacing) N = -N;
    float d = max(dot(N, uSun), 0.0);
    float lit = 1.0;
    if (uShadows == 1 && d > 0.0) {
        vec3 s = vSh.xyz / vSh.w * 0.5 + 0.5;   // NDC -> [0,1]
        if (s.x > 0.0 && s.x < 1.0 && s.y > 0.0 && s.y < 1.0 && s.z < 1.0) {
            float bias = max(0.0026 * (1.0 - d), 0.0009);
            float acc = 0.0;                     // PCF 3x3: penumbra suave
            vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
            for (int i = -1; i <= 1; ++i)
                for (int j = -1; j <= 1; ++j) {
                    float sm = texture(uShadowMap,
                                       s.xy + vec2(i, j) * texel).r;
                    acc += (s.z - bias) > sm ? 0.0 : 1.0;
                }
            lit = acc / 9.0;
        }
    }
    float shade = 0.42 + 0.58 * d * mix(0.25, 1.0, lit);
    vec3 base = uStyle == 1 ? vec3(0.918, 0.894, 0.839) : vC;   // mono = washi
    vec3 c = base * shade;
    c *= vec3(1.0, 0.985, 0.955);               // luz levemente quente
    if (uFogOn == 1) {                          // G6: neblina atmosférica
        float fd = clamp(exp(-uFogDens * distance(vP, uEye)), 0.0, 1.0);
        c = mix(uFogCol, c, fd);
    }
    frag = vec4(c, uStyle == 2 ? 0.42 : 1.0);   // raio-x = translúcido
})";

// faces TEXTURIZADAS: pos + normal + uv, mesma luz/sombra/estilo/clip
const char* kTexVs = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUv;
uniform mat4 uMvp;
uniform mat4 uLightMvp;
out vec3 vN;
out vec2 vUv;
out vec4 vSh;
out vec3 vP;
void main(){
    vN = aNrm; vUv = aUv; vP = aPos;
    vSh = uLightMvp * vec4(aPos,1.0);
    gl_Position = uMvp * vec4(aPos,1.0);
})";

const char* kTexFs = R"(#version 330 core
in vec3 vN;
in vec2 vUv;
in vec4 vSh;
in vec3 vP;
uniform vec3 uSun;
uniform int uShadows;
uniform sampler2D uShadowMap;
uniform sampler2D uTex;
uniform int uStyle;
uniform int uClipCount;
uniform vec4 uClips[4];
uniform int uFogOn;
uniform float uFogDens;
uniform vec3 uFogCol;
uniform vec3 uEye;
out vec4 frag;
void main(){
    for (int i = 0; i < uClipCount; ++i)
        if (dot(vP, uClips[i].xyz) > uClips[i].w) discard;
    // R31: TAMPA da seção — com corte ligado, face traseira exposta pinta
    // em sumi chapado (material "cortado"), sem gerar geometria nenhuma.
    if (uClipCount > 0 && !gl_FrontFacing) {
        frag = vec4(0.164, 0.152, 0.140, 1.0);
        return;
    }
    vec3 N = normalize(vN);
    if (!gl_FrontFacing) N = -N;
    float d = max(dot(N, uSun), 0.0);
    float lit = 1.0;
    if (uShadows == 1 && d > 0.0) {
        vec3 s = vSh.xyz / vSh.w * 0.5 + 0.5;
        if (s.x > 0.0 && s.x < 1.0 && s.y > 0.0 && s.y < 1.0 && s.z < 1.0) {
            float bias = max(0.0026 * (1.0 - d), 0.0009);
            float acc = 0.0;
            vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
            for (int i = -1; i <= 1; ++i)
                for (int j = -1; j <= 1; ++j)
                    acc += (s.z - bias) >
                                   texture(uShadowMap,
                                           s.xy + vec2(i, j) * texel).r
                               ? 0.0
                               : 1.0;
            lit = acc / 9.0;
        }
    }
    float shade = 0.42 + 0.58 * d * mix(0.25, 1.0, lit);
    vec3 base = uStyle == 1 ? vec3(0.918, 0.894, 0.839)
                            : texture(uTex, vUv).rgb;
    vec3 c = base * shade * vec3(1.0, 0.985, 0.955);
    if (uFogOn == 1) {
        float fd = clamp(exp(-uFogDens * distance(vP, uEye)), 0.0, 1.0);
        c = mix(uFogCol, c, fd);
    }
    frag = vec4(c, uStyle == 2 ? 0.42 : 1.0);
})";

// R5: GRADE PROCEDURAL INFINITA — quad gigante no chão; linhas por fract()
// com anti-alias (fwidth), passo 1 m + 10 m, ESMAECENDO com a distância do
// olho: a "borda do tapete" morreu.
const char* kGridQVs = R"(#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 uMvp;
uniform vec3 uCenter;
uniform float uSize;
out vec3 vP;
void main(){
    vP = vec3(uCenter.xy + aPos * uSize, 0.0);
    gl_Position = uMvp * vec4(vP, 1.0);
})";

const char* kGridQFs = R"(#version 330 core
in vec3 vP;
uniform vec4 uColor;
uniform vec3 uEye;
uniform float uFade;
uniform int uClipCount;
uniform vec4 uClips[4];
out vec4 frag;
float gridLine(vec2 p, float step){
    vec2 g = abs(fract(p / step - 0.5) - 0.5) / fwidth(p / step);
    return 1.0 - min(min(g.x, g.y), 1.0);
}
void main(){
    for (int i = 0; i < uClipCount; ++i)
        if (dot(vP, uClips[i].xyz) > uClips[i].w) discard;
    float i = max(gridLine(vP.xy, 1.0) * 0.55, gridLine(vP.xy, 10.0));
    float a = uColor.a * i * exp(-distance(vP, uEye) / uFade);
    if (a < 0.004) discard;
    frag = vec4(uColor.rgb, a);
})";

const char* kDepthVs = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMvp;
void main(){ gl_Position = uMvp * vec4(aPos,1.0); })";

const char* kDepthFs = R"(#version 330 core
void main(){})";

const char* kLineVs = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMvp;
out vec3 vP;
void main(){ vP = aPos; gl_Position = uMvp * vec4(aPos,1.0); })";

const char* kLineFs = R"(#version 330 core
in vec3 vP;
uniform vec4 uColor;
uniform int uClipCount;
uniform vec4 uClips[4];
uniform int uFogOn;
uniform float uFogDens;
uniform vec3 uFogCol;
uniform vec3 uEye;
out vec4 frag;
void main(){
    for (int i = 0; i < uClipCount; ++i)
        if (dot(vP, uClips[i].xyz) > uClips[i].w) discard;
    frag = uColor;
})";

const char* kBgVs = R"(#version 330 core
layout(location=0) in vec2 aPos;
out float vT;
void main(){ vT = aPos.y * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); })";

const char* kBgFs = R"(#version 330 core
in float vT;
uniform vec3 uTop;
uniform vec3 uBottom;
out vec4 frag;
void main(){ frag = vec4(mix(uBottom, uTop, smoothstep(0.0, 1.0, vT)), 1.0); })";

// Shader quebrado desenhava tudo preto em silêncio: o zendo.exe é app GUI
// (subsystem windows), então o qWarning do Qt vai pro OutputDebugString e o
// QA headless não vê nada. Falha de compile/link grita direto no stderr e
// derruba o processo — o --shot morre com exit code 2 em vez de mentir no PNG.
void buildProgram(QOpenGLShaderProgram& p, const char* vs, const char* fs) {
    if (!p.addShaderFromSourceCode(QOpenGLShader::Vertex, vs) ||
        !p.addShaderFromSourceCode(QOpenGLShader::Fragment, fs) ||
        !p.link()) {
        std::fprintf(stderr, "GLSL FATAL: programa nao compilou/linkou:\n%s\n",
                     qPrintable(p.log()));
        std::fflush(stderr);
        std::exit(2);
    }
}

// VBO simples (pos3) com VAO — serve linhas E fills sem sombreamento.
void uploadPos3(QOpenGLVertexArrayObject& vao, QOpenGLBuffer& vbo,
                const std::vector<float>& data, QOpenGLFunctions* f) {
    if (!vao.isCreated()) vao.create();
    vao.bind();
    if (!vbo.isCreated()) vbo.create();
    vbo.bind();
    vbo.allocate(data.data(), int(data.size() * sizeof(float)));
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    vao.release();
}

// Triângulos (ear-clipping do kernel) de UMA face — fills de hover/seleção.
void faceTris(const HalfEdgeMesh& m, HalfEdgeMesh::Idx f,
              std::vector<float>& out) {
    std::vector<Point3> tris;
    m.triangulateFace(f, tris);
    for (const Point3& p : tris)
        out.insert(out.end(), {float(p.x), float(p.y), float(p.z)});
}

void pushSeg(std::vector<float>& v, const Point3& a, const Point3& b) {
    v.insert(v.end(), {float(a.x), float(a.y), float(a.z),
                       float(b.x), float(b.y), float(b.z)});
}

// parâmetro t da reta C+n·t mais próxima do raio o+d·s (n e d unitários).
// R5.2: o SINAL estava trocado desde a F1 (w = o−C sem compensar) — o
// arrasto do Puxar e a trava de eixo do Mover andavam ao CONTRÁRIO do mouse.
// Derivação: t = (n·u − (n·d)(d·u)) / (1−(n·d)²), com u = C−o = −w.
double lineParamClosestToRay(const Point3& C, const Vec3& n, const Point3& o,
                             const Vec3& d) {
    const Vec3 w = o - C;
    const double b = n.dot(d), d1 = n.dot(w), e = d.dot(w);
    const double den = 1.0 - b * b;
    if (std::abs(den) < 1e-9) return 0.0;
    return (d1 - b * e) / den;
}

} // namespace

Viewport3D::Viewport3D(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);     // hover de face / fantasma sem botão apertado
}

Viewport3D::~Viewport3D() {
    makeCurrent();
    m_vboMesh.destroy(); m_vboEdge.destroy(); m_vboGround.destroy();
    m_vboGrid.destroy(); m_vboBg.destroy();
    m_vboSel.destroy(); m_vboHov.destroy();
    m_vboGhost.destroy(); m_vboSnap.destroy();
    doneCurrent();
}

// R5: paleta do ambiente — Amanhecer (ateliê claro) ⇄ Noite (palco sumi)
QVector3D Viewport3D::bgTop() const { return m_dayOn ? kDayTop : kBgTop; }
QVector3D Viewport3D::bgBottom() const {
    return m_dayOn ? kDayBottom : kBgBottom;
}
QVector4D Viewport3D::gridInk() const { return m_dayOn ? kDayGrid : kGridInk; }
QVector4D Viewport3D::groundInkC() const {
    return m_dayOn ? kDayGround : kGroundInk;
}
QVector4D Viewport3D::sketchInkC() const {
    return m_dayOn ? kDaySketch : kNightSketch;
}

// R5: a PALETA pinta a seleção corrente (face, multi ou sólido inteiro)
void Viewport3D::paletteApplyColor(float r, float g, float b) {
    std::vector<std::pair<int, Idx>> alvo;
    auto solidAll = [&](int mi) {
        const HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
        for (Idx f = 0; f < Idx(m.faceCount()); ++f) alvo.push_back({mi, f});
    };
    if (m_selWhole && m_selMesh >= 0 && m_selMesh < int(m_meshes.size()))
        solidAll(m_selMesh);
    else if (m_selMesh >= 0 && m_selFace != HalfEdgeMesh::kNone)
        alvo.push_back({m_selMesh, m_selFace});
    for (const auto& p : m_selFacesMulti) alvo.push_back(p);
    for (const int mi : m_selSolidsMulti)
        if (mi >= 0 && mi < int(m_meshes.size())) solidAll(mi);
    if (alvo.empty()) {
        emit pickInfo(QStringLiteral(
            "Paleta: selecione uma FACE (clique) ou um SÓLIDO (duplo) e "
            "então escolha o material."));
        return;
    }
    pushUndo();
    for (const auto& [mi, f] : alvo) {
        if (mi < 0 || mi >= int(m_meshes.size())) continue;
        m_meshes[std::size_t(mi)].faceColors[f] = {r, g, b};
        m_meshes[std::size_t(mi)].faceTex.erase(f);
    }
    m_edited = true;
    buildRenderArrays();
    emit pickInfo(QStringLiteral("Paleta: %1 face(s) pintadas — Ctrl+Z "
                                 "desfaz.").arg(alvo.size()));
    update();
}

// ---------------------------------------------------------------------------
//  R6: O BALDE — arma o material UMA vez, cada clique pinta. Ctrl pinta o
//  sólido inteiro; Alt é conta-gotas. Chega de pintar→cor→parede em loop.
// ---------------------------------------------------------------------------
void Viewport3D::setActiveColor(float r, float g, float b) {
    m_mat.kind = 0;
    m_mat.rgb[0] = r;
    m_mat.rgb[1] = g;
    m_mat.rgb[2] = b;
    setTool(Tool::Paint);
    emit pickInfo(QStringLiteral(
        "BALDE armado com a cor — clique nas faces (Ctrl = sólido inteiro · "
        "Alt = conta-gotas · Esc sai)."));
}

void Viewport3D::setActiveTexture(const QString& path, double scale) {
    m_mat.kind = 1;
    m_mat.texPath = path;
    m_mat.texScale = scale;
    setTool(Tool::Paint);
    emit pickInfo(QStringLiteral(
                      "BALDE armado com a textura em %1 m por tile — clique "
                      "nas faces (digite pra trocar a escala · Ctrl = sólido "
                      "· Esc sai).")
                      .arg(scale, 0, 'f', 2));
}

void Viewport3D::paintClickAt(const QPoint& pos, bool wholeSolid,
                              bool sample, int match) {
    int mi = -1;
    Idx f = HalfEdgeMesh::kNone;
    if (!pickAt(pos, mi, f)) return;
    MeshPart& part = m_meshes[std::size_t(mi)];
    if (match != 0 && m_mat.kind == 0) {
        // R8: pinta TODAS as faces com o MESMO material da clicada
        // (Shift = no modelo inteiro · Ctrl+Shift = só neste objeto)
        auto keyOf = [&](const MeshPart& mp, Idx ff) -> QString {
            const auto tf = mp.faceTex.find(ff);
            if (tf != mp.faceTex.end()) return QStringLiteral("T:") + tf->second;
            const auto fc = mp.faceColors.find(ff);
            if (fc == mp.faceColors.end()) return QStringLiteral("washi");
            return QStringLiteral("C:%1,%2,%3")
                .arg(int(fc->second[0] * 255))
                .arg(int(fc->second[1] * 255))
                .arg(int(fc->second[2] * 255));
        };
        const QString alvo = keyOf(part, f);
        pushUndo();
        int n = 0;
        const int lo = match == 2 ? mi : 0;
        const int hi = match == 2 ? mi + 1 : int(m_meshes.size());
        for (int i = lo; i < hi; ++i) {
            MeshPart& mp = m_meshes[std::size_t(i)];
            for (Idx ff = 0; ff < Idx(mp.mesh.faceCount()); ++ff)
                if (keyOf(mp, ff) == alvo) {
                    mp.faceColors[ff] = {m_mat.rgb[0], m_mat.rgb[1],
                                         m_mat.rgb[2]};
                    mp.faceTex.erase(ff);
                    ++n;
                }
        }
        m_edited = true;
        buildRenderArrays();
        emit pickInfo(QStringLiteral("Pintadas %1 face(s) com o material "
                                     "igual ao da clicada.").arg(n));
        update();
        return;
    }
    if (sample) {                        // Alt: conta-gotas
        const auto tf = part.faceTex.find(f);
        if (tf != part.faceTex.end()) {
            const auto te = m_texLib.find(tf->second);
            if (te != m_texLib.end()) {
                m_mat.kind = 1;
                m_mat.texPath = te->second.file;
                m_mat.texScale = part.texScale;
            }
        } else {
            const auto fc = part.faceColors.find(f);
            m_mat.kind = 0;
            m_mat.rgb[0] = fc != part.faceColors.end() ? fc->second[0]
                                                       : kWallColor.x();
            m_mat.rgb[1] = fc != part.faceColors.end() ? fc->second[1]
                                                       : kWallColor.y();
            m_mat.rgb[2] = fc != part.faceColors.end() ? fc->second[2]
                                                       : kWallColor.z();
        }
        emit pickInfo(QStringLiteral(
            "Conta-gotas: material da face copiado pro balde."));
        return;
    }
    if (m_mat.kind == 1) {               // textura: reusa o applyTexture
        const int oM = m_selMesh;
        const Idx oF = m_selFace;
        const bool oW = m_selWhole;
        m_selMesh = mi;
        m_selFace = f;
        m_selWhole = wholeSolid;
        applyTexture(m_mat.texPath, m_mat.texScale);
        m_selMesh = oM;
        m_selFace = oF;
        m_selWhole = oW;
        m_hlDirty = true;
        update();
        return;
    }
    pushUndo();                          // cor: direto no mapa da face
    if (wholeSolid) {
        for (Idx ff = 0; ff < Idx(part.mesh.faceCount()); ++ff) {
            part.faceColors[ff] = {m_mat.rgb[0], m_mat.rgb[1], m_mat.rgb[2]};
            part.faceTex.erase(ff);
        }
    } else {
        part.faceColors[f] = {m_mat.rgb[0], m_mat.rgb[1], m_mat.rgb[2]};
        part.faceTex.erase(f);
    }
    m_edited = true;
    buildRenderArrays();
    emit pickInfo(QStringLiteral("Pintado — continue clicando (Esc sai)."));
    update();
}

void Viewport3D::qaPaintAt(const QString& s) {
    // série de cliques "nx,ny[,ctrl];nx,ny;…" — prova o pintar CONTÍNUO
    for (const QString& tk : s.split(';', Qt::SkipEmptyParts)) {
        const QStringList c = tk.split(',');
        if (c.size() < 2) continue;
        const QString mod = c.size() > 2 ? c[2] : QString();
        paintClickAt(QPoint(int(c[0].toDouble() * width()),
                            int(c[1].toDouble() * height())),
                     mod == QLatin1String("ctrl"), false,
                     mod == QLatin1String("shift")
                         ? 1
                         : mod == QLatin1String("cs") ? 2 : 0);
    }
}

// ---------------------------------------------------------------------------
//  R7: AS TRÊS QUE FALTAVAM — arco 2pt+curvar · transferidor · escala viva
// ---------------------------------------------------------------------------
namespace {
// pontos do arco por corda A→B + sagitta s (sinal = lado), no plano z de A
std::vector<Point3> arcPoints(const Point3& A, const Point3& B, double s) {
    std::vector<Point3> out;
    const double cx = (A.x + B.x) / 2, cy = (A.y + B.y) / 2;
    const double dx = B.x - A.x, dy = B.y - A.y;
    const double c = std::hypot(dx, dy);
    if (c < 1e-9 || std::abs(s) < 1e-6) return out;
    const double R = (s * s + c * c / 4.0) / (2.0 * std::abs(s));
    // centro: na perpendicular da corda, do lado OPOSTO à flecha
    const double nx = -dy / c, ny = dx / c;   // normal unitária da corda
    const double sgn = s > 0 ? 1.0 : -1.0;
    const double ox = cx - sgn * nx * (R - std::abs(s));
    const double oy = cy - sgn * ny * (R - std::abs(s));
    const double a0 = std::atan2(A.y - oy, A.x - ox);
    double a1 = std::atan2(B.y - oy, B.x - ox);
    // varre pelo lado da flecha (arco MENOR quando s < R)
    double sweep = a1 - a0;
    while (sweep > 3.14159265358979) sweep -= 2 * 3.14159265358979;
    while (sweep < -3.14159265358979) sweep += 2 * 3.14159265358979;
    if (std::abs(s) > R + 1e-9) return out;   // impossível
    const int N = std::clamp(int(std::abs(sweep) / 0.26) + 3, 4, 32);
    for (int i = 0; i <= N; ++i) {
        const double a = a0 + sweep * (double(i) / N);
        out.push_back({ox + R * std::cos(a), oy + R * std::sin(a), A.z});
    }
    // garante extremidades EXATAS (cura de circuito depende disso)
    out.front() = A;
    out.back() = B;
    return out;
}
} // namespace

// alvos dos gestos de girar/escalar: multi/grupo se houver, senão o sólido
std::vector<int> Viewport3D::gestureTargets() const {
    if (!m_selSolidsMulti.empty()) return m_selSolidsMulti;
    if (m_selMesh >= 0 && m_selMesh < int(m_meshes.size()))
        return {m_selMesh};
    return {};
}

// ---- ARCO ------------------------------------------------------------------
void Viewport3D::arcHover(const QPoint& pos) {
    m_ghost.clear();
    m_snapMark.clear();
    Point3 p;
    if (pencilPointAt(pos, p)) {
        if (m_arcStage == 1) {
            pushSeg(m_ghost, m_arcA, p);
            liveVcb(QStringLiteral("corda %1 m")
                        .arg((p - m_arcA).length(), 0, 'f', 2));
        } else if (m_arcStage == 2) {
            // sagitta = distância assinada do cursor à corda (plano XY)
            const double dx = m_arcB.x - m_arcA.x, dy = m_arcB.y - m_arcA.y;
            const double c = std::hypot(dx, dy);
            if (c > 1e-9) {
                m_arcSag = ((p.x - m_arcA.x) * (-dy) + (p.y - m_arcA.y) * dx) / c;
                const auto pts = arcPoints(m_arcA, m_arcB, m_arcSag);
                for (std::size_t i = 0; i + 1 < pts.size(); ++i)
                    pushSeg(m_ghost, pts[i], pts[i + 1]);
                if (!pts.empty()) {
                    const double R =
                        (m_arcSag * m_arcSag + c * c / 4.0) /
                        (2.0 * std::abs(m_arcSag) + 1e-12);
                    liveVcb(QStringLiteral("r %1 m").arg(R, 0, 'f', 2));
                }
            }
        }
    }
    m_ghostDirty = true;
    update();
}

void Viewport3D::arcClick(const QPoint& pos) {
    Point3 p;
    if (!pencilPointAt(pos, p)) return;
    if (m_arcStage == 0) {
        m_arcA = p;
        m_arcStage = 1;
        emit pickInfo(QStringLiteral("Arco: clique o 2º ponto da CORDA."));
    } else if (m_arcStage == 1) {
        if ((p - m_arcA).lengthSq() < 1e-8) return;
        m_arcB = p;
        m_arcB.z = m_arcA.z;                 // arco vive num plano só
        m_arcStage = 2;
        emit pickInfo(QStringLiteral(
            "Arco: CURVE com o mouse e clique — ou digite o raio."));
    } else {
        commitArc(m_arcSag);
    }
}

void Viewport3D::commitArc(double sagitta) {
    const auto pts = arcPoints(m_arcA, m_arcB, sagitta);
    if (pts.size() < 2) return;
    m_arcStage = 0;
    m_ghost.clear();
    m_ghostDirty = true;
    // cada trecho passa pelo LÁPIS: o heal correto (checa ANTES de inserir)
    // vem de graça — se o arco fechar um circuito do rascunho, a face nasce
    m_chainActive = false;
    bool healed = false;
    for (const Point3& q : pts) {
        const bool wasActive = m_chainActive;
        pencilClick(q);
        if (wasActive && !m_chainActive) {   // tryHealFace fechou o circuito
            healed = true;
            break;
        }
    }
    m_chainActive = false;
    if (!healed)
        emit pickInfo(QStringLiteral(
                          "Arco no rascunho (%1 segmentos) — feche o "
                          "circuito para nascer a face.")
                          .arg(pts.size() - 1));
    update();
}

// ---- TRANSFERIDOR (rotação interativa, plano do chão) ----------------------
void Viewport3D::rotHover(const QPoint& pos) {
    m_ghost.clear();
    m_snapMark.clear();
    Point3 p;
    if (m_rotStage >= 1 && rotPlanePoint(pos, p)) {
        if (m_rotStage == 1) {
            pushSeg(m_ghost, m_rotC, p);
        } else {
            // ângulo no PLANO do transferidor (base U/V)
            const Vec3 cur = p - m_rotC;
            double ang = (std::atan2(cur.dot(m_rotV), cur.dot(m_rotU)) -
                          std::atan2(m_rotRef.dot(m_rotV),
                                     m_rotRef.dot(m_rotU))) *
                         180.0 / 3.14159265358979;
            while (ang > 180.0) ang -= 360.0;
            while (ang < -180.0) ang += 360.0;
            const double snap = std::round(ang / 15.0) * 15.0;
            if (std::abs(ang - snap) < 4.0) ang = snap;   // ímã de 15°
            m_rotAng = ang;
            liveVcb(QStringLiteral("%1°").arg(ang, 0, 'f', 1));
            const double rad = ang * 3.14159265358979 / 180.0;
            auto rot = [&](const Point3& q) {             // Rodrigues no eixo
                const Vec3 v = q - m_rotC;
                const Vec3 r = v * std::cos(rad) +
                               m_rotN.cross(v) * std::sin(rad) +
                               m_rotN * (m_rotN.dot(v) * (1.0 -
                                                          std::cos(rad)));
                return m_rotC + r;
            };
            pushSeg(m_ghost, m_rotC, m_rotC + m_rotRef);
            pushSeg(m_ghost, m_rotC, rot(m_rotC + m_rotRef));
            std::vector<Point3> lines;                    // fantasma girado
            for (const int mi : gestureTargets()) {
                lines.clear();
                m_meshes[std::size_t(mi)].mesh.edgeLines(lines);
                for (std::size_t i = 0; i + 1 < lines.size(); i += 2)
                    pushSeg(m_ghost, rot(lines[i]), rot(lines[i + 1]));
            }
        }
    }
    m_ghostDirty = true;
    update();
}

// ponto no PLANO do transferidor (face escolhida ou chão)
bool Viewport3D::rotPlanePoint(const QPoint& pos, Point3& out) const {
    Point3 orig;
    Vec3 dir;
    if (!rayAt(pos, orig, dir)) return false;
    const double den = dir.dot(m_rotN);
    if (std::abs(den) < 1e-9) return false;
    const double t = (m_rotC - orig).dot(m_rotN) / den;
    if (t <= 0.0) return false;
    out = orig + dir * t;
    return true;
}

void Viewport3D::rotClick(const QPoint& pos, bool copy) {
    if (m_rotStage == 0) {
        if (gestureTargets().empty()) {
            emit pickInfo(QStringLiteral(
                "Transferidor: selecione um sólido/grupo antes."));
            return;
        }
        // R8: o transferidor se ORIENTA pela face sob o cursor (parede =
        // girar no plano da parede); no vazio, plano do chão
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        Point3 hit;
        if (pickAt(pos, mi, f, &hit)) {
            m_rotC = hit;
            m_rotN = m_meshes[std::size_t(mi)].mesh.faceNormal(f);
        } else {
            Point3 p;
            if (!pencilPointAt(pos, p)) return;
            m_rotC = p;
            m_rotN = Vec3{0, 0, 1};
        }
        m_rotU = std::abs(m_rotN.z) > 0.9 ? Vec3{1, 0, 0}
                                          : Vec3{0, 0, 1}.cross(m_rotN);
        m_rotU = m_rotU.normalized();
        m_rotV = m_rotN.cross(m_rotU).normalized();
        m_rotStage = 1;
        emit pickInfo(QStringLiteral(
            "Transferidor: clique a REFERÊNCIA (o braço zero)."));
        return;
    }
    Point3 p;
    if (!rotPlanePoint(pos, p)) return;
    if (m_rotStage == 1) {
        const Vec3 r = p - m_rotC;
        if (r.lengthSq() < 1e-8) return;
        m_rotRef = r;
        m_rotStage = 2;
        emit pickInfo(QStringLiteral(
            "Transferidor: GIRE e clique (ímã de 15°) — Ctrl copia; ou "
            "digite o ângulo."));
    } else {
        commitRotate(m_rotAng, copy);
    }
}

void Viewport3D::commitRotate(double deg, bool copy) {
    const auto alvo = gestureTargets();
    if (alvo.empty() || std::abs(deg) < 1e-6) return;
    pushUndo();
    const double rad = deg * 3.14159265358979 / 180.0;
    const double rc = std::cos(rad), rs = std::sin(rad);
    auto rotAx = [&](const Point3& p) {   // R29: mesma Rodrigues do rotateAxis
        const Vec3 d{p.x - m_rotC.x, p.y - m_rotC.y, p.z - m_rotC.z};
        const Vec3 r = d * rc + m_rotN.cross(d) * rs +
                       m_rotN * (m_rotN.dot(d) * (1.0 - rc));
        return Point3{m_rotC.x + r.x, m_rotC.y + r.y, m_rotC.z + r.z};
    };
    if (!copy) transformDimAnchors(alvo, rotAx);   // R29: cotas UMA vez, união
    for (const int mi : alvo) {          // R8: eixo = normal do plano escolhido
        if (copy) {
            MeshPart clone = m_meshes[std::size_t(mi)];
            clone.mesh.rotateAxis(m_rotC, m_rotN, rad);
            m_meshes.push_back(std::move(clone));
        } else {
            m_meshes[std::size_t(mi)].mesh.rotateAxis(m_rotC, m_rotN, rad);
        }
    }
    m_lastOp.kind = 6;                   // ×N radial no VCB continua valendo
    m_lastOp.mesh = alvo.front();
    m_lastOp.p1 = m_rotC;
    m_lastOp.a = deg;
    m_edited = true;
    m_rotStage = 0;
    m_ghost.clear();
    m_ghostDirty = true;
    setTool(Tool::Select);
    buildRenderArrays();
    m_hlDirty = true;
    if (copy) emit structureChanged();
    emit pickInfo(QStringLiteral("%1 %2° — digite x6 para a matriz radial; "
                                 "Ctrl+Z desfaz.")
                      .arg(copy ? QStringLiteral("Copiado girado")
                                : QStringLiteral("Girado"))
                      .arg(deg, 0, 'f', 1));
    update();
}

// ---- ESCALA VIVA (two-click, uniforme sobre o centro) -----------------------
void Viewport3D::scaleHover(const QPoint& pos) {
    m_ghost.clear();
    m_snapMark.clear();
    Point3 p;
    if (m_scStage == 1 && pencilPointAt(pos, p)) {
        const double d = (p - m_scCenter).length();
        m_scF = m_scBase > 1e-9 ? d / m_scBase : 1.0;
        liveVcb(QStringLiteral("x %1").arg(m_scF, 0, 'f', 2));
        std::vector<Point3> lines;
        for (const int mi : gestureTargets()) {
            lines.clear();
            m_meshes[std::size_t(mi)].mesh.edgeLines(lines);
            for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
                auto sc = [&](const Point3& q) {
                    return m_scCenter + (q - m_scCenter) * m_scF;
                };
                pushSeg(m_ghost, sc(lines[i]), sc(lines[i + 1]));
            }
        }
    }
    m_ghostDirty = true;
    update();
}

void Viewport3D::scaleClick(const QPoint& pos) {
    Point3 p;
    if (!pencilPointAt(pos, p)) return;
    if (m_scStage == 0) {
        const auto alvo = gestureTargets();
        if (alvo.empty()) {
            emit pickInfo(QStringLiteral(
                "Escala: selecione um sólido/grupo antes."));
            return;
        }
        m_scCenter = m_meshes[std::size_t(alvo.front())].mesh.bboxCenter();
        m_scBase = std::max(1e-9, (p - m_scCenter).length());
        m_scStage = 1;
        emit pickInfo(QStringLiteral(
            "Escala: AFASTE/APROXIME do centro e clique — ou digite o "
            "fator."));
    } else {
        commitScale(m_scF);
    }
}

void Viewport3D::commitScale(double f) {
    const auto alvo = gestureTargets();
    if (alvo.empty() || f < 0.01 || std::abs(f - 1.0) < 1e-6) return;
    pushUndo();
    const int scAxis = m_scAxis;
    const Point3 sc = m_scCenter;
    auto scl = [&](const Point3& p) {   // R29: cota escala junto (uniforme/eixo)
        Point3 q = p;
        if (scAxis == 0) return sc + (p - sc) * f;
        if (scAxis == 1) q.x = sc.x + (p.x - sc.x) * f;
        else if (scAxis == 2) q.y = sc.y + (p.y - sc.y) * f;
        else q.z = sc.z + (p.z - sc.z) * f;
        return q;
    };
    transformDimAnchors(alvo, scl);   // R29: cotas UMA vez, contra a união
    for (const int mi : alvo) {
        HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
        if (m_scAxis == 0) {
            m.scaleAbout(m_scCenter, f);
        } else {                         // R8: escala por EIXO (setas)
            for (std::size_t v = 0; v < m.vertexCount(); ++v) {
                Point3 p = m.vertex(Idx(v)).p;
                if (m_scAxis == 1)
                    p.x = m_scCenter.x + (p.x - m_scCenter.x) * f;
                else if (m_scAxis == 2)
                    p.y = m_scCenter.y + (p.y - m_scCenter.y) * f;
                else
                    p.z = m_scCenter.z + (p.z - m_scCenter.z) * f;
                m.moveVertex(Idx(v), p);
            }
        }
    }
    m_edited = true;
    m_scStage = 0;
    m_ghost.clear();
    m_ghostDirty = true;
    setTool(Tool::Select);
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Escalado x%1 — Ctrl+Z desfaz.")
                      .arg(f, 0, 'f', 2));
    update();
}

// ---- OFFSET INTERATIVO (R8): clique a face, arraste, digite, dblclick ------
void Viewport3D::offsetHover(const QPoint& pos) {
    m_ghost.clear();
    m_snapMark.clear();
    if (m_offStage == 1 && m_offMesh >= 0 &&
        m_offMesh < int(m_meshes.size())) {
        Point3 orig;
        Vec3 dir;
        const HalfEdgeMesh& m = m_meshes[std::size_t(m_offMesh)].mesh;
        const Vec3 n = m.faceNormal(m_offFace);
        if (rayAt(pos, orig, dir)) {
            const double den = dir.dot(n);
            if (std::abs(den) > 1e-9) {
                const double t = (m_offP0 - orig).dot(n) / den;
                if (t > 0.0) {
                    m_offD = (orig + dir * t - m_offP0).length();
                    liveVcb(QStringLiteral("%1 m").arg(m_offD, 0, 'f', 3));
                    // fantasma: contorno encolhido em direção ao centroide
                    const Point3 c = m.faceCentroid(m_offFace);
                    auto shr = [&](const Point3& q) {
                        Vec3 d2 = c - q;
                        const double l = d2.length();
                        return l > 1e-9 ? q + d2 * std::min(0.95, m_offD / l)
                                        : q;
                    };
                    for (const Idx h : m.faceHalfEdges(m_offFace)) {
                        if (m.isBridge(h)) continue;
                        const Point3& a = m.vertex(m.halfEdge(h).origin).p;
                        const Point3& b =
                            m.vertex(m.halfEdge(m.halfEdge(h).next).origin).p;
                        pushSeg(m_ghost, shr(a), shr(b));
                    }
                }
            }
        }
    }
    m_ghostDirty = true;
    update();
}

void Viewport3D::offsetClick(const QPoint& pos) {
    if (m_offStage == 0) {
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        Point3 hit;
        if (!pickAt(pos, mi, f, &hit)) return;
        m_offMesh = mi;
        m_offFace = f;
        m_offP0 = hit;
        m_selMesh = mi;                  // o offset opera na seleção
        m_selFace = f;
        m_selWhole = false;
        m_hlDirty = true;
        m_offStage = 1;
        emit pickInfo(QStringLiteral(
            "Offset: ARRASTE pra dentro e clique — ou digite a distância."));
        update();
    } else {
        m_offStage = 0;
        if (m_offD > 1e-4) offsetSelectedFace(m_offD);
    }
}

// ---- FOLLOW ME pelo PERÍMETRO de uma face (R8) ------------------------------
void Viewport3D::armFollowPerimeter() {
    if (m_selMesh < 0 || m_selFace == HalfEdgeMesh::kNone || m_selWhole) {
        emit pickInfo(QStringLiteral(
            "Follow Me (perímetro): selecione a FACE-PERFIL primeiro."));
        return;
    }
    m_fmPerimArm = true;
    emit pickInfo(QStringLiteral(
        "Follow Me: agora clique na FACE-CAMINHO — o perfil percorre o "
        "PERÍMETRO dela (moldura/cornija)."));
}

void Viewport3D::followMePerimeter(int pmi, Idx pf) {
    if (m_selMesh < 0 || m_selFace == HalfEdgeMesh::kNone) return;
    const HalfEdgeMesh& src = m_meshes[std::size_t(m_selMesh)].mesh;
    std::vector<Point3> prof;
    for (const Idx v : src.faceVertices(m_selFace))
        prof.push_back(src.vertex(v).p);
    if (prof.size() < 3) return;
    const Point3 c0 = src.faceCentroid(m_selFace);
    const Vec3 n0 = src.faceNormal(m_selFace);
    const HalfEdgeMesh& pm = m_meshes[std::size_t(pmi)].mesh;
    std::vector<Point3> path;
    for (const Idx v : pm.faceVertices(pf)) path.push_back(pm.vertex(v).p);
    const std::size_t n = path.size();
    if (n < 3) return;

    auto rot = [](const Point3& p, const Point3& piv, const Vec3& ax,
                  double ang) {
        const Vec3 v = p - piv;
        const Vec3 r = v * std::cos(ang) + ax.cross(v) * std::sin(ang) +
                       ax * (ax.dot(v) * (1.0 - std::cos(ang)));
        return piv + r;
    };
    pushUndo();
    MeshPart part;
    HalfEdgeMesh& m = part.mesh;
    std::vector<std::vector<Idx>> rings(n);
    // anel semente alinhado ao 1º trecho, já no bissetor do vértice 0
    const Vec3 d0 = (path[1] - path[0]).normalized();
    Vec3 ax0 = n0.cross(d0);
    const double sl = ax0.length();
    const double ct = std::clamp(n0.dot(d0), -1.0, 1.0);
    std::vector<Point3> cur;
    for (const Point3& p : prof) {
        Point3 q = p + (path[0] - c0);
        if (sl > 1e-9) q = rot(q, path[0], ax0 * (1.0 / sl),
                               std::atan2(sl, ct));
        else if (ct < 0.0)
            q = rot(q, path[0],
                    (std::abs(n0.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0})
                        .cross(n0)
                        .normalized(),
                    3.14159265358979);
        cur.push_back(q);
    }
    auto project = [&](std::vector<Point3>& ring, const Vec3& dPrev,
                       const Point3& at, const Vec3& nP) {
        for (Point3& q : ring) {
            const double den = dPrev.dot(nP);
            if (std::abs(den) < 1e-9) return false;
            q = q + dPrev * ((at - q).dot(nP) / den);
        }
        return true;
    };
    // projeta o anel semente no BISSETOR do vértice 0 (fecho correto)
    {
        const Vec3 dIn = (path[0] - path[n - 1]).normalized();
        Vec3 nP = dIn + d0;
        const double l = nP.length();
        nP = l > 1e-9 ? nP * (1.0 / l) : d0;
        if (!project(cur, d0, path[0], nP)) {
            undoLast();
            m_redo.clear();
            return;
        }
    }
    for (Idx i = 0; i < Idx(n); ++i) {
        if (i > 0) {
            const Vec3 dPrev =
                (path[i] - path[i - 1]).normalized();
            const Vec3 dNext =
                (path[(i + 1) % n] - path[i]).normalized();
            Vec3 nP = dPrev + dNext;
            const double l = nP.length();
            nP = l > 1e-9 ? nP * (1.0 / l) : dPrev;
            if (!project(cur, dPrev, path[i], nP)) {
                undoLast();
                m_redo.clear();
                emit pickInfo(QStringLiteral(
                    "Follow Me: canto fechado demais no perímetro."));
                return;
            }
        }
        for (const Point3& q : cur) rings[i].push_back(m.addVertex(q));
    }
    const std::size_t P = prof.size();
    for (std::size_t i = 0; i < n; ++i) {         // quads com WRAP, sem tampas
        const std::size_t i2 = (i + 1) % n;
        for (std::size_t j = 0; j < P; ++j) {
            const std::size_t j2 = (j + 1) % P;
            if (m.addFace({rings[i][j], rings[i][j2], rings[i2][j2],
                           rings[i2][j]}) == HalfEdgeMesh::kNone) {
                undoLast();
                m_redo.clear();
                emit pickInfo(QStringLiteral(
                    "Follow Me: a varredura fechada degenerou."));
                return;
            }
        }
    }
    std::string why;
    if (!m.checkIntegrity(&why)) {
        undoLast();
        m_redo.clear();
        emit pickInfo(QStringLiteral("Follow Me: integridade falhou (%1).")
                          .arg(QString::fromStdString(why)));
        return;
    }
    part.compName = QStringLiteral("Moldura");
    m_meshes.push_back(std::move(part));
    m_selMesh = int(m_meshes.size()) - 1;
    m_selFace = 0;
    m_selWhole = true;
    m_edited = true;
    m_hlDirty = true;
    buildRenderArrays();
    emit structureChanged();
    emit pickInfo(QStringLiteral(
                      "Follow Me: moldura FECHADA pelo perímetro (%1 faces).")
                      .arg(m_meshes.back().mesh.faceCount()));
    update();
}

// QA R8: "px,py,cx,cy" — pick da face-perfil + clique na face-caminho
void Viewport3D::qaFmPerim(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 4) return;
    int mi = -1;
    Idx f = HalfEdgeMesh::kNone;
    if (!pickAt(QPoint(int(c[0].toDouble() * width()),
                       int(c[1].toDouble() * height())), mi, f))
        return;
    m_selMesh = mi;
    m_selFace = f;
    m_selWhole = false;
    m_hlDirty = true;
    int pmi = -1;
    Idx pf = HalfEdgeMesh::kNone;
    if (pickAt(QPoint(int(c[2].toDouble() * width()),
                      int(c[3].toDouble() * height())), pmi, pf))
        followMePerimeter(pmi, pf);
}

// QA R7
void Viewport3D::qaArc(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 5) return;
    setTool(Tool::Arc);
    arcClick(QPoint(int(c[0].toDouble() * width()),
                    int(c[1].toDouble() * height())));
    arcClick(QPoint(int(c[2].toDouble() * width()),
                    int(c[3].toDouble() * height())));
    // raio digitado -> sagitta (arco menor, lado +)
    const double R = c[4].toDouble();
    const double cc = (m_arcB - m_arcA).length();
    if (R >= cc / 2 && cc > 1e-9) {
        const double sag = R - std::sqrt(R * R - cc * cc / 4.0);
        commitArc(sag);
    }
    emit pickInfo(QStringLiteral("arc: %1 segmentos no rascunho")
                      .arg(m_sketch.size()));
}

void Viewport3D::qaProtractor(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() < 5) return;
    setTool(Tool::Rotate);
    rotClick(QPoint(int(c[0].toDouble() * width()),
                    int(c[1].toDouble() * height())), false);
    rotClick(QPoint(int(c[2].toDouble() * width()),
                    int(c[3].toDouble() * height())), false);
    commitRotate(c[4].toDouble(),
                 c.size() > 5 && c[5] == QLatin1String("copy"));
}

void Viewport3D::qaScaleTo(const QString& s) {
    const QStringList c = s.split(',');
    const auto alvo = gestureTargets();
    if (alvo.empty()) return;
    m_scCenter = m_meshes[std::size_t(alvo.front())].mesh.bboxCenter();
    if (c.size() > 1) m_scAxis = c[1] == "x" ? 1 : c[1] == "y" ? 2 : 3;
    commitScale(c[0].toDouble());
    m_scAxis = 0;
}

// R8: enquadrar TUDO mantendo o ângulo do olhar
// R52: bbox do que MERECE aparecer na foto. A R45 ensinou (a duras penas) que
// enquadrar pelo bbox CRU quebra: a rua de 130 m do Sobrado inflou a caixa e a
// casa saiu do tamanho de uma formiga. Terreno, rua e calçada são ACHATADOS —
// ocupam metros de chão e centímetros de altura. Descarto quem tem menos de
// 1 m de altura E é largo; sobra a arquitetura (casa, árvore, muro).
// Se TUDO for achatado (um estudo só de piso), volto ao bbox cru — enquadrar
// alguma coisa é melhor que enquadrar nada.
bool Viewport3D::boundsFoto(Point3& lo, Point3& hi) const {
    struct Cx { Point3 l, h; double vol; };
    std::vector<Cx> cand;
    Point3 loCru{1e300, 1e300, 1e300}, hiCru{-1e300, -1e300, -1e300};
    bool anyCru = false;
    for (const MeshPart& part : m_meshes) {
        if (part.hidden) continue;
        Point3 l{1e300, 1e300, 1e300}, h{-1e300, -1e300, -1e300};
        bool v0 = false;
        for (std::size_t v = 0; v < part.mesh.vertexCount(); ++v) {
            const Point3& p = part.mesh.vertex(Idx(v)).p;
            l.x = std::min(l.x, p.x); h.x = std::max(h.x, p.x);
            l.y = std::min(l.y, p.y); h.y = std::max(h.y, p.y);
            l.z = std::min(l.z, p.z); h.z = std::max(h.z, p.z);
            v0 = true;
        }
        if (!v0) continue;
        loCru.x = std::min(loCru.x, l.x); hiCru.x = std::max(hiCru.x, h.x);
        loCru.y = std::min(loCru.y, l.y); hiCru.y = std::max(hiCru.y, h.y);
        loCru.z = std::min(loCru.z, l.z); hiCru.z = std::max(hiCru.z, h.z);
        anyCru = true;
        const double alt = h.z - l.z;
        const double larg = std::max(h.x - l.x, h.y - l.y);
        // chapado + largo + NO CHÃO. O `l.z < 0.5` não é detalhe: terreno e
        // rua se distinguem por POSIÇÃO, não só por tamanho. Sem ele, laje de
        // cobertura / marquise / carport (0,2 m de espessura, 5 m de vão, a
        // z=2,5) caem no descarte e saem CORTADOS da foto quando avançam além
        // do corpo da casa. Espelho d'água e deck, aterrados, seguem fora ✓.
        if (alt < 1.0 && larg > 4.0 && l.z < 0.5) continue;
        cand.push_back({l, h, (h.x - l.x) * (h.y - l.y) * alt});
    }
    if (cand.empty()) {
        if (!anyCru) return false;
        lo = loCru; hi = hiCru;    // estudo só de piso: enquadra o que tem
        return true;
    }
    // O ASSUNTO da foto é o PRÉDIO — não o lote. Tirar o terreno do bbox NÃO
    // BASTA: 5 árvores espalhadas num terreno de 30 m deixam a caixa com 28 m
    // de largura, a câmera recua pra 44 m e a casa vira um ponto no gramado
    // (vi na prova visual). Fotógrafo enquadra o assunto e deixa o resto
    // sangrar pra fora do quadro.
    //
    // Assunto = a maior MASSA CONECTADA, não a maior PEÇA. A diferença não é
    // acadêmica: o Sobrado tem ~195 sólidos e cada parede é fina, então a
    // maior *peça* dele é uma ÁRVORE (copa frondosa tem bbox maior que parede
    // de 20 cm) — o enquadramento ia fotografar a árvore. Junto quem se
    // ENCOSTA em componentes conexos e fico com o de maior volume somado: a
    // casa inteira (paredes + lajes + telhado) é um componente gordo; cada
    // árvore, sozinha no gramado (o terreno já saiu), é um componente magro.
    const double f = 0.5;      // "encostado" tolera junta imperfeita
    const auto tocam = [f](const Cx& a, const Cx& b) {
        return a.l.x <= b.h.x + f && a.h.x >= b.l.x - f &&
               a.l.y <= b.h.y + f && a.h.y >= b.l.y - f &&
               a.l.z <= b.h.z + f && a.h.z >= b.l.z - f;
    };
    const int n = int(cand.size());
    std::vector<int> comp(std::size_t(n), -1);
    int nComp = 0;
    for (int i = 0; i < n; ++i) {          // flood-fill (n pequeno: ~200)
        if (comp[std::size_t(i)] >= 0) continue;
        std::vector<int> fila{i};
        comp[std::size_t(i)] = nComp;
        while (!fila.empty()) {
            const int k = fila.back();
            fila.pop_back();
            for (int j = 0; j < n; ++j) {
                if (comp[std::size_t(j)] >= 0) continue;
                if (!tocam(cand[std::size_t(k)], cand[std::size_t(j)])) continue;
                comp[std::size_t(j)] = nComp;
                fila.push_back(j);
            }
        }
        ++nComp;
    }
    std::vector<double> vol(std::size_t(nComp), 0.0);
    for (int i = 0; i < n; ++i)
        vol[std::size_t(comp[std::size_t(i)])] += cand[std::size_t(i)].vol;
    const int alvo = int(std::max_element(vol.begin(), vol.end()) - vol.begin());
    lo = Point3{1e300, 1e300, 1e300};
    hi = Point3{-1e300, -1e300, -1e300};
    for (int i = 0; i < n; ++i) {
        if (comp[std::size_t(i)] != alvo) continue;
        const Cx& c = cand[std::size_t(i)];
        lo.x = std::min(lo.x, c.l.x); hi.x = std::max(hi.x, c.h.x);
        lo.y = std::min(lo.y, c.l.y); hi.y = std::max(hi.y, c.h.y);
        lo.z = std::min(lo.z, c.l.z); hi.z = std::max(hi.z, c.h.z);
    }
    return true;
}

// R52: dado o bbox e o azimute, planta a câmera onde um FOTÓGRAFO plantaria:
// no olho de quem está de pé (1,65 m), a uma distância que faz o objeto caber.
// PURA (só matemática) porque é o que o QA precisa exercitar sem abrir janela.
// O yaw vem de FORA de propósito: o usuário já escolheu de que lado olha —
// corrijo a ALTURA, não a intenção dele.
void Viewport3D::enquadrarFoto(const Point3& lo, const Point3& hi,
                               double yawDeg, double fovYDeg, double aspect,
                               Point3& eye, Point3& tgt) {
    const Point3 c = (lo + hi) * 0.5;
    const double fovY = qDegreesToRadians(std::clamp(fovYDeg, 10.0, 100.0));
    const double tY = std::tan(fovY * 0.5);
    const double tX = tY * std::max(0.2, aspect);       // FOV horizontal
    const double yaw = qDegreesToRadians(yawDeg);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    // mira a 40% da altura: o eixo do prédio, não o telhado nem o rodapé
    tgt = Point3{c.x, c.y, lo.z + (hi.z - lo.z) * 0.40};

    // A distância sai do que precisa CABER em cada eixo da IMAGEM — não de uma
    // esfera circunscrita: cena de arquitetura é uma PANQUECA (28×21×5), a
    // esfera tem raio 18 e jogava a câmera a 52 m (casa do tamanho de um selo).
    // Com o yaw na mão a projeção é exata, então nada de "pior caso":
    //  · ACROSS = a largura que aparece de lado (o que precisa caber em X);
    //  · ALONG  = a meia-profundidade na direção do olhar — a face PRÓXIMA
    //    fica ALONG mais perto que o centro, e ignorar isso não é ser
    //    conservador, é errar (a diagonal superestimava o across e o along
    //    faltava; os dois erros se cancelavam por SORTE em casa quadrada).
    const double dx = hi.x - lo.x, dy = hi.y - lo.y;
    const double across = (dx * std::fabs(sy) + dy * std::fabs(cy)) * 0.5;
    const double along  = (dx * std::fabs(cy) + dy * std::fabs(sy)) * 0.5;
    // mirando a 40%, a folga de CIMA é 60% da altura — não os 50% de um
    // "meiaAlt": num prédio alto o topo cortaria.
    const double acimaAlvo = std::max(0.5, hi.z - tgt.z);
    const double d = along + std::max(acimaAlvo / tY, across / tX) * 1.06;

    // Altura do olho: 1,65 m é o fotógrafo DE PÉ — certo pra casa, errado pra
    // um estudo de mesa. Uma cadeira de 0,9 m a ~1,4 m de distância daria 43°
    // de mergulho e o céu sumiria de novo — no exato gesto que isto conserta.
    // Fotógrafo agacha: limito o mergulho a 10°.
    const double olho = std::min(1.65, tgt.z + d * std::tan(qDegreesToRadians(10.0)));
    eye = Point3{c.x + cy * d, c.y + sy * d, std::max(lo.z + 0.15, olho)};
}

void Viewport3D::zoomExtents() {
    Point3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    bool any = false;
    for (const MeshPart& part : m_meshes) {
        if (part.hidden) continue;
        for (std::size_t v = 0; v < part.mesh.vertexCount(); ++v) {
            const Point3& p = part.mesh.vertex(Idx(v)).p;
            lo.x = std::min(lo.x, p.x); hi.x = std::max(hi.x, p.x);
            lo.y = std::min(lo.y, p.y); hi.y = std::max(hi.y, p.y);
            lo.z = std::min(lo.z, p.z); hi.z = std::max(hi.z, p.z);
            any = true;
        }
    }
    if (!any) { resetCamera(); return; }
    const Point3 c = (lo + hi) * 0.5;
    const double r = std::max(1.0, (hi - lo).length() * 0.5);
    m_target[0] = float(c.x);
    m_target[1] = float(c.y);
    m_target[2] = float(c.z);
    m_dist = std::max(4.0f, float(r) * 2.2f);
    update();
}

QStringList Viewport3D::textureNames() const {
    QStringList out;
    for (const auto& [name, te] : m_texLib) out.append(name);
    return out;
}

QString Viewport3D::texturePath(const QString& name) const {
    const auto it = m_texLib.find(name);
    return it != m_texLib.end() ? it->second.file : QString();
}

// R13: escala da textura da SELEÇÃO (texScale é por parte — todas as faces
// texturizadas do sólido vestem juntas)
double Viewport3D::textureScaleSelected() const {
    if (m_selMesh < 0 || m_selMesh >= int(m_meshes.size())) return -1.0;
    const MeshPart& part = m_meshes[std::size_t(m_selMesh)];
    return part.faceTex.empty() ? -1.0 : part.texScale;
}

void Viewport3D::setTextureScaleSelected(double m) {
    if (m_selMesh < 0 || m_selMesh >= int(m_meshes.size())) return;
    MeshPart& part = m_meshes[std::size_t(m_selMesh)];
    if (part.faceTex.empty() || m < 1e-4) return;
    pushUndo();
    part.texScale = std::max(0.05, m);
    m_edited = true;
    buildRenderArrays();
    emit pickInfo(QStringLiteral("Textura revestida em %1 m por tile — "
                                 "Ctrl+Z desfaz.")
                      .arg(part.texScale, 0, 'f', 2));
    update();
}

// R9/R11: registra uma textura da biblioteca SEM segurar a imagem cheia —
// thumb pré-gerada (thumbs/<arquivo>) ou, na falta, gerada uma vez aqui.
void Viewport3D::addLibraryTexture(const QString& name, const QString& path) {
    if (m_texLib.count(name)) return;
    const QFileInfo fi(path);
    QImage thumb(fi.dir().filePath(QStringLiteral("thumbs/") + fi.fileName()));
    if (thumb.isNull()) {
        const QImage full(path);
        if (full.isNull()) return;
        thumb = full.scaled(64, 64, Qt::IgnoreAspectRatio,
                            Qt::SmoothTransformation);
    }
    m_texLib[name] = {path, QImage(), thumb, 0};
}

QImage Viewport3D::textureImage(const QString& name) const {
    const auto it = m_texLib.find(name);
    if (it == m_texLib.end()) return QImage();
    return it->second.thumb.isNull() ? it->second.img : it->second.thumb;
}

void Viewport3D::setPlant(const PlantScene& plant, double wallHeight) {
    m_plant = plant;
    if (wallHeight >= 0.5) m_wallHeight = wallHeight;
    rebuildScene();
    resetCamera();
    update();
}

void Viewport3D::setWallHeight(double h) {
    if (h < 0.5 || std::abs(h - m_wallHeight) < 1e-9) return;
    m_wallHeight = h;    // R3: quem re-importa a planta é o conector (janela)
    update();
}

void Viewport3D::resetCamera() {
    m_target[0] = m_center[0]; m_target[1] = m_center[1]; m_target[2] = m_center[2];
    m_yaw = -125.0f; m_pitch = 30.0f;
    m_dist = std::max(6.0f, m_radius * 2.3f);
    update();
}

void Viewport3D::setCameraPose(float yawDeg, float pitchDeg, float distFactor) {
    m_target[0] = m_center[0]; m_target[1] = m_center[1]; m_target[2] = m_center[2];
    m_yaw = yawDeg;
    m_pitch = std::clamp(pitchDeg, -10.0f, 89.0f);
    m_dist = std::max(1.0f, m_radius * 2.3f * distFactor);
    update();
}

// R27: entra/sai do modo primeira-pessoa. Ao ENTRAR, planta o olho na altura
// de 1,6 m sobre o ponto que a câmera orbital olhava (m_target x,y) e nivela o
// olhar no horizonte — o usuário "aparece de pé" onde estava mirando.
void Viewport3D::setWalkthrough(bool on) {
    if (on == m_walk) return;
    if (on) {
        m_orbYaw = m_yaw;                     // salva a órbita p/ restaurar
        m_orbPitch = m_pitch;
        m_walkEye[0] = m_target[0];
        m_walkEye[1] = m_target[1];
        m_walkEye[2] = 1.6f;                  // piso é z=0 no app; olho a 1,6 m
        // órbita mira PRA m_target (visão = -dir); no walk a visão é +dir —
        // meia-volta preserva a direção pra qual o usuário estava olhando.
        m_yaw += 180.0f;
        m_pitch = 0.0f;                        // nivelado no horizonte
        m_walk = true;
        if (!m_walkTimer) {                    // R28: movimento contínuo (~60 Hz)
            m_walkTimer = new QTimer(this);
            m_walkTimer->setInterval(16);
            connect(m_walkTimer, &QTimer::timeout, this,
                    [this] { walkStep(); });
        }
        m_walkKeys.clear();
        disarmGestures();                     // R28/R30/R34: entrar no walk desarma
        m_walkTimer->start();
        emit pickInfo(QStringLiteral(
            "Walkthrough: W/A/S/D (ou setas) andam · arraste olha em volta · "
            "Esc sai."));
    } else {
        m_walk = false;
        if (m_walkTimer) m_walkTimer->stop();
        m_walkKeys.clear();
        m_yaw = m_orbYaw;                     // órbita volta exatamente como era
        m_pitch = m_orbPitch;
        emit pickInfo(QStringLiteral("Walkthrough encerrado — órbita de volta."));
    }
    emit walkthroughChanged(m_walk);
    update();
}

// QA: posiciona a câmera de 1ª pessoa exata "ex,ey,ez,yaw,pitch" e mostra.
void Viewport3D::qaWalk(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 5) return;
    m_walk = true;
    m_walkEye[0] = c[0].toFloat();
    m_walkEye[1] = c[1].toFloat();
    m_walkEye[2] = c[2].toFloat();
    m_yaw = c[3].toFloat();
    m_pitch = std::clamp(c[4].toFloat(), -89.0f, 89.0f);
    emit pickInfo(QStringLiteral("Walkthrough @ %1,%2,%3 (QA).")
                      .arg(m_walkEye[0], 0, 'f', 2)
                      .arg(m_walkEye[1], 0, 'f', 2)
                      .arg(m_walkEye[2], 0, 'f', 2));
    emit walkthroughChanged(true);
    update();
}

// QA R28: exercita o INTEGRADOR do movimento contínuo — insere a tecla no
// conjunto e chama walkStep() N vezes (o mesmo caminho do timer), sem depender
// de tecla física nem do relógio. Requer estar no walk (via --walk antes).
void Viewport3D::qaWalkSim(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 2 || c[0].isEmpty()) return;
    static const std::map<QChar, int> kMap{
        {'W', Qt::Key_W}, {'S', Qt::Key_S}, {'A', Qt::Key_A}, {'D', Qt::Key_D}};
    const auto it = kMap.find(c[0].at(0).toUpper());
    if (it == kMap.end()) return;
    m_walkKeys.insert(it->second);
    const int n = c[1].toInt();
    // R49: dt FIXO aqui de propósito — este harness mede distância
    // exata (W×60 = 2,688 m). Com o relógio real, o laço apertado daria
    // dt≈0 e o teste viraria ruído: o app consertado quebraria a prova.
    for (int i = 0; i < n; ++i) walkStep(0.016);
    m_walkKeys.remove(it->second);
    emit pickInfo(QStringLiteral("walksim: olho @ %1,%2,%3 após %4 passos")
                      .arg(m_walkEye[0], 0, 'f', 3)
                      .arg(m_walkEye[1], 0, 'f', 3)
                      .arg(m_walkEye[2], 0, 'f', 3)
                      .arg(n));
}

// R28: arma o próximo clique pra posicionar a câmera de 1ª pessoa.
void Viewport3D::armPositionCamera() {
    if (m_walk) setWalkthrough(false);   // se já anda, volta pra órbita 1º
    setTool(Tool::Select);               // (isso zera m_armPositionCam)
    m_armPositionCam = true;             // ...então arma DEPOIS
    emit pickInfo(QStringLiteral(
        "Posicionar câmera: clique um ponto do chão (ou piso) — você fica de "
        "pé ali, 1,6 m acima."));
}

// QA: clique de posicionar câmera "nx,ny" (dispara o caminho real do arm).
void Viewport3D::qaPosCam(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 2) return;
    armPositionCamera();
    const QPoint p(int(c[0].toDouble() * width()),
                   int(c[1].toDouble() * height()));
    // simula press+release parados no mesmo ponto (clique)
    m_pressPos = p;
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(p), QPointF(p),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mouseReleaseEvent(&rel);
}

// ---------------------------------------------------------------------------
//  Cena: paredes -> malhas half-edge + linework 2D no chão + grade
// ---------------------------------------------------------------------------
void Viewport3D::buildBoxMesh(const double l0[2], const double l1[2],
                              const double r0[2], const double r1[2],
                              double z0, double z1, int wallNo) {
    if (z1 - z0 < 1e-6) return;
    MeshPart part;
    part.wallNo = wallNo;
    HalfEdgeMesh& m = part.mesh;
    // v0..v3 base (L0,L1,R1,R0) · v4..v7 topo (mesma ordem)
    const Idx v0 = m.addVertex({l0[0], l0[1], z0});
    const Idx v1 = m.addVertex({l1[0], l1[1], z0});
    const Idx v2 = m.addVertex({r1[0], r1[1], z0});
    const Idx v3 = m.addVertex({r0[0], r0[1], z0});
    const Idx v4 = m.addVertex({l0[0], l0[1], z1});
    const Idx v5 = m.addVertex({l1[0], l1[1], z1});
    const Idx v6 = m.addVertex({r1[0], r1[1], z1});
    const Idx v7 = m.addVertex({r0[0], r0[1], z1});
    // CCW vistas DE FORA (normal para fora — o EMPURRAR/PUXAR depende disso)
    m.addFace({v4, v5, v1, v0});   // face esquerda
    m.addFace({v6, v7, v3, v2});   // face direita
    m.addFace({v7, v4, v0, v3});   // arremate (início)
    m.addFace({v5, v6, v2, v1});   // arremate (fim)
    m.addFace({v7, v6, v5, v4});   // tampa superior
    m.addFace({v0, v1, v2, v3});   // base
    m_meshes.push_back(std::move(part));
}

void Viewport3D::rebuildScene() {
    m_meshes.clear();
    m_sketch.clear();
    m_chainActive = false;
    m_wallCount = std::size_t(m_plant.wallCount);

    // R3: as paredes já chegam extrudadas em CAIXAS neutras pelo conector
    for (const PlantScene::Box& b : m_plant.boxes)
        buildBoxMesh(b.l0, b.l1, b.r0, b.r1, b.z0, b.z1, b.wallNo);
    finishScene();
}

// chão/grade/bbox (do doc + das malhas correntes) + arrays + reset de estado
void Viewport3D::finishScene() {
    m_ground.clear();
    m_grid.clear();
    m_selMesh = m_hovMesh = -1;
    m_selFace = m_hovFace = HalfEdgeMesh::kNone;
    m_selEdgeMesh = -1;
    m_selEdge = HalfEdgeMesh::kNone;
    m_hlDirty = true;
    m_undo.clear();
    m_edited = false;
    m_dirtyBase = false;      // R48: base do disco = limpa
    cancelRectStage();
    cancelLineStage();
    cancelMoveStage();
    cancelCircleStage();
    m_selWhole = false;

    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    auto grow = [&](double x, double y) {
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    };

    // R3: o linework do chão já vem tesselado no pacote do conector
    m_ground = m_plant.groundLines;
    for (std::size_t i = 0; i + 2 < m_ground.size(); i += 3)
        grow(double(m_ground[i]), double(m_ground[i + 1]));
    for (const MeshPart& part : m_meshes)
        for (std::size_t v = 0; v < part.mesh.vertexCount(); ++v) {
            const Point3& p = part.mesh.vertex(Idx(v)).p;
            grow(p.x, p.y);
        }

    if (minX > maxX) { minX = -10; maxX = 10; minY = -10; maxY = 10; }

    // grade do terreno (passo cresce com a extensão; ~<=70 linhas por eixo)
    const double margin = std::max(4.0, (maxX - minX) * 0.25);
    const double gx0 = std::floor(minX - margin), gx1 = std::ceil(maxX + margin);
    const double gy0 = std::floor(minY - margin), gy1 = std::ceil(maxY + margin);
    double step = 1.0;
    while ((gx1 - gx0) / step > 70.0 || (gy1 - gy0) / step > 70.0) step *= 2.0;
    for (double x = gx0; x <= gx1 + 1e-9; x += step) {
        m_grid.insert(m_grid.end(), {float(x), float(gy0), 0.0f,
                                     float(x), float(gy1), 0.0f});
    }
    for (double y = gy0; y <= gy1 + 1e-9; y += step) {
        m_grid.insert(m_grid.end(), {float(gx0), float(y), 0.0f,
                                     float(gx1), float(y), 0.0f});
    }

    // enquadramento: centro + raio (inclui a altura das paredes)
    m_center[0] = float((minX + maxX) * 0.5);
    m_center[1] = float((minY + maxY) * 0.5);
    m_center[2] = float(m_wallCount ? m_wallHeight * 0.45 : 0.0);
    const double rx = (maxX - minX) * 0.5, ry = (maxY - minY) * 0.5;
    m_radius = float(std::max(3.0, std::hypot(rx, ry)));

    // eixos globais na origem (X/Y/Z, comprimento proporcional à cena)
    m_axes.clear();
    const float ax = std::max(2.0f, m_radius * 0.35f);
    m_axes.insert(m_axes.end(), {0, 0, 0.002f, ax, 0, 0.002f});
    m_axes.insert(m_axes.end(), {0, 0, 0.002f, 0, ax, 0.002f});
    m_axes.insert(m_axes.end(), {0, 0, 0, 0, 0, ax});

    buildRenderArrays();
    emit pickInfo(QString());
}

void Viewport3D::buildRenderArrays() {
    syncDimAnchors();      // R26: choke point único — re-snap/órfã das cotas
    m_tris.clear();
    m_edges.clear();
    m_sketchInk.clear();
    for (TexBatch& tbat : m_texBatches) tbat.data.clear();
    auto texBatchFor = [&](const QString& name) -> std::vector<float>& {
        for (TexBatch& tbat : m_texBatches)
            if (tbat.tex == name) return tbat.data;
        m_texBatches.push_back({name, {}, nullptr, nullptr, 0});
        return m_texBatches.back().data;
    };
    for (const MeshPart& part : m_meshes) {
        if (part.hidden) continue;
        std::vector<Point3> tris;
        std::vector<Idx> faceOf;
        part.mesh.triangulate(tris, &faceOf);
        // G6: SOFTEN automático — soma das normais por vértice; quando o
        // diedro é suave (<~40°), o canto usa a normal média e o cilindro
        // fica REDONDO; quinas de caixa (90°) continuam vivas.
        using VK = std::tuple<long long, long long, long long>;
        auto vk = [](const Point3& p) {
            return VK{llround(p.x * 1e5), llround(p.y * 1e5),
                      llround(p.z * 1e5)};
        };
        std::map<VK, Vec3> vsum;
        for (Idx ff = 0; ff < Idx(part.mesh.faceCount()); ++ff) {
            const Vec3 fn = part.mesh.faceNormal(ff);
            for (const Idx v : part.mesh.faceVertices(ff)) {
                Vec3& s = vsum[vk(part.mesh.vertex(v).p)];
                s = s + fn;
            }
        }
        auto smoothN = [&](const Point3& p, const Vec3& fn) {
            const auto it = vsum.find(vk(p));
            if (it == vsum.end()) return fn;
            const double l = it->second.length();
            if (l < 1e-9) return fn;
            const Vec3 avg = it->second * (1.0 / l);
            return avg.dot(fn) > 0.766 ? avg : fn;   // cos 40°
        };
        for (std::size_t t = 0; t * 3 < tris.size(); ++t) {
            const Idx f = faceOf[t];
            const Vec3 n = part.mesh.faceNormal(f);
            const auto tf = part.faceTex.find(f);
            if (tf != part.faceTex.end() && m_texLib.count(tf->second)) {
                // face TEXTURIZADA: uv planar pela base da face
                const auto vs = part.mesh.faceVertices(f);
                const Point3 o = part.mesh.vertex(vs[0]).p;
                Vec3 U{1, 0, 0};
                for (std::size_t i = 0; i + 1 < vs.size(); ++i) {
                    U = part.mesh.vertex(vs[i + 1]).p -
                        part.mesh.vertex(vs[i]).p;
                    if (U.lengthSq() > 1e-12) break;
                }
                U = U.normalized();
                const Vec3 V = n.cross(U).normalized();
                const double sc = std::max(0.05, part.texScale);
                std::vector<float>& out = texBatchFor(tf->second);
                for (int k = 0; k < 3; ++k) {
                    const Point3& p = tris[t * 3 + std::size_t(k)];
                    const Vec3 d2 = p - o;
                    out.insert(out.end(),
                               {float(p.x), float(p.y), float(p.z), float(n.x),
                                float(n.y), float(n.z),
                                float(d2.dot(U) / sc), float(d2.dot(V) / sc)});
                }
                continue;
            }
            const auto it = part.faceColors.find(f);
            const float dm = ctxDim(part);   // G5: fora do contexto esmaece
            const float cr = dm * (it != part.faceColors.end() ? it->second[0]
                                                               : kWallColor.x());
            const float cg = dm * (it != part.faceColors.end() ? it->second[1]
                                                               : kWallColor.y());
            const float cb = dm * (it != part.faceColors.end() ? it->second[2]
                                                               : kWallColor.z());
            for (int k = 0; k < 3; ++k) {
                const Point3& p = tris[t * 3 + std::size_t(k)];
                const Vec3 sn = smoothN(p, n);
                m_tris.insert(m_tris.end(),
                              {float(p.x), float(p.y), float(p.z),
                               float(sn.x), float(sn.y), float(sn.z),
                               cr, cg, cb});
            }
        }
        std::vector<Point3> lines;
        part.mesh.edgeLines(lines);
        for (std::size_t i = 0; i + 1 < lines.size(); i += 2) {
            if (!part.hiddenEdges.empty()) {   // R8: Shift+borracha ocultou
                auto ka = vk(lines[i]), kb = vk(lines[i + 1]);
                if (part.hiddenEdges.count(ka < kb ? std::make_pair(ka, kb)
                                                   : std::make_pair(kb, ka)))
                    continue;
            }
            m_edges.insert(m_edges.end(),
                           {float(lines[i].x), float(lines[i].y),
                            float(lines[i].z), float(lines[i + 1].x),
                            float(lines[i + 1].y), float(lines[i + 1].z)});
        }
    }
    // R5: rascunho + guias têm TINTA PRÓPRIA (visível no breu E no claro) —
    // antes iam pro VBO das arestas em nanquim e sumiam no fundo escuro
    for (const auto& [a, b] : m_sketch)
        m_sketchInk.insert(m_sketchInk.end(),
                           {float(a.x), float(a.y), float(a.z),
                            float(b.x), float(b.y), float(b.z)});
    for (const auto& [a, b] : m_guides)
        m_sketchInk.insert(m_sketchInk.end(),
                           {float(a.x), float(a.y), float(a.z),
                            float(b.x), float(b.y), float(b.z)});
    m_sceneDirty = true;
    emit structureChanged();                    // outliner (debounce na janela)
}

QString Viewport3D::partLabel(int i) const {
    if (i < 0 || i >= int(m_meshes.size())) return {};
    const MeshPart& p = m_meshes[std::size_t(i)];
    QString base;
    if (!p.compName.isEmpty())
        base = QStringLiteral("⟨%1⟩ #%2").arg(p.compName).arg(i);
    else if (p.wallNo > 0)
        base = QStringLiteral("Parede %1 · #%2").arg(p.wallNo).arg(i);
    else
        base = QStringLiteral("Sólido #%1").arg(i);
    if (!p.tag.isEmpty()) base += QStringLiteral(" [%1]").arg(p.tag);
    return base;
}

void Viewport3D::setPartHidden(int i, bool h) {
    if (i < 0 || i >= int(m_meshes.size())) return;
    if (m_meshes[std::size_t(i)].hidden == h) return;
    pushUndo();
    m_meshes[std::size_t(i)].hidden = h;
    if (h && m_selMesh == i) {
        m_selMesh = -1;
        m_selFace = HalfEdgeMesh::kNone;
        m_selWhole = false;
        m_hlDirty = true;
    }
    m_edited = true;
    buildRenderArrays();
    update();
}

void Viewport3D::selectPart(int i) {
    if (i < 0 || i >= int(m_meshes.size()) ||
        m_meshes[std::size_t(i)].hidden)
        return;
    m_selMesh = i;
    m_selFace = 0;
    m_selWhole = true;
    m_selEdgeMesh = -1;
    m_selEdge = HalfEdgeMesh::kNone;
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("%1 selecionado (via Estrutura).")
                      .arg(partLabel(i)));
    update();
}

void Viewport3D::uploadScene() {
    // malha das paredes (pos3 + normal3 + cor3)
    if (!m_vaoMesh.isCreated()) m_vaoMesh.create();
    m_vaoMesh.bind();
    if (!m_vboMesh.isCreated()) m_vboMesh.create();
    m_vboMesh.bind();
    m_vboMesh.allocate(m_tris.data(), int(m_tris.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float),
                          reinterpret_cast<void*>(6 * sizeof(float)));
    m_vaoMesh.release();
    m_nMesh = int(m_tris.size() / 9);

    uploadPos3(m_vaoEdge, m_vboEdge, m_edges, this);
    m_nEdge = int(m_edges.size() / 3);
    uploadPos3(m_vaoGround, m_vboGround, m_ground, this);
    m_nGround = int(m_ground.size() / 3);
    uploadPos3(m_vaoSketch, m_vboSketch, m_sketchInk, this);   // R5
    m_nSketch = int(m_sketchInk.size() / 3);
    uploadPos3(m_vaoGrid, m_vboGrid, m_grid, this);
    m_nGrid = int(m_grid.size() / 3);
    uploadPos3(m_vaoAxes, m_vboAxes, m_axes, this);
    m_nAxes = int(m_axes.size() / 3);

    // texturas GL: SÓ as referenciadas por lotes (R11 — biblioteca grande);
    // a imagem cheia é lida do disco na 1ª vez que o material é usado
    for (const TexBatch& ref : m_texBatches) {
        if (ref.data.empty()) continue;
        const auto itref = m_texLib.find(ref.tex);
        if (itref == m_texLib.end() || itref->second.glId) continue;
        TexEntry& te = itref->second;
        const QImage full = te.img.isNull() ? QImage(te.file) : te.img;
        if (full.isNull()) continue;
        const QImage img =
            full.convertToFormat(QImage::Format_RGBA8888).mirrored();
        glGenTextures(1, &te.glId);
        glBindTexture(GL_TEXTURE_2D, te.glId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width(), img.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    for (TexBatch& tbat : m_texBatches) {
        if (!tbat.vao) {
            tbat.vao = new QOpenGLVertexArrayObject(this);
            tbat.vao->create();
            tbat.vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
            tbat.vbo->create();
        }
        tbat.vao->bind();
        tbat.vbo->bind();
        tbat.vbo->allocate(tbat.data.data(),
                           int(tbat.data.size() * sizeof(float)));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<void*>(6 * sizeof(float)));
        tbat.vao->release();
        tbat.count = int(tbat.data.size() / 8);
    }

    m_sceneDirty = false;
}

void Viewport3D::uploadHighlights() {
    std::vector<float> sel, hov;
    m_selIsEdge = false;
    if (m_selEdgeMesh >= 0 && m_selEdgeMesh < int(m_meshes.size())) {
        const HalfEdgeMesh& m = m_meshes[std::size_t(m_selEdgeMesh)].mesh;
        const Point3& a = m.vertex(m.halfEdge(m_selEdge).origin).p;
        const Point3& b =
            m.vertex(m.halfEdge(m.halfEdge(m_selEdge).next).origin).p;
        pushSeg(sel, a, b);
        m_selIsEdge = true;
    } else if (m_selWhole && m_selMesh >= 0 &&
               m_selMesh < int(m_meshes.size())) {
        const HalfEdgeMesh& m = m_meshes[std::size_t(m_selMesh)].mesh;
        for (Idx f = 0; f < Idx(m.faceCount()); ++f) faceTris(m, f, sel);
    } else if (m_selMesh >= 0 && m_selMesh < int(m_meshes.size()))
        faceTris(m_meshes[std::size_t(m_selMesh)].mesh, m_selFace, sel);
    if (m_hovMesh >= 0 && m_hovMesh < int(m_meshes.size()) &&
        !(m_hovMesh == m_selMesh && m_hovFace == m_selFace))
        faceTris(m_meshes[std::size_t(m_hovMesh)].mesh, m_hovFace, hov);
    // G3: a multi-seleção brilha junto (faces avulsas + sólidos da caixa)
    for (const auto& [mi, f] : m_selFacesMulti)
        if (mi >= 0 && mi < int(m_meshes.size()) && !m_selIsEdge)
            faceTris(m_meshes[std::size_t(mi)].mesh, f, sel);
    for (const int mi : m_selSolidsMulti)
        if (mi >= 0 && mi < int(m_meshes.size()) && !m_selIsEdge) {
            const HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
            for (Idx f = 0; f < Idx(m.faceCount()); ++f) faceTris(m, f, sel);
        }
    uploadPos3(m_vaoSel, m_vboSel, sel, this);
    m_nSel = int(sel.size() / 3);
    uploadPos3(m_vaoHov, m_vboHov, hov, this);
    m_nHov = int(hov.size() / 3);
    m_hlDirty = false;

    // R16: o painel INFO da Bandeja acompanha a seleção
    QString info;
    if (!m_selSolidsMulti.empty()) {
        info = QStringLiteral("%1 sólido(s) na multi-seleção")
                   .arg(m_selSolidsMulti.size());
    } else if (m_selEdgeMesh >= 0) {
        info = QStringLiteral("Aresta selecionada (%1)")
                   .arg(partLabel(m_selEdgeMesh));
    } else if (m_selMesh >= 0 && m_selMesh < int(m_meshes.size())) {
        const MeshPart& p = m_meshes[std::size_t(m_selMesh)];
        Point3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
        for (std::size_t v = 0; v < p.mesh.vertexCount(); ++v) {
            const Point3& q = p.mesh.vertex(Idx(v)).p;
            lo.x = std::min(lo.x, q.x); hi.x = std::max(hi.x, q.x);
            lo.y = std::min(lo.y, q.y); hi.y = std::max(hi.y, q.y);
            lo.z = std::min(lo.z, q.z); hi.z = std::max(hi.z, q.z);
        }
        info = QStringLiteral("%1\n%2 faces · %3 × %4 × %5 m")
                   .arg(partLabel(m_selMesh))
                   .arg(p.mesh.faceCount())
                   .arg(hi.x - lo.x, 0, 'f', 2)
                   .arg(hi.y - lo.y, 0, 'f', 2)
                   .arg(hi.z - lo.z, 0, 'f', 2);
        if (!p.group.isEmpty())
            info += QStringLiteral("\nGrupo: %1").arg(p.group);
        if (!p.tag.isEmpty())
            info += QStringLiteral("\nTag: %1").arg(p.tag);
        if (!m_selWhole && m_selFace != HalfEdgeMesh::kNone) {
            info += QStringLiteral("\nFace: %1 m²")
                        .arg(p.mesh.faceArea(m_selFace), 0, 'f', 2);
            const auto tf = p.faceTex.find(m_selFace);
            if (tf != p.faceTex.end())
                info += QStringLiteral(" · %1 (%2 m)")
                            .arg(tf->second)
                            .arg(p.texScale, 0, 'f', 2);
            else {
                const auto fc = p.faceColors.find(m_selFace);
                if (fc != p.faceColors.end())
                    info += QStringLiteral(" · cor %1,%2,%3")
                                .arg(int(fc->second[0] * 255))
                                .arg(int(fc->second[1] * 255))
                                .arg(int(fc->second[2] * 255));
                else
                    info += QStringLiteral(" · washi");
            }
        }
    } else {
        info = QStringLiteral("Nada selecionado");
    }
    emit entityInfo(info);
}

void Viewport3D::uploadGhost() {
    uploadPos3(m_vaoGhost, m_vboGhost, m_ghost, this);
    m_nGhost = int(m_ghost.size() / 3);
    uploadPos3(m_vaoSnap, m_vboSnap, m_snapMark, this);
    m_nSnap = int(m_snapMark.size() / 3);
    m_ghostDirty = false;
}

// ---------------------------------------------------------------------------
//  GL
// ---------------------------------------------------------------------------
void Viewport3D::initializeGL() {
    initializeOpenGLFunctions();
    buildProgram(m_progMesh, kMeshVs, kMeshFs);
    buildProgram(m_progLine, kLineVs, kLineFs);
    buildProgram(m_progBg, kBgVs, kBgFs);
    buildProgram(m_progDepth, kDepthVs, kDepthFs);
    buildProgram(m_progTex, kTexVs, kTexFs);
    buildProgram(m_progGrid, kGridQVs, kGridQFs);   // R5: grade infinita
    {
        static const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
        if (!m_vaoGridQ.isCreated()) m_vaoGridQ.create();
        m_vaoGridQ.bind();
        if (!m_vboGridQ.isCreated()) m_vboGridQ.create();
        m_vboGridQ.bind();
        m_vboGridQ.allocate(quad, sizeof(quad));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                              nullptr);
        m_vaoGridQ.release();
    }

    // shadow map: FBO só de profundidade
    glGenTextures(1, &m_shadowTex);
    glBindTexture(GL_TEXTURE_2D, m_shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowRes,
                 kShadowRes, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &m_shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           m_shadowTex, 0);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

    // quad de fundo em NDC (triangle strip)
    static const float bg[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    m_vaoBg.create(); m_vaoBg.bind();
    m_vboBg.create(); m_vboBg.bind();
    m_vboBg.allocate(bg, sizeof(bg));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    m_vaoBg.release();

    glEnable(GL_MULTISAMPLE);
    m_glReady = true;
}

void Viewport3D::resizeGL(int, int) {}

QMatrix4x4 Viewport3D::viewMatrix() const {
    const float yaw = qDegreesToRadians(m_yaw);
    const float pitch = qDegreesToRadians(m_pitch);
    const QVector3D tgt(m_target[0], m_target[1], m_target[2]);
    const QVector3D dir(std::cos(pitch) * std::cos(yaw),
                        std::cos(pitch) * std::sin(yaw),
                        std::sin(pitch));
    QMatrix4x4 v;
    if (m_walk) {   // R27: olho FIXO no lugar, olhando na direção (1ª pessoa)
        const QVector3D eye(m_walkEye[0], m_walkEye[1], m_walkEye[2]);
        v.lookAt(eye, eye + dir, QVector3D(0, 0, 1));
    } else {
        v.lookAt(tgt + dir * m_dist, tgt, QVector3D(0, 0, 1));
    }
    return v;
}

// R36: a MESMA matemática do viewMatrix, devolvida em mundo pro Fotógrafo.
void Viewport3D::cameraWorld(cad::Point3& eye, cad::Point3& tgt) const {
    const float yaw = qDegreesToRadians(m_yaw);
    const float pitch = qDegreesToRadians(m_pitch);
    const QVector3D dir(std::cos(pitch) * std::cos(yaw),
                        std::cos(pitch) * std::sin(yaw),
                        std::sin(pitch));
    if (m_walk) {
        eye = {m_walkEye[0], m_walkEye[1], m_walkEye[2]};
        tgt = {m_walkEye[0] + dir.x() * 5.0, m_walkEye[1] + dir.y() * 5.0,
               m_walkEye[2] + dir.z() * 5.0};
    } else {
        tgt = {m_target[0], m_target[1], m_target[2]};
        eye = {m_target[0] + dir.x() * m_dist, m_target[1] + dir.y() * m_dist,
               m_target[2] + dir.z() * m_dist};
    }
}

// R37: a MESMA conta do computeSunDir, estática e pura — devolve os ângulos
// Nishita direto (o diálogo do Fotógrafo escolhe hora sem mexer na Bandeja).
void Viewport3D::sunAnglesFor(int month, double hour, double latDeg,
                              double& elevDeg, double& azimDeg) {
    const double deg = 3.14159265358979 / 180.0;
    const double day = month * 30.4 - 15.0;
    const double decl = -23.45 * std::cos((360.0 / 365.0) * (day + 10) * deg);
    const double H = 15.0 * (hour - 12.0);
    const double sinAlt = std::sin(latDeg * deg) * std::sin(decl * deg) +
                          std::cos(latDeg * deg) * std::cos(decl * deg) *
                              std::cos(H * deg);
    const double alt = std::asin(std::clamp(sinAlt, -1.0, 1.0));
    double az = std::atan2(
        std::sin(H * deg),
        std::cos(H * deg) * std::sin(latDeg * deg) -
            std::tan(decl * deg) * std::cos(latDeg * deg));
    const double x = -std::sin(az) * std::cos(alt);
    const double y = -std::cos(az) * std::cos(alt);
    const double z = std::max(0.03, std::sin(alt));
    elevDeg = qRadiansToDegrees(std::asin(std::clamp(z, -1.0, 1.0)));
    azimDeg = qRadiansToDegrees(std::atan2(x, y));
    if (azimDeg < 0) azimDeg += 360.0;
}

// R36: sol da Bandeja em ângulos Nishita (elevação; azimute horário do norte).
// m_sunDirW aponta PARA o sol em mundo (x=leste, y=norte, z=cima).
bool Viewport3D::sunAngles(double& elevDeg, double& azimDeg) const {
    const double x = m_sunDirW.x(), y = m_sunDirW.y(), z = m_sunDirW.z();
    elevDeg = qRadiansToDegrees(std::asin(std::clamp(z, -1.0, 1.0)));
    azimDeg = qRadiansToDegrees(std::atan2(x, y));
    if (azimDeg < 0) azimDeg += 360.0;
    return m_sunOn;
}

QMatrix4x4 Viewport3D::projMatrix() const {
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
    QMatrix4x4 p;
    const float far = std::max(m_dist, m_radius) * 30.0f;
    if (m_ortho && !m_walk) {               // G6: projeção PARALELA (técnica)
        const float h = m_dist * std::tan(m_fov * 0.5f * 3.14159265f / 180.f);
        p.ortho(-h * aspect, h * aspect, -h, h, -far, far);
    } else {
        // R27: no walk o olho encosta nas paredes — near fixo baixo evita que
        // a parede à frente seja recortada pelo near plane escalado pela cena.
        const float near = m_walk ? 0.05f : std::max(0.01f, far * 0.0004f);
        p.perspective(m_fov, aspect, near, far);
    }
    return p;
}

// posição do sol: declinação + ângulo horário -> vetor PARA o sol
void Viewport3D::computeSunDir() {
    const double deg = 3.14159265358979 / 180.0;
    const double day = m_sunMonth * 30.4 - 15.0;
    const double decl = -23.45 * std::cos((360.0 / 365.0) * (day + 10) * deg);
    const double H = 15.0 * (m_sunHour - 12.0);
    const double lat = m_sunLat;
    const double sinAlt = std::sin(lat * deg) * std::sin(decl * deg) +
                          std::cos(lat * deg) * std::cos(decl * deg) *
                              std::cos(H * deg);
    const double alt = std::asin(std::clamp(sinAlt, -1.0, 1.0));
    double az = std::atan2(
        std::sin(H * deg),
        std::cos(H * deg) * std::sin(lat * deg) -
            std::tan(decl * deg) * std::cos(lat * deg));
    // az medido do SUL, positivo p/ oeste -> mundo: x=leste, y=norte, z=cima
    const double x = -std::sin(az) * std::cos(alt);
    const double y = -std::cos(az) * std::cos(alt);
    const double z = std::max(0.03, std::sin(alt));   // nunca abaixo do chão
    m_sunDirW = QVector3D(float(x), float(y), float(z)).normalized();
}

void Viewport3D::renderShadowPass() {
    // ortográfica do sol cobrindo a cena
    const float r = std::max(4.0f, m_radius * 1.35f);
    const QVector3D C(m_center[0], m_center[1], m_center[2]);
    QMatrix4x4 view;
    view.lookAt(C + m_sunDirW * (r * 2.5f), C,
                std::abs(m_sunDirW.z()) > 0.95f ? QVector3D(0, 1, 0)
                                                : QVector3D(0, 0, 1));
    QMatrix4x4 proj;
    proj.ortho(-r, r, -r, r, 0.1f, r * 6.0f);
    m_lightMvp = proj * view;

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glViewport(0, 0, kShadowRes, kShadowRes);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    m_progDepth.bind();
    m_progDepth.setUniformValue("uMvp", m_lightMvp);
    if (m_nMesh) {
        m_vaoMesh.bind();
        glDrawArrays(GL_TRIANGLES, 0, m_nMesh);
        m_vaoMesh.release();
    }
    for (const TexBatch& tbat : m_texBatches)   // texturizadas também sombreiam
        if (tbat.count && tbat.vao) {
            tbat.vao->bind();
            glDrawArrays(GL_TRIANGLES, 0, tbat.count);
            tbat.vao->release();
        }
    m_progDepth.release();
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, int(width() * dpr), int(height() * dpr));
}

// OBJ triangulado (Y-up do Blender fica a cargo do import; escrevemos Z-up).
// R15: agora com .mtl ao lado — as CORES por face viajam junto (usemtl).
int Viewport3D::exportObj(const QString& path) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return 0;
    const QString mtlName =
        QFileInfo(path).completeBaseName() + QStringLiteral(".mtl");
    QTextStream ts(&f);
    ts << "# Zendo — ecossistema Zen (Z-up, metros)\n";
    ts << "mtllib " << mtlName << "\n";
    std::map<std::array<float, 3>, QString> mats;   // cor -> nome do material
    auto matDe = [&](const std::array<float, 3>& c) {
        const auto it = mats.find(c);
        if (it != mats.end()) return it->second;
        const QString nome = QStringLiteral("cor_%1").arg(mats.size());
        mats[c] = nome;
        return nome;
    };
    const std::array<float, 3> kDefWashi{0.918f, 0.894f, 0.839f};
    int vBase = 1, nTris = 0, gi = 0;
    for (const MeshPart& part : m_meshes) {
        if (part.hidden) continue;
        std::vector<Point3> tris;
        std::vector<Idx> faceOf;
        part.mesh.triangulate(tris, &faceOf);
        if (tris.empty()) continue;
        ts << "g " << (part.compName.isEmpty()
                           ? QStringLiteral("solido_%1").arg(gi)
                           : part.compName)
           << "\n";
        ++gi;
        for (const Point3& p : tris)
            ts << "v " << p.x << ' ' << p.y << ' ' << p.z << "\n";
        QString matAtual;
        for (std::size_t t = 0; t * 3 < tris.size(); ++t) {
            const auto fc = part.faceColors.find(faceOf[t]);
            const QString m =
                matDe(fc != part.faceColors.end() ? fc->second : kDefWashi);
            if (m != matAtual) {
                ts << "usemtl " << m << "\n";
                matAtual = m;
            }
            ts << "f " << vBase + int(t * 3) << ' ' << vBase + int(t * 3 + 1)
               << ' ' << vBase + int(t * 3 + 2) << "\n";
            ++nTris;
        }
        vBase += int(tris.size());
    }
    QFile fm(QFileInfo(path).absolutePath() + QStringLiteral("/") + mtlName);
    if (fm.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ms(&fm);
        ms << "# Zendo (cores por face)\n";
        for (const auto& [c, nome] : mats)
            ms << "newmtl " << nome << "\nKd " << c[0] << ' ' << c[1] << ' '
               << c[2] << "\n";
    }
    return nTris;
}

// glTF 2.0: .gltf (JSON) + .bin + imagens ao lado — cores por vértice,
// texturas com UV e materiais PBR; raiz rotacionada Z-up -> Y-up.
int Viewport3D::exportGltf(const QString& path) const {
    const QString binName =
        QFileInfo(path).completeBaseName() + QStringLiteral(".bin");
    const QString dir = QFileInfo(path).absolutePath();
    QByteArray bin;
    QJsonArray accessors, bufferViews, meshes, nodes, materials, textures,
        images, samplers;
    QJsonArray rootChildren;
    std::map<QString, int> matOfTex;    // textura -> índice do material

    auto pushView = [&](const QByteArray& data, int target) {
        const int idx = bufferViews.size();
        bufferViews.append(QJsonObject{{"buffer", 0},
                                       {"byteOffset", bin.size()},
                                       {"byteLength", data.size()},
                                       {"target", target}});
        bin.append(data);
        while (bin.size() % 4) bin.append('\0');
        return idx;
    };
    auto pushAcc = [&](int view, int count, const char* type, bool withMinMax,
                       const float* mn, const float* mx) {
        const int idx = accessors.size();
        QJsonObject a{{"bufferView", view}, {"componentType", 5126},
                      {"count", count}, {"type", type}};
        if (withMinMax) {
            a["min"] = QJsonArray{mn[0], mn[1], mn[2]};
            a["max"] = QJsonArray{mx[0], mx[1], mx[2]};
        }
        accessors.append(a);
        return idx;
    };

    // material base: cores por vértice
    materials.append(QJsonObject{
        {"name", "zen-vertex-color"},
        {"pbrMetallicRoughness",
         QJsonObject{{"baseColorFactor", QJsonArray{1, 1, 1, 1}},
                     {"metallicFactor", 0.0},
                     {"roughnessFactor", 0.9}}}});
    auto matForTex = [&](const QString& tex) {
        const auto it = matOfTex.find(tex);
        if (it != matOfTex.end()) return it->second;
        const auto te = m_texLib.find(tex);
        if (te == m_texLib.end()) return 0;
        QFile::copy(te->second.file, dir + "/" + tex);   // imagem ao lado
        if (samplers.isEmpty())
            samplers.append(QJsonObject{{"wrapS", 10497}, {"wrapT", 10497}});
        const int img = images.size();
        images.append(QJsonObject{{"uri", tex}});
        const int t = textures.size();
        textures.append(QJsonObject{{"sampler", 0}, {"source", img}});
        const int m = materials.size();
        materials.append(QJsonObject{
            {"name", tex},
            {"pbrMetallicRoughness",
             QJsonObject{{"baseColorTexture", QJsonObject{{"index", t}}},
                         {"metallicFactor", 0.0},
                         {"roughnessFactor", 0.9}}}});
        matOfTex[tex] = m;
        return m;
    };

    int nTris = 0;
    for (const MeshPart& part : m_meshes) {
        if (part.hidden) continue;
        std::vector<Point3> tris;
        std::vector<Idx> faceOf;
        part.mesh.triangulate(tris, &faceOf);
        if (tris.empty()) continue;
        // agrupa por textura ("" = cor por vértice)
        std::map<QString, std::vector<std::size_t>> groups;
        for (std::size_t t = 0; t * 3 < tris.size(); ++t) {
            const auto tf = part.faceTex.find(faceOf[t]);
            groups[tf != part.faceTex.end() && m_texLib.count(tf->second)
                       ? tf->second
                       : QString()]
                .push_back(t);
        }
        QJsonArray prims;
        for (const auto& [tex, list] : groups) {
            QByteArray pos, nrm, col, uv;
            float mn[3] = {1e30f, 1e30f, 1e30f};
            float mx[3] = {-1e30f, -1e30f, -1e30f};
            for (const std::size_t t : list) {
                const Idx f = faceOf[t];
                const Vec3 n = part.mesh.faceNormal(f);
                const auto pc = part.faceColors.find(f);
                const float cr =
                    pc != part.faceColors.end() ? pc->second[0] : 0.918f;
                const float cg =
                    pc != part.faceColors.end() ? pc->second[1] : 0.894f;
                const float cb =
                    pc != part.faceColors.end() ? pc->second[2] : 0.839f;
                // base UV planar (igual ao render)
                Point3 o{};
                Vec3 U{1, 0, 0}, V{0, 1, 0};
                if (!tex.isEmpty()) {
                    const auto vs = part.mesh.faceVertices(f);
                    o = part.mesh.vertex(vs[0]).p;
                    for (std::size_t i = 0; i + 1 < vs.size(); ++i) {
                        U = part.mesh.vertex(vs[i + 1]).p -
                            part.mesh.vertex(vs[i]).p;
                        if (U.lengthSq() > 1e-12) break;
                    }
                    U = U.normalized();
                    V = n.cross(U).normalized();
                }
                for (int k = 0; k < 3; ++k) {
                    const Point3& p = tris[t * 3 + std::size_t(k)];
                    const float fx = float(p.x), fy = float(p.y),
                                fz = float(p.z);
                    pos.append(reinterpret_cast<const char*>(&fx), 4);
                    pos.append(reinterpret_cast<const char*>(&fy), 4);
                    pos.append(reinterpret_cast<const char*>(&fz), 4);
                    mn[0] = std::min(mn[0], fx); mx[0] = std::max(mx[0], fx);
                    mn[1] = std::min(mn[1], fy); mx[1] = std::max(mx[1], fy);
                    mn[2] = std::min(mn[2], fz); mx[2] = std::max(mx[2], fz);
                    const float nx = float(n.x), ny = float(n.y),
                                nz = float(n.z);
                    nrm.append(reinterpret_cast<const char*>(&nx), 4);
                    nrm.append(reinterpret_cast<const char*>(&ny), 4);
                    nrm.append(reinterpret_cast<const char*>(&nz), 4);
                    if (tex.isEmpty()) {
                        col.append(reinterpret_cast<const char*>(&cr), 4);
                        col.append(reinterpret_cast<const char*>(&cg), 4);
                        col.append(reinterpret_cast<const char*>(&cb), 4);
                    } else {
                        const double sc = std::max(0.05, part.texScale);
                        const Vec3 d2 = p - o;
                        const float tu = float(d2.dot(U) / sc);
                        const float tv = float(d2.dot(V) / sc);
                        uv.append(reinterpret_cast<const char*>(&tu), 4);
                        uv.append(reinterpret_cast<const char*>(&tv), 4);
                    }
                }
                ++nTris;
            }
            const int count = int(list.size() * 3);
            QJsonObject attrs{
                {"POSITION",
                 pushAcc(pushView(pos, 34962), count, "VEC3", true, mn, mx)},
                {"NORMAL", pushAcc(pushView(nrm, 34962), count, "VEC3", false,
                                   nullptr, nullptr)}};
            QJsonObject prim{{"mode", 4}};
            if (tex.isEmpty()) {
                attrs["COLOR_0"] = pushAcc(pushView(col, 34962), count, "VEC3",
                                           false, nullptr, nullptr);
                prim["material"] = 0;
            } else {
                attrs["TEXCOORD_0"] = pushAcc(pushView(uv, 34962), count,
                                              "VEC2", false, nullptr, nullptr);
                prim["material"] = matForTex(tex);
            }
            prim["attributes"] = attrs;
            prims.append(prim);
        }
        const int meshIdx = meshes.size();
        meshes.append(QJsonObject{{"primitives", prims}});
        const int nodeIdx = nodes.size() + 1;   // 0 é a raiz
        nodes.append(QJsonObject{
            {"mesh", meshIdx},
            {"name", part.compName.isEmpty()
                         ? QStringLiteral("solido_%1").arg(nodeIdx)
                         : part.compName}});
        rootChildren.append(nodeIdx);
    }
    if (nTris == 0) return 0;

    // raiz: Z-up (nosso) -> Y-up (glTF): rotação de -90° em X
    QJsonArray allNodes;
    allNodes.append(QJsonObject{
        {"name", "zendo-zup"},
        {"rotation", QJsonArray{-0.70710678, 0, 0, 0.70710678}},
        {"children", rootChildren}});
    for (const QJsonValue& n : nodes) allNodes.append(n);

    QJsonObject root{
        {"asset", QJsonObject{{"version", "2.0"},
                              {"generator", "Zendo (ecossistema Zen)"}}},
        {"scene", 0},
        {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}},
        {"nodes", allNodes},
        {"meshes", meshes},
        {"materials", materials},
        {"accessors", accessors},
        {"bufferViews", bufferViews},
        {"buffers", QJsonArray{QJsonObject{{"uri", binName},
                                           {"byteLength", bin.size()}}}}};
    if (!textures.isEmpty()) {
        root["textures"] = textures;
        root["images"] = images;
        root["samplers"] = samplers;
    }
    QFile fb(dir + "/" + binName);
    if (!fb.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 0;
    fb.write(bin);
    fb.close();
    QFile fj(path);
    if (!fj.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 0;
    fj.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return nTris;
}

bool Viewport3D::applyTexture(const QString& imagePath, double scaleM) {
    // R38: alvo pode ser MULTI — faces Ctrl+clicadas, caixa de sólidos, ou a
    // seleção única de sempre (o atrito dos "62 sólidos um a um" morreu aqui).
    const std::vector<int> alvos = gestureTargets();
    if (alvos.empty() && m_selFacesMulti.empty()) {
        emit pickInfo(QStringLiteral(
            "Textura: selecione uma face (ou sólidos — caixa/duplo clique)."));
        return false;
    }
    QImage img(imagePath);
    if (img.isNull()) {
        emit pickInfo(QStringLiteral("Não consegui ler a imagem."));
        return false;
    }
    const QString name = QFileInfo(imagePath).fileName();
    if (!m_texLib.count(name))
        m_texLib[name] = {imagePath, img,
                          img.scaled(64, 64, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation),
                          0};
    pushUndo();
    int nAlvos = 0;
    if (!m_selFacesMulti.empty()) {          // faces Ctrl+clicadas
        for (const auto& [mi, f] : m_selFacesMulti) {
            if (mi < 0 || mi >= int(m_meshes.size())) continue;
            m_meshes[std::size_t(mi)].texScale = scaleM;
            m_meshes[std::size_t(mi)].faceTex[f] = name;
            ++nAlvos;
        }
    } else if (alvos.size() == 1 && alvos[0] == m_selMesh && !m_selWhole) {
        MeshPart& part = m_meshes[std::size_t(m_selMesh)];   // uma FACE só
        part.texScale = scaleM;
        part.faceTex[m_selFace] = name;
        nAlvos = 1;
    } else {                                 // sólido(s) inteiro(s)
        for (const int mi : alvos) {
            if (mi < 0 || mi >= int(m_meshes.size())) continue;
            MeshPart& part = m_meshes[std::size_t(mi)];
            part.texScale = scaleM;
            for (Idx f = 0; f < Idx(part.mesh.faceCount()); ++f)
                part.faceTex[f] = name;
            ++nAlvos;
        }
    }
    m_edited = true;
    buildRenderArrays();
    emit pickInfo(QStringLiteral(
                      "Textura \"%1\" aplicada em %2 alvo(s) (ladrilho %3 m).")
                      .arg(name)
                      .arg(nAlvos)
                      .arg(scaleM, 0, 'f', 2));
    update();
    return true;
}

// R38: texturas REFERENCIADAS pelas faces mas sem imagem carregada na lib —
// o silêncio que entrega estudo cinza vira aviso no load.
QStringList Viewport3D::missingTextures() const {
    QStringList out;
    for (const MeshPart& part : m_meshes)
        for (const auto& [f, t] : part.faceTex)
            if (!t.isEmpty() && !m_texLib.count(t) && !out.contains(t))
                out << t;
    return out;
}

// R38: nome→arquivo das texturas USADAS (pro "Salvar como pacote").
std::map<QString, QString> Viewport3D::usedTextureFiles() const {
    std::set<QString> usadas;
    for (const MeshPart& part : m_meshes)
        for (const auto& [f, t] : part.faceTex) usadas.insert(t);
    std::map<QString, QString> out;
    for (const auto& [name, te] : m_texLib)
        if (usadas.count(name)) out[name] = te.file;
    return out;
}

// R38: re-aponta uma textura da lib pro arquivo copiado do pacote.
void Viewport3D::retargetTexture(const QString& name, const QString& newFile) {
    auto it = m_texLib.find(name);
    if (it != m_texLib.end()) it->second.file = newFile;
}

void Viewport3D::setStyle(int s) {
    m_style = std::clamp(s, 0, 2);
    emit pickInfo(m_style == 0   ? QStringLiteral("Estilo: normal.")
                  : m_style == 1 ? QStringLiteral("Estilo: monocromático.")
                                 : QStringLiteral("Estilo: raio-x."));
    update();
}

// R32: acrescenta uma seção à lista (n JÁ unitário). false = lotado.
bool Viewport3D::addSection(const Vec3& nUnit, double d) {
    if (int(m_sections.size()) >= kMaxSections) {
        emit pickInfo(QStringLiteral(
            "Máximo de %1 seções ativas — remova uma (menu Ver) antes.")
                          .arg(kMaxSections));
        return false;
    }
    m_sections.push_back({nUnit, d});
    m_edited = true;                 // R32: seção agora PERSISTE no .zendo
    return true;
}

void Viewport3D::setClip(bool on, char axis, double pos) {
    if (!on) {
        clearSections();
        return;
    }
    const Vec3 n = (axis == 'X' || axis == 'x') ? Vec3{1, 0, 0}
                                                : Vec3{0, 1, 0};
    if (!addSection(n, pos)) return;
    emit pickInfo(QStringLiteral("Seção %1 = %2 m adicionada (%3 ativa(s)).")
                      .arg(QChar(axis).toUpper()).arg(pos, 0, 'f', 2)
                      .arg(m_sections.size()));
    update();
}

// R30/R32: plano de corte GERAL (normal n, offset d) — descarta dot(P,n) > d;
// cada chamada ADICIONA uma seção (todas cortam juntas).
void Viewport3D::setClipPlane(const Vec3& n, double d) {
    const double len = n.length();
    if (len < 1e-9) return;
    const Vec3 u = n * (1.0 / len);
    const double dd = d / len;
    if (!addSection(u, dd)) return;
    // avisa se o plano ficou na BORDA (nenhum vértice do lado descartado — ex.
    // clicou a face externa de uma parede): o corte não remove nada.
    bool cutsSomething = false;
    for (const MeshPart& part : m_meshes) {
        for (std::size_t v = 0; v < part.mesh.vertexCount() && !cutsSomething;
             ++v) {
            const Point3& p = part.mesh.vertex(Idx(v)).p;
            if (u.dot(p) > dd + 1e-4) cutsSomething = true;
        }
        if (cutsSomething) break;
    }
    emit pickInfo(cutsSomething
                      ? QStringLiteral("Seção no plano (normal %1,%2,%3) — "
                                       "%4 ativa(s); menu remove.")
                            .arg(u.x, 0, 'f', 2).arg(u.y, 0, 'f', 2)
                            .arg(u.z, 0, 'f', 2).arg(m_sections.size())
                      : QStringLiteral("Plano na borda externa — nada cortado; "
                                       "clique uma face INTERNA."));
    update();
}

void Viewport3D::removeLastSection() {
    if (m_sections.empty()) {
        emit pickInfo(QStringLiteral("Nenhuma seção ativa."));
        return;
    }
    m_sections.pop_back();
    m_edited = true;
    emit pickInfo(QStringLiteral("Seção removida — %1 ativa(s).")
                      .arg(m_sections.size()));
    update();
}

void Viewport3D::clearSections() {
    if (!m_sections.empty()) m_edited = true;
    m_sections.clear();
    emit pickInfo(QStringLiteral("Seções desligadas."));
    update();
}

QJsonArray Viewport3D::sectionsJson() const {        // R32: persistência
    QJsonArray arr;
    for (const Section& s : m_sections)
        arr.append(QJsonArray{s.n.x, s.n.y, s.n.z, s.d});
    return arr;
}

void Viewport3D::setSectionsJson(const QJsonArray& arr) {
    m_sections.clear();
    for (const QJsonValue& v : arr) {
        const QJsonArray a = v.toArray();
        if (a.size() != 4 || int(m_sections.size()) >= kMaxSections) continue;
        m_sections.push_back({{a[0].toDouble(), a[1].toDouble(),
                               a[2].toDouble()},
                              a[3].toDouble()});
    }
    update();
}

// R32: sobe a lista de seções pro programa (até kMaxSections planos).
void Viewport3D::setClipUniforms(QOpenGLShaderProgram& p) {
    QVector4D arr[kMaxSections];
    const int n = std::min(int(m_sections.size()), kMaxSections);
    for (int i = 0; i < n; ++i)
        arr[i] = QVector4D(float(m_sections[i].n.x), float(m_sections[i].n.y),
                           float(m_sections[i].n.z), float(m_sections[i].d));
    p.setUniformValueArray("uClips", arr, kMaxSections);
    p.setUniformValue("uClipCount", n);
}

// R30: arma o próximo clique pra cortar no plano da face clicada.
void Viewport3D::armClipFace() {
    setTool(Tool::Select);           // (isso zera m_armClipFace)
    m_armClipFace = true;            // ...então arma DEPOIS
    emit pickInfo(QStringLiteral(
        "Seção na face: clique uma face — o corte assume o plano dela."));
}

// QA: arma e sintetiza o clique — exercita o caminho REAL arm→release (não
// duplica o handler), incluindo a ordem do arm (lição R27/R28).
void Viewport3D::qaClipFace(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 2) return;
    armClipFace();
    const QPoint p(int(c[0].toDouble() * width()),
                   int(c[1].toDouble() * height()));
    m_pressPos = p;
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(p), QPointF(p),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mouseReleaseEvent(&rel);
}

void Viewport3D::qaClipPlane(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 4) return;
    setClipPlane({c[0].toDouble(), c[1].toDouble(), c[2].toDouble()},
                 c[3].toDouble());
}

// R34: arma DESLIZAR a última seção — clicar e arrastar move o plano ao
// longo da própria normal (o SketchUp faz isso com o Move na section plane).
void Viewport3D::armClipDrag() {
    if (m_sections.empty()) {
        emit pickInfo(QStringLiteral(
            "Nenhuma seção ativa — crie uma antes de deslizar."));
        return;
    }
    setTool(Tool::Select);           // (isso desarma os outros gestos)
    m_armClipDrag = true;            // ...então arma DEPOIS
    emit pickInfo(QStringLiteral(
        "Deslizar seção: clique e ARRASTE — o plano segue a normal; "
        "solte pra fixar, Esc cancela."));
}

void Viewport3D::slideLastSection(double delta) {
    if (m_sections.empty()) {
        emit pickInfo(QStringLiteral("Nenhuma seção ativa."));
        return;
    }
    m_sections.back().d += delta;
    m_edited = true;
    emit pickInfo(QStringLiteral("Seção deslizada %1 m (d = %2).")
                      .arg(delta, 0, 'f', 3)
                      .arg(m_sections.back().d, 0, 'f', 3));
    update();
}

void Viewport3D::qaClipSlide(double d) { slideLastSection(d); }

// ===========================================================================
//  R41 — ESCADA paramétrica: UM sólido maciço (perfil dente-de-serra
//  extrudado pela largura), espelhos calculados pra ~17,5 cm.
// ===========================================================================
void Viewport3D::armStair(double width, double height, double run) {
    setTool(Tool::Select);           // (desarma os outros gestos)
    m_armStair = true;               // ...então arma DEPOIS (lição R27/R28)
    m_stairStage = 0;
    m_stairW = std::clamp(width, 0.3, 5.0);
    m_stairH = std::clamp(height, 0.4, 12.0);
    m_stairRun = std::clamp(run, 0.15, 0.6);
    emit pickInfo(QStringLiteral(
        "Escada: clique o PÉ da escada no chão (depois a direção de subida)."));
}

void Viewport3D::buildStair(const Point3& origin, const Vec3& dirIn,
                            double width, double height, double run) {
    Vec3 dir{dirIn.x, dirIn.y, 0.0};
    if (dir.length() < 1e-6) {
        emit pickInfo(QStringLiteral("Escada: direção nula."));
        return;
    }
    dir = dir.normalized();
    const Vec3 side{-dir.y, dir.x, 0.0};
    const int n = std::max(2, int(std::lround(height / 0.175)));
    const double rise = height / n;
    // perfil (u ao longo da subida, z): dente-de-serra fechado, 2n+2 pontos
    std::vector<std::pair<double, double>> prof;
    prof.push_back({0.0, 0.0});
    for (int i = 0; i < n; ++i) {
        prof.push_back({i * run, (i + 1) * rise});
        prof.push_back({(i + 1) * run, (i + 1) * rise});
    }
    prof.push_back({n * run, 0.0});
    MeshPart part;
    HalfEdgeMesh& m = part.mesh;
    const auto P = [&](double u, double z, double s) {
        return Point3{origin.x + dir.x * u + side.x * s,
                      origin.y + dir.y * u + side.y * s, origin.z + z};
    };
    std::vector<Idx> lo, hi;
    for (const auto& [u, z] : prof) lo.push_back(m.addVertex(P(u, z, -width / 2)));
    for (const auto& [u, z] : prof) hi.push_back(m.addVertex(P(u, z, +width / 2)));
    // mesmo padrão de winding do empilhado do Ctrl+Pull (R31): base
    // invertida, topo direto, quads laterais lo→hi.
    std::vector<Idx> rev(lo.rbegin(), lo.rend());
    bool ok = m.addFace(rev) != HalfEdgeMesh::kNone &&
              m.addFace(hi) != HalfEdgeMesh::kNone;
    for (std::size_t j = 0; ok && j < prof.size(); ++j) {
        const std::size_t j2 = (j + 1) % prof.size();
        ok = m.addFace({lo[j], lo[j2], hi[j2], hi[j]}) != HalfEdgeMesh::kNone;
    }
    std::string why;
    if (!ok || !m.checkIntegrity(&why)) {
        emit pickInfo(QStringLiteral("Escada falhou na integridade: %1")
                          .arg(QString::fromStdString(why)));
        return;
    }
    // reveste de madeira — a lib fotográfica é PREGUIÇOSA (R11): carrega a
    // imagem agora se ainda não entrou no cache de texturas usadas.
    const QString mad = QStringLiteral("Madeira 095.jpg");
    if (!m_texLib.count(mad)) {
        const QString arq = QCoreApplication::applicationDirPath() +
                            QStringLiteral("/assets/materiais/") + mad;
        const QImage img(arq);
        if (!img.isNull())
            m_texLib[mad] = {arq, img,
                             img.scaled(64, 64, Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation),
                             0};
    }
    for (Idx f = 0; f < Idx(m.faceCount()); ++f) {
        if (m_texLib.count(mad))
            part.faceTex[f] = mad;
        else
            part.faceColors[f] = {0.72f, 0.52f, 0.32f};
    }
    part.texScale = 1.2;
    pushUndo();
    m_meshes.push_back(std::move(part));
    m_edited = true;
    buildRenderArrays();
    emit pickInfo(QStringLiteral(
                      "Escada: %1 degraus (espelho %2 cm, piso %3 cm) — "
                      "1 sólido, integridade OK.")
                      .arg(n)
                      .arg(rise * 100.0, 0, 'f', 1)
                      .arg(run * 100.0, 0, 'f', 0));
    update();
}

void Viewport3D::qaStair(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 7) return;
    buildStair({c[0].toDouble(), c[1].toDouble(), 0.0},
               {c[2].toDouble(), c[3].toDouble(), 0.0}, c[4].toDouble(),
               c[5].toDouble(), c[6].toDouble());
}

// ===========================================================================
//  R43 — GUARDA-CORPO paramétrico: montantes grafite + painel de vidro
//  (cor REF_VIDRO — o Fotógrafo o transforma em vidro real) + corrimão,
//  agrupados pra mover como um só. 2 cliques: início e fim.
// ===========================================================================
void Viewport3D::armGuard(double height, double gap) {
    setTool(Tool::Select);
    m_armGuard = true;
    m_guardStage = 0;
    m_guardH = std::clamp(height, 0.5, 2.0);
    m_guardGap = std::clamp(gap, 0.5, 3.0);
    emit pickInfo(QStringLiteral(
        "Guarda-corpo: clique o INÍCIO (no chão ou em cima de uma laje)."));
}

void Viewport3D::buildGuard(const Point3& a, const Point3& bIn,
                            double height, double gap) {
    const Point3 b{bIn.x, bIn.y, a.z};      // sempre nivelado no z do início
    Vec3 dir{b.x - a.x, b.y - a.y, 0.0};
    const double len = dir.length();
    if (len < 0.2) {
        emit pickInfo(QStringLiteral("Guarda-corpo: comprimento mínimo 20 cm."));
        return;
    }
    dir = dir.normalized();
    const Vec3 side{-dir.y, dir.x, 0.0};
    // caixa alinhada ao vão: u ao longo, s lateral, z absoluto (mesmo
    // winding do makeBox do Ensō-san — u≡x, s≡y num frame direito)
    const auto boxAt = [&](double u0, double u1, double s0, double s1,
                           double z0, double z1) {
        MeshPart part;
        HalfEdgeMesh& m = part.mesh;
        const auto P = [&](double u, double s, double z) {
            return Point3{a.x + dir.x * u + side.x * s,
                          a.y + dir.y * u + side.y * s, z};
        };
        const Idx v0 = m.addVertex(P(u0, s0, z0));
        const Idx v1 = m.addVertex(P(u1, s0, z0));
        const Idx v2 = m.addVertex(P(u1, s1, z0));
        const Idx v3 = m.addVertex(P(u0, s1, z0));
        const Idx v4 = m.addVertex(P(u0, s0, z1));
        const Idx v5 = m.addVertex(P(u1, s0, z1));
        const Idx v6 = m.addVertex(P(u1, s1, z1));
        const Idx v7 = m.addVertex(P(u0, s1, z1));
        bool ok = m.addFace({v3, v2, v1, v0}) != HalfEdgeMesh::kNone &&
                  m.addFace({v4, v5, v6, v7}) != HalfEdgeMesh::kNone &&
                  m.addFace({v0, v1, v5, v4}) != HalfEdgeMesh::kNone &&
                  m.addFace({v2, v3, v7, v6}) != HalfEdgeMesh::kNone &&
                  m.addFace({v1, v2, v6, v5}) != HalfEdgeMesh::kNone &&
                  m.addFace({v3, v0, v4, v7}) != HalfEdgeMesh::kNone;
        std::string why;
        if (!ok || !m.checkIntegrity(&why)) part.mesh = HalfEdgeMesh{};
        return part;
    };
    const std::array<float, 3> grafite{0.235f, 0.235f, 0.251f};
    const std::array<float, 3> vidro{0.659f, 0.780f, 0.816f};   // REF_VIDRO
    std::vector<MeshPart> parts;
    const auto push = [&](MeshPart p, const std::array<float, 3>& cor) {
        if (p.mesh.vertexCount() == 0) return false;
        for (Idx f = 0; f < Idx(p.mesh.faceCount()); ++f) p.faceColors[f] = cor;
        parts.push_back(std::move(p));
        return true;
    };
    const int nSeg = std::max(1, int(std::ceil(len / gap)));
    bool ok = true;
    for (int i = 0; ok && i <= nSeg; ++i) {          // montantes 5×5 cm
        const double u = len * i / nSeg;
        ok = push(boxAt(std::clamp(u - 0.025, 0.0, len - 0.05),
                        std::clamp(u + 0.025, 0.05, len), -0.025, 0.025,
                        a.z, a.z + height - 0.04),
                  grafite);
    }
    // painel de vidro contínuo + corrimão por cima
    ok = ok && push(boxAt(0.02, len - 0.02, -0.010, 0.010, a.z + 0.08,
                          a.z + height - 0.06),
                    vidro);
    ok = ok && push(boxAt(0.0, len, -0.030, 0.030, a.z + height - 0.04,
                          a.z + height),
                    grafite);
    if (!ok) {
        emit pickInfo(
            QStringLiteral("Guarda-corpo falhou na integridade — nada criado."));
        return;
    }
    // grupo com nome único: mover/copiar pega o conjunto inteiro (R4)
    QSet<QString> nomes;
    for (const auto& mp : m_meshes)
        if (mp.group.startsWith(QStringLiteral("Guarda-corpo")))
            nomes.insert(mp.group);
    QString gname = QStringLiteral("Guarda-corpo");
    for (int i = 2; nomes.contains(gname); ++i)
        gname = QStringLiteral("Guarda-corpo %1").arg(i);
    pushUndo();
    for (auto& p : parts) {
        p.group = gname;
        m_meshes.push_back(std::move(p));
    }
    m_edited = true;
    buildRenderArrays();
    emit pickInfo(QStringLiteral("Guarda-corpo: %1 m, %2 montantes + vidro + "
                                 "corrimão — agrupado (⟨%3⟩).")
                      .arg(len, 0, 'f', 2)
                      .arg(nSeg + 1)
                      .arg(gname));
    update();
}

void Viewport3D::qaGuard(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 7) return;
    buildGuard({c[0].toDouble(), c[1].toDouble(), c[4].toDouble()},
               {c[2].toDouble(), c[3].toDouble(), c[4].toDouble()},
               c[5].toDouble(), c[6].toDouble());
}

// ===========================================================================
//  R43 — LAJE COM ABERTURA: moldura fechada (16 vértices / 16 faces, genus 1)
//  construída direto — sem booleana. 4 cliques no chão definem laje e
//  abertura em planta; cota do topo e espessura vêm do diálogo.
// ===========================================================================
void Viewport3D::armSlabHole(double zTop, double thick) {
    setTool(Tool::Select);
    m_armSlab = true;
    m_slabStage = 0;
    m_slabZ = std::clamp(zTop, 0.2, 30.0);
    m_slabTh = std::clamp(thick, 0.05, 1.0);
    emit pickInfo(QStringLiteral(
        "Laje com abertura: clique os 2 CANTOS da laje (depois os 2 da "
        "abertura) — em planta, no chão."));
}

void Viewport3D::buildSlabHole(double x1, double y1, double x2, double y2,
                               double hx1, double hy1, double hx2, double hy2,
                               double zTop, double thick) {
    if (x2 < x1) std::swap(x1, x2);
    if (y2 < y1) std::swap(y1, y2);
    if (hx2 < hx1) std::swap(hx1, hx2);
    if (hy2 < hy1) std::swap(hy1, hy2);
    // abertura precisa caber com folga (lição kEdgeGap: fronteira exata é
    // topologia diferente, não tolerância)
    const double g = 0.005;
    if (hx1 < x1 + g || hx2 > x2 - g || hy1 < y1 + g || hy2 > y2 - g ||
        hx2 - hx1 < 0.05 || hy2 - hy1 < 0.05) {
        emit pickInfo(QStringLiteral(
            "Laje: a abertura precisa ficar DENTRO da laje (folga ≥ 5 mm)."));
        return;
    }
    const double zb = zTop - thick;
    MeshPart part;
    HalfEdgeMesh& m = part.mesh;
    // cantos em ordem CCW vistos de cima: SW, SE, NE, NW
    const double ox[4] = {x1, x2, x2, x1}, oy[4] = {y1, y1, y2, y2};
    const double ix[4] = {hx1, hx2, hx2, hx1}, iy[4] = {hy1, hy1, hy2, hy2};
    Idx o[4], O[4], i_[4], I[4];
    for (int k = 0; k < 4; ++k) {
        o[k] = m.addVertex({ox[k], oy[k], zb});
        O[k] = m.addVertex({ox[k], oy[k], zTop});
        i_[k] = m.addVertex({ix[k], iy[k], zb});
        I[k] = m.addVertex({ix[k], iy[k], zTop});
    }
    // 16 faces: topo em anel (+z), base em anel (−z, anel invertido),
    // 4 laterais externas (pra fora) e 4 paredes da abertura — cuja normal
    // de fora aponta PRA DENTRO do vazio (lição de cavidade da R21/R25).
    bool ok = true;
    for (int k = 0; ok && k < 4; ++k) {
        const int k2 = (k + 1) % 4;
        ok = m.addFace({O[k], O[k2], I[k2], I[k]}) != HalfEdgeMesh::kNone &&
             m.addFace({i_[k], i_[k2], o[k2], o[k]}) != HalfEdgeMesh::kNone &&
             m.addFace({o[k], o[k2], O[k2], O[k]}) != HalfEdgeMesh::kNone &&
             m.addFace({i_[k2], i_[k], I[k], I[k2]}) != HalfEdgeMesh::kNone;
    }
    std::string why;
    if (!ok || !m.checkIntegrity(&why)) {
        emit pickInfo(QStringLiteral("Laje falhou na integridade: %1")
                          .arg(QString::fromStdString(why)));
        return;
    }
    for (Idx f = 0; f < Idx(m.faceCount()); ++f)
        part.faceColors[f] = {0.72f, 0.70f, 0.66f};   // concreto claro
    pushUndo();
    m_meshes.push_back(std::move(part));
    m_edited = true;
    buildRenderArrays();
    emit pickInfo(QStringLiteral(
                      "Laje %1×%2 m (esp. %3 cm) com abertura %4×%5 m — "
                      "integridade OK.")
                      .arg(x2 - x1, 0, 'f', 2)
                      .arg(y2 - y1, 0, 'f', 2)
                      .arg(thick * 100.0, 0, 'f', 0)
                      .arg(hx2 - hx1, 0, 'f', 2)
                      .arg(hy2 - hy1, 0, 'f', 2));
    update();
}

void Viewport3D::qaSlabHole(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 10) return;
    buildSlabHole(c[0].toDouble(), c[1].toDouble(), c[2].toDouble(),
                  c[3].toDouble(), c[4].toDouble(), c[5].toDouble(),
                  c[6].toDouble(), c[7].toDouble(), c[8].toDouble(),
                  c[9].toDouble());
}

// QA do GESTO real (lição R27/R28: flag que seta estado final não testa o
// caminho de entrada): arma e sintetiza press→move→release pelo handler.
void Viewport3D::qaClipDrag(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 4) return;
    armClipDrag();
    const QPoint p0(int(c[0].toDouble() * width()),
                    int(c[1].toDouble() * height()));
    const QPoint p1(int(c[2].toDouble() * width()),
                    int(c[3].toDouble() * height()));
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(p0), QPointF(p0),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove, QPointF(p1), QPointF(p1),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    mouseMoveEvent(&move);
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(p1), QPointF(p1),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mouseReleaseEvent(&rel);
}

QJsonArray Viewport3D::texturesJson(const QString& studyDir) const {
    // R38: persiste SÓ as texturas USADAS pelas faces — a lib inteira (155+)
    // entrava em todo .zendo com caminhos frágeis da máquina (ruído + a
    // razão de entregas quebrarem); a lib do app se registra sozinha no boot.
    std::set<QString> usadas;
    for (const MeshPart& part : m_meshes)
        for (const auto& [f, t] : part.faceTex) usadas.insert(t);
    QJsonArray arr;
    for (const auto& [name, te] : m_texLib)
        if (usadas.count(name))
            arr.append(QJsonObject{
                {"name", name},
                {"file", QDir(studyDir).relativeFilePath(te.file)}});
    return arr;
}

void Viewport3D::setTexturesJson(const QJsonArray& arr,
                                 const QString& studyDir) {
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("name").toString();
        const QString abs =
            QDir(studyDir).absoluteFilePath(o.value("file").toString());
        QImage img(abs);
        if (!name.isEmpty() && !img.isNull())
            m_texLib[name] = {abs, img,
                              img.scaled(64, 64, Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation),
                              0};
    }
    buildRenderArrays();
}

void Viewport3D::setSun(bool on, int month, double hour, double latDeg) {
    m_sunOn = on;
    m_sunMonth = std::clamp(month, 1, 12);
    m_sunHour = std::clamp(hour, 0.0, 24.0);
    m_sunLat = latDeg;
    computeSunDir();
    emit pickInfo(on ? QStringLiteral(
                           "Sol: mês %1, %2h, lat %3° — sombras ligadas.")
                           .arg(m_sunMonth)
                           .arg(m_sunHour, 0, 'f', 1)
                           .arg(m_sunLat, 0, 'f', 1)
                     : QStringLiteral("Sombras desligadas."));
    update();
}

void Viewport3D::paintGL() {
    if (m_sceneDirty) uploadScene();
    if (m_hlDirty) uploadHighlights();
    if (m_ghostDirty) uploadGhost();
    if (m_sunOn) renderShadowPass();

    glDisable(GL_DEPTH_TEST);
    m_progBg.bind();
    m_progBg.setUniformValue("uTop", bgTop());        // R5: Amanhecer/Noite
    m_progBg.setUniformValue("uBottom", bgBottom());
    m_vaoBg.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vaoBg.release();
    m_progBg.release();

    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    const QVector3D eyePos =
        viewMatrix().inverted().map(QVector3D(0, 0, 0));

    {   // R5: grade INFINITA procedural — segue o alvo, esmaece no horizonte
        // R10: SEM escrever depth — o discard das partes transparentes gravava
        // z=0 só nas linhas e elas furavam faces coplanares no chão (visível
        // com material escuro; washi sobre washi escondia).
        glDepthMask(GL_FALSE);
        m_progGrid.bind();
        m_progGrid.setUniformValue("uMvp", mvp);
        m_progGrid.setUniformValue(
            "uCenter", QVector3D(m_target[0], m_target[1], 0.0f));
        m_progGrid.setUniformValue("uSize", std::max(400.0f, m_dist * 40.0f));
        m_progGrid.setUniformValue("uColor", gridInk());
        m_progGrid.setUniformValue("uEye", eyePos);
        m_progGrid.setUniformValue("uFade", std::max(35.0f, m_dist * 3.5f));
        setClipUniforms(m_progGrid);
        m_vaoGridQ.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_vaoGridQ.release();
        m_progGrid.release();
        glDepthMask(GL_TRUE);
    }

    m_progLine.bind();
    m_progLine.setUniformValue("uMvp", mvp);
    setClipUniforms(m_progLine);
    if (m_nAxes >= 6) {                     // eixos globais (X/Y/Z) na origem
        m_vaoAxes.bind();
        glLineWidth(1.6f);
        m_progLine.setUniformValue("uColor", QVector4D(0.63f, 0.40f, 0.31f, 0.9f));
        glDrawArrays(GL_LINES, 0, 2);       // X terracota
        m_progLine.setUniformValue("uColor", QVector4D(0.44f, 0.56f, 0.50f, 0.9f));
        glDrawArrays(GL_LINES, 2, 2);       // Y sálvia
        m_progLine.setUniformValue("uColor", QVector4D(0.37f, 0.49f, 0.59f, 0.9f));
        glDrawArrays(GL_LINES, 4, 2);       // Z azul-cinza
        glLineWidth(1.0f);
        m_vaoAxes.release();
    }
    if (m_nGround) {
        m_progLine.setUniformValue("uColor", groundInkC());
        m_vaoGround.bind(); glDrawArrays(GL_LINES, 0, m_nGround); m_vaoGround.release();
    }
    if (m_nSketch) {                    // R5: o traço do lápis VISÍVEL
        m_progLine.setUniformValue("uColor", sketchInkC());
        glLineWidth(2.0f);
        m_vaoSketch.bind();
        glDrawArrays(GL_LINES, 0, m_nSketch);
        m_vaoSketch.release();
        glLineWidth(1.0f);
    }
    m_progLine.release();

    if (m_nMesh) {
        m_progMesh.bind();
        m_progMesh.setUniformValue("uFogOn", m_fogOn ? 1 : 0);
        m_progMesh.setUniformValue("uFogDens", m_fogDens);
        m_progMesh.setUniformValue("uFogCol", bgBottom());   // R5
        m_progMesh.setUniformValue("uEye", eyePos);
        m_progMesh.setUniformValue("uMvp", mvp);
        m_progMesh.setUniformValue("uSun", m_sunOn ? m_sunDirW : kSunDir);
        m_progMesh.setUniformValue("uShadows", m_sunOn ? 1 : 0);
        m_progMesh.setUniformValue("uLightMvp", m_lightMvp);
        m_progMesh.setUniformValue("uShadowMap", 0);
        m_progMesh.setUniformValue("uStyle", m_style);
        setClipUniforms(m_progMesh);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_shadowTex);
        glEnable(GL_POLYGON_OFFSET_FILL);       // arestas passam na frente
        glPolygonOffset(1.0f, 1.0f);
        if (m_style == 2) glDepthMask(GL_FALSE);    // raio-x
        m_vaoMesh.bind(); glDrawArrays(GL_TRIANGLES, 0, m_nMesh); m_vaoMesh.release();
        glDisable(GL_POLYGON_OFFSET_FILL);
        m_progMesh.release();
    }
    // faces texturizadas (mesma luz/sombra, unidade 1)
    {
        bool any = false;
        for (const TexBatch& tbat : m_texBatches) any |= tbat.count > 0;
        if (any) {
            m_progTex.bind();
            m_progTex.setUniformValue("uFogOn", m_fogOn ? 1 : 0);
            m_progTex.setUniformValue("uFogDens", m_fogDens);
            m_progTex.setUniformValue("uFogCol", bgBottom());   // R5
            m_progTex.setUniformValue("uEye", eyePos);
            m_progTex.setUniformValue("uMvp", mvp);
            m_progTex.setUniformValue("uSun", m_sunOn ? m_sunDirW : kSunDir);
            m_progTex.setUniformValue("uShadows", m_sunOn ? 1 : 0);
            m_progTex.setUniformValue("uLightMvp", m_lightMvp);
            m_progTex.setUniformValue("uShadowMap", 0);
            m_progTex.setUniformValue("uTex", 1);
            m_progTex.setUniformValue("uStyle", m_style);
            setClipUniforms(m_progTex);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_shadowTex);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
            for (const TexBatch& tbat : m_texBatches) {
                if (!tbat.count || !tbat.vao) continue;
                const auto it = m_texLib.find(tbat.tex);
                if (it == m_texLib.end() || !it->second.glId) continue;
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, it->second.glId);
                tbat.vao->bind();
                glDrawArrays(GL_TRIANGLES, 0, tbat.count);
                tbat.vao->release();
            }
            glDisable(GL_POLYGON_OFFSET_FILL);
            glActiveTexture(GL_TEXTURE0);
            m_progTex.release();
        }
    }
    if (m_style == 2) glDepthMask(GL_TRUE);

    // fills de hover/seleção: entre a malha (1,1) e as arestas (sem offset)
    if (m_nHov || m_nSel) {
        m_progLine.bind();
        m_progLine.setUniformValue("uMvp", mvp);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.4f, 0.4f);
        if (m_nHov) {
            m_progLine.setUniformValue("uColor", kHovFill);
            m_vaoHov.bind(); glDrawArrays(GL_TRIANGLES, 0, m_nHov); m_vaoHov.release();
        }
        if (m_nSel) {
            m_progLine.setUniformValue("uColor", kSelFill);
            m_vaoSel.bind();
            if (m_selIsEdge) {
                glLineWidth(4.0f);
                glDrawArrays(GL_LINES, 0, m_nSel);
                glLineWidth(1.0f);
            } else {
                glDrawArrays(GL_TRIANGLES, 0, m_nSel);
            }
            m_vaoSel.release();
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        m_progLine.release();
    }

    if (m_nEdge) {
        m_progLine.bind();
        m_progLine.setUniformValue("uMvp", mvp);
        m_progLine.setUniformValue("uColor", kEdgeColor);
        glLineWidth(1.4f);
        m_vaoEdge.bind(); glDrawArrays(GL_LINES, 0, m_nEdge); m_vaoEdge.release();
        glLineWidth(1.0f);
        m_progLine.release();
    }

    // fantasma do retângulo + marcador de inferência: sempre visíveis
    if (m_nGhost || m_nSnap) {
        glDisable(GL_DEPTH_TEST);
        m_progLine.bind();
        m_progLine.setUniformValue("uMvp", mvp);
        if (m_nGhost) {
            m_progLine.setUniformValue("uColor", kGhostInk);
            glLineWidth(1.6f);
            m_vaoGhost.bind(); glDrawArrays(GL_LINES, 0, m_nGhost); m_vaoGhost.release();
        }
        if (m_nSnap) {
            m_progLine.setUniformValue("uColor", kSnapInk);
            glLineWidth(2.2f);
            m_vaoSnap.bind(); glDrawArrays(GL_LINES, 0, m_nSnap); m_vaoSnap.release();
        }
        glLineWidth(1.0f);
        m_progLine.release();
        glEnable(GL_DEPTH_TEST);
    }
    // G1/G3: overlay QPainter — inferência + caixa de seleção viva
    if (m_infer.kind != 0 || !m_acquired.empty() || m_boxSelecting ||
        !m_guides.empty() || !m_dims.empty()) {
        glBindVertexArray(0);
        QPainter qp(this);
        qp.setRenderHint(QPainter::Antialiasing);
        drawInferOverlay(qp);
        if (m_boxSelecting) {
            const bool window = m_boxEnd.x() >= m_boxStart.x();
            const QRectF r(m_boxStart, m_boxEnd);
            QPen pen(QColor(0xc2, 0xa0, 0x63, 220), 1.2);
            if (!window) pen.setStyle(Qt::DashLine);   // cruzando = tracejada
            qp.setPen(pen);
            qp.setBrush(QColor(194, 160, 99, window ? 26 : 14));
            qp.drawRect(r.normalized());
        }
    }
}

// ---------------------------------------------------------------------------
//  Picking: mouse -> raio no mundo -> Möller–Trumbore nas malhas
// ---------------------------------------------------------------------------
bool Viewport3D::rayAt(const QPoint& pos, Point3& orig, Vec3& dir) const {
    if (width() <= 0 || height() <= 0) return false;
    const float nx = 2.0f * float(pos.x()) / float(width()) - 1.0f;
    const float ny = 1.0f - 2.0f * float(pos.y()) / float(height());
    bool ok = false;
    const QMatrix4x4 inv = (projMatrix() * viewMatrix()).inverted(&ok);
    if (!ok) return false;
    const QVector4D h0 = inv * QVector4D(nx, ny, -1.0f, 1.0f);
    const QVector4D h1 = inv * QVector4D(nx, ny, 1.0f, 1.0f);
    if (std::abs(h0.w()) < 1e-12f || std::abs(h1.w()) < 1e-12f) return false;
    const QVector3D a = h0.toVector3DAffine();
    const QVector3D b = h1.toVector3DAffine();
    orig = {a.x(), a.y(), a.z()};
    dir = Vec3{b.x() - a.x(), b.y() - a.y(), b.z() - a.z()}.normalized();
    return dir.lengthSq() > 1e-12;
}

bool Viewport3D::pickAt(const QPoint& pos, int& meshOut, Idx& faceOut,
                        Point3* hitOut) const {
    Point3 orig;
    Vec3 dir;
    if (!rayAt(pos, orig, dir)) return false;

    bool found = false;
    double bestT = 0.0;
    for (int i = 0; i < int(m_meshes.size()); ++i) {
        if (m_meshes[std::size_t(i)].hidden || !inCtx(i)) continue;
        const auto hit = m_meshes[std::size_t(i)].mesh.pickRay(orig, dir);
        if (hit.hit && (!found || hit.t < bestT)) {
            found = true;
            bestT = hit.t;
            meshOut = i;
            faceOut = hit.face;
            if (hitOut) *hitOut = hit.point;
        }
    }
    return found;
}

QString Viewport3D::faceInfo(int meshIdx, Idx face) const {
    const MeshPart& part = m_meshes[std::size_t(meshIdx)];
    const Vec3 n = part.mesh.faceNormal(face);
    const char* kind = std::abs(n.z) > 0.7 ? (n.z > 0 ? "topo" : "base")
                                           : "face";
    const QString quem = part.wallNo > 0
                             ? QStringLiteral("Parede %1").arg(part.wallNo)
                             : QStringLiteral("Sólido");   // R1: neutro
    // R55: a dica segue a FERRAMENTA. Com o balde na mão, "P: empurrar/puxar"
    // é conselho da ferramenta errada — quem está pintando quer saber o que o
    // clique vai vestir, não como extrudar.
    const QString dica =
        m_tool == Tool::Paint
            ? QStringLiteral("clique pinta · Ctrl = sólido inteiro")
            : QStringLiteral("P: empurrar/puxar");
    return QStringLiteral("%1 · %2 · %3 m² — %4")
        .arg(quem)
        .arg(QString::fromUtf8(kind))
        .arg(part.mesh.faceArea(face), 0, 'f', 2)
        .arg(dica);
}

void Viewport3D::selectAt(const QPoint& pos) {
    m_selEdgeMesh = -1;
    m_selEdge = HalfEdgeMesh::kNone;
    m_selWhole = false;
    // aresta ganha quando o clique passa raspando nela (como no SketchUp)
    int emi = -1;
    Idx ehe = HalfEdgeMesh::kNone;
    if (pickEdgeAt(pos, emi, ehe)) {
        m_selEdgeMesh = emi;
        m_selEdge = ehe;
        m_selMesh = -1;
        m_selFace = HalfEdgeMesh::kNone;
        const HalfEdgeMesh& m = m_meshes[std::size_t(emi)].mesh;
        const Point3& a = m.vertex(m.halfEdge(ehe).origin).p;
        const Point3& b = m.vertex(m.halfEdge(m.halfEdge(ehe).next).origin).p;
        emit pickInfo(QStringLiteral(
                          "Aresta · %1 m — Delete: apagar (funde as faces)")
                          .arg((b - a).length(), 0, 'f', 2));
        m_hlDirty = true;
        update();
        return;
    }
    int mi = -1;
    Idx f = HalfEdgeMesh::kNone;
    if (pickAt(pos, mi, f)) {
        m_selMesh = mi;
        m_selFace = f;
        emit pickInfo(faceInfo(mi, f));
    } else {
        m_selMesh = -1;
        m_selFace = HalfEdgeMesh::kNone;
        emit pickInfo(QString());
    }
    m_hlDirty = true;
    update();
}

// ---------------------------------------------------------------------------
//  Edição de estudo: undo por snapshot + push/pull
// ---------------------------------------------------------------------------
void Viewport3D::pushUndo() {
    m_undo.push_back({m_meshes, m_sketch, m_dims});   // malhas + rascunho + cotas
    if (int(m_undo.size()) > kMaxUndo)
        m_undo.erase(m_undo.begin());
    m_redo.clear();                            // edição nova invalida o refazer
}

bool Viewport3D::undoLast() {
    if (m_undo.empty()) return false;
    m_redo.push_back({m_meshes, m_sketch, m_dims});   // G2: desfeito é refazível
    m_meshes = std::move(m_undo.back().meshes);
    m_sketch = std::move(m_undo.back().sketch);
    m_dims = std::move(m_undo.back().dims);     // R29: cota volta junto
    m_undo.pop_back();
    // R48: base recuperada continua suja mesmo com a pilha vazia — ela
    // não existe em disco nenhum. (Sem o m_dirtyBase, um Ctrl+Z depois
    // de recuperar zerava isto e o app fechava sem perguntar.)
    m_edited = m_dirtyBase || !m_undo.empty();
    // índices podem ter mudado: limpa seleção/hover/etapas
    m_selMesh = m_hovMesh = -1;
    m_selFace = m_hovFace = HalfEdgeMesh::kNone;
    m_selEdgeMesh = -1;
    m_selEdge = HalfEdgeMesh::kNone;
    m_selWhole = false;
    cancelRectStage();
    cancelLineStage();
    cancelMoveStage();
    cancelCircleStage();
    m_hlDirty = true;
    buildRenderArrays();
    emit pickInfo(QString());
    update();
    return true;
}

// G2: REFAZER — restaura o topo da pilha espelho (sem passar por pushUndo,
// que a limparia); o estado corrente volta pra pilha de desfazer.
bool Viewport3D::redoLast() {
    if (m_redo.empty()) return false;
    m_undo.push_back({m_meshes, m_sketch, m_dims});
    m_meshes = std::move(m_redo.back().meshes);
    m_sketch = std::move(m_redo.back().sketch);
    m_dims = std::move(m_redo.back().dims);     // R29: cota refaz junto
    m_redo.pop_back();
    m_edited = true;
    m_selMesh = m_hovMesh = -1;
    m_selFace = m_hovFace = HalfEdgeMesh::kNone;
    m_selEdgeMesh = -1;
    m_selEdge = HalfEdgeMesh::kNone;
    m_selWhole = false;
    m_hlDirty = true;
    buildRenderArrays();
    emit pickInfo(QString());
    update();
    return true;
}

// G2: BORRACHA — apaga a aresta sob o cursor (tolerância em px de tela).
// Rascunho do lápis some direto; aresta de sólido DISSOLVE quando as duas
// faces são coplanares (senão ela segura o volume e fica).
bool Viewport3D::eraseAt(const QPoint& pos, bool hide) {
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    const QPointF m(pos);
    constexpr double kTol2 = 8.0 * 8.0;
    auto hit2d = [&](const Point3& a, const Point3& b) {
        QPointF sa, sb;
        if (!toScreen(mvp, a, sa) || !toScreen(mvp, b, sb)) return false;
        const QPointF ab = sb - sa;
        const double ll = ab.x() * ab.x() + ab.y() * ab.y();
        double t = ll > 1e-12 ? ((m.x() - sa.x()) * ab.x() +
                                 (m.y() - sa.y()) * ab.y()) / ll
                              : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const QPointF q = sa + ab * t - m;
        return q.x() * q.x() + q.y() * q.y() < kTol2;
    };
    for (std::size_t i = 0; i < m_sketch.size(); ++i) {
        if (!hit2d(m_sketch[i].first, m_sketch[i].second)) continue;
        m_sketch.erase(m_sketch.begin() + long(i));
        m_erasedAny = true;
        m_edited = true;
        buildRenderArrays();
        emit pickInfo(QStringLiteral("Borracha: aresta do rascunho apagada."));
        update();
        return true;
    }
    for (std::size_t i = 0; i < m_guides.size(); ++i) {   // guias (G4)
        if (!hit2d(m_guides[i].first, m_guides[i].second)) continue;
        m_guides.erase(m_guides.begin() + long(i));
        m_erasedAny = true;
        buildRenderArrays();
        emit pickInfo(QStringLiteral("Borracha: guia apagada."));
        update();
        return true;
    }
    for (std::size_t i = 0; i < m_dims.size(); ++i) {     // cotas (R26)
        QPointF da, db;
        if (!dimScreenLine(m_dims[i], mvp, da, db)) continue;
        const QPointF ab = db - da;
        const double ll = ab.x() * ab.x() + ab.y() * ab.y();
        double t = ll > 1e-12
                       ? ((m.x() - da.x()) * ab.x() + (m.y() - da.y()) * ab.y()) / ll
                       : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const QPointF q = da + ab * t - m;
        if (q.x() * q.x() + q.y() * q.y() >= kTol2) continue;
        m_dims.erase(m_dims.begin() + long(i));
        m_erasedAny = true;
        buildRenderArrays();
        emit pickInfo(QStringLiteral("Borracha: cota apagada."));
        update();
        return true;
    }
    for (int mi = 0; mi < int(m_meshes.size()); ++mi) {
        MeshPart& part = m_meshes[std::size_t(mi)];
        if (part.hidden || !inCtx(mi)) continue;
        HalfEdgeMesh& msh = part.mesh;
        for (Idx h = 0; h < Idx(msh.halfEdgeCount()); ++h) {
            const HalfEdgeMesh::HalfEdge& he = msh.halfEdge(h);
            if (he.twin != HalfEdgeMesh::kNone && he.twin < h) continue;
            if (msh.isBridge(h)) continue;
            const Point3& a = msh.vertex(he.origin).p;
            const Point3& b = msh.vertex(msh.halfEdge(he.next).origin).p;
            if (!hit2d(a, b)) continue;
            if (hide) {                  // R8: Shift OCULTA a aresta
                auto k = [](const Point3& q) {
                    return std::tuple{llround(q.x * 1e5), llround(q.y * 1e5),
                                      llround(q.z * 1e5)};
                };
                auto ka = k(a), kb = k(b);
                part.hiddenEdges.insert(ka < kb ? std::make_pair(ka, kb)
                                                : std::make_pair(kb, ka));
                m_erasedAny = true;
                m_edited = true;
                buildRenderArrays();
                emit pickInfo(QStringLiteral(
                    "Aresta OCULTADA (a geometria fica) — Shift+borracha."));
                update();
                return true;
            }
            if (he.twin == HalfEdgeMesh::kNone) {
                emit pickInfo(QStringLiteral(
                    "Borracha: aresta de borda — não dá pra dissolver."));
                return false;
            }
            const Vec3 na = msh.faceNormal(he.face);
            const Vec3 nb = msh.faceNormal(msh.halfEdge(he.twin).face);
            if (na.dot(nb) < 0.9999) {
                emit pickInfo(QStringLiteral(
                    "Borracha: essa aresta SEGURA o volume (faces em ângulo) "
                    "— use Del para apagar o sólido."));
                return false;
            }
            Idx gone = HalfEdgeMesh::kNone;
            if (!msh.dissolveEdge(h, &gone)) {
                emit pickInfo(QStringLiteral(
                    "Borracha: essa aresta não pode ser dissolvida."));
                return false;
            }
            std::map<Idx, std::array<float, 3>> nc;   // remapeia cores
            for (const auto& [f, col] : part.faceColors) {
                if (f == gone) continue;
                nc[f > gone ? f - 1 : f] = col;
            }
            part.faceColors = std::move(nc);
            std::map<Idx, QString> nt;                // e texturas por face
            for (const auto& [f, t] : part.faceTex) {
                if (f == gone) continue;
                nt[f > gone ? f - 1 : f] = t;
            }
            part.faceTex = std::move(nt);
            m_selMesh = -1;
            m_selFace = HalfEdgeMesh::kNone;
            m_selEdgeMesh = -1;
            m_selEdge = HalfEdgeMesh::kNone;
            m_erasedAny = true;
            m_edited = true;
            m_hlDirty = true;
            buildRenderArrays();
            emit pickInfo(QStringLiteral(
                "Borracha: aresta dissolvida — as faces viraram UMA."));
            update();
            return true;
        }
    }
    return false;
}

// G2: AUTOFOLD — depois de mover vértices, toda face vizinha que ficou
// NÃO-PLANA ganha vincos (diagonais a partir do vértice movido) até que
// cada pedaço volte a ser plano. É o que mantém o sólido honesto.
void Viewport3D::autofold(HalfEdgeMesh& m, const std::vector<Idx>& moved) {
    auto isMoved = [&](Idx v) {
        return std::find(moved.begin(), moved.end(), v) != moved.end();
    };
    std::vector<Idx> faces;
    for (Idx h = 0; h < Idx(m.halfEdgeCount()); ++h) {
        if (!isMoved(m.halfEdge(h).origin)) continue;
        const Idx f = m.halfEdge(h).face;
        if (f != HalfEdgeMesh::kNone &&
            std::find(faces.begin(), faces.end(), f) == faces.end())
            faces.push_back(f);
    }
    auto planar = [&](Idx f) {
        const auto vs = m.faceVertices(f);
        if (vs.size() <= 3) return true;
        const Vec3 n = m.faceNormal(f);
        const Point3 c = m.faceCentroid(f);
        for (Idx v : vs)
            if (std::abs((m.vertex(v).p - c).dot(n)) > 1e-6) return false;
        return true;
    };
    for (std::size_t qi = 0; qi < faces.size() && qi < 64; ++qi) {
        Idx f = faces[qi];
        for (int guard = 0; guard < 16 && !planar(f); ++guard) {
            const auto vs = m.faceVertices(f);
            std::size_t mp = 0;
            for (std::size_t i = 0; i < vs.size(); ++i)
                if (isMoved(vs[i])) { mp = i; break; }
            const Idx nf = m.splitFace(f, vs[mp], vs[(mp + 2) % vs.size()]);
            if (nf == HalfEdgeMesh::kNone) break;
            faces.push_back(nf);          // a outra metade também se checa
        }
    }
}

void Viewport3D::deleteSelected() {
    if (!m_selSolidsMulti.empty()) {       // G3: apaga a multi inteira
        pushUndo();
        std::vector<int> order = m_selSolidsMulti;
        std::sort(order.rbegin(), order.rend());
        for (const int mi : order)
            if (mi >= 0 && mi < int(m_meshes.size()))
                m_meshes.erase(m_meshes.begin() + mi);
        const std::size_t n = order.size();
        m_selSolidsMulti.clear();
        m_selFacesMulti.clear();
        m_selMesh = -1;
        m_selFace = HalfEdgeMesh::kNone;
        m_selWhole = false;
        m_edited = true;
        m_hlDirty = true;
        buildRenderArrays();
        emit structureChanged();
        emit pickInfo(QStringLiteral("%1 sólido(s) apagado(s) — Ctrl+Z "
                                     "desfaz.").arg(n));
        update();
        return;
    }
    if (m_selEdgeMesh >= 0) {              // aresta: dissolve (funde as faces)
        pushUndo();
        MeshPart& part = m_meshes[std::size_t(m_selEdgeMesh)];
        Idx gone = HalfEdgeMesh::kNone;
        if (!part.mesh.dissolveEdge(m_selEdge, &gone)) {
            m_undo.pop_back();
            emit pickInfo(QStringLiteral(
                "Essa aresta não pode ser dissolvida (borda do sólido)."));
            return;
        }
        // remapeia as cores (face `gone` saiu; índices acima descem 1)
        std::map<Idx, std::array<float, 3>> nc;
        for (const auto& [f, c] : part.faceColors) {
            if (f == gone) continue;
            nc[f > gone ? f - 1 : f] = c;
        }
        part.faceColors = std::move(nc);
        m_selEdgeMesh = -1;
        m_selEdge = HalfEdgeMesh::kNone;
        m_edited = true;
        buildRenderArrays();
        m_hlDirty = true;
        emit pickInfo(QStringLiteral("Linha apagada — faces fundidas."));
        update();
        return;
    }
    if (m_selMesh < 0) {
        emit pickInfo(QStringLiteral("Selecione uma face ou aresta primeiro."));
        return;
    }
    pushUndo();
    m_meshes.erase(m_meshes.begin() + m_selMesh);
    m_selMesh = m_hovMesh = -1;
    m_selFace = m_hovFace = HalfEdgeMesh::kNone;
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Sólido apagado — Ctrl+Z desfaz."));
    update();
}

bool Viewport3D::pushPullSelected(double dist) {
    if (m_selWhole) {
        emit pickInfo(QStringLiteral(
            "Empurrar/Puxar age numa FACE — clique simples nela primeiro."));
        return false;
    }
    if (m_selMesh < 0 || m_selMesh >= int(m_meshes.size()) ||
        std::abs(dist) < 1e-9)
        return false;
    pushUndo();
    HalfEdgeMesh& mesh = m_meshes[std::size_t(m_selMesh)].mesh;
    // R31: carona da cota no PULL — âncoras nos vértices da face puxada seguem
    // a face (delta = normal×dist). O extrudeFace deixa os vértices originais
    // no anel e cria NOVOS no topo — sem isto a cota ficava "válida mas velha"
    // (media até o anel antigo). Coletar ANTES; aplicar só se a extrusão vingou.
    std::vector<Point3> faceVerts;
    for (const Idx v : mesh.faceVertices(m_selFace))
        faceVerts.push_back(mesh.vertex(v).p);
    const Vec3 dvec = mesh.faceNormal(m_selFace) * dist;
    const Idx cap = mesh.extrudeFace(m_selFace, dist);
    std::string why;
    if (cap == HalfEdgeMesh::kNone || !mesh.checkIntegrity(&why)) {
        undoLast();                     // não deve acontecer; restaura o snapshot
        return false;
    }
    transformDimAnchors(faceVerts,
                        [&dvec](const Point3& p) { return p + dvec; });
    m_selFace = cap;                    // a tampa preserva o id — seleção segue
    m_lastOp = {3, m_selMesh, cap, {}, {}, {}, dist, 0.0};   // VCB refaz
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(faceInfo(m_selMesh, m_selFace));
    update();
    return true;
}

bool Viewport3D::paintSelected(const QColor& c) {
    if (m_selMesh < 0 || m_selMesh >= int(m_meshes.size()) || !c.isValid())
        return false;
    pushUndo();
    MeshPart& part = m_meshes[std::size_t(m_selMesh)];
    const std::array<float, 3> col{float(c.redF()), float(c.greenF()),
                                   float(c.blueF())};
    if (m_selWhole) {                       // sólido inteiro pintado de uma vez
        for (Idx f = 0; f < Idx(part.mesh.faceCount()); ++f)
            part.faceColors[f] = col;
    } else {
        part.faceColors[m_selFace] = col;
    }
    m_edited = true;
    buildRenderArrays();
    update();
    return true;
}

// ---------------------------------------------------------------------------
//  Documento de estudo (.zendo): malhas como sopa de polígonos + cores
// ---------------------------------------------------------------------------
void Viewport3D::cameraState(float& yaw, float& pitch, float& dist,
                             float tgt[3]) const {
    yaw = m_yaw; pitch = m_pitch; dist = m_dist;
    tgt[0] = m_target[0]; tgt[1] = m_target[1]; tgt[2] = m_target[2];
}

void Viewport3D::setCameraState(float yaw, float pitch, float dist,
                                const float tgt[3]) {
    m_yaw = yaw;
    m_pitch = std::clamp(pitch, -10.0f, 89.0f);
    m_dist = std::max(0.5f, dist);
    m_target[0] = tgt[0]; m_target[1] = tgt[1]; m_target[2] = tgt[2];
    update();
}

QJsonArray Viewport3D::studyMeshes() const {
    QJsonArray arr;
    for (const MeshPart& part : m_meshes) {
        QJsonObject o;
        o["wallNo"] = part.wallNo;
        if (part.hidden) o["hidden"] = true;
        if (!part.compName.isEmpty()) o["comp"] = part.compName;
        if (!part.group.isEmpty()) o["group"] = part.group;   // G5
        if (!part.tag.isEmpty()) o["tag"] = part.tag;         // G5
        if (!part.hiddenEdges.empty()) {                      // R8
            QJsonArray he;
            for (const auto& [ka, kb] : part.hiddenEdges)
                he.append(QJsonArray{double(std::get<0>(ka)) / 1e5,
                                     double(std::get<1>(ka)) / 1e5,
                                     double(std::get<2>(ka)) / 1e5,
                                     double(std::get<0>(kb)) / 1e5,
                                     double(std::get<1>(kb)) / 1e5,
                                     double(std::get<2>(kb)) / 1e5});
            o["hiddenEdges"] = he;
        }
        if (!part.faceTex.empty()) {
            QJsonArray ft;
            for (const auto& [f, t] : part.faceTex)
                ft.append(QJsonObject{{"f", int(f)}, {"t", t}});
            o["faceTex"] = ft;
            o["texScale"] = part.texScale;
        }
        QJsonArray verts;
        for (std::size_t v = 0; v < part.mesh.vertexCount(); ++v) {
            const Point3& p = part.mesh.vertex(Idx(v)).p;
            verts.append(QJsonArray{p.x, p.y, p.z});
        }
        o["verts"] = verts;
        QJsonArray faces;
        for (std::size_t f = 0; f < part.mesh.faceCount(); ++f) {
            QJsonArray loop;
            for (const Idx v : part.mesh.faceVertices(Idx(f)))
                loop.append(int(v));
            faces.append(loop);
        }
        o["faces"] = faces;
        if (!part.faceColors.empty()) {
            QJsonArray cols;
            for (const auto& [f, c] : part.faceColors) {
                cols.append(QJsonObject{
                    {"f", int(f)},
                    {"c", QJsonArray{int(c[0] * 255.0f + 0.5f),
                                     int(c[1] * 255.0f + 0.5f),
                                     int(c[2] * 255.0f + 0.5f)}}});
            }
            o["colors"] = cols;
        }
        arr.append(o);
    }
    return arr;
}

bool Viewport3D::setStudy(const PlantScene& plant, const QJsonArray& meshes,
                          double wallHeightM) {
    std::vector<MeshPart> parts;
    std::size_t maxWall = 0;
    for (const QJsonValue& mv : meshes) {
        const QJsonObject o = mv.toObject();
        MeshPart part;
        part.wallNo = o.value("wallNo").toInt(0);
        part.hidden = o.value("hidden").toBool(false);
        part.compName = o.value("comp").toString();
        part.group = o.value("group").toString();     // G5
        part.tag = o.value("tag").toString();         // G5
        for (const QJsonValue& hv : o.value("hiddenEdges").toArray()) {   // R8
            const QJsonArray a = hv.toArray();
            if (a.size() != 6) continue;
            auto mk = [&](int i) {
                return std::tuple{llround(a[i].toDouble() * 1e5),
                                  llround(a[i + 1].toDouble() * 1e5),
                                  llround(a[i + 2].toDouble() * 1e5)};
            };
            auto ka = mk(0), kb = mk(3);
            part.hiddenEdges.insert(ka < kb ? std::make_pair(ka, kb)
                                            : std::make_pair(kb, ka));
        }
        part.texScale = o.value("texScale").toDouble(1.0);
        for (const QJsonValue& fv : o.value("faceTex").toArray()) {
            const QJsonObject fo = fv.toObject();
            part.faceTex[Idx(fo.value("f").toInt())] =
                fo.value("t").toString();
        }
        maxWall = std::max(maxWall, std::size_t(std::max(0, part.wallNo)));
        for (const QJsonValue& vv : o.value("verts").toArray()) {
            const QJsonArray a = vv.toArray();
            part.mesh.addVertex({a.at(0).toDouble(), a.at(1).toDouble(),
                                 a.at(2).toDouble()});
        }
        for (const QJsonValue& fv : o.value("faces").toArray()) {
            std::vector<Idx> loop;
            for (const QJsonValue& iv : fv.toArray())
                loop.push_back(Idx(iv.toInt()));
            if (part.mesh.addFace(loop) == HalfEdgeMesh::kNone) return false;
        }
        std::string why;
        if (!part.mesh.checkIntegrity(&why)) return false;
        for (const QJsonValue& cv : o.value("colors").toArray()) {
            const QJsonObject co = cv.toObject();
            const QJsonArray c = co.value("c").toArray();
            part.faceColors[Idx(co.value("f").toInt())] =
                {float(c.at(0).toInt()) / 255.0f,
                 float(c.at(1).toInt()) / 255.0f,
                 float(c.at(2).toInt()) / 255.0f};
        }
        parts.push_back(std::move(part));
    }
    m_plant = plant;                 // R3: pacote neutro (linhas do chão)
    if (wallHeightM >= 0.5) m_wallHeight = wallHeightM;
    m_meshes = std::move(parts);
    m_sketch.clear();
    m_chainActive = false;
    m_wallCount = maxWall;
    finishScene();
    resetCamera();
    update();
    return true;
}

// ---------------------------------------------------------------------------
//  Ferramenta RETÂNGULO: desenhar na face, com inferência
// ---------------------------------------------------------------------------
// R34 (lição da R30 cumprida): a 3ª "arma de clique" nasceu → helper ÚNICO.
// Toda flag de "arma o próximo clique/arrasto" É limpa aqui, e este helper é
// chamado em setTool, setWalkthrough(true) e no Esc — os 3 pontos da lição.
void Viewport3D::disarmGestures() {
    m_armPositionCam = false;   // R28
    m_armClipFace = false;      // R30
    m_armClipDrag = false;      // R34
    m_armStair = false;         // R41
    m_stairStage = 0;
    m_armGuard = false;         // R43
    m_guardStage = 0;
    m_armSlab = false;          // R43
    m_slabStage = 0;
}

void Viewport3D::setTool(Tool t) {
    disarmGestures();           // pegar qualquer ferramenta desarma gestos
    if (m_tool == t) return;
    m_tool = t;
    if (t != Tool::Select) m_lastToolUsed = t;   // R4: Enter repete
    cancelRectStage();
    cancelLineStage();
    cancelMoveStage();
    cancelCircleStage();
    setCursor(t == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    if (t == Tool::Rect)
        emit pickInfo(QStringLiteral(
            "Retângulo: clique o 1º canto sobre uma face — ou no CHÃO, para "
            "um sólido novo (Esc sai)"));
    else if (t == Tool::Line)
        emit pickInfo(QStringLiteral(
            "Linha: clique o 1º ponto numa ARESTA da face (a linha divide a "
            "face; Esc sai)"));
    else if (t == Tool::Move)
        emit pickInfo(QStringLiteral(
                          "%1: clique o PONTO-BASE (Esc cancela).")
                          .arg(m_moveCopy ? QStringLiteral("Copiar sólido")
                                          : QStringLiteral("Mover sólido")));
    else if (t == Tool::Circle)
        emit pickInfo(QStringLiteral(
            "Círculo: clique o CENTRO numa face — ou no chão (Esc sai)"));
    else if (t == Tool::Pull)
        emit pickInfo(QStringLiteral(
            "Empurrar/Puxar: ARRASTE uma face pela normal — solte e digite a "
            "medida exata (Esc sai)"));
    else if (t == Tool::Erase)
        emit pickInfo(QStringLiteral(
            "Borracha: clique/ARRASTE sobre arestas — rascunho apaga, faces "
            "coplanares fundem (Esc sai)"));
    else if (t == Tool::Tape)
        emit pickInfo(QStringLiteral(
            "Fita métrica: clique 2 pontos (a inferência guia) — mede e "
            "deixa uma GUIA (Esc sai)"));
    else if (t == Tool::Paint)
        emit pickInfo(QStringLiteral(
            "BALDE: clique nas faces pra pintar — Ctrl = sólido inteiro · "
            "Alt = conta-gotas · a paleta troca o material (Esc sai)"));
    else if (t == Tool::Arc)
        emit pickInfo(QStringLiteral(
            "ARCO: clique os 2 pontos da corda, depois CURVE e clique — "
            "ou digite o raio (Esc sai)"));
    else if (t == Tool::Rotate)
        emit pickInfo(QStringLiteral(
            "TRANSFERIDOR: clique o CENTRO, a referência, e GIRE (ímã de "
            "15°; Ctrl copia; digite o ângulo) — Esc sai"));
    else if (t == Tool::Scale)
        emit pickInfo(QStringLiteral(
            "ESCALA: clique um ponto-base e afaste/aproxime do centro — "
            "setas travam eixo; ou digite o fator (Esc sai)"));
    else if (t == Tool::Offset)
        emit pickInfo(QStringLiteral(
            "OFFSET: clique a face, ARRASTE pra dentro e clique — digite a "
            "distância; duplo clique repete a última (Esc sai)"));
    else
        emit pickInfo(QString());
    emit toolChanged(int(t));
    update();
}

void Viewport3D::cancelRectStage() {
    m_rectStage = 0;
    m_rectMesh = -1;
    m_rectFace = HalfEdgeMesh::kNone;
    m_ghost.clear();
    m_snapMark.clear();
    m_ghostDirty = true;
}

// ---------------------------------------------------------------------------
//  G1 — MOTOR DE INFERÊNCIA: pontos tipados, alinhamentos e extensões.
//  Trabalha em ESPAÇO DE TELA (px), como o SketchUp: tolerância constante
//  independente do zoom. Fontes: arestas de todas as malhas visíveis
//  (edgeLines pula pontes), rascunho do lápis, origem, pontos adquiridos.
// ---------------------------------------------------------------------------
namespace {
// parâmetro t da linha (p0 + ld·t) mais próxima do raio (ro + rd·s)
inline bool lineClosestToRay(const Point3& p0, const Vec3& ld, const Point3& ro,
                             const Vec3& rd, double& t) {
    const Vec3 w0 = p0 - ro;
    const double a = ld.dot(ld), b = ld.dot(rd), c = rd.dot(rd);
    const double d = ld.dot(w0), e = rd.dot(w0);
    const double den = a * c - b * b;
    if (std::abs(den) < 1e-12) return false;
    t = (b * e - c * d) / den;
    return true;
}
} // namespace

bool Viewport3D::toScreen(const QMatrix4x4& mvp, const Point3& p,
                          QPointF& out) const {
    const QVector4D c = mvp * QVector4D(float(p.x), float(p.y), float(p.z), 1.f);
    if (c.w() < 1e-6f) return false;
    out = QPointF((c.x() / c.w() * 0.5 + 0.5) * width(),
                  (0.5 - c.y() / c.w() * 0.5) * height());
    return true;
}

Viewport3D::Infer Viewport3D::inferAt(const QPoint& pos, const Point3* base,
                                      bool allowFace, bool allowGround) const {
    Infer out;
    Point3 ro;
    Vec3 rd;
    if (!rayAt(pos, ro, rd)) return out;

    // Shift TRAVOU uma direção: tudo colapsa na linha travada
    if (m_shiftLock && m_lockKind != 0) {
        double t = 0.0;
        if (lineClosestToRay(m_lockP, m_lockDir, ro, rd, t)) {
            out.kind = m_lockKind;
            out.p = m_lockP + m_lockDir * t;
            out.hasRef = true;
            out.ref = m_lockP;
        }
        return out;
    }
    // setas travaram um eixo de DESENHO (lápis com traço ativo)
    if (m_axisDrawLock != 0 && base) {
        const Vec3 ax = m_axisDrawLock == 1   ? Vec3{1, 0, 0}
                        : m_axisDrawLock == 2 ? Vec3{0, 1, 0}
                                              : Vec3{0, 0, 1};
        double t = 0.0;
        if (lineClosestToRay(*base, ax, ro, rd, t)) {
            out.kind = 6 + m_axisDrawLock;
            out.p = *base + ax * t;
            out.hasRef = true;
            out.ref = *base;
        }
        return out;
    }

    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    const QPointF m(pos);
    constexpr double kTolPx = 9.0;
    const double tol2 = kTolPx * kTolPx;
    // melhores candidatos POR CATEGORIA (px²); decide-se por prioridade fixa
    struct Cand { double d{1e30}; Point3 p{}; bool ref{false}; Point3 rp{}; };
    Cand vert, mid, orig, onEdge, ext, align;
    int alignAxis = 0;
    auto tryPt = [&](Cand& c, const Point3& w) {
        QPointF s;
        if (!toScreen(mvp, w, s)) return;
        const QPointF d = s - m;
        const double dd = d.x() * d.x() + d.y() * d.y();
        if (dd < c.d && dd < tol2) { c.d = dd; c.p = w; }
    };

    std::vector<Point3> lines;
    std::vector<Point3> soup;
    for (int i = 0; i < int(m_meshes.size()); ++i) {
        const MeshPart& mp = m_meshes[std::size_t(i)];
        if (mp.hidden || !inCtx(i)) continue;
        lines.clear();
        mp.mesh.edgeLines(lines);
        soup.insert(soup.end(), lines.begin(), lines.end());
    }
    for (const auto& [a, b] : m_sketch) {
        soup.push_back(a);
        soup.push_back(b);
    }
    for (const auto& [a, b] : m_guides) {       // guias inferem também (G4)
        soup.push_back(a);
        soup.push_back(b);
    }
    for (std::size_t i = 0; i + 1 < soup.size(); i += 2) {
        const Point3& a = soup[i];
        const Point3& b = soup[i + 1];
        tryPt(vert, a);
        tryPt(vert, b);
        tryPt(mid, (a + b) * 0.5);
        const Vec3 ab = b - a;
        const double ll = ab.lengthSq();
        if (ll < 1e-18) continue;
        double t = 0.0;
        if (!lineClosestToRay(a, ab, ro, rd, t)) continue;
        if (t > 0.06 && t < 0.94) {                    // sobre a aresta
            tryPt(onEdge, a + ab * t);
        } else if ((t < -0.05 || t > 1.05) && t > -2.5 && t < 3.5) {
            Cand c = ext;                              // extensão da aresta
            tryPt(c, a + ab * t);
            if (c.d < ext.d) {
                ext = c;
                ext.ref = true;
                ext.rp = t < 0.0 ? a : b;
            }
        }
    }
    tryPt(orig, Point3{0, 0, 0});

    // alinhamento pelos EIXOS com pontos de referência (base + adquiridos)
    std::vector<Point3> refs;
    if (base) refs.push_back(*base);
    refs.insert(refs.end(), m_acquired.begin(), m_acquired.end());
    static const Vec3 kAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (const Point3& r : refs) {
        for (int a = 0; a < 3; ++a) {
            double t = 0.0;
            if (!lineClosestToRay(r, kAxes[a], ro, rd, t)) continue;
            if (std::abs(t) < 0.02) continue;          // em cima do próprio ref
            Cand c = align;
            tryPt(c, r + kAxes[a] * t);
            if (c.d < align.d) {
                align = c;
                align.ref = true;
                align.rp = r;
                alignAxis = a + 1;
            }
        }
    }

    // prioridade: extremidade > médio > origem > na-aresta > extensão >
    //             eixo > face > chão
    auto take = [&](const Cand& c, int kind) {
        out.kind = kind;
        out.p = c.p;
        out.hasRef = c.ref;
        out.ref = c.rp;
    };
    if (vert.d < 1e29) take(vert, 1);
    else if (mid.d < 1e29) take(mid, 2);
    else if (orig.d < 1e29) take(orig, 6);
    else if (onEdge.d < 1e29) take(onEdge, 3);
    else if (ext.d < 1e29) take(ext, 10);
    else if (align.d < 1e29) take(align, 6 + alignAxis);
    else {
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        Point3 hit;
        if (allowFace && pickAt(pos, mi, f, &hit)) {
            out.kind = 4;
            out.p = hit;
        } else if (allowGround && std::abs(rd.z) > 1e-9) {
            const double t = -ro.z / rd.z;
            if (t > 0.0) {
                out.kind = 5;
                out.p = ro + rd * t;
            }
        }
    }
    // hover em ponto forte ADQUIRE a referência ("a partir de") — LRU de 3
    if (out.kind == 1 || out.kind == 2) {
        const auto k = [](const Point3& p) {
            return std::tuple{llround(p.x * 1e5), llround(p.y * 1e5),
                              llround(p.z * 1e5)};
        };
        bool has = base && k(*base) == k(out.p);
        for (const Point3& a : m_acquired) has |= k(a) == k(out.p);
        if (!has) {
            m_acquired.push_back(out.p);
            if (m_acquired.size() > 3)
                m_acquired.erase(m_acquired.begin());
        }
    }
    return out;
}

namespace {
const char* inferLabel(int kind) {
    static const char* kL[] = {"",          "Extremidade", "Ponto médio",
                               "Na aresta", "Na face",     "No chão",
                               "Origem",    "No eixo X",   "No eixo Y",
                               "No eixo Z", "Extensão da aresta"};
    return (kind >= 0 && kind <= 10) ? kL[kind] : "";
}
QColor inferColor(int kind) {
    switch (kind) {
        case 1: return QColor(0x7d, 0xc4, 0x7f);   // extremidade: verde
        case 2: return QColor(0x7f, 0xc9, 0xce);   // ponto médio: ciano
        case 3: return QColor(0xd9, 0x88, 0x80);   // na aresta: vermelho
        case 4: return QColor(0x85, 0xa8, 0xc7);   // na face: azul
        case 5: return QColor(0xdc, 0xd6, 0xc8);   // no chão: washi
        case 6: return QColor(0xe0, 0xc0, 0x69);   // origem: latão claro
        case 7: return QColor(0xd2, 0x82, 0x64);   // eixo X: terracota
        case 8: return QColor(0x8c, 0xb4, 0xa0);   // eixo Y: sálvia
        case 9: return QColor(0x78, 0xa0, 0xc3);   // eixo Z: azul-cinza
        default: return QColor(0xdc, 0xd6, 0xc8);  // extensão: washi
    }
}
} // namespace

// desenha a inferência corrente por cima do GL: glifo + dica + pontilhado
void Viewport3D::drawInferOverlay(QPainter& qp) {
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    // R26: cotas persistentes — linhas de extensão + linha de cota deslocada
    // + texto; âmbar quando órfã (âncora sumiu numa edição/booleana).
    if (!m_dims.empty()) {
        QFont dfnt = qp.font();
        dfnt.setPointSizeF(8.0);
        qp.setFont(dfnt);
        for (const Dim3D& d : m_dims) {
            // R34: COTA ANGULAR — braços + arco + graus, tudo screen-space
            if (d.kind == 1) {
                QPointF sa, sv, sc;
                if (!toScreen(mvp, d.a, sa) || !toScreen(mvp, d.b, sv) ||
                    !toScreen(mvp, d.c, sc))
                    continue;
                const QColor col = d.orphan ? QColor(0xd9, 0x88, 0x80)
                                            : QColor(0xc2, 0xa0, 0x63);
                qp.setPen(QPen(QColor(col.red(), col.green(), col.blue(),
                                      150), 1.1));
                qp.drawLine(sv, sa);
                qp.drawLine(sv, sc);
                const QPointF va = sa - sv, vc = sc - sv;
                const double la = std::hypot(va.x(), va.y());
                const double lc = std::hypot(vc.x(), vc.y());
                if (la < 4 || lc < 4) continue;
                const double r = 0.4 * std::min(la, lc);
                // ângulos em graus (Qt: y cresce pra baixo → nega o y)
                const double aa = std::atan2(-va.y(), va.x()) * 180.0 / M_PI;
                const double ac = std::atan2(-vc.y(), vc.x()) * 180.0 / M_PI;
                double sweep = ac - aa;
                while (sweep > 180.0) sweep -= 360.0;
                while (sweep < -180.0) sweep += 360.0;
                qp.setPen(QPen(col, 1.3));
                qp.setBrush(Qt::NoBrush);
                qp.drawArc(QRectF(sv.x() - r, sv.y() - r, 2 * r, 2 * r),
                           int(aa * 16), int(sweep * 16));
                const Vec3 u = (d.a - d.b).normalized();
                const Vec3 v = (d.c - d.b).normalized();
                const double deg =
                    std::acos(std::clamp(u.dot(v), -1.0, 1.0)) * 180.0 /
                    M_PI;
                const QString lbl =
                    d.orphan ? QStringLiteral("? ° (órfã)")
                             : QStringLiteral("%1°").arg(deg, 0, 'f', 1);
                // texto na bissetriz (em tela), um pouco além do arco
                QPointF bis = va / la + vc / lc;
                const double lb = std::hypot(bis.x(), bis.y());
                bis = lb > 1e-6 ? bis / lb : QPointF(1, 0);
                const QPointF tp = sv + bis * (r + 16);
                const QSizeF ts = qp.fontMetrics().boundingRect(lbl).size();
                const QRectF box(tp - QPointF(ts.width() / 2 + 4,
                                              ts.height() / 2 + 2),
                                 ts + QSizeF(8, 4));
                qp.setPen(Qt::NoPen);
                qp.setBrush(QColor(16, 18, 22, 200));
                qp.drawRoundedRect(box, 3, 3);
                qp.setPen(col);
                qp.drawText(box, Qt::AlignCenter, lbl);
                continue;
            }
            QPointF sa, sb, da, db;
            if (!toScreen(mvp, d.a, sa) || !toScreen(mvp, d.b, sb)) continue;
            if (!dimScreenLine(d, mvp, da, db)) continue;
            const QColor col = d.orphan ? QColor(0xd9, 0x88, 0x80)
                                        : QColor(0xc2, 0xa0, 0x63);
            QPen extPen(QColor(col.red(), col.green(), col.blue(), 130), 1.0);
            qp.setPen(extPen);
            qp.drawLine(sa, da);
            qp.drawLine(sb, db);
            qp.setPen(QPen(col, 1.3));
            qp.drawLine(da, db);
            const QString lbl =
                d.orphan
                    ? QStringLiteral("? m (órfã)")
                    : QStringLiteral("%1 m").arg((d.b - d.a).length(), 0,
                                                 'f', 3);
            const QPointF mid = (da + db) * 0.5;
            const QSizeF ts = qp.fontMetrics().boundingRect(lbl).size();
            const QRectF box(mid - QPointF(ts.width() / 2 + 4,
                                           ts.height() / 2 + 2),
                             ts + QSizeF(8, 4));
            qp.setPen(Qt::NoPen);
            qp.setBrush(QColor(16, 18, 22, 200));
            qp.drawRoundedRect(box, 3, 3);
            qp.setPen(col);
            qp.drawText(box, Qt::AlignCenter, lbl);
        }
    }
    // G6: cotas das GUIAS — a medida vive no meio da linha, discreta
    if (!m_guides.empty()) {
        QFont gf = qp.font();
        gf.setPointSizeF(8.0);
        qp.setFont(gf);
        for (const auto& [a, b] : m_guides) {
            QPointF s;
            if (!toScreen(mvp, (a + b) * 0.5, s)) continue;
            const QString lbl =
                QStringLiteral("%1 m").arg((b - a).length(), 0, 'f', 2);
            const QSizeF ts = qp.fontMetrics().boundingRect(lbl).size();
            const QRectF box(s - QPointF(ts.width() / 2 + 4, ts.height() / 2),
                             ts + QSizeF(8, 4));
            qp.setPen(Qt::NoPen);
            qp.setBrush(QColor(16, 18, 22, 190));
            qp.drawRoundedRect(box, 3, 3);
            qp.setPen(QColor(194, 160, 99));
            qp.drawText(box, Qt::AlignCenter, lbl);
        }
    }
    qp.setBrush(Qt::NoBrush);                       // adquiridos: aros latão
    qp.setPen(QPen(QColor(194, 160, 99, 170), 1.4));
    for (const Point3& a : m_acquired) {
        QPointF s;
        if (toScreen(mvp, a, s)) qp.drawEllipse(s, 3.5, 3.5);
    }
    if (!m_infer.kind) return;
    QPointF s;
    if (!toScreen(mvp, m_infer.p, s)) return;
    const QColor col = inferColor(m_infer.kind);
    if (m_infer.hasRef) {                           // linha pontilhada até o ref
        QPointF r;
        if (toScreen(mvp, m_infer.ref, r)) {
            QPen dash(col, 1.3, Qt::DashLine);
            qp.setPen(dash);
            qp.drawLine(r, s);
        }
    }
    qp.setPen(QPen(QColor(16, 18, 22), 1.2));       // glifo por tipo
    qp.setBrush(col);
    switch (m_infer.kind) {
        case 1: case 2: case 6:                     // pontos: círculo cheio
            qp.drawEllipse(s, 4.5, 4.5);
            break;
        case 3: case 10:                            // aresta: quadrado
            qp.drawRect(QRectF(s.x() - 4, s.y() - 4, 8, 8));
            break;
        default: {                                  // face/chão/eixo: losango
            const QPointF d[4] = {s + QPointF(0, -5), s + QPointF(5, 0),
                                  s + QPointF(0, 5), s + QPointF(-5, 0)};
            qp.drawPolygon(d, 4);
        }
    }
    QString lbl = QString::fromUtf8(inferLabel(m_infer.kind));
    if (m_shiftLock) lbl += QStringLiteral(" — travado");
    QFont f = qp.font();
    f.setPointSizeF(8.5);
    qp.setFont(f);
    const QSizeF ts = qp.fontMetrics().boundingRect(lbl).size();
    const QRectF box(s + QPointF(12, 8), ts + QSizeF(10, 6));
    qp.setPen(Qt::NoPen);
    qp.setBrush(QColor(16, 18, 22, 215));
    qp.drawRoundedRect(box, 3, 3);
    qp.setPen(QColor(0xdc, 0xd6, 0xc8));
    qp.drawText(box, Qt::AlignCenter, lbl);
}

// QA G1: sequência de hovers "nx,ny;nx,ny…" — cada um infere (e adquire);
// o último fica no overlay e vai pro dump via pickInfo.
void Viewport3D::qaMouseMove(double nx, double ny) {
    const QPointF p(nx * width(), ny * height());
    QMouseEvent ev(QEvent::MouseMove, p, mapToGlobal(p), Qt::NoButton,
                   Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(this, &ev);      // o caminho real, não um atalho
}

void Viewport3D::qaHover(const QString& seq) {
    Infer in;
    const QStringList pts = seq.split(';', Qt::SkipEmptyParts);
    for (const QString& s : pts) {
        const QStringList c = s.split(',');
        if (c.size() != 2) continue;
        const QPoint pos(int(c[0].toDouble() * width()),
                         int(c[1].toDouble() * height()));
        in = inferAt(pos, nullptr, true, true);
        m_infer = in;
    }
    static const char* kN[] = {"nada",    "extremidade", "ponto-medio",
                               "na-aresta", "na-face",   "no-chao",
                               "origem",  "eixo-X",      "eixo-Y",
                               "eixo-Z",  "extensao"};
    QString msg = QStringLiteral("infer: %1 @ %2,%3,%4")
                      .arg(QString::fromUtf8(kN[in.kind]))
                      .arg(in.p.x, 0, 'f', 2)
                      .arg(in.p.y, 0, 'f', 2)
                      .arg(in.p.z, 0, 'f', 2);
    if (in.hasRef)
        msg += QStringLiteral(" ref %1,%2,%3")
                   .arg(in.ref.x, 0, 'f', 2)
                   .arg(in.ref.y, 0, 'f', 2)
                   .arg(in.ref.z, 0, 'f', 2);
    emit pickInfo(msg);
    update();
}

// QA G2: borracha num ponto; vmove "nx,ny,dx,dy,dz"; sketch3d em mundo
void Viewport3D::qaErase(double nx, double ny, bool hide) {
    m_erasedAny = false;
    pushUndo();
    if (!eraseAt(QPoint(int(nx * width()), int(ny * height())), hide) &&
        !m_undo.empty())
        m_undo.pop_back();
}

void Viewport3D::qaVertexMove(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 5) return;
    const QPoint pos(int(c[0].toDouble() * width()),
                     int(c[1].toDouble() * height()));
    const Vec3 d{c[2].toDouble(), c[3].toDouble(), c[4].toDouble()};
    const Infer in = inferAt(pos, nullptr, false, false);
    if (in.kind != 1) {
        emit pickInfo(QStringLiteral("vmove: sem vértice no ponto (kind %1)")
                          .arg(in.kind));
        return;
    }
    for (int mi = 0; mi < int(m_meshes.size()); ++mi) {
        if (m_meshes[std::size_t(mi)].hidden) continue;
        HalfEdgeMesh& msh = m_meshes[std::size_t(mi)].mesh;
        std::vector<Idx> mv;
        for (Idx v = 0; v < Idx(msh.vertexCount()); ++v)
            if ((msh.vertex(v).p - in.p).lengthSq() < 1e-10) mv.push_back(v);
        if (mv.empty()) continue;
        pushUndo();
        const std::size_t before = msh.faceCount();
        for (Idx v : mv) msh.moveVertex(v, msh.vertex(v).p + d);
        autofold(msh, mv);
        std::string why;
        if (!msh.checkIntegrity(&why)) {
            undoLast();
            m_redo.clear();
            emit pickInfo(QStringLiteral("vmove: integridade falhou (%1)")
                              .arg(QString::fromStdString(why)));
            return;
        }
        m_edited = true;
        buildRenderArrays();
        emit pickInfo(QStringLiteral(
                          "vmove: ok — faces %1 -> %2 (vincos do autofold)")
                          .arg(before)
                          .arg(msh.faceCount()));
        update();
        return;
    }
}

void Viewport3D::qaSketch3d(const QString& s) {
    setTool(Tool::Line);
    const QStringList pts = s.split(';', Qt::SkipEmptyParts);
    for (const QString& t : pts) {
        const QStringList c = t.split(',');
        if (c.size() != 3) continue;
        pencilClick(Point3{c[0].toDouble(), c[1].toDouble(), c[2].toDouble()});
    }
    m_chainActive = false;
    emit pickInfo(QStringLiteral("sketch3d: %1 malha(s) · %2 aresta(s) de "
                                 "rascunho")
                      .arg(m_meshes.size())
                      .arg(m_sketch.size()));
}

// snap de inferência: extremidades e pontos médios das arestas da face
void Viewport3D::applySnap(int meshIdx, Idx face, Point3& p, int& kind) const {
    kind = 0;
    if (m_exactClick) return;            // R2: medida digitada é sagrada
    const HalfEdgeMesh& m = m_meshes[std::size_t(meshIdx)].mesh;
    const double tol = double(m_dist) * 0.012;     // ~10 px na tela
    double best = tol * tol;
    Point3 bestP = p;
    for (const Idx h : m.faceHalfEdges(face)) {
        if (m.isBridge(h)) continue;
        const Point3& a = m.vertex(m.halfEdge(h).origin).p;
        const Point3& b = m.vertex(m.halfEdge(m.halfEdge(h).next).origin).p;
        const Point3 mid = (a + b) * 0.5;
        const double da = (a - p).lengthSq();
        const double dm = (mid - p).lengthSq();
        if (da < best) { best = da; bestP = a; kind = 1; }
        if (dm < best) { best = dm; bestP = mid; kind = 2; }
    }
    p = bestP;
}

bool Viewport3D::rectPointAt(const QPoint& pos, Point3& out,
                             int& snapKind) const {
    snapKind = 0;
    if (m_rectStage == 0) {
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        Point3 hit;
        if (!pickAt(pos, mi, f, &hit)) return false;
        applySnap(mi, f, hit, snapKind);
        m_infer = Infer{snapKind != 0 ? snapKind : 4, hit, false, {}};
        out = hit;
        return true;
    }
    // etapa 1: interseção do raio com o PLANO da face (ou do CHÃO) escolhido
    Point3 orig;
    Vec3 dir;
    if (!rayAt(pos, orig, dir)) return false;
    const double den = dir.dot(m_rectN);
    if (std::abs(den) < 1e-9) return false;
    const double t = (m_rectP1 - orig).dot(m_rectN) / den;
    if (t <= 0.0) return false;
    Point3 p = orig + dir * t;
    if (m_rectMesh >= 0) {                  // snap só quando há face de verdade
        applySnap(m_rectMesh, m_rectFace, p, snapKind);
        const Vec3 off = p - m_rectP1;      // projeta de volta no plano
        p = m_rectP1 + m_rectU * off.dot(m_rectU) + m_rectV * off.dot(m_rectV);
    }
    m_infer = Infer{snapKind != 0 ? snapKind : (m_rectMesh >= 0 ? 4 : 5),
                    p, false, {}};
    out = p;
    return true;
}

// todos os cantos dentro da face (par-ímpar no plano U/V) e longe das bordas
bool Viewport3D::cornersInsideFace(int meshIdx, Idx face,
                                   const std::vector<Point3>& corners,
                                   const Point3& origin, const Vec3& U,
                                   const Vec3& V) const {
    const HalfEdgeMesh& m = m_meshes[std::size_t(meshIdx)].mesh;
    struct P2 { double u, v; };
    std::vector<P2> poly;
    std::vector<std::pair<P2, P2>> segs;
    for (const Idx h : m.faceHalfEdges(face)) {
        const Point3& a = m.vertex(m.halfEdge(h).origin).p;
        const Point3& b = m.vertex(m.halfEdge(m.halfEdge(h).next).origin).p;
        const Vec3 oa = a - origin, ob = b - origin;
        const P2 pa{oa.dot(U), oa.dot(V)};
        const P2 pb{ob.dot(U), ob.dot(V)};
        poly.push_back(pa);
        if (!m.isBridge(h)) segs.push_back({pa, pb});
    }
    constexpr double kEdgeGap = 0.005;   // 5 mm de folga das bordas
    for (const Point3& c : corners) {
        const Vec3 oc = c - origin;
        const double u = oc.dot(U), v = oc.dot(V);
        bool in = false;                 // par-ímpar (o keyhole exclui o furo)
        for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
            if ((poly[i].v > v) != (poly[j].v > v) &&
                u < (poly[j].u - poly[i].u) * (v - poly[i].v) /
                        (poly[j].v - poly[i].v) + poly[i].u)
                in = !in;
        }
        if (!in) return false;
        for (const auto& [a, b] : segs) {   // distância 2D ao segmento
            const double lx = b.u - a.u, ly = b.v - a.v;
            const double ll = lx * lx + ly * ly;
            double w = ll > 1e-18 ? ((u - a.u) * lx + (v - a.v) * ly) / ll : 0.0;
            w = std::clamp(w, 0.0, 1.0);
            const double d = std::hypot(u - (a.u + lx * w), v - (a.v + ly * w));
            if (d < kEdgeGap) return false;
        }
    }
    return true;
}

void Viewport3D::rectHover(const QPoint& pos) {
    m_ghost.clear();
    m_snapMark.clear();
    Point3 p;
    int kind = 0;
    if (rectPointAt(pos, p, kind)) {
        if (m_rectStage == 1) {
            const Vec3 off = p - m_rectP1;
            double du = off.dot(m_rectU), dv = off.dot(m_rectV);
            // R8: Ctrl = o 1º clique é o CENTRO (como no SketchUp)
            Point3 anc = m_rectP1;
            if (QGuiApplication::queryKeyboardModifiers() &
                Qt::ControlModifier) {
                anc = m_rectP1 - m_rectU * du - m_rectV * dv;
                du *= 2.0;
                dv *= 2.0;
            }
            m_lastDu = du;               // R2: sinais p/ medida digitada
            m_lastDv = dv;
            liveVcb(QStringLiteral("%1 x %2 m")
                        .arg(std::abs(du), 0, 'f', 2)
                        .arg(std::abs(dv), 0, 'f', 2));
            const Point3 c1 = anc;
            const Point3 c2 = anc + m_rectU * du;
            const Point3 c3 = anc + m_rectU * du + m_rectV * dv;
            const Point3 c4 = anc + m_rectV * dv;
            pushSeg(m_ghost, c1, c2);
            pushSeg(m_ghost, c2, c3);
            pushSeg(m_ghost, c3, c4);
            pushSeg(m_ghost, c4, c1);
        }
    } else {
        m_infer = Infer{};               // sem alvo: some o glifo
    }
    m_ghostDirty = true;
    update();
}

// ============================================================================
//  R18: FUSÃO COPLANAR — formas desenhadas sobrepostas no MESMO plano viram
//  UMA forma só (o "sticky geometry" do SketchUp), em vez de sólidos
//  "travesseiro" empilhados sem se tocar de verdade.
// ============================================================================
namespace {
struct Pt2 { double u, v; };

double cross2p(const Pt2& O, const Pt2& A, const Pt2& B) {
    return (A.u - O.u) * (B.v - O.v) - (A.v - O.v) * (B.u - O.u);
}
bool ccw2(const std::vector<Pt2>& p) {
    double area = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i)
        area += cross2p({0, 0}, p[i], p[(i + 1) % p.size()]);
    return area >= 0.0;
}
bool pointInPoly2(const std::vector<Pt2>& poly, const Pt2& p) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if ((poly[i].v > p.v) != (poly[j].v > p.v) &&
            p.u < (poly[j].u - poly[i].u) * (p.v - poly[i].v) /
                          (poly[j].v - poly[i].v) +
                      poly[i].u)
            in = !in;
    }
    return in;
}
bool segX(const Pt2& a0, const Pt2& a1, const Pt2& b0, const Pt2& b1,
          double& t) {
    const double d1u = a1.u - a0.u, d1v = a1.v - a0.v;
    const double d2u = b1.u - b0.u, d2v = b1.v - b0.v;
    const double den = d1u * d2v - d1v * d2u;
    if (std::abs(den) < 1e-12) return false;       // paralelas/colineares
    const double t1 = ((b0.u - a0.u) * d2v - (b0.v - a0.v) * d2u) / den;
    const double t2 = ((b0.u - a0.u) * d1v - (b0.v - a0.v) * d1u) / den;
    constexpr double e = 1e-9;
    if (t1 < -e || t1 > 1 + e || t2 < -e || t2 > 1 + e) return false;
    t = std::clamp(t1, 0.0, 1.0);
    return true;
}
// insere um vértice em `poly` a cada ponto onde uma aresta de `other` cruza
std::vector<Pt2> subdiv(const std::vector<Pt2>& poly,
                        const std::vector<Pt2>& other) {
    std::vector<Pt2> out;
    const std::size_t n = poly.size(), m = other.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Pt2 &a = poly[i], &b = poly[(i + 1) % n];
        out.push_back(a);
        std::vector<std::pair<double, Pt2>> hits;
        for (std::size_t j = 0; j < m; ++j) {
            double t;
            if (segX(a, b, other[j], other[(j + 1) % m], t))
                hits.push_back(
                    {t, {a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t}});
        }
        std::sort(hits.begin(), hits.end(),
                  [](const auto& x, const auto& y) { return x.first < y.first; });
        for (const auto& [t, p] : hits)
            if (t > 1e-7 && t < 1 - 1e-7) out.push_back(p);
    }
    return out;
}
// une A e B (simples, sem furo); devolve {} se as caixas nem se tocam (fica
// a critério do chamador manter os dois sólidos separados)
std::vector<Pt2> unionSimple2D(std::vector<Pt2> A, std::vector<Pt2> B) {
    if (!ccw2(A)) std::reverse(A.begin(), A.end());
    if (!ccw2(B)) std::reverse(B.begin(), B.end());
    double au0 = 1e300, au1 = -1e300, av0 = 1e300, av1 = -1e300;
    double bu0 = 1e300, bu1 = -1e300, bv0 = 1e300, bv1 = -1e300;
    for (const auto& p : A) {
        au0 = std::min(au0, p.u); au1 = std::max(au1, p.u);
        av0 = std::min(av0, p.v); av1 = std::max(av1, p.v);
    }
    for (const auto& p : B) {
        bu0 = std::min(bu0, p.u); bu1 = std::max(bu1, p.u);
        bv0 = std::min(bv0, p.v); bv1 = std::max(bv1, p.v);
    }
    constexpr double kGap = 1e-6;
    if (au1 < bu0 - kGap || bu1 < au0 - kGap || av1 < bv0 - kGap ||
        bv1 < av0 - kGap)
        return {};                                  // caixas nem tocam

    const std::vector<Pt2> Asub = subdiv(A, B);
    const std::vector<Pt2> Bsub = subdiv(B, A);
    const bool semCorte = Asub.size() == A.size() && Bsub.size() == B.size();
    if (semCorte) {                       // sem interseção: contido ou solto
        bool bInA = true;
        for (const auto& p : B) bInA &= pointInPoly2(A, p);
        if (bInA) return A;
        bool aInB = true;
        for (const auto& p : A) aInB &= pointInPoly2(B, p);
        if (aInB) return B;
        return {};                        // não se tocam de verdade
    }
    // aresta sobrevive se seu ponto médio está FORA do outro polígono
    auto arestasVivas = [](const std::vector<Pt2>& self,
                           const std::vector<Pt2>& other) {
        std::vector<std::pair<Pt2, Pt2>> out;
        for (std::size_t i = 0; i < self.size(); ++i) {
            const Pt2 &a = self[i], &b = self[(i + 1) % self.size()];
            const Pt2 mid{(a.u + b.u) * 0.5, (a.v + b.v) * 0.5};
            if (!pointInPoly2(other, mid)) out.push_back({a, b});
        }
        return out;
    };
    std::vector<std::pair<Pt2, Pt2>> vivas = arestasVivas(Asub, B);
    const auto vb = arestasVivas(Bsub, A);
    vivas.insert(vivas.end(), vb.begin(), vb.end());
    if (vivas.empty()) return {};

    // solda por posição + trilha o contorno (uma saída por vértice no caso
    // comum de união simples sem furo)
    auto keyOf = [](const Pt2& p) {
        return std::pair<long long, long long>{std::llround(p.u * 1e6),
                                                std::llround(p.v * 1e6)};
    };
    std::map<std::pair<long long, long long>, Pt2> pontos;
    std::map<std::pair<long long, long long>, std::pair<long long, long long>>
        prox;
    for (const auto& [a, b] : vivas) {
        pontos[keyOf(a)] = a;
        pontos[keyOf(b)] = b;
        prox[keyOf(a)] = keyOf(b);
    }
    std::vector<std::vector<Pt2>> loops;
    std::set<std::pair<long long, long long>> visto;
    for (const auto& [k0, p0] : pontos) {
        if (visto.count(k0) || !prox.count(k0)) continue;
        std::vector<Pt2> loop;
        auto k = k0;
        int guard = 0;
        while (!visto.count(k) && prox.count(k) && guard++ < 4096) {
            visto.insert(k);
            loop.push_back(pontos[k]);
            k = prox[k];
        }
        if (loop.size() >= 3 && k == k0) loops.push_back(std::move(loop));
    }
    if (loops.empty()) return {};
    // maior loop por área = contorno externo (ignora furos residuais raros)
    std::size_t best = 0;
    double bestArea = -1.0;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        double area = 0.0;
        for (std::size_t j = 0; j < loops[i].size(); ++j)
            area += cross2p({0, 0}, loops[i][j], loops[i][(j + 1) % loops[i].size()]);
        area = std::abs(area);
        if (area > bestArea) { bestArea = area; best = i; }
    }
    return loops[best];
}
} // namespace

// funde `pts` (contorno fechado, plano horizontal, normal +Z) com qualquer
// "travesseiro" existente (2 faces, ainda não puxado) que ela sobreponha na
// MESMA altura; devolve o contorno unido (pts inalterado se nada se tocou) e
// remove do m_meshes os travesseiros consumidos
std::vector<Point3> Viewport3D::mergeGroundFootprint(std::vector<Point3> pts) {
    if (pts.empty()) return pts;
    const double z0 = pts.front().z;
    const Vec3 U{1, 0, 0}, V{0, 1, 0};
    auto to2 = [&](const std::vector<Point3>& p) {
        std::vector<Pt2> out;
        for (const Point3& q : p) out.push_back({q.x, q.y});
        return out;
    };
    std::vector<int> consumidos;
    bool changed = true;
    int guard = 0;
    while (changed && guard++ < 32) {
        changed = false;
        for (int i = 0; i < int(m_meshes.size()); ++i) {
            if (std::find(consumidos.begin(), consumidos.end(), i) !=
                consumidos.end())
                continue;
            const MeshPart& part = m_meshes[std::size_t(i)];
            if (part.mesh.faceCount() != 2) continue;   // só "travesseiro" cru
            Idx topF = HalfEdgeMesh::kNone;
            for (Idx f = 0; f < 2; ++f)
                if (part.mesh.faceNormal(f).z > 0.99) { topF = f; break; }
            if (topF == HalfEdgeMesh::kNone) continue;
            const auto vs = part.mesh.faceVertices(topF);
            if (vs.size() < 3) continue;
            Point3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
            for (std::size_t v = 0; v < part.mesh.vertexCount(); ++v) {
                const Point3& q = part.mesh.vertex(Idx(v)).p;
                lo.z = std::min(lo.z, q.z); hi.z = std::max(hi.z, q.z);
            }
            if (hi.z - lo.z > 0.005) continue;    // já puxado — não é alvo
            if (std::abs(hi.z - z0) > 1e-4) continue;   // outra altura
            std::vector<Point3> foot;
            for (const Idx v : vs) foot.push_back(part.mesh.vertex(v).p);
            const auto uni = unionSimple2D(to2(pts), to2(foot));
            if (uni.empty()) continue;            // não se tocam de verdade
            pts.clear();
            for (const Pt2& p : uni) pts.push_back({p.u, p.v, z0});
            consumidos.push_back(i);
            changed = true;
        }
    }
    if (!consumidos.empty()) {
        std::sort(consumidos.rbegin(), consumidos.rend());
        for (const int i : consumidos) m_meshes.erase(m_meshes.begin() + i);
        if (m_selMesh >= 0)
            for (const int i : consumidos)
                if (i < m_selMesh) --m_selMesh;
                else if (i == m_selMesh) m_selMesh = -1;
    }
    (void)U; (void)V;
    return pts;
}

// R18: sólido "travesseiro" (topo N-gon a `pts` + base 1mm abaixo) — a forma
// crua que P levanta virando caixa/prisma fechado. `pts` pode ter QUALQUER
// número de vértices (fusão coplanar gera formas não-retangulares).
int Viewport3D::buildGroundPillow(const std::vector<Point3>& pts, Idx* topOut) {
    MeshPart part;
    HalfEdgeMesh& m = part.mesh;
    std::vector<Idx> top, bot;
    for (const Point3& p : pts) top.push_back(m.addVertex(p));
    for (const Point3& p : pts) bot.push_back(m.addVertex({p.x, p.y, p.z - 0.001}));
    Idx topF = m.addFace(top);
    if (topF != HalfEdgeMesh::kNone && m.faceNormal(topF).z < 0.0) {
        part.mesh = HalfEdgeMesh{};           // topo saiu de cabeça p/ baixo
        HalfEdgeMesh& m2 = part.mesh;
        std::vector<Idx> top2, bot2;
        for (auto it = pts.rbegin(); it != pts.rend(); ++it)
            top2.push_back(m2.addVertex(*it));
        for (auto it = pts.rbegin(); it != pts.rend(); ++it)
            bot2.push_back(m2.addVertex({it->x, it->y, it->z - 0.001}));
        topF = m2.addFace(top2);
        std::vector<Idx> rev2(bot2.rbegin(), bot2.rend());
        m2.addFace(rev2);
    } else if (topF != HalfEdgeMesh::kNone) {
        std::vector<Idx> rev(bot.rbegin(), bot.rend());
        m.addFace(rev);
    }
    if (topF == HalfEdgeMesh::kNone) return -1;
    m_meshes.push_back(std::move(part));
    if (topOut) *topOut = topF;
    return int(m_meshes.size()) - 1;
}

// ============================================================================
//  R19: FUSÃO COPLANAR SOBRE FACE EXISTENTE — a mesma ideia acima (unir
//  polígonos que se sobrepõem), agora para vãos (insets) na MESMA parede,
//  não só no chão. Um retângulo parcialmente sobreposto a uma janela já
//  aberta (e ainda não puxada) funde os dois num vão só, em vez de falhar.
// ============================================================================
namespace {
// separa o ciclo de uma face-anel (0+ furos via keyhole) em laço externo +
// laços de furo, usando isBridge — cada par de pontes (twins na MESMA face)
// delimita um arco de furo entre si (garantido pela construção de insetFace)
void extractRingLoops(const HalfEdgeMesh& m, HalfEdgeMesh::Idx ring,
                      std::vector<Point3>& outer,
                      std::vector<HalfEdgeMesh::Idx>& outerIdx,
                      std::vector<std::vector<Point3>>& holes,
                      std::vector<HalfEdgeMesh::Idx>& holeFace) {
    using Idx = HalfEdgeMesh::Idx;
    const std::vector<Idx> hes = m.faceHalfEdges(ring);
    const std::size_t n = hes.size();
    std::vector<bool> isBr(n), consumed(n, false);
    for (std::size_t i = 0; i < n; ++i) isBr[i] = m.isBridge(hes[i]);
    for (std::size_t i = 0; i < n; ++i) {
        if (!isBr[i] || consumed[i]) continue;
        const Idx twinHe = m.halfEdge(hes[i]).twin;
        std::size_t j = n;
        for (std::size_t k = 0; k < n; ++k)
            if (hes[k] == twinHe) { j = k; break; }
        if (j == n) continue;
        const std::size_t lo = std::min(i, j), hi = std::max(i, j);
        std::vector<Point3> loop;
        for (std::size_t k = lo + 1; k < hi; ++k) {
            loop.push_back(m.vertex(m.halfEdge(hes[k]).origin).p);
            consumed[k] = true;
        }
        consumed[lo] = consumed[hi] = true;
        if (loop.size() >= 3) {
            // primeira aresta do arco: seu twin mora na face do vão (g)
            holeFace.push_back(m.halfEdge(m.halfEdge(hes[lo + 1]).twin).face);
            holes.push_back(std::move(loop));
        }
    }
    for (std::size_t i = 0; i < n; ++i)
        if (!consumed[i]) {
            // guarda o ÍNDICE original (não só a posição) — o anel externo
            // reaproveita esses vértices na reconstrução, senão desconecta
            // das faces VIZINHAS do mesmo sólido que os compartilham
            outerIdx.push_back(m.halfEdge(hes[i]).origin);
            outer.push_back(m.vertex(m.halfEdge(hes[i]).origin).p);
        }
}
} // namespace

Viewport3D::Idx Viewport3D::insetOrMergeOnFace(int meshIdx, Idx ring,
                                               std::vector<Point3> poly,
                                               const Point3& origin,
                                               const Vec3& U, const Vec3& V) {
    if (cornersInsideFace(meshIdx, ring, poly, origin, U, V)) {
        pushUndo();
        const Idx r = m_meshes[std::size_t(meshIdx)].mesh.insetFace(ring, poly);
        std::string why;
        if (r == HalfEdgeMesh::kNone ||
            !m_meshes[std::size_t(meshIdx)].mesh.checkIntegrity(&why)) {
            undoLast();
            return HalfEdgeMesh::kNone;
        }
        return r;
    }
    // não coube de cara — pode ser sobreposição com um vão coplanar existente
    HalfEdgeMesh& mesh = m_meshes[std::size_t(meshIdx)].mesh;
    const Vec3 ringN = mesh.faceNormal(ring);
    const Point3 ringP0 = mesh.vertex(mesh.faceVertices(ring)[0]).p;
    auto to2 = [&](const std::vector<Point3>& pts) {
        std::vector<Pt2> out;
        for (const Point3& p : pts) {
            const Vec3 o = p - origin;
            out.push_back({o.dot(U), o.dot(V)});
        }
        return out;
    };
    auto from2 = [&](const std::vector<Pt2>& pts) {
        std::vector<Point3> out;
        for (const Pt2& q : pts) out.push_back(origin + U * q.u + V * q.v);
        return out;
    };
    // 1ª checagem: o polígono cabe no laço EXTERNO (ignorando os furos)? Se
    // nem isso, não há fusão que salve — mantém o erro de sempre.
    std::vector<Point3> outerLoop;
    std::vector<Idx> outerIdx;
    std::vector<std::vector<Point3>> holeLoops;
    std::vector<Idx> holeFaces;
    extractRingLoops(mesh, ring, outerLoop, outerIdx, holeLoops, holeFaces);
    if (outerLoop.size() < 3) return HalfEdgeMesh::kNone;   // sem furos: já era
    {
        const auto o2 = to2(outerLoop);
        for (const Pt2& p : to2(poly))
            if (!pointInPoly2(o2, p)) return HalfEdgeMesh::kNone;
    }
    // funde iterativamente com todo furo COPLANAR (não puxado) que sobrepõe
    std::vector<Pt2> merged = to2(poly);
    std::vector<Idx> consumidas;
    bool changed = true;
    int guard = 0;
    while (changed && guard++ < 16) {
        changed = false;
        for (std::size_t k = 0; k < holeLoops.size(); ++k) {
            if (std::find(consumidas.begin(), consumidas.end(), holeFaces[k]) !=
                consumidas.end())
                continue;
            // só funde vão AINDA COPLANAR com o anel (não puxado)
            bool coplanar = true;
            for (const Point3& p : holeLoops[k])
                coplanar &= std::abs((p - ringP0).dot(ringN)) < 1e-4;
            if (!coplanar) continue;
            const auto uni = unionSimple2D(merged, to2(holeLoops[k]));
            if (uni.empty()) continue;
            merged = uni;
            consumidas.push_back(holeFaces[k]);
            changed = true;
        }
    }
    if (consumidas.empty()) return HalfEdgeMesh::kNone;   // não sobrepôs vão algum
    // reconstrói por SOPA: tudo igual, exceto o anel e os vãos consumidos —
    // que renascem como UM anel com o vão unido. Pular índices EXIGE remapear
    // faceColors/faceTex das faces sobreviventes (outras paredes/vãos do
    // mesmo sólido) — sem isso a pintura delas vaza pra face errada.
    pushUndo();
    MeshPart& part = m_meshes[std::size_t(meshIdx)];
    HalfEdgeMesh nova;
    // copia TODOS os vértices primeiro, preservando os índices originais —
    // só assim faceVertices(f) da malha velha continua válido na nova
    for (std::size_t v = 0; v < mesh.vertexCount(); ++v)
        nova.addVertex(mesh.vertex(Idx(v)).p);
    std::vector<Idx> skip{ring};
    skip.insert(skip.end(), consumidas.begin(), consumidas.end());
    std::map<Idx, Idx> oldToNew;
    bool ok = true;
    for (Idx f = 0; f < Idx(mesh.faceCount()) && ok; ++f) {
        if (std::find(skip.begin(), skip.end(), f) != skip.end()) continue;
        const Idx nf = nova.addFace(mesh.faceVertices(f));
        ok &= nf != HalfEdgeMesh::kNone;
        if (ok) oldToNew[f] = nf;
    }
    if (!ok) { undoLast(); m_redo.clear(); return HalfEdgeMesh::kNone; }
    // reusa os ÍNDICES ORIGINAIS do laço externo (já copiados acima) — não
    // cria vértice novo, senão o anel se desconecta das faces vizinhas
    const Idx outerF = nova.addFace(outerIdx);
    if (outerF == HalfEdgeMesh::kNone) {
        undoLast(); m_redo.clear(); return HalfEdgeMesh::kNone;
    }
    const Idx innerF = nova.insetFace(outerF, from2(merged));
    std::string why;
    if (innerF == HalfEdgeMesh::kNone || !nova.checkIntegrity(&why)) {
        undoLast(); m_redo.clear(); return HalfEdgeMesh::kNone;
    }
    std::map<Idx, std::array<float, 3>> nc;
    for (const auto& [f, c] : part.faceColors)
        if (oldToNew.count(f)) nc[oldToNew[f]] = c;
    part.faceColors = std::move(nc);
    std::map<Idx, QString> nt;
    for (const auto& [f, t] : part.faceTex)
        if (oldToNew.count(f)) nt[oldToNew[f]] = t;
    part.faceTex = std::move(nt);
    mesh = std::move(nova);
    return innerF;
}

namespace {
// R21: acha TODOS os pares de tampas antiparalelas + mesma área cujas
// laterais são TODAS perpendiculares a elas — cada par é uma leitura válida
// de "eixo do prisma" (uma caixa comum tem ATÉ 3: largura/profundidade/
// altura são todas "prismas retos" simultaneamente). O chamador testa cada
// candidato contra o alvo e fica com o que realmente atravessa de lado a
// lado — não dá pra saber de antemão qual eixo o usuário quis.
struct PrismCandidate {
    Vec3 axis, U, V;
    Point3 origin;
    std::vector<Point3> footprint;
    double length;
};

std::vector<PrismCandidate> findPrismAxes(const HalfEdgeMesh& m) {
    using Idx = HalfEdgeMesh::Idx;
    std::vector<PrismCandidate> out;
    const std::size_t nf = m.faceCount();
    for (std::size_t i = 0; i < nf; ++i) {
        const Vec3 ni = m.faceNormal(Idx(i));
        const double ai = m.faceArea(Idx(i));
        for (std::size_t j = i + 1; j < nf; ++j) {
            const Vec3 nj = m.faceNormal(Idx(j));
            if (ni.dot(nj) > -0.999) continue;
            const double aj = m.faceArea(Idx(j));
            if (std::abs(ai - aj) > 1e-4 * std::max(ai, aj)) continue;
            bool sidesOk = true;
            for (std::size_t k = 0; k < nf && sidesOk; ++k) {
                if (k == i || k == j) continue;
                if (std::abs(m.faceNormal(Idx(k)).dot(ni)) > 1e-3)
                    sidesOk = false;
            }
            if (!sidesOk) continue;
            const std::vector<Idx> loop = m.faceVertices(Idx(i));
            if (loop.size() < 3) continue;
            Vec3 u{};
            for (const Idx h : m.faceHalfEdges(Idx(i))) {
                if (m.isBridge(h)) continue;
                const Point3& a = m.vertex(m.halfEdge(h).origin).p;
                const Point3& b =
                    m.vertex(m.halfEdge(m.halfEdge(h).next).origin).p;
                u = b - a;
                if (u.lengthSq() > 1e-12) break;
            }
            if (u.lengthSq() < 1e-12) continue;
            PrismCandidate c;
            // eixo aponta PRA DENTRO (de i pra j) — s=0 na tampa i, s=length
            // na tampa j; usar a normal de FORA (ni) inverteria o sentido do
            // range [0,length] usado depois pra testar as faces do alvo
            c.axis = ni * -1.0;
            c.origin = m.vertex(loop[0]).p;
            c.U = u.normalized();
            c.V = c.axis.cross(c.U).normalized();
            for (const Idx v : loop) c.footprint.push_back(m.vertex(v).p);
            const Point3 p0j = m.vertex(m.faceVertices(Idx(j))[0]).p;
            c.length = (p0j - c.origin).dot(c.axis);
            if (c.length < 1e-6) continue;
            out.push_back(std::move(c));
        }
    }
    return out;
}
}   // namespace

QString Viewport3D::subtractSelected() {
    if (m_selSolidsMulti.size() != 2) {
        const QString msg = QStringLiteral(
            "Subtrair: selecione EXATAMENTE 2 sólidos (caixa de seleção) — "
            "o menor corta o maior.");
        emit pickInfo(msg);
        return msg;
    }
    const int ia = m_selSolidsMulti[0], ib = m_selSolidsMulti[1];
    auto bboxVol = [&](int mi) {
        const HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
        Point3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
        for (std::size_t v = 0; v < m.vertexCount(); ++v) {
            const Point3& p = m.vertex(Idx(v)).p;
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
            lo.z = std::min(lo.z, p.z);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
            hi.z = std::max(hi.z, p.z);
        }
        return (hi.x - lo.x) * (hi.y - lo.y) * (hi.z - lo.z);
    };
    const int targetMesh = bboxVol(ia) >= bboxVol(ib) ? ia : ib;
    const int cutterMesh = targetMesh == ia ? ib : ia;

    const std::vector<PrismCandidate> candidates =
        findPrismAxes(m_meshes[std::size_t(cutterMesh)].mesh);
    if (candidates.empty()) {
        const QString msg = QStringLiteral(
            "Subtrair: o cortador precisa ser um prisma reto (caixa, "
            "cilindro…) — formas livres ainda não suportadas.");
        emit pickInfo(msg);
        return msg;
    }

    HalfEdgeMesh& target = m_meshes[std::size_t(targetMesh)].mesh;
    struct Hit { Idx face; double s; std::vector<Point3> poly; Vec3 fn; Point3 p0; };
    // uma caixa comum lê como prisma em ATÉ 3 eixos (X/Y/Z) — testa cada
    // leitura contra o alvo. Prefere um corte de LADO A LADO (2 faces,
    // par antiparalelo — R21); se nenhuma leitura atravessar limpo, cai
    // pro POÇO CEGO (R22: exatamente 1 face furada, o cortador não sai
    // do outro lado).
    Vec3 axis{}, U{}, V{};
    Point3 origin{};
    std::vector<Point3> footprint;
    double length = 0.0;
    std::vector<Hit> hits;
    bool isThrough = false;
    std::size_t maxHits = 0;
    auto testCandidate = [&](const PrismCandidate& c) {
        std::vector<std::pair<double, double>> uvc;
        for (const Point3& p : c.footprint) {
            const Vec3 o = p - c.origin;
            uvc.push_back({o.dot(c.U), o.dot(c.V)});
        }
        std::vector<Hit> h;
        for (Idx f = 0; f < Idx(target.faceCount()); ++f) {
            const Vec3 fn = target.faceNormal(f);
            const double denom = c.axis.dot(fn);
            if (std::abs(denom) < 1e-6) continue;   // eixo raso: sem corte limpo
            const Point3 p0 = target.vertex(target.faceVertices(f)[0]).p;
            std::vector<Point3> poly;
            std::vector<double> ss;
            bool ok = true;
            for (const auto& [u, v] : uvc) {
                const double num =
                    (p0 - c.origin).dot(fn) - u * c.U.dot(fn) - v * c.V.dot(fn);
                const double s = num / denom;
                if (s < -1e-4 || s > c.length + 1e-4) { ok = false; break; }
                ss.push_back(s);
                poly.push_back(c.origin + c.axis * s + c.U * u + c.V * v);
            }
            if (!ok) continue;
            const double savg =
                std::accumulate(ss.begin(), ss.end(), 0.0) / double(ss.size());
            h.push_back({f, savg, std::move(poly), fn, p0});
        }
        return h;
    };
    for (const PrismCandidate& c : candidates) {
        std::vector<Hit> h = testCandidate(c);
        maxHits = std::max(maxHits, h.size());
        if (h.size() != 2) continue;
        std::sort(h.begin(), h.end(),
                 [](const Hit& a, const Hit& b) { return a.s < b.s; });
        if (h[0].fn.dot(h[1].fn) > -0.5) continue;   // não é um par limpo
        axis = c.axis; U = c.U; V = c.V; origin = c.origin;
        footprint = c.footprint; length = c.length;
        hits = std::move(h);
        isThrough = true;
        break;
    }
    if (hits.empty())
        for (const PrismCandidate& c : candidates) {
            std::vector<Hit> h = testCandidate(c);
            if (h.size() != 1) continue;
            axis = c.axis; U = c.U; V = c.V; origin = c.origin;
            footprint = c.footprint; length = c.length;
            hits = std::move(h);
            isThrough = false;
            break;
        }
    if (hits.empty()) {
        const QString msg = maxHits == 0
            ? QStringLiteral("Subtrair: os sólidos não se cruzam.")
            : QStringLiteral(
                  "Subtrair: geometria do corte não é suportada (%1 face(s) "
                  "na melhor tentativa) — tente um alvo mais simples.")
                  .arg(maxHits);
        emit pickInfo(msg);
        return msg;
    }
    const std::size_t N = footprint.size();
    std::vector<std::pair<double, double>> uv;
    for (const Point3& p : footprint) {
        const Vec3 o = p - origin;
        uv.push_back({o.dot(U), o.dot(V)});
    }

    // deriva a base 2D PRÓPRIA de cada face-alvo (não a do cortador) — é o
    // que insetOrMergeOnFace usa pra projeção/contenção
    auto faceBasis = [&](Idx f, Point3& o, Vec3& fu, Vec3& fv) {
        const Vec3 fn = target.faceNormal(f);
        o = target.vertex(target.faceVertices(f)[0]).p;
        Vec3 u{};
        for (const Idx h : target.faceHalfEdges(f)) {
            if (target.isBridge(h)) continue;
            const Point3& a = target.vertex(target.halfEdge(h).origin).p;
            const Point3& b =
                target.vertex(target.halfEdge(target.halfEdge(h).next).origin).p;
            u = b - a;
            if (u.lengthSq() > 1e-12) break;
        }
        fu = u.normalized();
        fv = fn.cross(fu).normalized();
    };

    pushUndo();
    const std::size_t mark = m_undo.size();

    Point3 o1{};
    Vec3 u1{}, v1b{};
    faceBasis(hits[0].face, o1, u1, v1b);
    const Idx hole1 = insetOrMergeOnFace(targetMesh, hits[0].face, hits[0].poly,
                                         o1, u1, v1b);
    if (hole1 == HalfEdgeMesh::kNone) {
        while (m_undo.size() >= mark) undoLast();
        const QString msg = QStringLiteral(
            "Subtrair: a área de corte não coube limpa na 1ª face.");
        emit pickInfo(msg);
        return msg;
    }
    Idx hole2 = HalfEdgeMesh::kNone;
    if (isThrough) {
        Point3 o2{};
        Vec3 u2{}, v2b{};
        faceBasis(hits[1].face, o2, u2, v2b);
        hole2 = insetOrMergeOnFace(targetMesh, hits[1].face, hits[1].poly,
                                   o2, u2, v2b);
        if (hole2 == HalfEdgeMesh::kNone) {
            while (m_undo.size() >= mark) undoLast();
            const QString msg = QStringLiteral(
                "Subtrair: a área de corte não coube limpa na 2ª face.");
            emit pickInfo(msg);
            return msg;
        }
    }
    while (m_undo.size() > mark) m_undo.pop_back();   // colapsa em 1 undo só

    // casa os vértices do 1º vão com o footprint por POSIÇÃO (não por ordem
    // — insetFace pode ter revertido/rotacionado o laço internamente); no
    // corte de lado a lado o 2º anel casa do mesmo jeito com o 2º vão, no
    // poço cego o 2º anel é um FUNDO NOVO (vértices que ainda não existem)
    auto roundKey = [](const Point3& p) {
        return std::make_tuple(std::llround(p.x * 1e6), std::llround(p.y * 1e6),
                               std::llround(p.z * 1e6));
    };
    std::map<decltype(roundKey(Point3{})), std::size_t> pos1ToFoot;
    for (std::size_t k = 0; k < N; ++k) pos1ToFoot[roundKey(hits[0].poly[k])] = k;
    std::vector<Idx> byFoot1(N, HalfEdgeMesh::kNone);
    bool matchOk = true;
    for (const Idx idx : target.faceVertices(hole1)) {
        auto it = pos1ToFoot.find(roundKey(target.vertex(idx).p));
        if (it == pos1ToFoot.end()) { matchOk = false; break; }
        byFoot1[it->second] = idx;
    }
    std::vector<Idx> byFoot2(N, HalfEdgeMesh::kNone);
    if (isThrough) {
        std::map<decltype(roundKey(Point3{})), std::size_t> pos2ToFoot;
        for (std::size_t k = 0; k < N; ++k) pos2ToFoot[roundKey(hits[1].poly[k])] = k;
        for (const Idx idx : target.faceVertices(hole2)) {
            if (!matchOk) break;
            auto it = pos2ToFoot.find(roundKey(target.vertex(idx).p));
            if (it == pos2ToFoot.end()) { matchOk = false; break; }
            byFoot2[it->second] = idx;
        }
    }
    for (std::size_t k = 0; k < N && matchOk; ++k)
        if (byFoot1[k] == HalfEdgeMesh::kNone ||
            (isThrough && byFoot2[k] == HalfEdgeMesh::kNone))
            matchOk = false;
    if (!matchOk) {
        undoLast();
        const QString msg = QStringLiteral(
            "Subtrair: falha ao alinhar o corte (face já tinha vão ali) — "
            "tente de novo.");
        emit pickInfo(msg);
        return msg;
    }

    // sopa final: tudo igual, exceto o(s) vão(s) removido(s) — o corte fica
    // aberto — + as paredes internas novas. No poço cego, o 2º anel (fundo)
    // é criado agora, com vértices NOVOS na profundidade s=length.
    HalfEdgeMesh nova;
    for (std::size_t v = 0; v < target.vertexCount(); ++v)
        nova.addVertex(target.vertex(Idx(v)).p);
    std::vector<Idx> ring2(N, HalfEdgeMesh::kNone);
    if (isThrough) {
        ring2 = byFoot2;
    } else {
        for (std::size_t k = 0; k < N; ++k)
            ring2[k] = nova.addVertex(origin + axis * length +
                                      U * uv[k].first + V * uv[k].second);
    }

    // paredes internas: um quad por lado do footprint, ligando o 1º anel ao
    // 2º; sentido corrigido pra apontar pro vazio da cavidade. Antes (R21-23)
    // isso usava "longe do centroide da seção" — falha perto de um canto
    // CÔNCAVO (achado da R24: um cortador não-convexo, ex. em L, pode ter o
    // centroide do lado ERRADO de uma aresta perto da reentrância). Método
    // exato, válido pra QUALQUER polígono simples: a normal de uma aresta é
    // o próprio vetor da aresta girado 90° no plano — só o SENTIDO do giro
    // depende de como o laço `uv` está orientado nessa base (U,V); aqui o
    // laço da face nasce CCW visto de FORA da face (convenção do kernel),
    // mas o eixo aqui aponta PRA DENTRO do cortador (R21: eixo = normal*-1),
    // então em (U,V) [U×V=eixo] o laço aparece invertido (CW) — e um laço CW
    // + convenção de CAVIDADE (normal pro vazio, não pra fora do material)
    // são dois sinais que se cancelam, dando a MESMA fórmula (dv,-du) que a
    // R24 usa pro caso de sólido — testado contra o caso já provado (parede
    // furada) pra confirmar antes de trocar.
    std::vector<std::vector<Idx>> newQuads;
    for (std::size_t k = 0; k < N; ++k) {
        const std::size_t k2 = (k + 1) % N;
        std::vector<Idx> quad{byFoot1[k], byFoot1[k2], ring2[k2], ring2[k]};
        const Point3& a = nova.vertex(quad[0]).p;
        const Point3& b = nova.vertex(quad[1]).p;
        const Point3& c = nova.vertex(quad[2]).p;
        const Vec3 nrm = (b - a).cross(c - a);
        const double edu = uv[k2].first - uv[k].first;
        const double edv = uv[k2].second - uv[k].second;
        const Vec3 outward = U * edv + V * (-edu);
        if (nrm.dot(outward) < 0.0) std::reverse(quad.begin(), quad.end());
        newQuads.push_back(std::move(quad));
    }
    // poço cego: fecha o fundo com uma tampa (N-gono) apontando pro vazio,
    // ou seja, de volta pra abertura (-eixo, já que o eixo aponta pra dentro
    // do cortador, da tampa de entrada pro fundo)
    std::vector<Idx> capLoop;
    if (!isThrough) {
        capLoop = ring2;
        Vec3 nrm{};
        for (std::size_t k = 0; k < N; ++k) {
            const Point3& a = nova.vertex(capLoop[k]).p;
            const Point3& b = nova.vertex(capLoop[(k + 1) % N]).p;
            nrm.x += (a.y - b.y) * (a.z + b.z);
            nrm.y += (a.z - b.z) * (a.x + b.x);
            nrm.z += (a.x - b.x) * (a.y + b.y);
        }
        if (nrm.dot(axis) > 0.0) std::reverse(capLoop.begin(), capLoop.end());
    }

    // guarda cada addFace com o PORQUÊ específico — sem isso, uma colisão de
    // aresta dirigida (típica de cortador tangente/na borda) só dizia
    // "geometria inválida" sem apontar qual das N faces novas foi a causa
    std::map<Idx, Idx> oldToNew;
    bool ok = true;
    QString failDetail;
    for (Idx f = 0; f < Idx(target.faceCount()) && ok; ++f) {
        if (f == hole1 || (isThrough && f == hole2)) continue;
        const Idx nf = nova.addFace(target.faceVertices(f));
        if (nf == HalfEdgeMesh::kNone) {
            ok = false;
            failDetail = QStringLiteral("face original %1 não copiou").arg(f);
            break;
        }
        oldToNew[f] = nf;
    }
    for (std::size_t qi = 0; qi < newQuads.size() && ok; ++qi) {
        if (nova.addFace(newQuads[qi]) == HalfEdgeMesh::kNone) {
            ok = false;
            failDetail = QStringLiteral("parede interna %1 colidiu com uma "
                                        "aresta já existente")
                             .arg(qi);
        }
    }
    if (ok && !isThrough && nova.addFace(capLoop) == HalfEdgeMesh::kNone) {
        ok = false;
        failDetail = QStringLiteral("tampa do fundo colidiu com uma aresta "
                                    "já existente");
    }
    std::string why;
    if (ok) ok = nova.checkIntegrity(&why);
    if (!ok) {
        undoLast();
        const QString detail = !failDetail.isEmpty()
                                    ? failDetail
                                : !why.empty()
                                    ? QString::fromStdString(why)
                                    : QStringLiteral("motivo desconhecido");
        const QString msg = QStringLiteral(
            "Subtrair: geometria resultante inválida (%1) — operação "
            "cancelada, nada foi alterado.")
                                .arg(detail);
        emit pickInfo(msg);
        return msg;
    }
    MeshPart& part = m_meshes[std::size_t(targetMesh)];
    std::map<Idx, std::array<float, 3>> nc;
    for (const auto& [f, c] : part.faceColors)
        if (oldToNew.count(f)) nc[oldToNew[f]] = c;
    part.faceColors = std::move(nc);
    std::map<Idx, QString> nt;
    for (const auto& [f, t] : part.faceTex)
        if (oldToNew.count(f)) nt[oldToNew[f]] = t;
    part.faceTex = std::move(nt);
    part.mesh = std::move(nova);

    m_meshes.erase(m_meshes.begin() + cutterMesh);
    const int newTargetIdx = cutterMesh < targetMesh ? targetMesh - 1 : targetMesh;
    m_selMesh = newTargetIdx;
    m_selFace = HalfEdgeMesh::kNone;
    m_selWhole = true;
    m_selSolidsMulti.clear();
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit structureChanged();
    const QString msg =
        isThrough
            ? QStringLiteral("Subtrair: túnel aberto (%1 lado(s)) — sólido "
                             "cortador removido.")
                  .arg(N)
            : QStringLiteral("Subtrair: poço aberto (%1 lado(s)) — sólido "
                             "cortador removido.")
                  .arg(N);
    emit pickInfo(msg);
    update();
    return msg;
}

// ============================================================================
//  R24: UNIÃO 3D — primeira booleana de junção. Escopo deliberadamente
//  restrito: os 2 sólidos precisam ser prismas retos (findPrismAxes) de eixo
//  PARALELO e MESMA extensão axial — só nesse caso a união vira um problema
//  2D já resolvido (a seção do resultado é a união das duas seções,
//  unionSimple2D da R18) em vez de exigir um motor de interseção 3D
//  genérico. Eixos não-paralelos, alturas diferentes ou 3+ sólidos ficam
//  fora, com mensagem honesta.
// ============================================================================
QString Viewport3D::uniteSelected() {
    if (m_selSolidsMulti.size() != 2) {
        const QString msg = QStringLiteral(
            "Unir: selecione EXATAMENTE 2 sólidos (caixa de seleção).");
        emit pickInfo(msg);
        return msg;
    }
    const int ia = m_selSolidsMulti[0], ib = m_selSolidsMulti[1];
    const std::vector<PrismCandidate> candA =
        findPrismAxes(m_meshes[std::size_t(ia)].mesh);
    const std::vector<PrismCandidate> candB =
        findPrismAxes(m_meshes[std::size_t(ib)].mesh);
    if (candA.empty() || candB.empty()) {
        const QString msg = QStringLiteral(
            "Unir: os dois sólidos precisam ser prismas retos (caixa, "
            "cilindro…) — formas livres ainda não suportadas.");
        emit pickInfo(msg);
        return msg;
    }

    // acha um par de eixos PARALELOS (mesmo sentido ou oposto) com a MESMA
    // extensão ao longo desse eixo nos dois sólidos
    constexpr double kExtentTol = 1e-3;
    const PrismCandidate* pa = nullptr;
    const PrismCandidate* pb = nullptr;
    for (const PrismCandidate& ca : candA) {
        const double sa0 = ca.origin.dot(ca.axis);
        const double aMin = std::min(sa0, sa0 + ca.length);
        const double aMax = std::max(sa0, sa0 + ca.length);
        for (const PrismCandidate& cb : candB) {
            if (std::abs(ca.axis.dot(cb.axis)) < 0.999) continue;   // não paralelo
            const double sb0 = cb.origin.dot(ca.axis);       // projeta no eixo de A
            const double sb1 = (cb.origin + cb.axis * cb.length).dot(ca.axis);
            const double bMin = std::min(sb0, sb1), bMax = std::max(sb0, sb1);
            if (std::abs(aMin - bMin) > kExtentTol ||
                std::abs(aMax - bMax) > kExtentTol)
                continue;   // extensão diferente
            pa = &ca; pb = &cb;
            break;
        }
        if (pa) break;
    }
    if (!pa) {
        const QString msg = QStringLiteral(
            "Unir: os sólidos precisam ser prismas de eixo PARALELO e MESMA "
            "extensão (ex. duas paredes de mesma altura) — tente outro par.");
        emit pickInfo(msg);
        return msg;
    }

    // projeta as duas seções na base 2D de A (origem na tampa "de baixo" do
    // intervalo compartilhado) e funde via unionSimple2D (motor da R18)
    const Vec3 axis = pa->axis, U = pa->U, V = pa->V;
    const double sa0 = pa->origin.dot(axis);
    const double sMin = std::min(sa0, sa0 + pa->length);
    const double sMax = std::max(sa0, sa0 + pa->length);
    const Point3 origin2D = pa->origin + axis * (sMin - sa0);

    std::vector<Pt2> polyA, polyB;
    for (const Point3& p : pa->footprint) {
        const Vec3 o = p - origin2D;
        polyA.push_back({o.dot(U), o.dot(V)});
    }
    for (const Point3& p : pb->footprint) {
        const Vec3 o = p - origin2D;
        polyB.push_back({o.dot(U), o.dot(V)});
    }
    // duas paredes que se ENCONTRAM NUM CANTO reto compartilham arestas
    // EXATAMENTE colineares (mesma reta) em vez de se cruzarem transversal —
    // unionSimple2D (R18) só detecta cruzamento transversal (segX descarta
    // segmentos paralelos/colineares de propósito), então o caso mais comum
    // de união ("parede encontra parede") ficaria sempre vazio sem isso.
    // Afasta cada polígono ~1 µm do seu próprio centroide (direções
    // diferentes por polígono já que os centroides diferem) só pra quebrar
    // a colinearidade exata — irrelevante pra geometria de arquitetura, mas
    // suficiente pra segX enxergar um cruzamento de verdade.
    auto nudgeOut = [](std::vector<Pt2>& poly) {
        double cu = 0.0, cv = 0.0;
        for (const Pt2& p : poly) { cu += p.u; cv += p.v; }
        cu /= double(poly.size()); cv /= double(poly.size());
        constexpr double kNudge = 1e-6;
        for (Pt2& p : poly) {
            p.u += (p.u - cu) * kNudge;
            p.v += (p.v - cv) * kNudge;
        }
    };
    nudgeOut(polyA);
    nudgeOut(polyB);
    const std::vector<Pt2> merged = unionSimple2D(polyA, polyB);
    if (merged.size() < 3) {
        const QString msg = QStringLiteral(
            "Unir: os sólidos não se tocam nessa direção — tente outro par.");
        emit pickInfo(msg);
        return msg;
    }

    // CCW em (u,v) ⟺ normal +eixo, GARANTIDO por construção (findPrismAxes
    // monta V = eixo × U, logo U × V = eixo) — normaliza o sentido antes de
    // extrudir pra não depender de qual ordem unionSimple2D devolveu
    double area2 = 0.0;
    for (std::size_t i = 0; i < merged.size(); ++i) {
        const Pt2& a = merged[i];
        const Pt2& b = merged[(i + 1) % merged.size()];
        area2 += a.u * b.v - b.u * a.v;
    }
    std::vector<Pt2> loop = merged;
    if (area2 < 0.0) std::reverse(loop.begin(), loop.end());
    // solda pontos vizinhos quase-coincidentes: o nudge acima quebra a
    // colinearidade exata deslocando A e B a partir de centroides DIFERENTES
    // — perto do canto compartilhado isso deixa "cacos" de aresta quase nula
    // (os cantos nudged de A e B não caem mais no mesmo ponto); sem soldar,
    // essas arestas de handful de nanômetros geram normais instáveis e
    // colidem com as arestas vizinhas ao extrudir
    {
        std::vector<Pt2> welded;
        constexpr double kWeld = 1e-4;
        for (const Pt2& p : loop) {
            if (!welded.empty()) {
                const double du = p.u - welded.back().u, dv = p.v - welded.back().v;
                if (du * du + dv * dv < kWeld * kWeld) continue;
            }
            welded.push_back(p);
        }
        if (welded.size() > 1) {
            const double du = welded.front().u - welded.back().u;
            const double dv = welded.front().v - welded.back().v;
            if (du * du + dv * dv < kWeld * kWeld) welded.pop_back();
        }
        loop = std::move(welded);
    }
    const std::size_t N = loop.size();
    if (N < 3) {
        const QString msg = QStringLiteral(
            "Unir: contorno fundido degenerado — tente outro par.");
        emit pickInfo(msg);
        return msg;
    }
    pushUndo();
    const double h = sMax - sMin;
    HalfEdgeMesh nova;
    std::vector<Idx> bot, top;
    for (const Pt2& p : loop) bot.push_back(nova.addVertex(origin2D + U * p.u + V * p.v));
    for (const Pt2& p : loop)
        top.push_back(nova.addVertex(origin2D + axis * h + U * p.u + V * p.v));
    std::vector<Idx> botRev(bot.rbegin(), bot.rend());
    QString failDetail;
    bool ok = nova.addFace(top) != HalfEdgeMesh::kNone;
    if (!ok) failDetail = QStringLiteral("tampa de cima não coube");
    if (ok) {
        ok = nova.addFace(botRev) != HalfEdgeMesh::kNone;
        if (!ok) failDetail = QStringLiteral("tampa de baixo não coube");
    }

    // paredes laterais: sólido de VERDADE (não cavidade) — normal aponta pra
    // FORA. Uma união de dois prismas pode sair NÃO-CONVEXA (ex. dois muros
    // em L têm um canto CÔNCAVO) — "longe do centroide" não é confiável
    // perto de um côncavo (o centroide pode ficar do lado errado daquela
    // aresta). O jeito exato, válido pra QUALQUER polígono simples: pra um
    // laço CCW (já garantido acima), a normal de fora de cada aresta é o
    // próprio vetor da aresta girado -90° no plano (dv,-du).
    for (std::size_t k = 0; k < N && ok; ++k) {
        const std::size_t k2 = (k + 1) % N;
        std::vector<Idx> quad{bot[k], bot[k2], top[k2], top[k]};
        const Point3& a = nova.vertex(quad[0]).p;
        const Point3& b = nova.vertex(quad[1]).p;
        const Point3& c = nova.vertex(quad[2]).p;
        const Vec3 nrm = (b - a).cross(c - a);
        const double edu = loop[k2].u - loop[k].u, edv = loop[k2].v - loop[k].v;
        const Vec3 outward = U * edv + V * (-edu);
        if (nrm.dot(outward) < 0.0) std::reverse(quad.begin(), quad.end());
        if (!(ok &= nova.addFace(quad) != HalfEdgeMesh::kNone) && failDetail.isEmpty())
            failDetail = QStringLiteral("parede lateral %1 não coube").arg(k);
    }
    std::string why;
    if (ok) ok = nova.checkIntegrity(&why);
    if (!ok) {
        undoLast();
        const QString detail = !failDetail.isEmpty()
                                    ? failDetail
                                : !why.empty()
                                    ? QString::fromStdString(why)
                                    : QStringLiteral("motivo desconhecido");
        const QString msg = QStringLiteral(
            "Unir: geometria resultante inválida (%1) — operação cancelada, "
            "nada foi alterado.")
                                .arg(detail);
        emit pickInfo(msg);
        return msg;
    }

    // remove os 2 originais (do maior índice pro menor — senão o 1º erase
    // desloca o índice do 2º) e insere o sólido fundido no fim; a pintura
    // dos 2 originais não migra (a topologia é inteiramente nova)
    const int hi = std::max(ia, ib), lo = std::min(ia, ib);
    m_meshes.erase(m_meshes.begin() + hi);
    m_meshes.erase(m_meshes.begin() + lo);
    MeshPart part;
    part.mesh = std::move(nova);
    m_meshes.push_back(std::move(part));
    m_selMesh = int(m_meshes.size()) - 1;
    m_selFace = HalfEdgeMesh::kNone;
    m_selWhole = true;
    m_selSolidsMulti.clear();
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit structureChanged();
    const QString msg = QStringLiteral(
                            "Unir: 2 sólidos viraram UM só (%1 lado(s) na "
                            "seção).")
                            .arg(N);
    emit pickInfo(msg);
    update();
    return msg;
}

void Viewport3D::rectClick(const QPoint& pos) {
    Point3 p;
    int kind = 0;
    if (m_rectStage == 0) {
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        Point3 hit;
        if (!pickAt(pos, mi, f, &hit)) {
            // fora de qualquer sólido: desenha NO CHÃO (z = 0)
            Point3 orig;
            Vec3 dir;
            if (!rayAt(pos, orig, dir) || std::abs(dir.z) < 1e-9) return;
            const double t = -orig.z / dir.z;
            if (t <= 0.0) return;
            m_rectN = {0, 0, 1};
            m_rectU = {1, 0, 0};
            m_rectV = {0, 1, 0};
            m_rectMesh = -2;                // sentinela: plano do chão
            m_rectFace = HalfEdgeMesh::kNone;
            m_rectP1 = orig + dir * t;
            m_rectStage = 1;
            emit pickInfo(QStringLiteral(
                "Retângulo no CHÃO: clique o canto oposto (vira um sólido "
                "novo — P para levantar)."));
            return;
        }
        applySnap(mi, f, hit, kind);
        const HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
        // base da face: U pela 1ª aresta não-ponte, V = N × U
        m_rectN = m.faceNormal(f);
        Vec3 u{};
        for (const Idx h : m.faceHalfEdges(f)) {
            if (m.isBridge(h)) continue;
            const Point3& a = m.vertex(m.halfEdge(h).origin).p;
            const Point3& b = m.vertex(m.halfEdge(m.halfEdge(h).next).origin).p;
            u = (b - a);
            if (u.lengthSq() > 1e-12) break;
        }
        m_rectU = u.normalized();
        m_rectV = m_rectN.cross(m_rectU).normalized();
        m_rectMesh = mi;
        m_rectFace = f;
        m_rectP1 = hit;
        m_rectStage = 1;
        emit pickInfo(QStringLiteral(
            "Retângulo: clique o canto OPOSTO na mesma face."));
        return;
    }
    // etapa 1 -> cria
    if (!rectPointAt(pos, p, kind)) return;
    const Vec3 off = p - m_rectP1;
    double du = off.dot(m_rectU), dv = off.dot(m_rectV);
    Point3 anc = m_rectP1;
    // R8: Ctrl = o 1º clique é o CENTRO (como no SketchUp)
    if (QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier) {
        anc = m_rectP1 - m_rectU * du - m_rectV * dv;
        du *= 2.0;
        dv *= 2.0;
    }
    if (std::abs(du) < kMinRect || std::abs(dv) < kMinRect) {
        emit pickInfo(QStringLiteral("Retângulo muito pequeno — afaste o 2º canto."));
        return;
    }
    const std::vector<Point3> corners{
        anc,
        anc + m_rectU * du,
        anc + m_rectU * du + m_rectV * dv,
        anc + m_rectV * dv};
    if (m_rectMesh == -2) {
        // NO CHÃO: nasce um sólido novo ("travesseiro" de 2 faces; o P
        // levanta a tampa e ele vira caixa fechada). R18: se sobrepõe outro
        // travesseiro cru, FUNDE num contorno só antes de criar.
        pushUndo();
        const std::size_t antes = m_meshes.size();
        const std::vector<Point3> merged = mergeGroundFootprint(corners);
        const bool fundiu = m_meshes.size() < antes;
        Idx top = HalfEdgeMesh::kNone;
        const int mi = buildGroundPillow(merged, &top);
        if (mi < 0) {
            undoLast();
            m_redo.clear();
            emit pickInfo(QStringLiteral("Não deu para criar o sólido aqui."));
            return;
        }
        m_selMesh = mi;
        m_selFace = top;
        m_lastOp = {1, -2, HalfEdgeMesh::kNone, m_rectP1, m_rectU, m_rectV,
                    du, dv};
        m_edited = true;
        cancelRectStage();
        buildRenderArrays();
        m_hlDirty = true;
        emit structureChanged();
        emit pickInfo(fundiu
                          ? QStringLiteral("Fundido com a geometria "
                                           "sobreposta — P: levantar.")
                          : QStringLiteral("Sólido criado no chão — P: "
                                           "levantar (puxar para cima)."));
        update();
        return;
    }
    // R19: se sobrepõe um vão coplanar já aberto NESSA face, funde os dois
    const Idx inner = insetOrMergeOnFace(m_rectMesh, m_rectFace, corners,
                                         m_rectP1, m_rectU, m_rectV);
    if (inner == HalfEdgeMesh::kNone) {
        emit pickInfo(QStringLiteral(
            "O retângulo precisa caber INTEIRO na face — tente de novo."));
        return;
    }
    m_selMesh = m_rectMesh;             // a face nova já nasce selecionada
    m_selFace = inner;
    m_lastOp = {1, m_rectMesh, m_rectFace, m_rectP1, m_rectU, m_rectV, du, dv};
    m_edited = true;
    const int mi = m_rectMesh;
    cancelRectStage();                  // pronto p/ o próximo retângulo
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral(
                      "Retângulo criado (%1 m²) — P: empurrar/puxar · Ctrl+Z: desfazer")
                      .arg(m_meshes[std::size_t(mi)].mesh.faceArea(m_selFace),
                           0, 'f', 2));
    update();
}

// ---------------------------------------------------------------------------
//  Ferramenta LINHA: dois pontos na BORDA da face -> splitEdge + splitFace
// ---------------------------------------------------------------------------
void Viewport3D::cancelLineStage() {
    m_lineStage = 0;
    m_lineMesh = -1;
    m_lineFace = HalfEdgeMesh::kNone;
    m_lineP1 = BoundaryPick{};
    m_ghost.clear();
    m_snapMark.clear();
    m_ghostDirty = true;
}

Viewport3D::BoundaryPick Viewport3D::boundaryPickAt(int meshIdx, Idx face,
                                                    const Point3& hit) const {
    BoundaryPick bp;
    const HalfEdgeMesh& m = m_meshes[std::size_t(meshIdx)].mesh;
    const double tol = double(m_dist) * 0.025;   // inferência generosa
    double bestV = tol * tol, bestE = tol * tol;
    Idx heEdge = HalfEdgeMesh::kNone;
    Point3 pEdge{};
    // 1º: vértices e pontos médios (prioridade); 2º: sobre a aresta
    for (const Idx h : m.faceHalfEdges(face)) {
        if (m.isBridge(h)) continue;
        const Idx va = m.halfEdge(h).origin;
        const Point3& a = m.vertex(va).p;
        const Point3& b = m.vertex(m.halfEdge(m.halfEdge(h).next).origin).p;
        const double da = (a - hit).lengthSq();
        if (da < bestV) { bestV = da; bp = {1, va, HalfEdgeMesh::kNone, a}; }
        const Point3 mid = (a + b) * 0.5;
        const double dm = (mid - hit).lengthSq();
        if (dm < bestV) { bestV = dm; bp = {2, HalfEdgeMesh::kNone, h, mid}; }
        const Vec3 ab = b - a;
        const double ll = ab.lengthSq();
        if (ll > 1e-18) {
            double u = (hit - a).dot(ab) / ll;
            u = std::clamp(u, 0.05, 0.95);        // longe das pontas
            const Point3 q = a + ab * u;
            const double dq = (q - hit).lengthSq();
            if (dq < bestE) { bestE = dq; heEdge = h; pEdge = q; }
        }
    }
    if (bp.kind == 0 && heEdge != HalfEdgeMesh::kNone)
        bp = {3, HalfEdgeMesh::kNone, heEdge, pEdge};
    return bp;
}

void Viewport3D::lineHover(const QPoint& pos) {
    m_ghost.clear();
    m_snapMark.clear();
    if (m_chainActive) {                    // fantasma do lápis
        Point3 gp;
        if (pencilPointAt(pos, gp)) {
            pushSeg(m_ghost, m_chainPt, gp);
            const Vec3 d = gp - m_chainPt;  // R4: leitura viva + direção
            const double dl = d.length();
            if (dl > 1e-9) m_lastMoveDir = d * (1.0 / dl);
            liveVcb(QStringLiteral("%1 m").arg(dl, 0, 'f', 3));
        }
        m_ghostDirty = true;
        update();
        return;
    }
    int mi = -1;
    Idx f = HalfEdgeMesh::kNone;
    Point3 hit;
    const int useMesh = m_lineStage == 1 ? m_lineMesh : -1;
    BoundaryPick bp;
    if (m_lineStage == 1) {
        // trava na face escolhida: raio contra o plano dela
        Point3 orig; Vec3 dir;
        if (rayAt(pos, orig, dir)) {
            const HalfEdgeMesh& m = m_meshes[std::size_t(useMesh)].mesh;
            const Vec3 n = m.faceNormal(m_lineFace);
            const double den = dir.dot(n);
            if (std::abs(den) > 1e-9) {
                const double t = (m_lineP1.p - orig).dot(n) / den;
                if (t > 0.0) bp = boundaryPickAt(useMesh, m_lineFace,
                                                 orig + dir * t);
            }
        }
        if (bp.kind != 0) pushSeg(m_ghost, m_lineP1.p, bp.p);
    } else if (pickAt(pos, mi, f, &hit)) {
        bp = boundaryPickAt(mi, f, hit);
    }
    // G1: o overlay tipado substitui a cruz genérica
    m_infer = bp.kind != 0 ? Infer{bp.kind, bp.p, false, {}} : Infer{};
    m_ghostDirty = true;
    update();
}

// ---------------------------------------------------------------------------
//  LÁPIS (face-healing): arestas soltas no chão; circuito fechado vira FACE
// ---------------------------------------------------------------------------
namespace {
inline std::tuple<long long, long long, long long> vkey(const Point3& p) {
    return {llround(p.x * 1e5), llround(p.y * 1e5), llround(p.z * 1e5)};
}
} // namespace

bool Viewport3D::pencilPointAt(const QPoint& pos, Point3& out) const {
    // G1: motor de inferência pleno — extremidades/médios de TUDO, alinhamento
    // pelos eixos com pontos adquiridos, travas (setas/Shift) e, com traço
    // ativo, desenhar NO ESPAÇO ao longo dos eixos (fora do chão).
    const Point3* base = m_chainActive ? &m_chainPt : nullptr;
    const Infer in = inferAt(pos, base, false, true);
    if (in.kind == 0) return false;
    m_infer = in;
    out = in.p;
    return true;
}

// circuito: BFS de b até a pelas ARESTAS EXISTENTES; achou = face nasce
bool Viewport3D::tryHealFace(const Point3& a, const Point3& b) {
    // G2: circuito coplanar fecha face em QUALQUER plano (não só no chão)
    using K = std::tuple<long long, long long, long long>;
    std::map<K, std::vector<std::pair<K, Point3>>> adj;
    std::map<K, Point3> pos;
    for (const auto& [p, q] : m_sketch) {
        adj[vkey(p)].push_back({vkey(q), q});
        adj[vkey(q)].push_back({vkey(p), p});
        pos[vkey(p)] = p;
        pos[vkey(q)] = q;
    }
    const K ka = vkey(a), kb = vkey(b);
    std::map<K, K> parent;
    std::vector<K> queue{kb};
    parent[kb] = kb;
    bool found = false;
    for (std::size_t qi = 0; qi < queue.size() && !found; ++qi) {
        for (const auto& [nk, np] : adj[queue[qi]]) {
            if (parent.count(nk)) continue;
            parent[nk] = queue[qi];
            if (nk == ka) { found = true; break; }
            queue.push_back(nk);
        }
    }
    if (!found) return false;
    std::vector<Point3> loop;                       // a -> ... -> b
    for (K k = ka; k != kb; k = parent[k]) loop.push_back(pos[k]);
    loop.push_back(b);
    if (loop.size() < 3) return false;
    // plano do circuito (Newell) + teste de COPLANARIDADE (G2)
    Vec3 n{0, 0, 0};
    Point3 c{0, 0, 0};
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Point3& p = loop[i];
        const Point3& q = loop[(i + 1) % loop.size()];
        n.x += (p.y - q.y) * (p.z + q.z);
        n.y += (p.z - q.z) * (p.x + q.x);
        n.z += (p.x - q.x) * (p.y + q.y);
        c = c + p;
    }
    const double nl = n.length();
    if (nl < 1e-6) return false;                    // degenerado (sem área)
    n = n * (1.0 / nl);
    c = c * (1.0 / double(loop.size()));
    for (const Point3& p : loop)                    // fora do plano? sem face
        if (std::abs((p - c).dot(n)) > 1e-5) {
            emit pickInfo(QStringLiteral(
                "Lápis: circuito fechado mas NÃO COPLANAR — as arestas ficam "
                "no rascunho."));
            return false;
        }
    if (n.z < 0.0 || (std::abs(n.z) < 1e-9 && n.x + n.y < 0.0)) {
        n = n * -1.0;                               // normal "pra cima/fora"
        std::reverse(loop.begin(), loop.end());
    }

    pushUndo();
    // consome as arestas do circuito (a nova a-b nem chegou a entrar)
    auto onLoop = [&](const Point3& p, const Point3& q) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Point3& u = loop[i];
            const Point3& v = loop[(i + 1) % loop.size()];
            if ((vkey(p) == vkey(u) && vkey(q) == vkey(v)) ||
                (vkey(p) == vkey(v) && vkey(q) == vkey(u)))
                return true;
        }
        return false;
    };
    m_sketch.erase(std::remove_if(m_sketch.begin(), m_sketch.end(),
                                  [&](const auto& e) {
                                      return onLoop(e.first, e.second);
                                  }),
                   m_sketch.end());
    // FACE-HEALING: a face nasce como sólido-folha (travesseiro), pullável.
    // R19: se o circuito é HORIZONTAL (chão), funde com travesseiro cru
    // sobreposto — a mesma fusão que Retângulo/Círculo já fazem no chão.
    const bool noChao = std::abs(n.z - 1.0) < 1e-6;
    // R20: se NÃO é chão, procura uma face JÁ EXISTENTE coplanar (parede
    // etc.) — vira um vão nela (inset limpo ou fusão com vão sobreposto),
    // igual ao Retângulo/Círculo já fazem sobre face, em vez de nascer
    // sólido-folha solto.
    if (!noChao) {
        for (std::size_t mi = 0; mi < m_meshes.size(); ++mi) {
            HalfEdgeMesh& fm = m_meshes[mi].mesh;
            for (Idx f = 0; f < Idx(fm.faceCount()); ++f) {
                const Vec3 fn = fm.faceNormal(f);
                if (std::abs(fn.dot(n)) < 0.999) continue;   // não coplanar
                const Point3 p0 = fm.vertex(fm.faceVertices(f)[0]).p;
                if (std::abs((p0 - c).dot(n)) > 1e-4) continue;  // outro plano
                Vec3 u{};
                for (const Idx h : fm.faceHalfEdges(f)) {
                    if (fm.isBridge(h)) continue;
                    const Point3& ea = fm.vertex(fm.halfEdge(h).origin).p;
                    const Point3& eb =
                        fm.vertex(fm.halfEdge(fm.halfEdge(h).next).origin).p;
                    u = eb - ea;
                    if (u.lengthSq() > 1e-12) break;
                }
                if (u.lengthSq() < 1e-12) continue;
                u = u.normalized();
                const Vec3 v = fn.cross(u).normalized();
                // o traço do Lápis pode ter saído CW nessa base — reorienta
                // pra CCW (mesma convenção do Retângulo/Círculo) antes de
                // tentar, senão o inset pode falhar ou sair invertido
                auto to2 = [&](const Point3& p) {
                    const Vec3 o = p - p0;
                    return std::pair<double, double>{o.dot(u), o.dot(v)};
                };
                double area2 = 0.0;
                for (std::size_t i = 0; i < loop.size(); ++i) {
                    const auto uv0 = to2(loop[i]);
                    const auto uv1 = to2(loop[(i + 1) % loop.size()]);
                    area2 += uv0.first * uv1.second - uv1.first * uv0.second;
                }
                std::vector<Point3> poly = loop;
                if (area2 < 0.0) std::reverse(poly.begin(), poly.end());
                const Idx inner = insetOrMergeOnFace(int(mi), f, poly, p0, u, v);
                if (inner == HalfEdgeMesh::kNone) continue;
                m_selMesh = int(mi);
                m_selFace = inner;
                m_selWhole = false;
                m_chainActive = false;
                m_edited = true;
                buildRenderArrays();
                m_hlDirty = true;
                emit structureChanged();
                emit pickInfo(QStringLiteral(
                                  "✏️ Circuito fechado: vão aberto na face "
                                  "existente (%1 m²) — P: puxar!")
                                  .arg(fm.faceArea(inner), 0, 'f', 2));
                update();
                return true;
            }
        }
    }
    const std::size_t antes = m_meshes.size();
    const std::vector<Point3> pts = noChao ? mergeGroundFootprint(loop) : loop;
    const bool fundiu = noChao && m_meshes.size() < antes;
    MeshPart part;
    HalfEdgeMesh& m = part.mesh;
    std::vector<Idx> top, bot;
    for (const Point3& p : pts) top.push_back(m.addVertex(p));
    for (const Point3& p : pts)                     // folha fina PELA NORMAL
        bot.push_back(m.addVertex(p - n * 0.001));
    std::vector<Idx> rev(bot.rbegin(), bot.rend());
    const Idx topF = m.addFace(top);
    m.addFace(rev);
    std::string why;
    if (topF == HalfEdgeMesh::kNone || !m.checkIntegrity(&why)) {
        undoLast();
        m_redo.clear();          // estado inválido não pode virar refazer
        return false;
    }
    m_meshes.push_back(std::move(part));
    m_selMesh = int(m_meshes.size()) - 1;
    m_selFace = topF;
    m_selWhole = false;
    m_chainActive = false;
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit structureChanged();
    emit pickInfo(fundiu
                      ? QStringLiteral("✏️ Circuito fechado: fundido com a "
                                       "geometria sobreposta — P: puxar!")
                      : QStringLiteral("✏️ Circuito fechado: FACE criada "
                                       "(%1 lados) — P: puxar!")
                            .arg(pts.size()));
    update();
    return true;
}

void Viewport3D::pencilClick(Point3 p) {
    if (!m_chainActive) {
        m_chainPt = p;
        m_chainActive = true;
        emit pickInfo(QStringLiteral(
            "Lápis: clique o próximo ponto — feche o circuito para nascer a "
            "FACE (Esc encerra o traço)."));
        return;
    }
    if ((p - m_chainPt).lengthSq() < 1e-8) return;
    // R5.1: fechamento TOLERANTE — ponto digitado/quase-lá gruda no
    // extremo de rascunho mais próximo (1 mm) antes de tentar curar
    {
        double best = 1e-6;               // (1e-3 m)²
        Point3 snap = p;
        auto consider = [&](const Point3& v) {
            const double d = (v - p).lengthSq();
            if (d < best) { best = d; snap = v; }
        };
        for (const auto& [a, b] : m_sketch) { consider(a); consider(b); }
        p = snap;
    }
    // fecha circuito? (verifica ANTES de inserir a aresta nova)
    if (tryHealFace(m_chainPt, p)) return;
    pushUndo();
    m_sketch.push_back({m_chainPt, p});
    m_chainPt = p;
    m_edited = true;
    buildRenderArrays();
    emit pickInfo(QStringLiteral("Lápis: %1 aresta(s) no rascunho.")
                      .arg(m_sketch.size()));
    update();
}

void Viewport3D::lineClick(const QPoint& pos) {
    if (m_lineStage == 0) {
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        Point3 hit;
        if (!pickAt(pos, mi, f, &hit)) {
            // fora de qualquer face: LÁPIS no chão (arestas soltas)
            Point3 gp;
            if (pencilPointAt(pos, gp)) pencilClick(gp);
            return;
        }
        const BoundaryPick bp = boundaryPickAt(mi, f, hit);
        if (bp.kind == 0) {
            emit pickInfo(QStringLiteral(
                              "Linha: aproxime o clique de uma ARESTA da face "
                              "(toque em %1; %2; %3).")
                              .arg(hit.x, 0, 'f', 2)
                              .arg(hit.y, 0, 'f', 2)
                              .arg(hit.z, 0, 'f', 2));
            return;
        }
        m_lineMesh = mi;
        m_lineFace = f;
        m_lineP1 = bp;
        m_lineStage = 1;
        emit pickInfo(QStringLiteral(
            "Linha: clique o 2º ponto noutra aresta da MESMA face."));
        return;
    }
    // 2º ponto: resolve contra o plano da face escolhida
    Point3 orig; Vec3 dir;
    if (!rayAt(pos, orig, dir)) return;
    HalfEdgeMesh& mesh = m_meshes[std::size_t(m_lineMesh)].mesh;
    const Vec3 n = mesh.faceNormal(m_lineFace);
    const double den = dir.dot(n);
    if (std::abs(den) < 1e-9) return;
    const double t = (m_lineP1.p - orig).dot(n) / den;
    if (t <= 0.0) return;
    const BoundaryPick bp2 = boundaryPickAt(m_lineMesh, m_lineFace,
                                            orig + dir * t);
    if (bp2.kind == 0) {
        emit pickInfo(QStringLiteral(
            "Linha: o 2º ponto também precisa estar numa aresta da face."));
        return;
    }
    if ((bp2.kind != 1 && m_lineP1.kind != 1 && bp2.he == m_lineP1.he) ||
        (bp2.kind == 1 && m_lineP1.kind == 1 && bp2.vertex == m_lineP1.vertex)) {
        emit pickInfo(QStringLiteral(
            "Linha: pontos na MESMA aresta não dividem a face."));
        return;
    }
    pushUndo();
    // resolve os vértices (splitEdge quando o ponto cai na aresta/meio)
    const Idx vA = m_lineP1.kind == 1 ? m_lineP1.vertex
                                      : mesh.splitEdge(m_lineP1.he, m_lineP1.p);
    const Idx vB = bp2.kind == 1 ? bp2.vertex
                                 : mesh.splitEdge(bp2.he, bp2.p);
    std::string why;
    Idx g = HalfEdgeMesh::kNone;
    if (vA != HalfEdgeMesh::kNone && vB != HalfEdgeMesh::kNone && vA != vB)
        g = mesh.splitFace(m_lineFace, vA, vB);
    if (g == HalfEdgeMesh::kNone || !mesh.checkIntegrity(&why)) {
        undoLast();
        emit pickInfo(QStringLiteral(
            "Linha: esses dois pontos não dividem a face (tente arestas "
            "opostas)."));
        return;
    }
    m_selMesh = m_lineMesh;
    m_selFace = g;
    m_edited = true;
    const int mi = m_lineMesh;
    cancelLineStage();
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral(
                      "Face dividida (%1 m²) — P: empurrar/puxar · Ctrl+Z: desfazer")
                      .arg(m_meshes[std::size_t(mi)].mesh.faceArea(m_selFace),
                           0, 'f', 2));
    update();
}

bool Viewport3D::moveSelectedZ(double dz) {
    if (m_selMesh < 0 || m_selMesh >= int(m_meshes.size()) ||
        std::abs(dz) < 1e-9) {
        emit pickInfo(QStringLiteral(
            "Mover em Z: selecione um sólido (duplo clique) primeiro."));
        return false;
    }
    pushUndo();
    dragDimAnchors(m_meshes[std::size_t(m_selMesh)].mesh, {0.0, 0.0, dz});
    m_meshes[std::size_t(m_selMesh)].mesh.translate({0.0, 0.0, dz});
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Sólido %1 %2 m — Ctrl+Z desfaz.")
                      .arg(dz > 0 ? QStringLiteral("subiu")
                                  : QStringLiteral("desceu"))
                      .arg(std::abs(dz), 0, 'f', 2));
    update();
    return true;
}

// TELHADO de duas águas: prisma sobre o bounding-box do sólido selecionado
// (beiral = o próprio bbox; cumeeira ao longo do eixo maior).
bool Viewport3D::roofSelected(double ridgeH, double beiral, bool hip) {
    if (m_selMesh < 0 || m_selMesh >= int(m_meshes.size()) || ridgeH < 0.05) {
        emit pickInfo(QStringLiteral(
            "Telhado: selecione o sólido-base primeiro (duplo clique)."));
        return false;
    }
    const HalfEdgeMesh& base = m_meshes[std::size_t(m_selMesh)].mesh;

    // R55: o telhado agora ACOMPANHA O GIRO do sólido.
    // Até aqui esta função media o bbox no eixo do MUNDO — sobre um sólido
    // girado o telhado saía reto, e o "beiral" era medido da CAIXA que envolve
    // o sólido, não das paredes: no dogfooding da R54, um volume a 8° ganhou
    // um telhado ortogonal com beiral fantasma de até 1,2 m variando ao longo
    // da borda. O Fable previu isto lendo este código, e o meu próprio script
    // de entrega desviava da ferramenta construindo o telhado à mão por causa
    // disso — quando a entrega desvia da ferramenta, a ferramenta está devendo.
    //
    // A cura não é refazer nada: é medir no FRAME DO SÓLIDO. Acho o ângulo
    // pela aresta horizontal mais longa do SÓLIDO (o loop varre todo z, não só
    // o footprint — numa caixa dá no mesmo; num sólido cuja aresta horizontal
    // mais longa esteja no topo, o θ vem de lá, e ainda assim é melhor que o
    // θ=0 forçado de antes), projeto os vértices lá,
    // faço exatamente a mesma conta de sempre em coordenadas locais, e giro
    // os 6 vértices de volta no fim.
    // Sólido alinhado dá θ = 0 EXATO (atan2(0,L) = 0 → cos=1, sin=0): a saída
    // fica byte-idêntica à de antes, e a regressão sai de graça.
    double theta = 0.0;
    {
        double melhor = -1.0;
        for (std::size_t h = 0; h < base.halfEdgeCount(); ++h) {
            const Idx he = Idx(h);
            const Point3& p = base.vertex(base.halfEdge(he).origin).p;
            const Point3& q =
                base.vertex(base.halfEdge(base.halfEdge(he).next).origin).p;
            if (std::abs(p.z - q.z) > 1e-6) continue;       // só arestas horiz.
            const double dx = q.x - p.x, dy = q.y - p.y;
            const double L2 = dx * dx + dy * dy;
            if (L2 <= melhor || L2 < 1e-12) continue;
            melhor = L2;
            theta = std::atan2(dy, dx);
        }
        // dobra pra [0°, 90°): o retângulo tem 4 arestas, e as 4 descrevem o
        // mesmo frame a menos de múltiplos de 90°.
        const double q90 = M_PI * 0.5;
        theta = std::fmod(theta, q90);
        if (theta < 0) theta += q90;
        if (theta > q90 * 0.5) theta -= q90;      // escolhe o giro menor
    }
    const double ct = std::cos(theta), st = std::sin(theta);
    const auto paraLocal = [ct, st](double x, double y) {
        return std::pair<double, double>{x * ct + y * st, -x * st + y * ct};
    };
    const auto paraMundo = [ct, st](double x, double y) {
        return std::pair<double, double>{x * ct - y * st, x * st + y * ct};
    };

    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300, zt = -1e300;
    for (std::size_t v = 0; v < base.vertexCount(); ++v) {
        const Point3& p = base.vertex(Idx(v)).p;
        const auto l = paraLocal(p.x, p.y);          // bbox NO FRAME do sólido
        x0 = std::min(x0, l.first);  x1 = std::max(x1, l.first);
        y0 = std::min(y0, l.second); y1 = std::max(y1, l.second);
        zt = std::max(zt, p.z);
    }
    x0 -= beiral; x1 += beiral;                  // BEIRAL: avança o bbox
    y0 -= beiral; y1 += beiral;
    pushUndo();
    MeshPart part;
    HalfEdgeMesh& m = part.mesh;
    const bool ridgeX = (x1 - x0) >= (y1 - y0);   // cumeeira no eixo longo
    const double zr = zt + ridgeH;
    Idx a, b, c, d, r0, r1;
    // addVertex em MUNDO, computado em LOCAL: o resto da função (tacaniça,
    // oitões, base) segue idêntico, porque no frame local ele é o de sempre.
    const auto vLocal = [&m, &paraMundo](double lx, double ly, double z) {
        const auto w = paraMundo(lx, ly);
        return m.addVertex({w.first, w.second, z});
    };
    a = vLocal(x0, y0, zt);
    b = vLocal(x1, y0, zt);
    c = vLocal(x1, y1, zt);
    d = vLocal(x0, y1, zt);
    // QUATRO águas: cumeeira encurtada (tacaniças a 45°)
    const double dHip =
        hip ? std::min((ridgeX ? y1 - y0 : x1 - x0) * 0.5,
                       (ridgeX ? x1 - x0 : y1 - y0) * 0.5 * 0.98)
            : 0.0;
    if (ridgeX) {
        const double ym = (y0 + y1) * 0.5;
        r0 = vLocal(x0 + dHip, ym, zr);
        r1 = vLocal(x1 - dHip, ym, zr);
        m.addFace({a, b, r1, r0});   // água sul
        m.addFace({c, d, r0, r1});   // água norte
        m.addFace({d, a, r0});       // oitão oeste
        m.addFace({b, c, r1});       // oitão leste
    } else {
        const double xm = (x0 + x1) * 0.5;
        r0 = vLocal(xm, y0 + dHip, zr);
        r1 = vLocal(xm, y1 - dHip, zr);
        m.addFace({b, c, r1, r0});   // água leste
        m.addFace({d, a, r0, r1});   // água oeste
        m.addFace({a, b, r0});       // oitão sul
        m.addFace({c, d, r1});       // oitão norte
    }
    m.addFace({a, d, c, b});         // base
    std::string why;
    if (!m.checkIntegrity(&why)) {
        m_undo.pop_back();
        return false;
    }
    m_meshes.push_back(std::move(part));
    m_selMesh = int(m_meshes.size()) - 1;
    m_selFace = 0;
    m_selWhole = true;
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral(
                      "Telhado de duas águas criado (cumeeira +%1 m).")
                      .arg(ridgeH, 0, 'f', 2));
    update();
    return true;
}

// TERRENO: platô sólido sob o modelo inteiro (margem ao redor).
bool Viewport3D::addTerrain(double thick, double margin) {
    double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
    for (const MeshPart& p : m_meshes)
        for (std::size_t v = 0; v < p.mesh.vertexCount(); ++v) {
            const Point3& q = p.mesh.vertex(Idx(v)).p;
            x0 = std::min(x0, q.x); x1 = std::max(x1, q.x);
            y0 = std::min(y0, q.y); y1 = std::max(y1, q.y);
        }
    if (x0 > x1) { x0 = -8; x1 = 8; y0 = -8; y1 = 8; }
    x0 -= margin; x1 += margin; y0 -= margin; y1 += margin;
    pushUndo();
    MeshPart part;
    HalfEdgeMesh& m = part.mesh;
    const double zb = -thick, zt2 = -0.002;   // topo 2 mm abaixo do z=0
    const double tal = thick * 2.0;           // TALUDE: base avança 2:1
    const Idx v0 = m.addVertex({x0 - tal, y0 - tal, zb});
    const Idx v1 = m.addVertex({x1 + tal, y0 - tal, zb});
    const Idx v2 = m.addVertex({x1 + tal, y1 + tal, zb});
    const Idx v3 = m.addVertex({x0 - tal, y1 + tal, zb});
    const Idx t0 = m.addVertex({x0, y0, zt2});
    const Idx t1 = m.addVertex({x1, y0, zt2});
    const Idx t2 = m.addVertex({x1, y1, zt2});
    const Idx t3 = m.addVertex({x0, y1, zt2});
    m.addFace({t0, t1, t2, t3});
    m.addFace({v3, v2, v1, v0});
    m.addFace({v0, v1, t1, t0});
    m.addFace({v2, v3, t3, t2});
    m.addFace({v1, v2, t2, t1});
    m.addFace({v3, v0, t0, t3});
    m_meshes.push_back(std::move(part));
    m_meshes.back().faceColors[0] = {0.42f, 0.47f, 0.40f};   // topo verde-terra
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Terreno criado sob o modelo."));
    update();
    return true;
}

// ---------------------------------------------------------------------------
//  S2: ocultar/isolar/mostrar + COMPONENTES (definição compartilhada)
// ---------------------------------------------------------------------------
void Viewport3D::hideSelected() {
    if (!m_selSolidsMulti.empty()) {       // G3: oculta a multi inteira
        pushUndo();
        for (const int mi : m_selSolidsMulti)
            if (mi >= 0 && mi < int(m_meshes.size()))
                m_meshes[std::size_t(mi)].hidden = true;
        const std::size_t n = m_selSolidsMulti.size();
        m_selSolidsMulti.clear();
        m_edited = true;
        buildRenderArrays();
        m_hlDirty = true;
        emit pickInfo(QStringLiteral("%1 sólido(s) oculto(s) — 'Mostrar "
                                     "tudo' traz de volta.").arg(n));
        update();
        return;
    }
    if (m_selMesh < 0) {
        emit pickInfo(QStringLiteral("Ocultar: selecione um sólido antes."));
        return;
    }
    pushUndo();
    m_meshes[std::size_t(m_selMesh)].hidden = true;
    m_selMesh = -1;
    m_selFace = HalfEdgeMesh::kNone;
    m_selWhole = false;
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Sólido oculto — 'Mostrar tudo' traz de volta."));
    update();
}

void Viewport3D::isolateSelected() {
    if (m_selMesh < 0) {
        emit pickInfo(QStringLiteral("Isolar: selecione um sólido antes."));
        return;
    }
    pushUndo();
    int shown = 0;
    for (int i = 0; i < int(m_meshes.size()); ++i) {
        m_meshes[std::size_t(i)].hidden = (i != m_selMesh);
        if (!m_meshes[std::size_t(i)].hidden) ++shown;
    }
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Isolado (1 de %1 visível).")
                      .arg(m_meshes.size()));
    update();
    (void)shown;
}

void Viewport3D::showAll() {
    pushUndo();
    for (MeshPart& p : m_meshes) p.hidden = false;
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Tudo visível."));
    update();
}

// R15: OBJ vira componente do usuário — soldado, escalado, eixo convertido
QString Viewport3D::importObjComponent(const QString& path, double escala,
                                       bool yUp) {
    objimp::Resultado r;
    QString erro;
    if (!objimp::importar(path, r, &erro)) {
        emit pickInfo(QStringLiteral("Import OBJ: %1").arg(erro));
        return QString();
    }
    if (yUp)                              // Blender e afins: Y-up -> Z-up
        r.mesh.rotateAxis({0, 0, 0}, {1, 0, 0}, 1.5707963267948966);
    if (escala > 1e-9 && std::abs(escala - 1.0) > 1e-12)
        r.mesh.scaleAbout({0, 0, 0}, escala);
    const QString base = QFileInfo(path).completeBaseName();
    QString nome = base;
    for (int i = 2; m_compDefs.count(nome); ++i)
        nome = QStringLiteral("%1 %2").arg(base).arg(i);
    MeshPart def;
    def.mesh = std::move(r.mesh);
    def.faceColors = std::move(r.cores);
    def.compName = nome;
    const Point3 c = def.mesh.bboxCenter();
    m_compDefs[nome] = std::move(def);
    m_edited = true;
    emit pickInfo(QStringLiteral("OBJ importado: \"%1\" (%2 faces, %3 "
                                 "descartadas) — clique para posicionar.")
                      .arg(nome)
                      .arg(r.faces)
                      .arg(r.descartadas));
    (void)c;
    return nome;
}

// ============================================================================
//  R17: A OFICINA — o kit que todo usuário de SketchUp instala no 1º dia
// ============================================================================
namespace {
// bordas abertas de uma malha como pares (origem, destino)
std::vector<std::pair<HalfEdgeMesh::Idx, HalfEdgeMesh::Idx>> bordasDe(
    const HalfEdgeMesh& m) {
    std::vector<std::pair<HalfEdgeMesh::Idx, HalfEdgeMesh::Idx>> out;
    for (HalfEdgeMesh::Idx h = 0; h < HalfEdgeMesh::Idx(m.halfEdgeCount());
         ++h) {
        const auto& he = m.halfEdge(h);
        if (he.twin != HalfEdgeMesh::kNone) continue;
        out.push_back({he.origin, m.halfEdge(he.next).origin});
    }
    return out;
}
// encadeia as bordas em LOOPS na direção reversa (b -> a): é a orientação
// que faz a tampa nascer com a normal pra FORA
std::vector<std::vector<HalfEdgeMesh::Idx>> loopsDeBorda(
    const std::vector<std::pair<HalfEdgeMesh::Idx, HalfEdgeMesh::Idx>>& bs) {
    std::map<HalfEdgeMesh::Idx, HalfEdgeMesh::Idx> prox;   // b -> a
    for (const auto& [a, b] : bs) prox[b] = a;
    std::vector<std::vector<HalfEdgeMesh::Idx>> loops;
    std::set<HalfEdgeMesh::Idx> visto;
    for (const auto& [ini, fim] : prox) {
        if (visto.count(ini)) continue;
        std::vector<HalfEdgeMesh::Idx> loop;
        HalfEdgeMesh::Idx v = ini;
        while (!visto.count(v) && prox.count(v)) {
            visto.insert(v);
            loop.push_back(v);
            v = prox[v];
        }
        if (loop.size() >= 3 && v == ini) loops.push_back(std::move(loop));
    }
    return loops;
}
} // namespace

// Inspetor de Sólidos (≈ Solid Inspector²): quem está aberto ou doente?
QString Viewport3D::inspectSolids() {
    QStringList problemas;
    int fechados = 0;
    for (int i = 0; i < int(m_meshes.size()); ++i) {
        const MeshPart& p = m_meshes[std::size_t(i)];
        std::string why;
        const bool integro = p.mesh.checkIntegrity(&why);
        const auto bordas = bordasDe(p.mesh);
        if (integro && bordas.empty()) {
            ++fechados;
            continue;
        }
        QString linha = partLabel(i) + QStringLiteral(": ");
        if (!bordas.empty())
            linha += QStringLiteral("%1 borda(s) aberta(s) em %2 furo(s)")
                         .arg(bordas.size())
                         .arg(loopsDeBorda(bordas).size());
        if (!integro)
            linha += QStringLiteral(" [integridade: %1]")
                         .arg(QString::fromStdString(why));
        problemas << linha;
    }
    const QString rel =
        problemas.isEmpty()
            ? QStringLiteral("Inspetor: todos os %1 sólido(s) fechados e "
                             "íntegros ✓")
                  .arg(m_meshes.size())
            : QStringLiteral("Inspetor: %1 — selecione o sólido e use "
                             "\"Consertar\" pra tampar.")
                  .arg(problemas.join(QStringLiteral(" · ")));
    emit pickInfo(rel);
    return rel;
}

// Consertar: tampa cada loop de borda do sólido selecionado com uma face
int Viewport3D::fixSelectedSolid() {
    if (m_selMesh < 0 || m_selMesh >= int(m_meshes.size())) {
        emit pickInfo(QStringLiteral(
            "Consertar: selecione o sólido furado antes (o Inspetor aponta)."));
        return 0;
    }
    MeshPart& p = m_meshes[std::size_t(m_selMesh)];
    const auto bordas = bordasDe(p.mesh);
    if (bordas.empty()) {
        emit pickInfo(QStringLiteral("Esse sólido já está fechado ✓"));
        return 0;
    }
    const auto loops = loopsDeBorda(bordas);
    if (loops.empty()) {
        emit pickInfo(QStringLiteral(
            "Consertar: as bordas não fecham loops — furo degenerado."));
        return 0;
    }
    pushUndo();
    HalfEdgeMesh nova;
    for (std::size_t v = 0; v < p.mesh.vertexCount(); ++v)
        nova.addVertex(p.mesh.vertex(Idx(v)).p);
    bool ok = true;
    for (std::size_t f = 0; f < p.mesh.faceCount(); ++f)
        ok &= nova.addFace(p.mesh.faceVertices(Idx(f))) != HalfEdgeMesh::kNone;
    int caps = 0;
    for (auto loop : loops) {
        if (nova.addFace(loop) != HalfEdgeMesh::kNone) {
            ++caps;
            continue;
        }
        std::reverse(loop.begin(), loop.end());
        if (nova.addFace(loop) != HalfEdgeMesh::kNone) ++caps;
    }
    std::string why;
    if (!ok || caps == 0 || !nova.checkIntegrity(&why)) {
        undoLast();
        m_redo.clear();
        emit pickInfo(QStringLiteral("Consertar: a costura falhou (%1).")
                          .arg(QString::fromStdString(why)));
        return 0;
    }
    p.mesh = std::move(nova);            // faces antigas mantêm os índices
    m_edited = true;
    m_hlDirty = true;
    buildRenderArrays();
    emit pickInfo(QStringLiteral("Consertado: %1 furo(s) tampado(s) — "
                                 "Ctrl+Z desfaz.")
                      .arg(caps));
    update();
    return caps;
}

// Limpeza (≈ CleanUp³): dissolve arestas entre faces coplanares de MESMO
// material e faz purge dos componentes de usuário sem instância
QString Viewport3D::cleanupModel() {
    pushUndo();
    int dissolvidas = 0, purgados = 0;
    for (MeshPart& p : m_meshes) {
        const auto matDe = [&p](Idx f) -> QString {
            const auto t = p.faceTex.find(f);
            if (t != p.faceTex.end()) return QStringLiteral("T:") + t->second;
            const auto c = p.faceColors.find(f);
            if (c == p.faceColors.end()) return QStringLiteral("w");
            return QStringLiteral("C:%1,%2,%3")
                .arg(c->second[0])
                .arg(c->second[1])
                .arg(c->second[2]);
        };
        bool denovo = true;
        while (denovo) {
            denovo = false;
            const HalfEdgeMesh& m = p.mesh;
            for (Idx h = 0; h < Idx(m.halfEdgeCount()); ++h) {
                const auto& he = m.halfEdge(h);
                if (he.twin == HalfEdgeMesh::kNone || he.twin < h) continue;
                if (m.isBridge(h)) continue;
                const Idx fa = he.face, fb = m.halfEdge(he.twin).face;
                if (fa == fb) continue;
                if (m.faceNormal(fa).dot(m.faceNormal(fb)) < 0.99999)
                    continue;
                if (matDe(fa) != matDe(fb)) continue;   // não mistura pintura
                Idx gone = HalfEdgeMesh::kNone;
                if (!p.mesh.dissolveEdge(h, &gone)) continue;
                ++dissolvidas;
                std::map<Idx, std::array<float, 3>> nc;
                for (const auto& [f, col] : p.faceColors) {
                    if (f == gone) continue;
                    nc[f > gone ? f - 1 : f] = col;
                }
                p.faceColors = std::move(nc);
                std::map<Idx, QString> nt;
                for (const auto& [f, t] : p.faceTex) {
                    if (f == gone) continue;
                    nt[f > gone ? f - 1 : f] = t;
                }
                p.faceTex = std::move(nt);
                denovo = true;
                break;                     // a malha foi reconstruída
            }
        }
    }
    for (auto it = m_compDefs.begin(); it != m_compDefs.end();) {
        const bool usado =
            m_libComps.contains(it->first) ||
            std::any_of(m_meshes.begin(), m_meshes.end(),
                        [&](const MeshPart& mp) {
                            return mp.compName == it->first;
                        });
        if (usado) {
            ++it;
        } else {
            it = m_compDefs.erase(it);
            ++purgados;
        }
    }
    if (dissolvidas == 0 && purgados == 0) {
        m_undo.pop_back();
        const QString rel =
            QStringLiteral("Limpeza: nada a limpar — modelo já enxuto ✓");
        emit pickInfo(rel);
        return rel;
    }
    m_selMesh = -1;
    m_selFace = HalfEdgeMesh::kNone;
    m_selWhole = false;
    m_edited = true;
    m_hlDirty = true;
    buildRenderArrays();
    emit structureChanged();
    const QString rel = QStringLiteral("Limpeza: %1 aresta(s) coplanar(es) "
                                       "dissolvida(s) · %2 definição(ões) "
                                       "purgada(s) — Ctrl+Z desfaz.")
                            .arg(dissolvidas)
                            .arg(purgados);
    emit pickInfo(rel);
    update();
    return rel;
}

// Espelhar (≈ Curic Mirror): reflete os alvos no eixo, pivô = centro do
// conjunto; windings invertidos preservando a ORDEM das faces (cores ficam)
void Viewport3D::mirrorSelected(int axis) {
    const auto alvo = gestureTargets();
    if (alvo.empty()) {
        emit pickInfo(QStringLiteral(
            "Espelhar: selecione um sólido, multi ou grupo antes."));
        return;
    }
    Point3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (const int mi : alvo) {
        const HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
        for (std::size_t v = 0; v < m.vertexCount(); ++v) {
            const Point3& q = m.vertex(Idx(v)).p;
            lo.x = std::min(lo.x, q.x); hi.x = std::max(hi.x, q.x);
            lo.y = std::min(lo.y, q.y); hi.y = std::max(hi.y, q.y);
            lo.z = std::min(lo.z, q.z); hi.z = std::max(hi.z, q.z);
        }
    }
    const double at = axis == 0 ? (lo.x + hi.x) / 2
                     : axis == 1 ? (lo.y + hi.y) / 2
                                 : (lo.z + hi.z) / 2;
    pushUndo();
    for (const int mi : alvo) {
        MeshPart& p = m_meshes[std::size_t(mi)];
        HalfEdgeMesh nova;
        for (std::size_t v = 0; v < p.mesh.vertexCount(); ++v) {
            Point3 q = p.mesh.vertex(Idx(v)).p;
            if (axis == 0) q.x = 2 * at - q.x;
            else if (axis == 1) q.y = 2 * at - q.y;
            else q.z = 2 * at - q.z;
            nova.addVertex(q);
        }
        bool ok = true;
        for (std::size_t f = 0; f < p.mesh.faceCount(); ++f) {
            auto loop = p.mesh.faceVertices(Idx(f));
            std::reverse(loop.begin(), loop.end());
            ok &= nova.addFace(loop) != HalfEdgeMesh::kNone;
        }
        if (!ok) {
            undoLast();
            m_redo.clear();
            emit pickInfo(QStringLiteral(
                "Espelhar: a malha resistiu (winding não fechou)."));
            return;
        }
        p.mesh = std::move(nova);
        p.hiddenEdges.clear();           // chaves de posição mudaram
    }
    m_edited = true;
    m_hlDirty = true;
    buildRenderArrays();
    emit pickInfo(QStringLiteral("Espelhado no eixo %1 — Ctrl+Z desfaz.")
                      .arg(axis == 0 ? QLatin1Char('X')
                           : axis == 1 ? QLatin1Char('Y')
                                       : QLatin1Char('Z')));
    update();
}

// R14: componente de FÁBRICA — vai pra biblioteca, não pro arquivo
void Viewport3D::addLibraryComponent(
    const QString& name, const cad::HalfEdgeMesh& mesh,
    const std::map<cad::HalfEdgeMesh::Idx, std::array<float, 3>>& cores,
    bool organico) {
    if (m_compDefs.count(name)) return;      // o do usuário tem prioridade
    MeshPart def;
    def.mesh = mesh;
    def.faceColors = cores;
    def.compName = name;
    m_compDefs[name] = std::move(def);
    m_libComps.insert(name);
    if (organico) m_organicos.insert(name);
}

bool Viewport3D::makeComponent(const QString& name) {
    if (m_selMesh < 0 || name.trimmed().isEmpty()) {
        emit pickInfo(QStringLiteral(
            "Componente: selecione um sólido e dê um nome."));
        return false;
    }
    pushUndo();
    m_meshes[std::size_t(m_selMesh)].compName = name;
    m_compDefs[name] = m_meshes[std::size_t(m_selMesh)];
    // R55: o nome deixou de ser o da fábrica — larga o jitter junto. Sem isto,
    // um componente DO USUÁRIO batizado de "Árvore" (o nome óbvio em pt-BR)
    // herdaria o giro aleatório e o ±15% da vegetação, calado.
    m_organicos.remove(name);
    m_edited = true;
    emit pickInfo(QStringLiteral(
                      "Componente \"%1\" criado — insira cópias pelo menu; "
                      "redefinir propaga a TODAS.")
                      .arg(name));
    return true;
}

bool Viewport3D::insertComponent(const QString& name, const Point3& at) {
    const auto it = m_compDefs.find(name);
    if (it == m_compDefs.end()) return false;
    pushUndo();
    MeshPart inst = it->second;
    // R55: vegetação não nasce em fôrma. Gira num ângulo qualquer e muda de
    // porte ±15%. A semente vem da POSIÇÃO: a mesma árvore no mesmo ponto sai
    // sempre igual (undo/redo e reabrir o arquivo não a remexem), mas duas
    // árvores lado a lado nunca são a mesma árvore.
    if (m_organicos.contains(name)) {
        unsigned s = unsigned(std::llround(at.x * 1000.0) * 73856093LL ^
                              std::llround(at.y * 1000.0) * 19349663LL);
        const auto frand = [&s]() {
            s = s * 1103515245u + 12345u;
            return double((s >> 16) & 0x7fff) / 32767.0;
        };
        const Point3 piv = inst.mesh.bboxCenter();
        inst.mesh.rotateZ(piv, frand() * 2.0 * 3.14159265358979);
        inst.mesh.scaleAbout(piv, 0.85 + 0.30 * frand());
    }
    // assenta a instância: centro XY no clique, base no z do clique
    double minZ = 1e300;
    for (std::size_t v = 0; v < inst.mesh.vertexCount(); ++v)
        minZ = std::min(minZ, inst.mesh.vertex(Idx(v)).p.z);
    const Point3 c = inst.mesh.bboxCenter();
    inst.mesh.translate({at.x - c.x, at.y - c.y, at.z - minZ});
    m_meshes.push_back(std::move(inst));
    m_selMesh = int(m_meshes.size()) - 1;
    m_selFace = 0;
    m_selWhole = true;
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("\"%1\" inserido — M move, redefinir propaga.")
                      .arg(name));
    update();
    return true;
}

int Viewport3D::redefineComponent() {
    if (m_selMesh < 0 ||
        m_meshes[std::size_t(m_selMesh)].compName.isEmpty()) {
        emit pickInfo(QStringLiteral(
            "Redefinir: selecione uma INSTÂNCIA de componente."));
        return 0;
    }
    pushUndo();
    const QString name = m_meshes[std::size_t(m_selMesh)].compName;
    // R42: dims da DEF ANTIGA — instância girada/escalada tem bbox diferente
    // e é PRESERVADA (antes, a reconstrução perdia o giro em silêncio).
    const auto dimsOf = [](const HalfEdgeMesh& mm) {
        Point3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
        for (std::size_t v = 0; v < mm.vertexCount(); ++v) {
            const Point3& p = mm.vertex(HalfEdgeMesh::Idx(v)).p;
            lo.x = std::min(lo.x, p.x); hi.x = std::max(hi.x, p.x);
            lo.y = std::min(lo.y, p.y); hi.y = std::max(hi.y, p.y);
            lo.z = std::min(lo.z, p.z); hi.z = std::max(hi.z, p.z);
        }
        return cad::Vec3{hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    };
    cad::Vec3 oldDims{-1, -1, -1};
    const auto itOld = m_compDefs.find(name);
    if (itOld != m_compDefs.end()) oldDims = dimsOf(itOld->second.mesh);
    MeshPart def = m_meshes[std::size_t(m_selMesh)];
    m_compDefs[name] = def;
    m_organicos.remove(name);        // R55: idem — a def agora é do usuário
    const Point3 dc = def.mesh.bboxCenter();
    int changed = 0, kept = 0;
    for (int i = 0; i < int(m_meshes.size()); ++i) {
        MeshPart& p = m_meshes[std::size_t(i)];
        if (i == m_selMesh || p.compName != name) continue;
        if (oldDims.x >= 0) {
            const cad::Vec3 d = dimsOf(p.mesh);
            if (std::abs(d.x - oldDims.x) > 1e-4 ||
                std::abs(d.y - oldDims.y) > 1e-4 ||
                std::abs(d.z - oldDims.z) > 1e-4) {
                ++kept;                  // girada/escalada: mantém como está
                continue;
            }
        }
        const Point3 c = p.mesh.bboxCenter();
        const bool wasHidden = p.hidden;
        p = def;
        p.hidden = wasHidden;
        p.mesh.translate(c - dc);
        ++changed;
    }
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(
        kept ? QStringLiteral("⟨%1⟩ propagado a %2 instância(s); %3 girada(s)/"
                              "escalada(s) mantida(s) como estão.")
                   .arg(name).arg(changed).arg(kept)
             : QStringLiteral("⟨%1⟩ propagado a %2 instância(s).")
                   .arg(name).arg(changed));
    update();
    return changed;
}

QStringList Viewport3D::componentNames() const {
    QStringList out;
    for (const auto& [k, v] : m_compDefs) out << k;
    return out;
}

QJsonArray Viewport3D::compsJson() const {
    QJsonArray arr;
    for (const auto& [name, part] : m_compDefs) {
        if (m_libComps.contains(name)) continue;   // R14: fábrica não salva
        QJsonObject o;
        o["name"] = name;
        QJsonArray verts, faces;
        for (std::size_t v = 0; v < part.mesh.vertexCount(); ++v) {
            const Point3& p = part.mesh.vertex(Idx(v)).p;
            verts.append(QJsonArray{p.x, p.y, p.z});
        }
        for (std::size_t f = 0; f < part.mesh.faceCount(); ++f) {
            QJsonArray loop;
            for (const Idx v : part.mesh.faceVertices(Idx(f)))
                loop.append(int(v));
            faces.append(loop);
        }
        o["verts"] = verts;
        o["faces"] = faces;
        QJsonArray cores;                    // R15: cores da definição
        for (const auto& [fc, c] : part.faceColors)
            cores.append(QJsonArray{int(fc), c[0], c[1], c[2]});
        if (!cores.isEmpty()) o["cores"] = cores;
        arr.append(o);
    }
    return arr;
}

void Viewport3D::setCompsJson(const QJsonArray& arr) {
    // R14: abrir um estudo NÃO despede o mobiliário de fábrica
    for (auto it = m_compDefs.begin(); it != m_compDefs.end();)
        it = m_libComps.contains(it->first) ? std::next(it)
                                            : m_compDefs.erase(it);
    for (const QJsonValue& cv : arr) {
        const QJsonObject o = cv.toObject();
        MeshPart part;
        part.compName = o.value("name").toString();
        for (const QJsonValue& vv : o.value("verts").toArray()) {
            const QJsonArray a = vv.toArray();
            part.mesh.addVertex({a.at(0).toDouble(), a.at(1).toDouble(),
                                 a.at(2).toDouble()});
        }
        bool ok = true;
        for (const QJsonValue& fv : o.value("faces").toArray()) {
            std::vector<Idx> loop;
            for (const QJsonValue& iv : fv.toArray())
                loop.push_back(Idx(iv.toInt()));
            ok &= part.mesh.addFace(loop) != HalfEdgeMesh::kNone;
        }
        for (const QJsonValue& cv2 : o.value("cores").toArray()) {   // R15
            const QJsonArray a = cv2.toArray();
            part.faceColors[Idx(a.at(0).toInt())] = {
                float(a.at(1).toDouble()), float(a.at(2).toDouble()),
                float(a.at(3).toDouble())};
        }
        if (ok && !part.compName.isEmpty()) {
            m_organicos.remove(part.compName);   // R55: veio do arquivo
            m_compDefs[part.compName] = std::move(part);
        }
    }
}

void Viewport3D::qaInsertComp(const QString& name, double nx, double ny) {
    const QPoint pos(int(nx * width()), int(ny * height()));
    int mi = -1;
    Idx f = HalfEdgeMesh::kNone;
    Point3 at;
    if (!pickAt(pos, mi, f, &at)) {
        Point3 o;
        Vec3 d;
        if (!rayAt(pos, o, d) || std::abs(d.z) < 1e-9) return;
        at = o + d * (-o.z / d.z);
    }
    insertComponent(name, at);
}

bool Viewport3D::rotateSelected(double deg) {
    if (m_selMesh < 0 || m_selMesh >= int(m_meshes.size()) ||
        std::abs(deg) < 1e-9) {
        emit pickInfo(QStringLiteral(
            "Rotacionar: selecione um sólido (duplo clique) primeiro."));
        return false;
    }
    pushUndo();
    HalfEdgeMesh& m = m_meshes[std::size_t(m_selMesh)].mesh;
    const Point3 piv = m.bboxCenter();
    const double rad = deg * 3.14159265358979 / 180.0;
    const double cc = std::cos(rad), ss = std::sin(rad);
    transformDimAnchors(m, [&](const Point3& p) {   // R29: cota gira junto
        const double dx = p.x - piv.x, dy = p.y - piv.y;
        return Point3{piv.x + dx * cc - dy * ss, piv.y + dx * ss + dy * cc,
                      p.z};
    });
    m.rotateZ(piv, rad);
    m_lastOp.kind = 6;              // G4: "x5" no VCB vira matriz radial
    m_lastOp.mesh = m_selMesh;
    m_lastOp.p1 = piv;
    m_lastOp.a = deg;
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Sólido rotacionado %1° — digite x5 ou /5 "
                                 "para a matriz radial. Ctrl+Z desfaz.")
                      .arg(deg, 0, 'f', 1));
    update();
    return true;
}

bool Viewport3D::scaleSelected(double f) {
    if (m_selMesh < 0 || m_selMesh >= int(m_meshes.size()) || f < 0.01 ||
        std::abs(f - 1.0) < 1e-9) {
        emit pickInfo(QStringLiteral(
            "Escalar: selecione um sólido e um fator (ex.: 1,5)."));
        return false;
    }
    pushUndo();
    HalfEdgeMesh& m = m_meshes[std::size_t(m_selMesh)].mesh;
    const Point3 spiv = m.bboxCenter();
    transformDimAnchors(m, [&](const Point3& p) {   // R29: cota escala junto
        return spiv + (p - spiv) * f;
    });
    m.scaleAbout(spiv, f);
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Sólido escalado ×%1 — Ctrl+Z desfaz.")
                      .arg(f, 0, 'f', 2));
    update();
    return true;
}

// OFFSET: anel interno paralelo ao contorno da face (convexa) a distância d.
bool Viewport3D::offsetSelectedFace(double d) {
    if (m_selMesh < 0 || m_selWhole || d < 0.005) {
        emit pickInfo(QStringLiteral(
            "Offset: clique numa FACE e informe a distância (>5 mm)."));
        return false;
    }
    const HalfEdgeMesh& m = m_meshes[std::size_t(m_selMesh)].mesh;
    for (const Idx h : m.faceHalfEdges(m_selFace))
        if (m.isBridge(h)) {
            emit pickInfo(QStringLiteral(
                "Offset: essa face já tem recorte — use outra."));
            return false;
        }
    std::vector<Point3> P;
    for (const Idx v : m.faceVertices(m_selFace)) P.push_back(m.vertex(v).p);
    const std::size_t n = P.size();
    if (n < 3) return false;
    const Vec3 N = m.faceNormal(m_selFace);
    // linhas das arestas deslocadas para DENTRO; vértice = interseção
    auto lineDir = [&](std::size_t i) {
        return (P[(i + 1) % n] - P[i]).normalized();
    };
    auto linePt = [&](std::size_t i) {
        return P[i] + N.cross(lineDir(i)) * d;
    };
    auto intersect = [&](std::size_t i, std::size_t j) {
        const Point3 p1 = linePt(i), p2 = linePt(j);
        const Vec3 d1 = lineDir(i), d2 = lineDir(j);
        const Vec3 w = p2 - p1;
        const double b = d1.dot(d2), e = d2.dot(w), d0 = d1.dot(w);
        const double den = 1.0 - b * b;
        if (std::abs(den) < 1e-12) return p2;      // paralelas: usa o ponto
        return p2 + d2 * ((b * d0 - e) / den);
    };
    std::vector<Point3> inner;
    for (std::size_t i = 0; i < n; ++i)
        inner.push_back(intersect((i + n - 1) % n, i));
    const Vec3 U = lineDir(0);
    const Vec3 V = N.cross(U).normalized();
    if (!cornersInsideFace(m_selMesh, m_selFace, inner, P[0], U, V)) {
        emit pickInfo(QStringLiteral(
            "Offset grande demais para essa face — reduza a distância."));
        return false;
    }
    pushUndo();
    const int mi = m_selMesh;
    const Idx f0 = m_selFace;
    const Idx innerF =
        m_meshes[std::size_t(mi)].mesh.insetFace(f0, inner);
    std::string why;
    if (innerF == HalfEdgeMesh::kNone ||
        !m_meshes[std::size_t(mi)].mesh.checkIntegrity(&why)) {
        undoLast();
        return false;
    }
    m_selMesh = mi;
    m_selFace = innerF;
    m_selWhole = false;
    m_lastOp = {4, mi, f0, {}, {}, {}, d, 0.0};
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral(
                      "Offset de %1 m criado — P: puxar (moldura/rebaixo).")
                      .arg(d, 0, 'f', 2));
    update();
    return true;
}

// ---------------------------------------------------------------------------
//  GRUDAR (weldUnion em cadeia): o sólido selecionado engole tudo que toca
//  por interface face-a-face coplanar (igual ou contida).
// ---------------------------------------------------------------------------
int Viewport3D::glueSelected() {
    if (!m_selWhole || m_selMesh < 0 || m_selMesh >= int(m_meshes.size())) {
        emit pickInfo(QStringLiteral(
            "Grudar: selecione um SÓLIDO primeiro (duplo clique)."));
        return 0;
    }
    pushUndo();
    int merges = 0;
    bool again = true;
    while (again) {
        again = false;
        for (int j = 0; j < int(m_meshes.size()); ++j) {
            if (j == m_selMesh) continue;
            HalfEdgeMesh out;
            if (!HalfEdgeMesh::weldUnion(
                    m_meshes[std::size_t(m_selMesh)].mesh,
                    m_meshes[std::size_t(j)].mesh, out))
                continue;
            m_meshes[std::size_t(m_selMesh)].mesh = std::move(out);
            m_meshes[std::size_t(m_selMesh)].faceColors.clear();
            m_meshes.erase(m_meshes.begin() + j);
            if (j < m_selMesh) --m_selMesh;
            ++merges;
            again = true;
            break;
        }
    }
    if (merges == 0) {
        m_undo.pop_back();
        emit pickInfo(QStringLiteral(
            "Nada encostado face-a-face para grudar neste sólido."));
        return 0;
    }
    m_selFace = 0;
    m_selWhole = true;
    m_selEdgeMesh = -1;
    m_selEdge = HalfEdgeMesh::kNone;
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral(
                      "GRUDADO: %1 sólido(s) fundidos num só (%2 faces) — "
                      "Ctrl+Z desfaz.")
                      .arg(merges + 1)
                      .arg(m_meshes[std::size_t(m_selMesh)].mesh.faceCount()));
    update();
    return merges;
}

// ---------------------------------------------------------------------------
//  Ferramenta CÍRCULO: centro + raio (na face, via insetFace; no chão, vira
//  sólido novo) — 24 lados; o P transforma em CILINDRO ou nicho redondo.
// ---------------------------------------------------------------------------
void Viewport3D::cancelCircleStage() {
    m_circStage = 0;
    m_circMesh = -1;
    m_circFace = HalfEdgeMesh::kNone;
    m_ghost.clear();
    m_snapMark.clear();
    m_ghostDirty = true;
}

namespace {
std::vector<Point3> circleLoop(const Point3& c, const Vec3& U, const Vec3& V,
                               double r, int N = 24) {
    std::vector<Point3> pts;
    if (N < 3) N = 24;                 // G4: polígono N lados (0/inv = círculo)
    for (int i = 0; i < N; ++i) {
        const double a = 2.0 * 3.14159265358979 * i / N;
        pts.push_back(c + U * (r * std::cos(a)) + V * (r * std::sin(a)));
    }
    return pts;
}
} // namespace

void Viewport3D::circleHover(const QPoint& pos) {
    m_ghost.clear();
    m_snapMark.clear();
    if (m_circStage == 1) {
        Point3 orig;
        Vec3 dir;
        if (rayAt(pos, orig, dir)) {
            const double den = dir.dot(m_circN);
            if (std::abs(den) > 1e-9) {
                const double t = (m_circC - orig).dot(m_circN) / den;
                if (t > 0.0) {
                    const Vec3 off = orig + dir * t - m_circC;
                    const double r = std::hypot(off.dot(m_circU),
                                                off.dot(m_circV));
                    liveVcb(QStringLiteral("r %1 m").arg(r, 0, 'f', 2));
                    const auto pts =
                        circleLoop(m_circC, m_circU, m_circV, r, m_polySides);
                    for (std::size_t i = 0; i < pts.size(); ++i)
                        pushSeg(m_ghost, pts[i], pts[(i + 1) % pts.size()]);
                }
            }
        }
    }
    m_ghostDirty = true;
    update();
}

void Viewport3D::circleClick(const QPoint& pos) {
    if (m_circStage == 0) {
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        Point3 hit;
        if (pickAt(pos, mi, f, &hit)) {
            int kind = 0;
            applySnap(mi, f, hit, kind);
            const HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
            m_circN = m.faceNormal(f);
            Vec3 u{};
            for (const Idx h : m.faceHalfEdges(f)) {
                if (m.isBridge(h)) continue;
                u = m.vertex(m.halfEdge(m.halfEdge(h).next).origin).p -
                    m.vertex(m.halfEdge(h).origin).p;
                if (u.lengthSq() > 1e-12) break;
            }
            m_circU = u.normalized();
            m_circV = m_circN.cross(m_circU).normalized();
            m_circMesh = mi;
            m_circFace = f;
        } else {
            Point3 orig;
            Vec3 dir;
            if (!rayAt(pos, orig, dir) || std::abs(dir.z) < 1e-9) return;
            const double t = -orig.z / dir.z;
            if (t <= 0.0) return;
            hit = orig + dir * t;
            m_circN = {0, 0, 1};
            m_circU = {1, 0, 0};
            m_circV = {0, 1, 0};
            m_circMesh = -2;                // chão
            m_circFace = HalfEdgeMesh::kNone;
        }
        m_circC = hit;
        m_circStage = 1;
        emit pickInfo(QStringLiteral("Círculo: clique o ponto do RAIO."));
        return;
    }
    Point3 orig;
    Vec3 dir;
    if (!rayAt(pos, orig, dir)) return;
    const double den = dir.dot(m_circN);
    if (std::abs(den) < 1e-9) return;
    const double t = (m_circC - orig).dot(m_circN) / den;
    if (t <= 0.0) return;
    const Vec3 off = orig + dir * t - m_circC;
    const double r = std::hypot(off.dot(m_circU), off.dot(m_circV));
    if (r < 0.05) {
        emit pickInfo(QStringLiteral("Raio muito pequeno — afaste o clique."));
        return;
    }
    const std::vector<Point3> pts =
        circleLoop(m_circC, m_circU, m_circV, r, m_polySides);
    if (m_circMesh == -2) {                 // no CHÃO: sólido novo (disco)
        pushUndo();
        const std::size_t antes = m_meshes.size();
        const std::vector<Point3> merged = mergeGroundFootprint(pts);
        const bool fundiu = m_meshes.size() < antes;
        Idx topF = HalfEdgeMesh::kNone;
        const int mi = buildGroundPillow(merged, &topF);
        if (mi < 0) {
            undoLast();
            m_redo.clear();
            emit pickInfo(QStringLiteral("Não deu para criar o disco aqui."));
            return;
        }
        m_selMesh = mi;
        m_selFace = topF;
        m_selWhole = false;
        m_lastOp = {2, -2, HalfEdgeMesh::kNone, m_circC, m_circU, m_circV,
                    r, 0.0};
        m_edited = true;
        cancelCircleStage();
        buildRenderArrays();
        m_hlDirty = true;
        emit structureChanged();
        emit pickInfo(fundiu
                          ? QStringLiteral("Fundido com a geometria "
                                           "sobreposta — P: levantar.")
                          : QStringLiteral("Disco criado no chão — P: "
                                           "levantar (vira um CILINDRO)."));
        update();
        return;
    }
    // R19: se sobrepõe um vão coplanar já aberto NESSA face, funde os dois
    const Idx inner = insetOrMergeOnFace(m_circMesh, m_circFace, pts, m_circC,
                                         m_circU, m_circV);
    if (inner == HalfEdgeMesh::kNone) {
        emit pickInfo(QStringLiteral(
            "O círculo precisa caber INTEIRO na face — tente de novo."));
        return;
    }
    m_selMesh = m_circMesh;
    m_selFace = inner;
    m_selWhole = false;
    m_lastOp = {2, m_circMesh, m_circFace, m_circC, m_circU, m_circV, r, 0.0};
    m_edited = true;
    cancelCircleStage();
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral(
        "Círculo criado — P: empurrar/puxar (cilindro ou nicho redondo)."));
    update();
}

// ---------------------------------------------------------------------------
//  Ferramenta MOVER/COPIAR sólido: ponto-base -> destino (plano horizontal)
// ---------------------------------------------------------------------------
void Viewport3D::cancelMoveStage() {
    m_moveStage = 0;
    m_moveMesh = -1;
    m_moveAxisLock = 0;
    m_moveMode = 0;
    m_moveVerts.clear();
    m_ghost.clear();
    m_snapMark.clear();
    m_ghostDirty = true;
}

void Viewport3D::startMoveCopy(bool copy) {
    // G2: sem seleção também vale — clique um vértice/aresta (sticky move)
    m_moveCopy = copy;
    setTool(Tool::Move);            // cancela estágios; SÓ DEPOIS o alvo
    m_moveMesh = m_selMesh;
    if (m_selMesh < 0)
        emit pickInfo(QStringLiteral(
            "%1: clique um VÉRTICE ou uma ARESTA (os vizinhos esticam) — ou "
            "selecione um sólido antes.")
                          .arg(copy ? QStringLiteral("Copiar")
                                    : QStringLiteral("Mover")));
}

// ponto no espaço: sobre um sólido (com snap de vértice) ou no plano
// horizontal do ponto-base (etapa 1) / no chão (etapa 0 sem sólido)
bool Viewport3D::movePointAt(const QPoint& pos, Point3& out) const {
    // eixo TRAVADO (setas): destino = ponto da reta do eixo mais perto do raio
    if (m_moveStage == 1 && m_moveAxisLock != 0) {
        Point3 orig;
        Vec3 dir;
        if (!rayAt(pos, orig, dir)) return false;
        const Vec3 ax = m_moveAxisLock == 1   ? Vec3{1, 0, 0}
                        : m_moveAxisLock == 2 ? Vec3{0, 1, 0}
                                              : Vec3{0, 0, 1};
        out = m_moveBase + ax * lineParamClosestToRay(m_moveBase, ax, orig, dir);
        m_infer = Infer{6 + m_moveAxisLock, out, true, m_moveBase};
        return true;
    }
    int mi = -1;
    Idx f = HalfEdgeMesh::kNone;
    Point3 hit;
    if (pickAt(pos, mi, f, &hit)) {
        int kind = 0;
        applySnap(mi, f, hit, kind);
        out = hit;
    } else {
        Point3 orig;
        Vec3 dir;
        if (!rayAt(pos, orig, dir)) return false;
        const double planeZ = m_moveStage == 1 ? m_moveBase.z : 0.0;
        if (std::abs(dir.z) < 1e-9) return false;
        const double t = (planeZ - orig.z) / dir.z;
        if (t <= 0.0) return false;
        out = orig + dir * t;
    }
    // G1: motor de inferência — snap tipado global + alinhamento pelos eixos
    // a partir do ponto-base (substitui o antigo cone de ±14°)
    {
        const Point3* base = m_moveStage == 1 ? &m_moveBase : nullptr;
        const Infer in = inferAt(pos, base, false, false);
        if (in.kind != 0) {
            m_infer = in;
            out = in.p;
        } else {
            m_infer = Infer{m_moveStage == 1 ? 4 : 5, out, false, {}};
        }
    }
    return true;
}

void Viewport3D::moveHover(const QPoint& pos) {
    m_ghost.clear();
    m_snapMark.clear();
    Point3 p;
    if (m_moveStage == 1 && movePointAt(pos, p) &&
        !m_selSolidsMulti.empty()) {
        // R4: fantasma da MULTI inteira acompanhando o mouse
        const Vec3 d = p - m_moveBase;
        const double dl = d.length();
        if (dl > 1e-9) m_lastMoveDir = d * (1.0 / dl);
        liveVcb(QStringLiteral("%1 m").arg(dl, 0, 'f', 2));
        std::vector<Point3> lines;
        for (const int mi : m_selSolidsMulti) {
            if (mi < 0 || mi >= int(m_meshes.size())) continue;
            lines.clear();
            m_meshes[std::size_t(mi)].mesh.edgeLines(lines);
            for (std::size_t i = 0; i + 1 < lines.size(); i += 2)
                pushSeg(m_ghost, lines[i] + d, lines[i + 1] + d);
        }
        pushSeg(m_ghost, m_moveBase, p);
        m_ghostDirty = true;
        update();
        return;
    }
    if (m_moveStage == 1 && movePointAt(pos, p) && m_moveMesh >= 0 &&
        m_moveMesh < int(m_meshes.size())) {
        const Vec3 d = p - m_moveBase;
        const double dl = d.length();    // R2: leitura viva + direção exata
        if (dl > 1e-9) m_lastMoveDir = d * (1.0 / dl);
        liveVcb(QStringLiteral("%1 m").arg(dl, 0, 'f', 2));
        const HalfEdgeMesh& msh = m_meshes[std::size_t(m_moveMesh)].mesh;
        if (m_moveMode != 0) {                 // G2: fantasma do sticky move
            auto moved = [&](Idx v) {
                return std::find(m_moveVerts.begin(), m_moveVerts.end(), v) !=
                       m_moveVerts.end();
            };
            for (Idx h = 0; h < Idx(msh.halfEdgeCount()); ++h) {
                const HalfEdgeMesh::HalfEdge& he = msh.halfEdge(h);
                if (he.twin != HalfEdgeMesh::kNone && he.twin < h) continue;
                if (msh.isBridge(h)) continue;
                const Idx va = he.origin;
                const Idx vb = msh.halfEdge(he.next).origin;
                const bool ma = moved(va), mb = moved(vb);
                if (!ma && !mb) continue;
                const Point3 a = msh.vertex(va).p + (ma ? d : Vec3{0, 0, 0});
                const Point3 b = msh.vertex(vb).p + (mb ? d : Vec3{0, 0, 0});
                pushSeg(m_ghost, a, b);
            }
        } else {
            std::vector<Point3> lines;
            msh.edgeLines(lines);
            for (std::size_t i = 0; i + 1 < lines.size(); i += 2)
                pushSeg(m_ghost, lines[i] + d, lines[i + 1] + d);
        }
        pushSeg(m_ghost, m_moveBase, p);        // vetor do deslocamento
    }
    m_ghostDirty = true;
    update();
}

void Viewport3D::moveClick(const QPoint& pos) {
    Point3 p;
    if (!movePointAt(pos, p)) return;
    if (m_moveStage == 0) {
        m_moveBase = p;
        m_moveStage = 1;
        // G2: clicou um VÉRTICE ou ARESTA? vira sticky-move de subentidade —
        // mas SÓ sem sólido pré-selecionado (seleção prévia move o sólido)
        m_moveMode = 0;
        m_moveVerts.clear();
        if (m_selSolidsMulti.empty() && m_moveMesh < 0 &&
            m_infer.kind == 1) {
            for (int mi = 0; mi < int(m_meshes.size()) && m_moveMode == 0;
                 ++mi) {
                if (m_meshes[std::size_t(mi)].hidden) continue;
                const HalfEdgeMesh& msh = m_meshes[std::size_t(mi)].mesh;
                for (Idx v = 0; v < Idx(msh.vertexCount()); ++v)
                    if ((msh.vertex(v).p - p).lengthSq() < 1e-10) {
                        m_moveMode = 1;
                        m_moveMesh = mi;
                        m_moveVerts.push_back(v);
                    }
            }
        } else if (m_selSolidsMulti.empty() && m_moveMesh < 0 &&
                   (m_infer.kind == 2 || m_infer.kind == 3)) {
            int mi = -1;
            Idx he = HalfEdgeMesh::kNone;
            if (pickEdgeAt(pos, mi, he)) {
                const HalfEdgeMesh& msh = m_meshes[std::size_t(mi)].mesh;
                m_moveMode = 2;
                m_moveMesh = mi;
                m_moveVerts = {msh.halfEdge(he).origin,
                               msh.halfEdge(msh.halfEdge(he).next).origin};
            }
        }
        emit pickInfo(QStringLiteral(
            "%1 %2: clique o DESTINO (vizinhos esticam junto).")
                          .arg(m_moveCopy ? QStringLiteral("Copiar")
                                          : QStringLiteral("Mover"))
                          .arg(m_moveMode == 1   ? QStringLiteral("VÉRTICE")
                               : m_moveMode == 2 ? QStringLiteral("ARESTA")
                                                 : QStringLiteral("sólido")));
        return;
    }
    const Vec3 d = p - m_moveBase;
    if (d.lengthSq() < 1e-12 ||
        (m_selSolidsMulti.empty() &&
         (m_moveMesh < 0 || m_moveMesh >= int(m_meshes.size()))))
        return;
    pushUndo();
    if (!m_selSolidsMulti.empty() && m_moveMode == 0) {
        // R4: MOVER/COPIAR a MULTI-seleção/grupo inteiro de uma vez
        const std::size_t n = m_selSolidsMulti.size();
        if (!m_moveCopy)             // R29: cotas UMA vez, contra a UNIÃO dos alvos
            transformDimAnchors(m_selSolidsMulti,
                                [&d](const Point3& p) { return p + d; });
        for (const int mi : m_selSolidsMulti) {
            if (mi < 0 || mi >= int(m_meshes.size())) continue;
            if (m_moveCopy) {
                MeshPart clone = m_meshes[std::size_t(mi)];
                clone.mesh.translate(d);
                m_meshes.push_back(std::move(clone));
            } else {
                m_meshes[std::size_t(mi)].mesh.translate(d);
            }
        }
        m_edited = true;
        cancelMoveStage();
        setTool(Tool::Select);
        buildRenderArrays();
        m_hlDirty = true;
        emit structureChanged();
        emit pickInfo(QStringLiteral("%1 sólido(s) %2 %3 m — Ctrl+Z desfaz.")
                          .arg(n)
                          .arg(m_moveCopy ? QStringLiteral("copiado(s) a")
                                          : QStringLiteral("movido(s)"))
                          .arg(d.length(), 0, 'f', 2));
        update();
        return;
    }
    if (m_moveMode != 0) {                 // G2: sticky move + AUTOFOLD
        HalfEdgeMesh& msh = m_meshes[std::size_t(m_moveMesh)].mesh;
        for (Idx v : m_moveVerts) msh.moveVertex(v, msh.vertex(v).p + d);
        autofold(msh, m_moveVerts);
        std::string why;
        if (!msh.checkIntegrity(&why)) {
            undoLast();
            m_redo.clear();                // estado quebrado não é refazível
            emit pickInfo(QStringLiteral(
                "Esse movimento quebraria o sólido — desfeito."));
            return;
        }
        const bool wasVert = m_moveMode == 1;
        m_edited = true;
        cancelMoveStage();
        setTool(Tool::Select);
        buildRenderArrays();
        m_hlDirty = true;
        emit pickInfo(QStringLiteral(
                          "%1 movido(a) %2 m — vizinhos esticaram; vincos "
                          "novos = autofold. Ctrl+Z desfaz.")
                          .arg(wasVert ? QStringLiteral("Vértice")
                                       : QStringLiteral("Aresta"))
                          .arg(d.length(), 0, 'f', 2));
        update();
        return;
    }
    if (m_moveCopy) {
        MeshPart clone = m_meshes[std::size_t(m_moveMesh)];
        clone.mesh.translate(d);
        m_meshes.push_back(std::move(clone));
        m_selMesh = int(m_meshes.size()) - 1;
        m_lastOp.kind = 5;          // G4: "x5" ou "/5" no VCB vira matriz
        m_lastOp.mesh = m_moveMesh;
        m_lastOp.U = d;
    } else {
        dragDimAnchors(m_meshes[std::size_t(m_moveMesh)].mesh, d);
        m_meshes[std::size_t(m_moveMesh)].mesh.translate(d);
        m_selMesh = m_moveMesh;
    }
    m_edited = true;
    const double len = d.length();
    cancelMoveStage();
    setTool(Tool::Select);
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Sólido %1 %2 m — Ctrl+Z desfaz.")
                      .arg(m_moveCopy ? QStringLiteral("copiado a")
                                      : QStringLiteral("movido"))
                      .arg(len, 0, 'f', 2));
    update();
}

// ---------------------------------------------------------------------------
//  Seleção de ARESTA: distância em pixels na tela (Delete = dissolver)
// ---------------------------------------------------------------------------
bool Viewport3D::pickEdgeAt(const QPoint& pos, int& meshOut, Idx& heOut) const {
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    const float w = float(width()), h = float(height());
    auto toScreen = [&](const Point3& p, float& sx, float& sy) {
        const QVector4D c = mvp * QVector4D(float(p.x), float(p.y),
                                            float(p.z), 1.0f);
        if (c.w() <= 1e-6f) return false;
        sx = (c.x() / c.w() * 0.5f + 0.5f) * w;
        sy = (0.5f - c.y() / c.w() * 0.5f) * h;
        return true;
    };
    const float px = float(pos.x()), py = float(pos.y());
    float best = 6.0f;                       // tolerância em pixels
    bool found = false;
    for (int i = 0; i < int(m_meshes.size()); ++i) {
        if (m_meshes[std::size_t(i)].hidden || !inCtx(i)) continue;
        const HalfEdgeMesh& m = m_meshes[std::size_t(i)].mesh;
        for (Idx hh = 0; hh < Idx(m.halfEdgeCount()); ++hh) {
            const Idx t = m.halfEdge(hh).twin;
            if (t != HalfEdgeMesh::kNone && t < hh) continue;
            if (m.isBridge(hh)) continue;
            const Point3& a = m.vertex(m.halfEdge(hh).origin).p;
            const Point3& b = m.vertex(m.halfEdge(m.halfEdge(hh).next).origin).p;
            float ax, ay, bx, by;
            if (!toScreen(a, ax, ay) || !toScreen(b, bx, by)) continue;
            const float lx = bx - ax, ly = by - ay;
            const float ll = lx * lx + ly * ly;
            float u = ll > 1e-9f ? ((px - ax) * lx + (py - ay) * ly) / ll : 0.0f;
            u = std::clamp(u, 0.0f, 1.0f);
            const float d = std::hypot(px - (ax + lx * u), py - (ay + ly * u));
            if (d < best) {
                best = d;
                meshOut = i;
                heOut = hh;
                found = true;
            }
        }
    }
    return found;
}

// ---------------------------------------------------------------------------
//  VCB: a medida digitada REFAZ a última operação com o valor exato
// ---------------------------------------------------------------------------
void Viewport3D::redoRect(double w, double h) {
    const LastOp op = m_lastOp;
    undoLast();
    const double du = (op.a < 0 ? -1.0 : 1.0) * std::abs(w);
    const double dv = (op.b < 0 ? -1.0 : 1.0) * std::abs(h);
    std::vector<Point3> corners{op.p1, op.p1 + op.U * du,
                                op.p1 + op.U * du + op.V * dv,
                                op.p1 + op.V * dv};
    if (op.mesh == -2) {
        if (du * dv < 0) std::reverse(corners.begin(), corners.end());
        pushUndo();
        const std::vector<Point3> merged = mergeGroundFootprint(corners);   // R18
        Idx topF = HalfEdgeMesh::kNone;
        const int mi = buildGroundPillow(merged, &topF);
        if (mi < 0) { undoLast(); m_redo.clear(); return; }
        m_selMesh = mi;
        m_selFace = topF;
        emit structureChanged();
    } else {
        if (op.mesh < 0 || op.mesh >= int(m_meshes.size())) return;
        const Idx inner = insetOrMergeOnFace(op.mesh, op.face, corners, op.p1,
                                             op.U, op.V);
        if (inner == HalfEdgeMesh::kNone) {
            emit pickInfo(QStringLiteral(
                "Medida não cabe na face — tente valores menores."));
            return;
        }
        m_selMesh = op.mesh;
        m_selFace = inner;
    }
    m_selWhole = false;
    m_lastOp = op;
    m_lastOp.a = du;
    m_lastOp.b = dv;
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Retângulo refeito: %1 × %2 m — P: puxar.")
                      .arg(std::abs(du), 0, 'f', 2)
                      .arg(std::abs(dv), 0, 'f', 2));
    update();
}

void Viewport3D::redoCircle(double r) {
    const LastOp op = m_lastOp;
    undoLast();
    const std::vector<Point3> pts =
        circleLoop(op.p1, op.U, op.V, r, m_polySides);
    if (op.mesh == -2) {
        pushUndo();
        const std::vector<Point3> merged = mergeGroundFootprint(pts);   // R18
        Idx topF = HalfEdgeMesh::kNone;
        const int mi = buildGroundPillow(merged, &topF);
        if (mi < 0) { undoLast(); m_redo.clear(); return; }
        m_selMesh = mi;
        m_selFace = topF;
        emit structureChanged();
    } else {
        if (op.mesh < 0 || op.mesh >= int(m_meshes.size())) return;
        const Idx inner = insetOrMergeOnFace(op.mesh, op.face, pts, op.p1,
                                             op.U, op.V);
        if (inner == HalfEdgeMesh::kNone) {
            emit pickInfo(QStringLiteral("Raio não cabe na face."));
            return;
        }
        m_selMesh = op.mesh;
        m_selFace = inner;
    }
    m_selWhole = false;
    m_lastOp = op;
    m_lastOp.a = r;
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("Círculo refeito: raio %1 m — P: puxar.")
                      .arg(r, 0, 'f', 2));
    update();
}

void Viewport3D::redoPull(double d) {
    const LastOp op = m_lastOp;
    undoLast();
    if (op.mesh < 0 || op.mesh >= int(m_meshes.size())) return;
    m_selMesh = op.mesh;
    m_selFace = op.face;
    m_selWhole = false;
    pushPullSelected((op.a < 0 ? -1.0 : 1.0) * std::abs(d));
}

// ---------------------------------------------------------------------------
//  G5: GRUPOS, CONTEXTO e TAGS — a fronteira da cola e a ordem do modelo
// ---------------------------------------------------------------------------
QString Viewport3D::partGroup(int i) const {
    return i >= 0 && i < int(m_meshes.size()) ? m_meshes[std::size_t(i)].group
                                              : QString();
}

void Viewport3D::makeGroup(const QString& name) {
    // agrupa a multi-seleção (caixa/Ctrl); sem multi, o sólido selecionado
    std::vector<int> alvo = m_selSolidsMulti;
    for (const auto& [mi, f] : m_selFacesMulti)
        if (std::find(alvo.begin(), alvo.end(), mi) == alvo.end())
            alvo.push_back(mi);
    if (alvo.empty() && m_selMesh >= 0) alvo.push_back(m_selMesh);
    if (alvo.empty() || name.trimmed().isEmpty()) {
        emit pickInfo(QStringLiteral(
            "Grupo: selecione sólidos antes (caixa de seleção ou Ctrl)."));
        return;
    }
    pushUndo();
    for (const int mi : alvo)
        if (mi >= 0 && mi < int(m_meshes.size()))
            m_meshes[std::size_t(mi)].group = name.trimmed();
    m_edited = true;
    emit structureChanged();
    emit pickInfo(QStringLiteral(
                      "Grupo \"%1\" criado com %2 sólido(s) — clique em um e "
                      "o grupo INTEIRO responde; duplo clique ENTRA nele.")
                      .arg(name.trimmed())
                      .arg(alvo.size()));
    update();
}

void Viewport3D::ungroupSelected() {
    QString g = m_selMesh >= 0 ? partGroup(m_selMesh) : QString();
    if (g.isEmpty() && !m_selSolidsMulti.empty())
        g = partGroup(m_selSolidsMulti.front());
    if (g.isEmpty()) {
        emit pickInfo(QStringLiteral("Desagrupar: selecione um grupo antes."));
        return;
    }
    pushUndo();
    int n = 0;
    for (MeshPart& p : m_meshes)
        if (p.group == g) { p.group.clear(); ++n; }
    m_edited = true;
    emit structureChanged();
    emit pickInfo(QStringLiteral("Grupo \"%1\" desfeito (%2 sólidos soltos).")
                      .arg(g).arg(n));
    update();
}

void Viewport3D::assignTag(const QString& tag) {
    std::vector<int> alvo = m_selSolidsMulti;
    if (alvo.empty() && m_selMesh >= 0) alvo.push_back(m_selMesh);
    if (alvo.empty()) {
        emit pickInfo(QStringLiteral("Tag: selecione sólidos antes."));
        return;
    }
    pushUndo();
    for (const int mi : alvo)
        if (mi >= 0 && mi < int(m_meshes.size()))
            m_meshes[std::size_t(mi)].tag = tag.trimmed();
    m_edited = true;
    emit structureChanged();
    emit pickInfo(QStringLiteral("Tag \"%1\" em %2 sólido(s) — Ver → Tags… "
                                 "liga/desliga.").arg(tag.trimmed())
                      .arg(alvo.size()));
    update();
}

void Viewport3D::setTagVisible(const QString& tag, bool vis) {
    pushUndo();
    int n = 0;
    for (MeshPart& p : m_meshes)
        if (p.tag == tag) { p.hidden = !vis; ++n; }
    m_edited = true;
    buildRenderArrays();
    m_hlDirty = true;
    emit structureChanged();
    emit pickInfo(QStringLiteral("Tag \"%1\": %2 sólido(s) %3.")
                      .arg(tag).arg(n)
                      .arg(vis ? QStringLiteral("visíveis")
                               : QStringLiteral("ocultos")));
    update();
}

QStringList Viewport3D::allTags() const {
    QStringList out;
    for (const MeshPart& p : m_meshes)
        if (!p.tag.isEmpty() && !out.contains(p.tag)) out.append(p.tag);
    return out;
}

// R42: assinatura barata da geometria — decide se o contexto EDITOU algo.
namespace {
quint64 meshHash(const HalfEdgeMesh& m) {
    quint64 h = quint64(m.vertexCount()) * 1000003ULL +
                quint64(m.faceCount()) * 10007ULL;
    for (std::size_t v = 0; v < m.vertexCount(); ++v) {
        const Point3& p = m.vertex(HalfEdgeMesh::Idx(v)).p;
        h = h * 1099511628211ULL + quint64(llround(p.x * 1e5));
        h = h * 1099511628211ULL + quint64(llround(p.y * 1e5));
        h = h * 1099511628211ULL + quint64(llround(p.z * 1e5));
    }
    return h;
}
}  // namespace

void Viewport3D::exitContext() {
    if (!inEditContext()) return;
    const int cm = m_ctxMesh;
    m_ctxGroup.clear();
    m_ctxMesh = -1;
    // R42: sair do contexto de um COMPONENTE editado propaga pra todas as
    // instâncias — a semântica do SketchUp, automática.
    if (cm >= 0 && cm < int(m_meshes.size()) &&
        !m_meshes[std::size_t(cm)].compName.isEmpty() &&
        meshHash(m_meshes[std::size_t(cm)].mesh) != m_ctxCompHash) {
        m_selMesh = cm;
        m_selWhole = true;
        m_selFace = 0;
        redefineComponent();             // mensagem própria (N instâncias)
        return;
    }
    buildRenderArrays();
    emit pickInfo(QStringLiteral("Saiu do contexto — o modelo inteiro "
                                 "responde de novo."));
    update();
}

// R1: ENSŌ-SAN — a figura humana de escala (1,75 m) do estudo novo.
// Duas caixas agrupadas: corpo + cabeça. Deletável como qualquer sólido.
void Viewport3D::addScaleFigure() {
    auto makeBox = [](double cx, double cy, double w, double d, double z0,
                      double z1) {
        MeshPart part;
        HalfEdgeMesh& m = part.mesh;
        const double hw = w / 2, hd = d / 2;
        const Idx a = m.addVertex({cx - hw, cy - hd, z0});
        const Idx b = m.addVertex({cx + hw, cy - hd, z0});
        const Idx c = m.addVertex({cx + hw, cy + hd, z0});
        const Idx dd = m.addVertex({cx - hw, cy + hd, z0});
        const Idx e = m.addVertex({cx - hw, cy - hd, z1});
        const Idx f = m.addVertex({cx + hw, cy - hd, z1});
        const Idx g = m.addVertex({cx + hw, cy + hd, z1});
        const Idx h = m.addVertex({cx - hw, cy + hd, z1});
        m.addFace({dd, c, b, a});
        m.addFace({e, f, g, h});
        m.addFace({a, b, f, e});
        m.addFace({c, dd, h, g});
        m.addFace({b, c, g, f});
        m.addFace({dd, a, e, h});
        return part;
    };
    pushUndo();
    MeshPart corpo = makeBox(1.2, 1.2, 0.45, 0.25, 0.0, 1.48);
    MeshPart cabeca = makeBox(1.2, 1.2, 0.22, 0.22, 1.50, 1.75);
    for (Idx f = 0; f < 6; ++f) {        // R4: pele de latão, não vulto sumi
        corpo.faceColors[f] = {0.761f, 0.627f, 0.388f};
        cabeca.faceColors[f] = {0.836f, 0.718f, 0.502f};
    }
    corpo.group = QStringLiteral("Ensō-san");
    cabeca.group = QStringLiteral("Ensō-san");
    corpo.compName = QStringLiteral("Ensō-san");
    m_meshes.push_back(std::move(corpo));
    m_meshes.push_back(std::move(cabeca));
    m_edited = false;                    // a figura não suja o estudo novo
    buildRenderArrays();
    emit structureChanged();
    emit pickInfo(QStringLiteral(
        "Ensō-san (1,75 m) dá a escala — desenhe ao lado dele; Del apaga o "
        "grupo quando não precisar mais."));
    update();
}

// R8: números com UNIDADE — "350cm"/"3500mm" viram 3,5 m; "3,5" segue metro
namespace {
std::vector<double> parseMeasures(const QString& typed) {
    std::vector<double> vals;
    for (QString tk : typed.split(';', Qt::SkipEmptyParts)) {
        tk = tk.trimmed();
        tk.replace(',', '.');
        double mult = 1.0;
        if (tk.endsWith(QLatin1String("cm"), Qt::CaseInsensitive)) {
            mult = 0.01;
            tk.chop(2);
        } else if (tk.endsWith(QLatin1String("mm"), Qt::CaseInsensitive)) {
            mult = 0.001;
            tk.chop(2);
        } else if (tk.endsWith(QLatin1String("m"), Qt::CaseInsensitive) ||
                   tk.endsWith(QLatin1String("r"), Qt::CaseInsensitive)) {
            tk.chop(1);          // "3,5m" (metro) ou "3r" (raio do arco)
        }
        bool ok = false;
        const double v = tk.toDouble(&ok);
        if (ok) vals.push_back(std::abs(v) * mult);
    }
    return vals;
}
} // namespace

// R2: Enter NO MEIO do gesto — o número digitado conclui a operação exata
bool Viewport3D::finishGestureExact(const QString& typed) {
    const std::vector<double> vals = parseMeasures(typed);
    if (vals.empty() && !typed.contains('s')) return false;
    // R13: BALDE armado com textura — o número digitado é a ESCALA (m/tile)
    if (m_tool == Tool::Paint && m_mat.kind == 1 && !vals.empty() &&
        vals[0] > 1e-4) {
        m_mat.texScale = vals[0];
        emit pickInfo(QStringLiteral(
                          "Balde: textura em %1 m por tile — os próximos "
                          "cliques vestem nessa escala.")
                          .arg(vals[0], 0, 'f', 2));
        return true;
    }
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    auto clickAt = [&](const Point3& w) {
        QPointF s;
        if (!toScreen(mvp, w, s)) return false;
        m_exactClick = true;
        if (m_tool == Tool::Rect)
            rectClick(QPoint(int(s.x()), int(s.y())));
        else
            circleClick(QPoint(int(s.x()), int(s.y())));
        m_exactClick = false;
        return true;
    };
    if (m_tool == Tool::Rect && m_rectStage == 1 && vals.size() >= 2) {
        const double su = m_lastDu < 0 ? -1.0 : 1.0;
        const double sv = m_lastDv < 0 ? -1.0 : 1.0;
        if (!clickAt(m_rectP1 + m_rectU * (su * vals[0]) +
                     m_rectV * (sv * vals[1])))
            return false;
        redoRect(vals[0], vals[1]);      // refina pro valor EXATO digitado
        return true;
    }
    if (m_tool == Tool::Circle && m_circStage == 1) {
        if (!clickAt(m_circC + m_circU * vals[0])) return false;
        redoCircle(vals[0]);
        return true;
    }
    if (m_pullDrag) {                    // pull exato no meio do arrasto
        const double sgn = m_pullT < 0 ? -1.0 : 1.0;
        m_meshes = std::move(m_pullBase);
        m_pullBase.clear();
        m_pullDrag = false;
        m_pullSticky = false;
        m_selMesh = m_pullMesh;
        m_selFace = m_pullFace;
        m_selWhole = false;
        pushPullSelected(sgn * vals[0]);
        return true;
    }
    // R8: "8s" muda os LADOS do círculo/polígono no meio do gesto
    if (typed.endsWith(QLatin1Char('s')) && m_tool == Tool::Circle) {
        const int n = typed.left(typed.size() - 1).toInt();
        if (n >= 3 && n <= 96) {
            setPolySides(n);
            emit pickInfo(QStringLiteral("%1 lados — continue o gesto.")
                              .arg(n));
        }
        return true;
    }
    if (m_tool == Tool::Arc && m_arcStage == 2 && !vals.empty() &&
        !typed.contains(QLatin1Char('r'), Qt::CaseInsensitive)) {
        // R8: número puro no arco = FLECHA (bulge), como no SketchUp
        commitArc((m_arcSag < 0 ? -1.0 : 1.0) * vals[0]);
        return true;
    }
    if (m_tool == Tool::Arc && m_arcStage == 2 && !vals.empty()) {
        // "3r" = RAIO digitado fecha o arco (menor arco, lado do mouse)
        const double R = vals[0];
        const double cc = (m_arcB - m_arcA).length();
        if (R >= cc / 2 && cc > 1e-9) {
            const double sag =
                (m_arcSag < 0 ? -1.0 : 1.0) *
                (R - std::sqrt(R * R - cc * cc / 4.0));
            commitArc(sag);
            return true;
        }
        emit pickInfo(QStringLiteral("Raio menor que meia-corda (%1 m).")
                          .arg(cc / 2, 0, 'f', 2));
        return true;
    }
    if (m_tool == Tool::Rotate && m_rotStage == 2) {
        commitRotate(typed.contains('-') ? -vals[0] : vals[0], false);
        return true;
    }
    if (m_tool == Tool::Scale && m_scStage == 1) {
        commitScale(vals[0]);
        return true;
    }
    if (m_tool == Tool::Offset && m_offStage == 1) {   // R8: offset exato
        m_offStage = 0;
        offsetSelectedFace(vals[0]);
        return true;
    }
    if (m_tool == Tool::Line && m_chainActive &&
        m_lastMoveDir.lengthSq() > 1e-12) {
        // R4: lápis exato — o número fecha o traço na direção corrente
        pencilClick(m_chainPt + m_lastMoveDir * vals[0]);
        return true;
    }
    if (m_tool == Tool::Tape && m_tapeLast > 1e-9 && !vals.empty()) {
        // R8: fita CALIBRA a maquete — mediu, digitou o valor certo, o
        // modelo INTEIRO escala pra bater (Ctrl+Z desfaz)
        const double f = vals[0] / m_tapeLast;
        if (f > 1e-6 && std::abs(f - 1.0) > 1e-9) {
            pushUndo();
            const Point3 org{0, 0, 0};
            for (MeshPart& part : m_meshes) part.mesh.scaleAbout(org, f);
            for (auto& [a, b] : m_sketch) { a = a * f; b = b * f; }
            for (auto& [a, b] : m_guides) { a = a * f; b = b * f; }
            for (Dim3D& dm : m_dims) {
                dm.a = dm.a * f; dm.b = dm.b * f; dm.c = dm.c * f;
            }
            m_edited = true;
            m_tapeLast = 0.0;
            buildRenderArrays();
            emit structureChanged();
            emit pickInfo(QStringLiteral(
                              "Maquete REDIMENSIONADA ×%1 (a medida agora é "
                              "%2 m) — Ctrl+Z desfaz.")
                              .arg(f, 0, 'f', 4)
                              .arg(vals[0], 0, 'f', 3));
            update();
        }
        return true;
    }
    if (m_tool == Tool::Move && m_moveStage == 1 && m_moveMode == 0 &&
        !m_selSolidsMulti.empty() && m_lastMoveDir.lengthSq() > 1e-12) {
        // R4: multi/grupo movido por valor exato
        const Vec3 d = m_lastMoveDir * vals[0];
        pushUndo();
        if (!m_moveCopy)             // R29: cotas UMA vez, contra a UNIÃO
            transformDimAnchors(m_selSolidsMulti,
                                [&d](const Point3& p) { return p + d; });
        for (const int mi : m_selSolidsMulti) {
            if (mi < 0 || mi >= int(m_meshes.size())) continue;
            if (m_moveCopy) {
                MeshPart clone = m_meshes[std::size_t(mi)];
                clone.mesh.translate(d);
                m_meshes.push_back(std::move(clone));
            } else {
                m_meshes[std::size_t(mi)].mesh.translate(d);
            }
        }
        m_edited = true;
        cancelMoveStage();
        setTool(Tool::Select);
        buildRenderArrays();
        m_hlDirty = true;
        emit structureChanged();
        emit pickInfo(QStringLiteral("Multi movida %1 m exatos.")
                          .arg(vals[0], 0, 'f', 2));
        update();
        return true;
    }
    if (m_tool == Tool::Move && m_moveStage == 1 && m_moveMode == 0 &&
        m_moveMesh >= 0 && m_lastMoveDir.lengthSq() > 1e-12) {
        const Vec3 d = m_lastMoveDir * vals[0];
        pushUndo();
        if (m_moveCopy) {
            MeshPart clone = m_meshes[std::size_t(m_moveMesh)];
            clone.mesh.translate(d);
            m_meshes.push_back(std::move(clone));
            m_lastOp.kind = 5;
            m_lastOp.mesh = m_moveMesh;
            m_lastOp.U = d;
        } else {
            m_meshes[std::size_t(m_moveMesh)].mesh.translate(d);
        }
        m_edited = true;
        cancelMoveStage();
        setTool(Tool::Select);
        buildRenderArrays();
        m_hlDirty = true;
        emit pickInfo(QStringLiteral("Movido %1 m exatos — Ctrl+Z desfaz.")
                          .arg(vals[0], 0, 'f', 2));
        update();
        return true;
    }
    return false;
}

void Viewport3D::qaRectExact(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 4) return;
    setTool(Tool::Rect);
    rectClick(QPoint(int(c[0].toDouble() * width()),
                     int(c[1].toDouble() * height())));
    m_lastDu = 1.0;
    m_lastDv = 1.0;
    finishGestureExact(QStringLiteral("%1;%2").arg(c[2], c[3]));
}

void Viewport3D::qaPencilExact(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 3) return;
    setTool(Tool::Line);
    Point3 p;
    if (!pencilPointAt(QPoint(int(c[0].toDouble() * width()),
                              int(c[1].toDouble() * height())), p))
        return;
    pencilClick(p);
    m_lastMoveDir = Vec3{1, 0, 0};       // QA: leste, comprimento exato
    finishGestureExact(c[2]);
    m_chainActive = false;
}

void Viewport3D::qaMoveLate(const QString& s, bool copy) {
    const QStringList c = s.split(',');
    if (c.size() != 4) return;
    qaMove(c[0].toDouble(), c[1].toDouble(), c[2].toDouble(),
           c[3].toDouble(), copy);
}

// G6: transição ANIMADA de cena — lerp da câmera em 500 ms (ease in-out)
void Viewport3D::animateCameraTo(float yaw, float pitch, float dist,
                                 const float tgt[3]) {
    if (m_camAnim) {
        m_camAnim->stop();
        m_camAnim->deleteLater();
    }
    const float y0 = m_yaw, p0 = m_pitch, d0 = m_dist;
    const float t0[3] = {m_target[0], m_target[1], m_target[2]};
    const float t1[3] = {tgt[0], tgt[1], tgt[2]};
    float dy = yaw - y0;                    // menor arco do yaw
    while (dy > 180.f) dy -= 360.f;
    while (dy < -180.f) dy += 360.f;
    QVariantAnimation* a = new QVariantAnimation(this);
    m_camAnim = a;
    a->setDuration(500);
    a->setStartValue(0.0f);
    a->setEndValue(1.0f);
    a->setEasingCurve(QEasingCurve::InOutCubic);
    connect(a, &QVariantAnimation::valueChanged, this,
            [=](const QVariant& v) {
                const float k = v.toFloat();
                const float tt[3] = {t0[0] + (t1[0] - t0[0]) * k,
                                     t0[1] + (t1[1] - t0[1]) * k,
                                     t0[2] + (t1[2] - t0[2]) * k};
                setCameraState(y0 + dy * k, p0 + (pitch - p0) * k,
                               d0 + (dist - d0) * k, tt);
            });
    a->start(QAbstractAnimation::DeleteWhenStopped);
    connect(a, &QAbstractAnimation::destroyed, this,
            [this] { m_camAnim = nullptr; });
}

void Viewport3D::qaCtxAt(double nx, double ny) {
    QMouseEvent ev(QEvent::MouseButtonDblClick,
                   QPointF(nx * width(), ny * height()), Qt::LeftButton,
                   Qt::LeftButton, Qt::NoModifier);
    mouseDoubleClickEvent(&ev);
}

// ---------------------------------------------------------------------------
//  G4: FOLLOW ME — varre a FACE selecionada (perfil) pelo caminho do LÁPIS.
//  O anel do perfil é transportado segmento a segmento; em cada joelho ele é
//  projetado no plano BISSETOR (miter), como no SketchUp. Tampas nas pontas.
// ---------------------------------------------------------------------------
void Viewport3D::followMe() {
    if (m_selMesh < 0 || m_selMesh >= int(m_meshes.size()) ||
        m_selFace == HalfEdgeMesh::kNone || m_selWhole) {
        emit pickInfo(QStringLiteral(
            "Follow Me: selecione a FACE-perfil (clique nela) e desenhe o "
            "caminho com o lápis antes."));
        return;
    }
    if (m_sketch.empty()) {
        emit pickInfo(QStringLiteral(
            "Follow Me: desenhe o CAMINHO com o lápis (L) — pode subir pelos "
            "eixos (setas)."));
        return;
    }
    const HalfEdgeMesh& src = m_meshes[std::size_t(m_selMesh)].mesh;
    std::vector<Point3> prof;
    for (const Idx v : src.faceVertices(m_selFace))
        prof.push_back(src.vertex(v).p);
    if (prof.size() < 3) return;
    const Point3 c0 = src.faceCentroid(m_selFace);
    const Vec3 n0 = src.faceNormal(m_selFace);

    // encadeia o caminho a partir da PONTA mais próxima do perfil
    using K = std::tuple<long long, long long, long long>;
    std::map<K, std::vector<Point3>> adj;
    std::map<K, Point3> at;
    for (const auto& [a, b] : m_sketch) {
        adj[vkey(a)].push_back(b);
        adj[vkey(b)].push_back(a);
        at[vkey(a)] = a;
        at[vkey(b)] = b;
    }
    Point3 start{};
    double bd = 1e300;
    bool has = false;
    for (const auto& [k, vs] : adj)
        if (vs.size() == 1) {
            const double d = (at[k] - c0).lengthSq();
            if (d < bd) { bd = d; start = at[k]; has = true; }
        }
    if (!has) {
        emit pickInfo(QStringLiteral(
            "Follow Me: o caminho precisa de uma PONTA livre perto do perfil."));
        return;
    }
    std::vector<Point3> path{start};
    std::map<K, bool> seen{{vkey(start), true}};
    for (K k = vkey(start); path.size() < 256;) {
        bool adv = false;
        for (const Point3& nb : adj[k])
            if (!seen.count(vkey(nb))) {
                path.push_back(nb);
                seen[vkey(nb)] = true;
                k = vkey(nb);
                adv = true;
                break;
            }
        if (!adv) break;
    }
    if (path.size() < 2) return;

    // roda o perfil para o plano ⟂ ao 1º segmento (Rodrigues) e transporta
    auto rot = [](const Point3& p, const Point3& piv, const Vec3& ax,
                  double ang) {
        const Vec3 v = p - piv;
        const Vec3 r = v * std::cos(ang) + ax.cross(v) * std::sin(ang) +
                       ax * (ax.dot(v) * (1.0 - std::cos(ang)));
        return piv + r;
    };
    const Vec3 d0 = (path[1] - path[0]).normalized();
    Vec3 ax = n0.cross(d0);
    const double sl = ax.length();
    const double ct = std::clamp(n0.dot(d0), -1.0, 1.0);
    std::vector<Point3> cur;
    for (const Point3& p : prof) {
        Point3 q = p + (path[0] - c0);
        if (sl > 1e-9)
            q = rot(q, path[0], ax * (1.0 / sl), std::atan2(sl, ct));
        else if (ct < 0.0) {
            Vec3 any = (std::abs(n0.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0})
                           .cross(n0).normalized();
            q = rot(q, path[0], any, 3.14159265358979);
        }
        cur.push_back(q);
    }
    pushUndo();
    MeshPart part;
    HalfEdgeMesh& m = part.mesh;
    std::vector<std::vector<Idx>> rings;
    auto addRing = [&](const std::vector<Point3>& r) {
        std::vector<Idx> ids;
        for (const Point3& p : r) ids.push_back(m.addVertex(p));
        rings.push_back(std::move(ids));
    };
    addRing(cur);
    for (std::size_t i = 1; i < path.size(); ++i) {
        const Vec3 dPrev = (path[i] - path[i - 1]).normalized();
        Vec3 nP = dPrev;
        if (i + 1 < path.size()) {
            nP = dPrev + (path[i + 1] - path[i]).normalized();
            const double l = nP.length();
            nP = l > 1e-9 ? nP * (1.0 / l) : dPrev;   // plano bissetor
        }
        std::vector<Point3> nxt;
        for (const Point3& q : cur) {
            const double den = dPrev.dot(nP);
            if (std::abs(den) < 1e-9) {
                undoLast();
                m_redo.clear();
                emit pickInfo(QStringLiteral(
                    "Follow Me: joelho fechado demais — suavize o caminho."));
                return;
            }
            nxt.push_back(q + dPrev * ((path[i] - q).dot(nP) / den));
        }
        addRing(nxt);
        cur = nxt;
    }
    const std::size_t P = prof.size();
    std::vector<Idx> capA(rings.front().rbegin(), rings.front().rend());
    m.addFace(capA);
    m.addFace(rings.back());
    for (std::size_t i = 0; i + 1 < rings.size(); ++i)
        for (std::size_t j = 0; j < P; ++j) {
            const std::size_t j2 = (j + 1) % P;
            m.addFace({rings[i][j], rings[i][j2],
                       rings[i + 1][j2], rings[i + 1][j]});
        }
    std::string why;
    if (!m.checkIntegrity(&why)) {
        undoLast();
        m_redo.clear();
        emit pickInfo(QStringLiteral(
            "Follow Me: a varredura degenerou (%1) — tente um caminho mais "
            "aberto.").arg(QString::fromStdString(why)));
        return;
    }
    part.compName = QStringLiteral("Varredura");
    m_meshes.push_back(std::move(part));
    m_selMesh = int(m_meshes.size()) - 1;
    m_selFace = 0;
    m_selWhole = true;
    m_edited = true;
    m_hlDirty = true;
    buildRenderArrays();
    emit structureChanged();
    emit pickInfo(QStringLiteral(
                      "Follow Me: perfil varrido por %1 trecho(s) — %2 faces.")
                      .arg(path.size() - 1)
                      .arg(m_meshes.back().mesh.faceCount()));
    update();
}

// G4: FITA MÉTRICA — dois pontos inferidos: mede e deixa uma GUIA
void Viewport3D::tapeClick(const QPoint& pos) {
    const Infer in =
        inferAt(pos, m_tapeStage == 1 ? &m_tapeP1 : nullptr, true, true);
    if (in.kind == 0) return;
    m_infer = in;
    if (m_tapeStage == 0) {
        m_tapeP1 = in.p;
        m_tapeStage = 1;
        emit pickInfo(QStringLiteral(
            "Fita métrica: clique o 2º ponto — mede e deixa uma GUIA."));
        return;
    }
    const double d = (in.p - m_tapeP1).length();
    if (d < 1e-9) return;
    m_guides.push_back({m_tapeP1, in.p});
    m_tapeStage = 0;
    m_tapeLast = d;      // R8: digitar agora REDIMENSIONA a maquete
    buildRenderArrays();
    emit pickInfo(QStringLiteral(
                      "Fita métrica: %1 m — guia criada (a borracha apaga; "
                      "os pontos dela inferem).")
                      .arg(d, 0, 'f', 3));
    update();
}

void Viewport3D::qaTape(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() < 4) return;
    setTool(Tool::Tape);
    tapeClick(QPoint(int(c[0].toDouble() * width()),
                     int(c[1].toDouble() * height())));
    tapeClick(QPoint(int(c[2].toDouble() * width()),
                     int(c[3].toDouble() * height())));
    if (c.size() > 4)                    // R8: 5º valor CALIBRA a maquete
        finishGestureExact(c[4]);
}

// R26: COTA — 3 cliques (A, B medidos; C só derruba o lado/offset da linha
// de cota). Âncora por POSIÇÃO (não índice de vértice: booleana/soup rebuild
// não preserva índice, mas preserva a posição dos vértices não tocados).
void Viewport3D::dimClick(const QPoint& pos, bool ang) {
    const Infer in =
        inferAt(pos, m_dimStage == 1 ? &m_dimA : nullptr, true, true);
    if (in.kind == 0) return;
    m_infer = in;
    // R34: COTA ANGULAR — Ctrl no 1º clique entra no fluxo de 3 pontos:
    // braço A, VÉRTICE, braço C. O texto se posiciona sozinho na bissetriz
    // (sem 4º clique). a/b/c são TODOS medidos (âncora individual).
    if (m_dimStage == 0 && ang) {
        m_dimAng = true;
        m_dimA = in.p;
        m_dimStage = 1;
        emit pickInfo(QStringLiteral(
            "Cota angular: agora clique o VÉRTICE do ângulo."));
        return;
    }
    if (m_dimAng) {
        if (m_dimStage == 1) {
            if ((in.p - m_dimA).length() < 1e-9) return;
            m_dimB = in.p;                      // b = vértice
            m_dimStage = 2;
            emit pickInfo(QStringLiteral(
                "Cota angular: clique um ponto no 2º braço."));
            return;
        }
        // stage 2: fecha com o 2º braço
        if ((in.p - m_dimB).length() < 1e-9) return;
        Dim3D d;
        d.a = m_dimA;
        d.b = m_dimB;
        d.c = in.p;
        d.kind = 1;
        m_dims.push_back(d);
        m_dimStage = 0;
        m_dimAng = false;
        const Vec3 u = (d.a - d.b).normalized(), v = (d.c - d.b).normalized();
        const double deg =
            std::acos(std::clamp(u.dot(v), -1.0, 1.0)) * 180.0 / M_PI;
        buildRenderArrays();
        emit pickInfo(QStringLiteral(
                          "Cota angular: %1° criada — a borracha apaga.")
                          .arg(deg, 0, 'f', 1));
        update();
        return;
    }
    if (m_dimStage == 0) {
        // R27: COTA DE ARESTA — clicar EM CIMA de uma aresta (inferência "na
        // aresta") mede a aresta inteira num clique só: pega os dois cantos
        // dela como A/B e pula direto pro posicionamento (como no SketchUp).
        // Clicar num EXTREMO (kind 1) ou ponto médio segue ponto-a-ponto.
        if (in.kind == 3) {
            int mi = -1;
            Idx he = HalfEdgeMesh::kNone;
            if (pickEdgeAt(pos, mi, he)) {
                const HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
                m_dimA = m.vertex(m.halfEdge(he).origin).p;
                m_dimB = m.vertex(m.halfEdge(m.halfEdge(he).next).origin).p;
                m_dimStage = 2;
                emit pickInfo(QStringLiteral(
                    "Cota da aresta (%1 m): clique pra posicionar a linha.")
                        .arg((m_dimB - m_dimA).length(), 0, 'f', 3));
                return;
            }
        }
        m_dimA = in.p;
        m_dimStage = 1;
        emit pickInfo(QStringLiteral("Cota: clique o 2º ponto a medir "
                                     "(ou clique uma aresta inteira)."));
        return;
    }
    if (m_dimStage == 1) {
        if ((in.p - m_dimA).length() < 1e-9) return;
        m_dimB = in.p;
        m_dimStage = 2;
        emit pickInfo(QStringLiteral(
            "Cota: clique pra posicionar a linha de cota."));
        return;
    }
    Dim3D d;
    d.a = m_dimA;
    d.b = m_dimB;
    d.c = in.p;
    m_dims.push_back(d);
    m_dimStage = 0;
    buildRenderArrays();
    emit pickInfo(QStringLiteral("Cota: %1 m criada — a borracha apaga.")
                      .arg((m_dimB - m_dimA).length(), 0, 'f', 3));
    update();
}

void Viewport3D::qaDim3d(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 9) return;
    Dim3D d;
    d.a = {c[0].toDouble(), c[1].toDouble(), c[2].toDouble()};
    d.b = {c[3].toDouble(), c[4].toDouble(), c[5].toDouble()};
    d.c = {c[6].toDouble(), c[7].toDouble(), c[8].toDouble()};
    m_dims.push_back(d);
    buildRenderArrays();
    emit pickInfo(QStringLiteral("Cota: %1 m criada (QA).")
                      .arg((d.b - d.a).length(), 0, 'f', 3));
}

// R34: cota ANGULAR direta (QA) — mesmos 9 números, b é o vértice.
void Viewport3D::qaDimAng(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() != 9) return;
    Dim3D d;
    d.a = {c[0].toDouble(), c[1].toDouble(), c[2].toDouble()};
    d.b = {c[3].toDouble(), c[4].toDouble(), c[5].toDouble()};
    d.c = {c[6].toDouble(), c[7].toDouble(), c[8].toDouble()};
    d.kind = 1;
    m_dims.push_back(d);
    buildRenderArrays();
    const Vec3 u = (d.a - d.b).normalized(), v = (d.c - d.b).normalized();
    const double deg =
        std::acos(std::clamp(u.dot(v), -1.0, 1.0)) * 180.0 / M_PI;
    emit pickInfo(QStringLiteral("Cota angular: %1° criada (QA).")
                      .arg(deg, 0, 'f', 2));
}

void Viewport3D::qaDimClick(const QString& s) {
    setTool(Tool::Dim);
    const QStringList pts = s.split(';', Qt::SkipEmptyParts);
    for (const QString& p : pts) {
        const QStringList c = p.split(',');
        if (c.size() != 2) continue;
        dimClick(QPoint(int(c[0].toDouble() * width()),
                        int(c[1].toDouble() * height())));
    }
}

QJsonArray Viewport3D::dimsJson() const {
    QJsonArray arr;
    for (const Dim3D& d : m_dims)
        arr.append(QJsonArray{d.a.x, d.a.y, d.a.z, d.b.x, d.b.y, d.b.z,
                              d.c.x, d.c.y, d.c.z, d.kind});   // R34: kind
    return arr;
}

void Viewport3D::setDimsJson(const QJsonArray& arr) {
    m_dims.clear();
    for (const QJsonValue& v : arr) {
        const QJsonArray s = v.toArray();
        if (s.size() != 9 && s.size() != 10) continue;   // 9 = legado linear
        Dim3D d;
        d.a = {s[0].toDouble(), s[1].toDouble(), s[2].toDouble()};
        d.b = {s[3].toDouble(), s[4].toDouble(), s[5].toDouble()};
        d.c = {s[6].toDouble(), s[7].toDouble(), s[8].toDouble()};
        if (s.size() == 10) d.kind = s[9].toInt();       // R34: angular
        m_dims.push_back(d);        // órfã NÃO se persiste — deriva no re-snap
    }
}

// re-snap por POSIÇÃO: roda no único choke point comum a toda edição de
// malha (topo de buildRenderArrays, ~70 chamadas). Índice de vértice não
// sobrevive a booleana (soup rebuild); posição de vértice NÃO TOCADO, sim.
void Viewport3D::syncDimAnchors() {
    if (m_dims.empty()) return;
    using VK = std::tuple<long long, long long, long long>;
    auto qk = [](const Point3& p) {
        return VK{llround(p.x * 1e5), llround(p.y * 1e5),
                  llround(p.z * 1e5)};
    };
    std::set<VK> verts;
    for (const MeshPart& part : m_meshes)
        for (Idx v = 0; v < Idx(part.mesh.vertexCount()); ++v)
            verts.insert(qk(part.mesh.vertex(v).p));
    for (Dim3D& d : m_dims)
        d.orphan = !verts.count(qk(d.a)) || !verts.count(qk(d.b)) ||
                   (d.kind == 1 && !verts.count(qk(d.c)));   // R34: angular
}                                                            // mede c também

// carona: MOVER explícito (não booleana) sabe o delta `d` na hora — arrasta
// junto qualquer âncora que bata (por posição) com um vértice do sólido
// ANTES do translate. Sem isso, mover uma parede orfanaria a cota dela.
void Viewport3D::dragDimAnchors(const HalfEdgeMesh& before, const Vec3& d) {
    transformDimAnchors(before, [&d](const Point3& p) { return p + d; });
}

// R29: carona genérica das cotas — a âncora que casa (por posição) com um
// vértice do sólido ANTES da edição sofre a MESMA transformação `xf`. Regra
// rígida da R26: a/b movem se casam; c (offset) só quando OS DOIS movem junto
// (senão a linha de cota torceria). Serve translação (dragDimAnchors),
// rotação (rotateZ/rotateAxis) e escala (scaleAbout).
namespace {
using DimVK = std::tuple<long long, long long, long long>;
DimVK dimQuant(const Point3& p) {
    return DimVK{llround(p.x * 1e5), llround(p.y * 1e5), llround(p.z * 1e5)};
}
}  // namespace

void Viewport3D::transformDimAnchors(
    const HalfEdgeMesh& before,
    const std::function<Point3(const Point3&)>& xf) {
    if (m_dims.empty()) return;
    std::set<DimVK> verts;
    for (Idx v = 0; v < Idx(before.vertexCount()); ++v)
        verts.insert(dimQuant(before.vertex(v).p));
    for (Dim3D& dm : m_dims) {
        const bool ma = verts.count(dimQuant(dm.a)) != 0;
        const bool mb = verts.count(dimQuant(dm.b)) != 0;
        if (ma) dm.a = xf(dm.a);
        if (mb) dm.b = xf(dm.b);
        // R34: na angular, c é MEDIDO — casa e move sozinho; na linear,
        // c é offset e só acompanha quando a E b movem juntos (regra R26).
        if (dm.kind == 1) {
            if (verts.count(dimQuant(dm.c))) dm.c = xf(dm.c);
        } else if (ma && mb) {
            dm.c = xf(dm.c);
        }
    }
}

// R31: versão restrita a um CONJUNTO de posições — usada pelo push/pull, onde
// só as âncoras nos vértices DA FACE puxada devem acompanhar (uma cota noutro
// canto do mesmo sólido tem que ficar parada).
void Viewport3D::transformDimAnchors(
    const std::vector<Point3>& pts,
    const std::function<Point3(const Point3&)>& xf) {
    if (m_dims.empty() || pts.empty()) return;
    std::set<DimVK> verts;
    for (const Point3& p : pts) verts.insert(dimQuant(p));
    for (Dim3D& dm : m_dims) {
        const bool ma = verts.count(dimQuant(dm.a)) != 0;
        const bool mb = verts.count(dimQuant(dm.b)) != 0;
        if (ma) dm.a = xf(dm.a);
        if (mb) dm.b = xf(dm.b);
        if (dm.kind == 1) {                              // R34: c medido
            if (verts.count(dimQuant(dm.c))) dm.c = xf(dm.c);
        } else if (ma && mb) {
            dm.c = xf(dm.c);
        }
    }
}

// R29: versão MULTI — a UNIÃO dos vértices de TODOS os alvos é montada antes de
// qualquer mutação e a transformação roda UMA vez por cota. Corrige: (1) cota
// entre dois sólidos ambos selecionados (senão a/b movem mas c nunca), e (2)
// dobra de transformação por aliasing (âncora movida coincidindo com vértice
// de outro alvo). Chamar ANTES de mutar as malhas.
void Viewport3D::transformDimAnchors(
    const std::vector<int>& targets,
    const std::function<Point3(const Point3&)>& xf) {
    if (m_dims.empty()) return;
    std::set<DimVK> verts;
    for (const int mi : targets) {
        if (mi < 0 || mi >= int(m_meshes.size())) continue;
        const HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
        for (Idx v = 0; v < Idx(m.vertexCount()); ++v)
            verts.insert(dimQuant(m.vertex(v).p));
    }
    for (Dim3D& dm : m_dims) {
        const bool ma = verts.count(dimQuant(dm.a)) != 0;
        const bool mb = verts.count(dimQuant(dm.b)) != 0;
        if (ma) dm.a = xf(dm.a);
        if (mb) dm.b = xf(dm.b);
        if (dm.kind == 1) {                              // R34: c medido
            if (verts.count(dimQuant(dm.c))) dm.c = xf(dm.c);
        } else if (ma && mb) {
            dm.c = xf(dm.c);
        }
    }
}

// linha de cota deslocada: projeta A/B/C, deriva o offset perpendicular em
// TELA (screen-space, igual ao resto do overlay — sem geometria 3D nova).
bool Viewport3D::dimScreenLine(const Dim3D& d, const QMatrix4x4& mvp,
                               QPointF& da, QPointF& db) const {
    QPointF sa, sb, sc;
    if (!toScreen(mvp, d.a, sa) || !toScreen(mvp, d.b, sb) ||
        !toScreen(mvp, d.c, sc))
        return false;
    if (d.kind == 1) {          // R34: angular — a "linha" pro hit-test da
        da = sa;                // borracha é a corda braço→braço (o desenho
        db = sc;                // do arco não passa por aqui)
        return true;
    }
    QPointF dir = sb - sa;
    const double len = std::hypot(dir.x(), dir.y());
    if (len < 1e-6) return false;
    dir /= len;
    const QPointF perp(-dir.y(), dir.x());
    const double off = (sc.x() - sa.x()) * perp.x() + (sc.y() - sa.y()) * perp.y();
    da = sa + perp * off;
    db = sb + perp * off;
    return true;
}

void Viewport3D::vcbApply(const QString& typed) {
    // G4: MATRIZ — "x5" multiplica a última cópia/rotação; "/5" divide o passo
    const QString t = typed.trimmed();
    if ((t.startsWith(QLatin1Char('x')) || t.startsWith(QLatin1Char('/'))) &&
        (m_lastOp.kind == 5 || m_lastOp.kind == 6)) {
        const bool mult = t.startsWith(QLatin1Char('x'));
        const int n = t.mid(1).toInt();
        const int src = m_lastOp.mesh;
        if (n < 2 || src < 0 || src >= int(m_meshes.size())) return;
        pushUndo();
        int made = 0;
        if (m_lastOp.kind == 5) {
            const Vec3 d = m_lastOp.U;
            if (mult)
                for (int i = 2; i <= n; ++i, ++made) {
                    MeshPart c = m_meshes[std::size_t(src)];
                    c.mesh.translate(d * double(i));
                    m_meshes.push_back(std::move(c));
                }
            else
                for (int i = 1; i < n; ++i, ++made) {
                    MeshPart c = m_meshes[std::size_t(src)];
                    c.mesh.translate(d * (double(i) / n));
                    m_meshes.push_back(std::move(c));
                }
        } else {
            const double rad = m_lastOp.a * 3.14159265358979 / 180.0;
            for (int i = 1; i < n; ++i, ++made) {
                MeshPart c = m_meshes[std::size_t(src)];
                c.mesh.rotateZ(m_lastOp.p1,
                               mult ? rad * i : rad * double(i) / n);
                m_meshes.push_back(std::move(c));
            }
        }
        m_edited = true;
        buildRenderArrays();
        m_hlDirty = true;
        emit structureChanged();
        emit pickInfo(QStringLiteral("Matriz: %1 cópia(s) nova(s) — Ctrl+Z "
                                     "desfaz tudo.").arg(made));
        update();
        return;
    }
    // R8: "8s" pós-commit refaz o círculo/polígono com N lados
    if (typed.endsWith(QLatin1Char('s')) && m_lastOp.kind == 2) {
        const int n = typed.left(typed.size() - 1).toInt();
        if (n >= 3 && n <= 96) {
            setPolySides(n);
            redoCircle(std::abs(m_lastOp.a) > 1e-9 ? std::abs(m_lastOp.a)
                                                   : 1.0);
        }
        return;
    }
    const std::vector<double> vals = parseMeasures(typed);
    if (vals.empty()) return;
    if (m_lastOp.kind == 1 && vals.size() >= 2) redoRect(vals[0], vals[1]);
    else if (m_lastOp.kind == 2) redoCircle(std::abs(vals[0]));
    else if (m_lastOp.kind == 3) redoPull(vals[0]);
    else if (m_lastOp.kind == 4) {          // offset re-digitado
        const LastOp op = m_lastOp;
        undoLast();
        m_selMesh = op.mesh;
        m_selFace = op.face;
        m_selWhole = false;
        offsetSelectedFace(std::abs(vals[0]));
    }
    else
        emit pickInfo(QStringLiteral(
            "Medidas: desenhe algo primeiro (retângulo/círculo/puxar) e "
            "digite o valor exato em seguida."));
}

void Viewport3D::qaPick(double nx, double ny) {
    selectAt(QPoint(int(nx * width()), int(ny * height())));
}

void Viewport3D::qaMove(double nx1, double ny1, double nx2, double ny2,
                        bool copy) {
    startMoveCopy(copy);
    if (m_tool != Tool::Move) return;
    moveClick(QPoint(int(nx1 * width()), int(ny1 * height())));
    moveClick(QPoint(int(nx2 * width()), int(ny2 * height())));
}

void Viewport3D::qaCircle(double nx1, double ny1, double nx2, double ny2) {
    setTool(Tool::Circle);
    circleClick(QPoint(int(nx1 * width()), int(ny1 * height())));
    circleClick(QPoint(int(nx2 * width()), int(ny2 * height())));
}

void Viewport3D::qaPencil(const QVector<double>& nxy) {
    setTool(Tool::Line);
    for (int i = 0; i + 1 < nxy.size(); i += 2)
        lineClick(QPoint(int(nxy[i] * width()), int(nxy[i + 1] * height())));
}

QJsonArray Viewport3D::sketchJson() const {
    QJsonArray arr;
    for (const auto& [a, b] : m_sketch)
        arr.append(QJsonArray{a.x, a.y, a.z, b.x, b.y, b.z});
    return arr;
}

QJsonArray Viewport3D::guidesJson() const {          // G4: fita métrica
    QJsonArray arr;
    for (const auto& [a, b] : m_guides)
        arr.append(QJsonArray{a.x, a.y, a.z, b.x, b.y, b.z});
    return arr;
}

void Viewport3D::setGuidesJson(const QJsonArray& arr) {
    m_guides.clear();
    for (const QJsonValue& v : arr) {
        const QJsonArray s = v.toArray();
        if (s.size() != 6) continue;
        m_guides.push_back({{s[0].toDouble(), s[1].toDouble(), s[2].toDouble()},
                            {s[3].toDouble(), s[4].toDouble(), s[5].toDouble()}});
    }
    buildRenderArrays();
    update();
}

void Viewport3D::setSketchJson(const QJsonArray& arr) {
    m_sketch.clear();
    for (const QJsonValue& v : arr) {
        const QJsonArray e = v.toArray();
        if (e.size() == 6)
            m_sketch.push_back(
                {{e.at(0).toDouble(), e.at(1).toDouble(), e.at(2).toDouble()},
                 {e.at(3).toDouble(), e.at(4).toDouble(), e.at(5).toDouble()}});
    }
    buildRenderArrays();
    update();
}

void Viewport3D::qaDoublePick(double nx, double ny) {
    QMouseEvent ev(QEvent::MouseButtonDblClick,
                   QPointF(nx * width(), ny * height()), Qt::LeftButton,
                   Qt::LeftButton, Qt::NoModifier);
    mouseDoubleClickEvent(&ev);
}

void Viewport3D::qaRect(double nx1, double ny1, double nx2, double ny2) {
    setTool(Tool::Rect);
    rectClick(QPoint(int(nx1 * width()), int(ny1 * height())));
    rectClick(QPoint(int(nx2 * width()), int(ny2 * height())));
}

void Viewport3D::qaLine(double nx1, double ny1, double nx2, double ny2) {
    setTool(Tool::Line);
    lineClick(QPoint(int(nx1 * width()), int(ny1 * height())));
    lineClick(QPoint(int(nx2 * width()), int(ny2 * height())));
}

QString Viewport3D::debugSelState() const {
    if (m_selMesh < 0) return QStringLiteral("sem selecao\n");
    const HalfEdgeMesh& m = m_meshes[std::size_t(m_selMesh)].mesh;
    QString s = QStringLiteral("mesh=%1 face=%2 v=%3 f=%4 he=%5\n")
                    .arg(m_selMesh).arg(m_selFace)
                    .arg(m.vertexCount()).arg(m.faceCount())
                    .arg(m.halfEdgeCount());
    std::string why;
    s += QStringLiteral("integridade=%1 %2\n")
             .arg(m.checkIntegrity(&why) ? "OK" : "FALHOU")
             .arg(QString::fromStdString(why));
    for (const auto v : m.faceVertices(m_selFace)) {
        const cad::Point3& p = m.vertex(v).p;
        s += QStringLiteral("  v%1: %2, %3, %4\n")
                 .arg(v).arg(p.x, 0, 'f', 3).arg(p.y, 0, 'f', 3)
                 .arg(p.z, 0, 'f', 3);
    }
    return s;
}

// ---------------------------------------------------------------------------
//  Câmera: MMB orbita (Shift+MMB pan) · RMB pan · roda zoom no cursor
//  R52: dizia "LMB órbita · RMB/MMB pan" — errado desde a G3, e era a
//  1ª linha que se lia antes destes handlers. A cola do menu Ajuda cita
//  esta função como fonte; documentação que mente é pior que ausente.
// ---------------------------------------------------------------------------
void Viewport3D::mousePressEvent(QMouseEvent* e) {
    m_lastPos = e->pos();
    m_pressPos = e->pos();
    // R27: no walkthrough, arrastar (qualquer botão) OLHA em volta — sem
    // ferramenta, sem caixa de seleção, sem pivô de órbita.
    if (m_walk) {
        m_drag = Drag::Orbit;
        m_hasPivot = false;
        return;
    }
    // R34: DESLIZAR SEÇÃO armado — o press ancora o arrasto no eixo da normal
    if (m_armClipDrag && e->button() == Qt::LeftButton &&
        !m_sections.empty()) {
        Point3 o;
        Vec3 dir;
        if (rayAt(e->pos(), o, dir)) {
            const Section& s = m_sections.back();
            m_clipD0 = s.d;
            m_clipP0 = Point3{s.n.x * s.d, s.n.y * s.d, s.n.z * s.d};
            m_clipT0 = lineParamClosestToRay(m_clipP0, s.n, o, dir);
            m_clipDragging = true;
            m_drag = Drag::None;
        }
        return;
    }
    // G3: no Selecionar, ARRASTAR abre a caixa de seleção (esq→dir janela,
    // dir→esq cruzando) — órbita fica no botão do MEIO, como no SketchUp
    if (e->button() == Qt::LeftButton && m_tool == Tool::Select) {
        m_boxSelecting = true;
        m_boxStart = m_boxEnd = e->pos();
        m_drag = Drag::None;
        return;
    }
    // BORRACHA (G2): clique apaga; segurar e arrastar apaga tudo que tocar
    if (e->button() == Qt::LeftButton && m_tool == Tool::Erase) {
        m_eraseGesture = true;
        m_erasedAny = false;
        m_eraseHide = (e->modifiers() & Qt::ShiftModifier) != 0;   // R8
        pushUndo();
        eraseAt(e->pos(), m_eraseHide);
        m_drag = Drag::None;
        return;
    }
    // PUSH/PULL arrastando: pegar a face inicia o arrasto pela normal
    if (e->button() == Qt::LeftButton && m_tool == Tool::Pull) {
        if (m_pullSticky) {              // R2: 2º clique confirma (via release)
            m_drag = Drag::None;
            return;
        }
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        if (pickAt(e->pos(), mi, f)) {
            m_pullDrag = true;
            m_pullLeave = (e->modifiers() & Qt::ControlModifier) != 0;
            m_pullMesh = mi;
            m_pullFace = f;
            m_pullBase = m_meshes;
            m_pullC = m_meshes[std::size_t(mi)].mesh.faceCentroid(f);
            m_pullN = m_meshes[std::size_t(mi)].mesh.faceNormal(f);
            m_pullT = 0.0;
            m_drag = Drag::None;
            return;
        }
    }
    if (e->button() == Qt::LeftButton) {
        m_drag = Drag::Orbit;
        // pivô da órbita = ponto da cena sob o cursor (como no SketchUp)
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        Point3 hit;
        m_hasPivot = pickAt(e->pos(), mi, f, &hit);
        if (m_hasPivot) {
            m_pivot[0] = float(hit.x);
            m_pivot[1] = float(hit.y);
            m_pivot[2] = float(hit.z);
        }
    } else if (e->button() == Qt::MiddleButton) {
        // padrão da indústria: botão do MEIO orbita (Shift+meio = pan)
        // R52: mexeu aqui? a cola do menu Ajuda descreve estes gestos À MÃO
        // (ZendoWindow::colaDeAtalhos, bloco "Câmera (mouse)") — a varredura
        // de QAction não enxerga evento de mouse, então nada avisa se
        // divergir. Ficou 52 levas sem estar escrito em lugar nenhum.
        m_drag = (e->modifiers() & Qt::ShiftModifier) ? Drag::Pan : Drag::Orbit;
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        Point3 hit;
        m_hasPivot = pickAt(e->pos(), mi, f, &hit);
        if (m_hasPivot) {
            m_pivot[0] = float(hit.x);
            m_pivot[1] = float(hit.y);
            m_pivot[2] = float(hit.z);
        }
    } else if (e->button() == Qt::RightButton)
        m_drag = Drag::Pan;
    // arrastar esconde o hover (evita realce fantasma durante a órbita)
    if (m_hovMesh >= 0) {
        m_hovMesh = -1; m_hovFace = HalfEdgeMesh::kNone;
        m_hlDirty = true;
    }
}

void Viewport3D::mouseMoveEvent(QMouseEvent* e) {
    const QPoint d = e->pos() - m_lastPos;
    m_lastPos = e->pos();
    if (m_eraseGesture) {                   // borracha arrastando (G2)
        eraseAt(e->pos(), m_eraseHide);
        return;
    }
    if (m_boxSelecting) {                   // caixa de seleção viva (G3)
        m_boxEnd = e->pos();
        update();
        return;
    }
    if (m_clipDragging) {                   // R34: seção deslizando ao vivo
        Point3 o;
        Vec3 dir;
        if (rayAt(e->pos(), o, dir) && !m_sections.empty()) {
            Section& s = m_sections.back();
            const double t = lineParamClosestToRay(m_clipP0, s.n, o, dir);
            s.d = m_clipD0 + (t - m_clipT0);
            update();
        }
        return;
    }
    if (m_pullDrag) {                       // preview vivo da extrusão
        Point3 o;
        Vec3 dir;
        if (rayAt(e->pos(), o, dir)) {
            m_pullT = lineParamClosestToRay(m_pullC, m_pullN, o, dir);
            m_meshes = m_pullBase;
            if (std::abs(m_pullT) > 1e-4)
                m_meshes[std::size_t(m_pullMesh)].mesh.extrudeFace(m_pullFace,
                                                                   m_pullT);
            buildRenderArrays();
            liveVcb(QStringLiteral("%1 m").arg(m_pullT, 0, 'f', 2));
            emit pickInfo(QStringLiteral(
                              "Puxar: %1 m — clique/solte confirma, ou DIGITE "
                              "a medida exata agora")
                              .arg(m_pullT, 0, 'f', 2));
            update();
        }
        return;
    }
    const bool dragging =
        m_drag != Drag::None &&
        (e->pos() - m_pressPos).manhattanLength() >= 6;
    if (dragging && m_drag == Drag::Orbit) {
        const float dyaw = -d.x() * 0.32f;
        // R27: no walkthrough dá pra olhar bem pra baixo (chão) e pra cima; e o
        // eixo vertical inverte pra "Look Around" do SketchUp (arrasta p/ cima
        // = olha p/ cima), enquanto a órbita mantém o sinal de sempre.
        const float pLo = m_walk ? -89.0f : -10.0f;
        const float dy = m_walk ? -d.y() : d.y();
        const float newPitch = std::clamp(m_pitch + dy * 0.32f, pLo, 89.0f);
        const float dpitch = newPitch - m_pitch;
        if (m_hasPivot) {                   // órbita EM TORNO do ponto clicado
            const QMatrix4x4 v = viewMatrix();
            const QVector3D right(v(0, 0), v(0, 1), v(0, 2));
            QMatrix4x4 R;
            R.rotate(dyaw, 0, 0, 1);
            R.rotate(-dpitch, right);
            const QVector3D P(m_pivot[0], m_pivot[1], m_pivot[2]);
            const QVector3D T(m_target[0], m_target[1], m_target[2]);
            const QVector3D T2 = P + R.map(T - P);
            m_target[0] = T2.x(); m_target[1] = T2.y(); m_target[2] = T2.z();
        }
        m_yaw += dyaw;
        m_pitch = newPitch;
        update();
        return;
    }
    if (dragging && m_drag == Drag::Pan) {
        const QMatrix4x4 v = viewMatrix();
        const QVector3D right(v(0, 0), v(0, 1), v(0, 2));
        const QVector3D up(v(1, 0), v(1, 1), v(1, 2));
        const float s = m_dist * 0.0016f;
        const QVector3D t = right * (-d.x() * s) + up * (d.y() * s);
        m_target[0] += t.x(); m_target[1] += t.y(); m_target[2] += t.z();
        update();
        return;
    }
    if (m_drag != Drag::None || m_meshes.empty()) return;

    if (m_tool == Tool::Move) {
        moveHover(e->pos());
        return;
    }
    if (m_tool == Tool::Arc) {              // R7
        arcHover(e->pos());
        return;
    }
    if (m_tool == Tool::Rotate) {           // R7
        rotHover(e->pos());
        return;
    }
    if (m_tool == Tool::Scale) {            // R7
        scaleHover(e->pos());
        return;
    }
    if (m_tool == Tool::Offset) {           // R8
        offsetHover(e->pos());
        return;
    }
    if (m_tool == Tool::Tape) {             // G4: fantasma da fita + glifo
        m_ghost.clear();
        m_snapMark.clear();
        const Infer in = inferAt(e->pos(),
                                 m_tapeStage == 1 ? &m_tapeP1 : nullptr,
                                 true, true);
        m_infer = in;
        if (m_tapeStage == 1 && in.kind != 0) {
            pushSeg(m_ghost, m_tapeP1, in.p);
            emit pickInfo(QStringLiteral("Fita: %1 m")
                              .arg((in.p - m_tapeP1).length(), 0, 'f', 3));
        }
        m_ghostDirty = true;
        update();
        return;
    }
    if (m_tool == Tool::Circle) {
        circleHover(e->pos());
        return;
    }
    if (m_tool == Tool::Rect || m_tool == Tool::Line) {
        if (m_tool == Tool::Rect) rectHover(e->pos());
        else lineHover(e->pos());
        const int stage = m_tool == Tool::Rect ? m_rectStage : m_lineStage;
        if (stage == 0) {                // realça a face-alvo do 1º clique
            int mi = -1;
            Idx f = HalfEdgeMesh::kNone;
            pickAt(e->pos(), mi, f);
            if (mi != m_hovMesh || f != m_hovFace) {
                m_hovMesh = mi; m_hovFace = f;
                m_hlDirty = true;
                update();
            }
        }
        return;
    }
    // hover: realce discreto da face sob o mouse (modo seleção)
    int mi = -1;
    Idx f = HalfEdgeMesh::kNone;
    pickAt(e->pos(), mi, f);
    if (mi != m_hovMesh || f != m_hovFace) {
        m_hovMesh = mi;
        m_hovFace = f;
        m_hlDirty = true;
        // R55: o BALDE agora ANUNCIA o alvo antes de pintar.
        // Com a seta dá pra clicar só pra conferir; com o balde, conferir é
        // errar — o clique já pinta. O realce sempre existiu (esta função é
        // fallthrough de toda ferramenta sem branch, o Paint incluso: o
        // "sem destaque" que o dogfooding relatou era falso-positivo do robô,
        // que teleporta o cursor e não gera mouseMove). O que faltava era o
        // TEXTO: o faceInfo só saía no CLIQUE da seta.
        if (m_tool == Tool::Paint) {
            if (mi >= 0 && f != HalfEdgeMesh::kNone) {
                emit pickInfo(faceInfo(mi, f));
            } else {
                // saiu de toda face: devolve a instrução do balde. Sem isto o
                // rodapé fica com a info órfã de uma face que o mouse já
                // deixou — pior que não dizer nada.
                emit pickInfo(QStringLiteral(
                    "BALDE: clique nas faces pra pintar — Ctrl = sólido "
                    "inteiro · Alt = conta-gotas · a paleta troca o material "
                    "(Esc sai)"));
            }
        }
        update();
    }
}

void Viewport3D::mouseReleaseEvent(QMouseEvent* e) {
    if (m_walk) { m_drag = Drag::None; return; }   // R27: sem ferramenta no walk
    if (m_clipDragging && e->button() == Qt::LeftButton) {
        m_clipDragging = false;                    // R34: solta = fixa o plano
        m_armClipDrag = false;
        if (!m_sections.empty()) {
            m_edited = true;
            emit pickInfo(QStringLiteral(
                "Seção fixada (deslizou %1 m; d = %2). Menu desliza de novo.")
                    .arg(m_sections.back().d - m_clipD0, 0, 'f', 3)
                    .arg(m_sections.back().d, 0, 'f', 3));
        }
        update();
        return;
    }
    if (m_eraseGesture && e->button() == Qt::LeftButton) {
        m_eraseGesture = false;             // gesto vazio não gasta undo
        if (!m_erasedAny && !m_undo.empty()) m_undo.pop_back();
        return;
    }
    if (m_pullDrag && e->button() == Qt::LeftButton) {
        // R2: soltar PARADO vira click-move-click — o preview segue o mouse
        if (!m_pullSticky &&
            (e->pos() - m_pressPos).manhattanLength() < 6) {
            m_pullSticky = true;
            emit pickInfo(QStringLiteral(
                "Puxar: mova o mouse e CLIQUE para confirmar — ou digite a "
                "medida."));
            return;
        }
        m_pullSticky = false;
        // confirma o arrasto: undo restaura a cena PRÉ-pull; refaz exato
        const double t = m_pullT;
        m_meshes = std::move(m_pullBase);
        m_pullBase.clear();
        m_pullDrag = false;
        if (std::abs(t) >= 1e-3 && m_pullLeave) {
            // G4: Ctrl+Puxar EMPILHA um volume novo — a face original fica
            const HalfEdgeMesh& src = m_meshes[std::size_t(m_pullMesh)].mesh;
            std::vector<Point3> loop;
            for (const Idx v : src.faceVertices(m_pullFace))
                loop.push_back(src.vertex(v).p);
            const Vec3 n = src.faceNormal(m_pullFace);
            pushUndo();
            MeshPart part;
            HalfEdgeMesh& nm = part.mesh;
            std::vector<Idx> lo, hi;
            for (const Point3& p : loop) lo.push_back(nm.addVertex(p));
            for (const Point3& p : loop) hi.push_back(nm.addVertex(p + n * t));
            std::vector<Idx> rev(lo.rbegin(), lo.rend());
            nm.addFace(rev);
            nm.addFace(hi);
            for (std::size_t j = 0; j < loop.size(); ++j) {
                const std::size_t j2 = (j + 1) % loop.size();
                nm.addFace({lo[j], lo[j2], hi[j2], hi[j]});
            }
            std::string why;
            if (nm.checkIntegrity(&why)) {
                m_meshes.push_back(std::move(part));
                m_selMesh = int(m_meshes.size()) - 1;
                m_selFace = 1;
                m_selWhole = false;
                m_edited = true;
                m_hlDirty = true;
                buildRenderArrays();
                emit structureChanged();
                emit pickInfo(QStringLiteral(
                                  "Ctrl+Puxar: volume NOVO empilhado (%1 m) "
                                  "— a face original ficou (G gruda).")
                                  .arg(t, 0, 'f', 2));
            } else {
                undoLast();
                m_redo.clear();
            }
            m_pullLeave = false;
            update();
        } else if (std::abs(t) >= 1e-3) {
            m_selMesh = m_pullMesh;
            m_selFace = m_pullFace;
            m_selWhole = false;
            pushPullSelected(t);            // registra LastOp: VCB re-digita
        } else {
            buildRenderArrays();
            emit pickInfo(QStringLiteral("Puxar cancelado."));
            update();
        }
        return;
    }
    if (m_boxSelecting && e->button() == Qt::LeftButton) {
        m_boxSelecting = false;             // G3: soltou a caixa
        if ((e->pos() - m_pressPos).manhattanLength() >= 6) {
            m_boxEnd = e->pos();
            applyBoxSelect();
            return;
        }                                   // parado = clique normal (cai)
    }
    const bool click = (e->pos() - m_pressPos).manhattanLength() < 6;
    m_drag = Drag::None;
    if (!click || e->button() != Qt::LeftButton) return;
    if (m_armStair) {                       // R41: ESCADA — pé e direção
        Point3 o;
        Vec3 d;
        if (rayAt(e->pos(), o, d) && std::abs(d.z) > 1e-9) {
            const double t = -o.z / d.z;    // interseção com o chão z=0
            if (t > 0) {
                const Point3 p{o.x + d.x * t, o.y + d.y * t, 0.0};
                if (m_stairStage == 0) {
                    m_stairOrigin = p;
                    m_stairStage = 1;
                    emit pickInfo(QStringLiteral(
                        "Escada: agora clique a DIREÇÃO de subida."));
                } else {
                    m_armStair = false;
                    m_stairStage = 0;
                    buildStair(m_stairOrigin,
                               {p.x - m_stairOrigin.x, p.y - m_stairOrigin.y,
                                0.0},
                               m_stairW, m_stairH, m_stairRun);
                }
            }
        }
        return;
    }
    if (m_armGuard) {                       // R43: GUARDA-CORPO — início e fim
        // aceita clique em cima de LAJE (face horizontal) ou no chão z=0
        Point3 o;
        Vec3 d;
        Point3 p{};
        bool hit = false;
        if (rayAt(e->pos(), o, d)) {
            int mi = -1;
            Idx f = HalfEdgeMesh::kNone;
            if (pickAt(e->pos(), mi, f)) {
                const HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
                const Vec3 n = m.faceNormal(f);
                const double den = n.dot(d);
                if (n.z > 0.9 && std::abs(den) > 1e-9) {
                    const double t = (n.dot(m.faceCentroid(f)) - n.dot(o)) / den;
                    if (t > 0) {
                        p = {o.x + d.x * t, o.y + d.y * t, o.z + d.z * t};
                        hit = true;
                    }
                }
            }
            if (!hit && std::abs(d.z) > 1e-9) {
                const double t = -o.z / d.z;
                if (t > 0) {
                    p = {o.x + d.x * t, o.y + d.y * t, 0.0};
                    hit = true;
                }
            }
        }
        if (hit) {
            if (m_guardStage == 0) {
                m_guardA = p;
                m_guardStage = 1;
                emit pickInfo(
                    QStringLiteral("Guarda-corpo: agora clique o FIM."));
            } else {
                m_armGuard = false;
                m_guardStage = 0;
                buildGuard(m_guardA, p, m_guardH, m_guardGap);
            }
        }
        return;
    }
    if (m_armSlab) {                        // R43: LAJE — 4 cantos em planta
        Point3 o;
        Vec3 d;
        if (rayAt(e->pos(), o, d) && std::abs(d.z) > 1e-9) {
            const double t = -o.z / d.z;
            if (t > 0) {
                const double px = o.x + d.x * t, py = o.y + d.y * t;
                if (m_slabStage < 3) {
                    m_slabPts[m_slabStage * 2] = px;
                    m_slabPts[m_slabStage * 2 + 1] = py;
                    ++m_slabStage;
                    emit pickInfo(
                        m_slabStage == 1
                            ? QStringLiteral("Laje: 2º canto da LAJE.")
                            : m_slabStage == 2
                                  ? QStringLiteral("Laje: 1º canto da ABERTURA.")
                                  : QStringLiteral("Laje: 2º canto da ABERTURA."));
                } else {
                    m_armSlab = false;
                    m_slabStage = 0;
                    buildSlabHole(m_slabPts[0], m_slabPts[1], m_slabPts[2],
                                  m_slabPts[3], m_slabPts[4], m_slabPts[5],
                                  px, py, m_slabZ, m_slabTh);
                }
            }
        }
        return;
    }
    if (m_armClipFace) {                    // R30: SEÇÃO no plano da face clicada
        m_armClipFace = false;
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        if (pickAt(e->pos(), mi, f)) {
            const HalfEdgeMesh& m = m_meshes[std::size_t(mi)].mesh;
            const Vec3 n = m.faceNormal(f);
            // R31: +0,1 mm pra face clicada (e coplanares) sobreviver inteira —
            // sem isso o dot≈d oscila por pixel e a TAMPA vira chuvisco.
            setClipPlane(n, n.dot(m.faceCentroid(f)) + 1e-4);
        } else {
            emit pickInfo(QStringLiteral(
                "Seção na face: nenhuma face sob o clique."));
        }
        return;
    }
    if (m_armPositionCam) {                 // R28: POSICIONAR CÂMERA (1ª pessoa)
        Point3 at;
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        if (pickAt(e->pos(), mi, f, &at)) {        // pega o ponto do sólido
            // R49: só se PISA nela. A face clicada precisa apontar pra cima:
            // clicar numa parede (n.z≈0) plantava o olho DENTRO dela, e o
            // teto/face inferior de laje (n.z<0) é o mesmo bug de cabeça pra
            // baixo. Recusar respeita mais que "ajustar" — o clique é um
            // contrato, e empurrar o olho pra um lugar que ele não pediu
            // mata a semântica de POSICIONAR.
            const Vec3 n = m_meshes[std::size_t(mi)].mesh.faceNormal(f);
            if (n.z <= 0.5) {
                // arm NÃO consumido: ele só reclica (Esc cancela, via
                // disarmGestures — a lição R30/R34 segue de pé).
                emit pickInfo(QStringLiteral(
                    "Posicionar câmera: clique um PISO (ou o chão) — não dá "
                    "pra ficar em pé numa parede."));
                return;
            }
        } else {
            Point3 o;
            Vec3 d2;
            if (!rayAt(e->pos(), o, d2) || std::abs(d2.z) < 1e-9) return;
            at = o + d2 * (-o.z / d2.z);
        }
        m_armPositionCam = false;
        // planta o olho SOBRE o ponto clicado: mira m_target ali e entra no
        // walk pelo setter REAL (setWalkthrough põe o olho a 1,6 m de m_target).
        m_target[0] = float(at.x);
        m_target[1] = float(at.y);
        m_target[2] = float(at.z);
        setWalkthrough(true);
        m_walkEye[2] = float(at.z) + 1.6f;   // 1,6 m ACIMA da superfície clicada
        update();
        return;
    }
    if (!m_pendingComp.isEmpty()) {         // posiciona a instância armada
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        Point3 at;
        if (!pickAt(e->pos(), mi, f, &at)) {
            Point3 o;
            Vec3 d2;
            if (!rayAt(e->pos(), o, d2) || std::abs(d2.z) < 1e-9) return;
            at = o + d2 * (-o.z / d2.z);
        }
        insertComponent(m_pendingComp, at);
        m_pendingComp.clear();
        return;
    }
    if (m_tool == Tool::Rect) rectClick(e->pos());
    else if (m_tool == Tool::Line) lineClick(e->pos());
    else if (m_tool == Tool::Move) {
        if (m_moveStage == 1 && (e->modifiers() & Qt::ControlModifier))
            m_moveCopy = true;           // R8: Ctrl no gesto alterna COPIAR
        moveClick(e->pos());
    }
    else if (m_tool == Tool::Circle) circleClick(e->pos());
    else if (m_tool == Tool::Tape) tapeClick(e->pos());
    else if (m_tool == Tool::Arc) arcClick(e->pos());
    else if (m_tool == Tool::Rotate)
        rotClick(e->pos(), e->modifiers() & Qt::ControlModifier);
    else if (m_tool == Tool::Scale) scaleClick(e->pos());
    else if (m_tool == Tool::Offset) offsetClick(e->pos());
    else if (m_tool == Tool::Dim)
        dimClick(e->pos(),
                 (e->modifiers() & Qt::ControlModifier) != 0);   // R34: Ctrl=angular
    else if (m_tool == Tool::Paint) {    // R6/R8: balde + matching
        const bool ctrl = e->modifiers() & Qt::ControlModifier;
        const bool shift = e->modifiers() & Qt::ShiftModifier;
        paintClickAt(e->pos(), ctrl && !shift,
                     e->modifiers() & Qt::AltModifier,
                     shift ? (ctrl ? 2 : 1) : 0);
    }
    else if (e->modifiers() &
             (Qt::ControlModifier | Qt::ShiftModifier)) {
        // G3: Ctrl SOMA · Shift ALTERNA · Ctrl+Shift TIRA (faces na multi)
        const int oM = m_selMesh;
        const Idx oF = m_selFace;
        const bool oW = m_selWhole;
        selectAt(e->pos());
        if (m_selMesh >= 0 && !m_selWhole) {
            const std::pair<int, Idx> it{m_selMesh, m_selFace};
            auto& v = m_selFacesMulti;
            const auto at = std::find(v.begin(), v.end(), it);
            const bool both = (e->modifiers() & Qt::ControlModifier) &&
                              (e->modifiers() & Qt::ShiftModifier);
            if (both) {
                if (at != v.end()) v.erase(at);
            } else if (e->modifiers() & Qt::ShiftModifier) {
                if (at != v.end()) v.erase(at);
                else v.push_back(it);
            } else if (at == v.end()) {
                v.push_back(it);
            }
        }
        m_selMesh = oM;                     // multi é aditiva: primário fica
        m_selFace = oF;
        m_selWhole = oW;
        m_hlDirty = true;
        emit pickInfo(selectionSummary());
        update();
    } else if (m_fmPerimArm) {              // R8: face-caminho do Follow Me
        m_fmPerimArm = false;
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        if (pickAt(e->pos(), mi, f)) followMePerimeter(mi, f);
    } else {
        m_selFacesMulti.clear();            // clique simples zera a multi
        m_selSolidsMulti.clear();
        selectAt(e->pos());
        // G5: clicar num sólido AGRUPADO seleciona o grupo inteiro (fora do
        // contexto dele — dentro, edita-se peça a peça)
        const QString g = partGroup(m_selMesh);
        if (!g.isEmpty() && m_ctxGroup != g) {
            for (int i = 0; i < int(m_meshes.size()); ++i)
                if (m_meshes[std::size_t(i)].group == g)
                    m_selSolidsMulti.push_back(i);
            m_selWhole = true;
            m_hlDirty = true;
            emit pickInfo(QStringLiteral(
                              "Grupo \"%1\": %2 sólido(s) — Del/H valem pro "
                              "bloco; DUPLO clique entra nele.")
                              .arg(g)
                              .arg(m_selSolidsMulti.size()));
            update();
        }
    }
}

// G3: caixa de seleção — esq→dir = JANELA (sólidos contidos), dir→esq =
// CRUZANDO (qualquer vértice dentro). Com Ctrl/Shift a caixa SOMA à multi.
void Viewport3D::applyBoxSelect() {
    const QMatrix4x4 mvp = projMatrix() * viewMatrix();
    const bool window = m_boxEnd.x() >= m_boxStart.x();
    const QRectF r(QPointF(std::min(m_boxStart.x(), m_boxEnd.x()),
                           std::min(m_boxStart.y(), m_boxEnd.y())),
                   QPointF(std::max(m_boxStart.x(), m_boxEnd.x()),
                           std::max(m_boxStart.y(), m_boxEnd.y())));
    if (!(QGuiApplication::keyboardModifiers() &
          (Qt::ControlModifier | Qt::ShiftModifier))) {
        m_selSolidsMulti.clear();
        m_selFacesMulti.clear();
    }
    for (int i = 0; i < int(m_meshes.size()); ++i) {
        const MeshPart& part = m_meshes[std::size_t(i)];
        if (part.hidden || !inCtx(i) || part.mesh.vertexCount() == 0) continue;
        bool all = true, any = false;
        for (std::size_t v = 0; v < part.mesh.vertexCount(); ++v) {
            QPointF s;
            const bool in = toScreen(mvp, part.mesh.vertex(Idx(v)).p, s) &&
                            r.contains(s);
            any |= in;
            all &= in;
        }
        if ((window ? all : any) &&
            std::find(m_selSolidsMulti.begin(), m_selSolidsMulti.end(), i) ==
                m_selSolidsMulti.end())
            m_selSolidsMulti.push_back(i);
    }
    m_selMesh = -1;
    m_selFace = HalfEdgeMesh::kNone;
    m_selEdgeMesh = -1;
    m_selEdge = HalfEdgeMesh::kNone;
    m_selWhole = false;
    m_hlDirty = true;
    emit pickInfo(QStringLiteral("%1 · %2")
                      .arg(window ? QStringLiteral("Janela (contidos)")
                                  : QStringLiteral("Cruzando (tocados)"))
                      .arg(selectionSummary()));
    update();
}

// G3: Entity Info compacto — o resumo vivo da seleção múltipla
QString Viewport3D::selectionSummary() const {
    double area = 0.0;
    for (const auto& [mi, f] : m_selFacesMulti)
        if (mi >= 0 && mi < int(m_meshes.size()))
            area += m_meshes[std::size_t(mi)].mesh.faceArea(f);
    QString s = QStringLiteral("Seleção: %1 sólido(s) · %2 face(s)")
                    .arg(m_selSolidsMulti.size())
                    .arg(m_selFacesMulti.size());
    if (!m_selFacesMulti.empty())
        s += QStringLiteral(" · área somada %1 m²").arg(area, 0, 'f', 2);
    return s;
}

void Viewport3D::qaSelBox(const QString& s) {
    const QStringList c = s.split(',');
    if (c.size() < 4) return;
    m_boxStart = QPoint(int(c[0].toDouble() * width()),
                        int(c[1].toDouble() * height()));
    m_boxEnd = QPoint(int(c[2].toDouble() * width()),
                      int(c[3].toDouble() * height()));
    applyBoxSelect();
    if (c.size() == 5 && c[4] == QLatin1String("del")) deleteSelected();
}

void Viewport3D::mouseDoubleClickEvent(QMouseEvent* e) {
    // gesto icônico do SketchUp: duplo clique no Pull REPETE a última distância
    if (m_tool == Tool::Pull && m_lastOp.kind == 3 &&
        std::abs(m_lastOp.a) > 1e-9) {
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        if (pickAt(e->pos(), mi, f)) {
            m_selMesh = mi;
            m_selFace = f;
            m_selWhole = false;
            pushPullSelected(m_lastOp.a);
        }
        return;
    }
    if (m_tool == Tool::Offset && m_lastOp.kind == 4 &&
        std::abs(m_lastOp.a) > 1e-9) {
        // R8: duplo clique no OFFSET repete a última distância
        int mi = -1;
        Idx f = HalfEdgeMesh::kNone;
        if (pickAt(e->pos(), mi, f)) {
            m_selMesh = mi;
            m_selFace = f;
            m_selWhole = false;
            offsetSelectedFace(std::abs(m_lastOp.a));
        }
        return;
    }
    if (m_tool != Tool::Select) return;
    // duplo clique = seleciona o SÓLIDO inteiro (Del/M/C/T operam nele);
    // num GRUPO (G5), o duplo clique ENTRA no contexto — o resto esmaece
    int mi = -1;
    Idx f = HalfEdgeMesh::kNone;
    if (pickAt(e->pos(), mi, f)) {
        const QString g = partGroup(mi);
        if (!g.isEmpty() && m_ctxGroup != g) {
            m_ctxGroup = g;
            m_ctxMesh = -1;
            m_selMesh = -1;
            m_selFace = HalfEdgeMesh::kNone;
            m_selWhole = false;
            m_selSolidsMulti.clear();
            m_selFacesMulti.clear();
            m_hlDirty = true;
            buildRenderArrays();
            emit pickInfo(QStringLiteral(
                              "Editando o grupo \"%1\" — o resto esmaeceu e "
                              "não responde (Esc sai).").arg(g));
            update();
            return;
        }
        // R42: 2º duplo clique numa INSTÂNCIA de componente já selecionada
        // entra no contexto DELA — editar aqui propaga a todas ao sair.
        const MeshPart& pref = m_meshes[std::size_t(mi)];
        if (!pref.compName.isEmpty() && m_selMesh == mi && m_selWhole &&
            m_ctxMesh != mi) {
            m_ctxMesh = mi;
            m_ctxGroup.clear();
            m_ctxCompHash = meshHash(pref.mesh);
            m_selMesh = -1;
            m_selFace = HalfEdgeMesh::kNone;
            m_selWhole = false;
            m_selSolidsMulti.clear();
            m_selFacesMulti.clear();
            m_hlDirty = true;
            buildRenderArrays();
            emit pickInfo(QStringLiteral(
                              "Editando o componente ⟨%1⟩ — o que mudar aqui "
                              "PROPAGA às outras instâncias ao sair (Esc).")
                              .arg(pref.compName));
            update();
            return;
        }
        m_selMesh = mi;
        m_selFace = f;
        m_selWhole = true;
        m_selEdgeMesh = -1;
        m_selEdge = HalfEdgeMesh::kNone;
        const MeshPart& part = m_meshes[std::size_t(mi)];
        emit pickInfo(QStringLiteral(
                          "SÓLIDO selecionado%1 · %2 faces — M: mover · C: "
                          "copiar · T: pintar · Del: apagar")
                          .arg(part.wallNo
                                   ? QStringLiteral(" (Parede %1)").arg(part.wallNo)
                                   : QString())
                          .arg(part.mesh.faceCount()));
        m_hlDirty = true;
        update();
    }
}

void Viewport3D::wheelEvent(QWheelEvent* e) {
    const float steps = float(e->angleDelta().y()) / 120.0f;
    if (m_walk) {                        // R27: roda ANDA pra frente/trás
        const float yaw = qDegreesToRadians(m_yaw);
        m_walkEye[0] += std::cos(yaw) * steps * 0.5f;
        m_walkEye[1] += std::sin(yaw) * steps * 0.5f;
        update();
        return;
    }
    const float oldDist = m_dist;
    m_dist = std::clamp(m_dist * std::pow(0.88f, steps),
                        m_radius * 0.02f, m_radius * 40.0f);
    // ZOOM AO CURSOR: o alvo desliza na direção do ponto sob o mouse
    const QPoint mp = e->position().toPoint();
    int mi = -1;
    Idx f = HalfEdgeMesh::kNone;
    Point3 hit;
    bool has = pickAt(mp, mi, f, &hit);
    if (!has) {
        Point3 o;
        Vec3 d;
        if (rayAt(mp, o, d) && std::abs(d.z) > 1e-9) {
            const double t = -o.z / d.z;
            if (t > 0.0) { hit = o + d * t; has = true; }
        }
    }
    if (has && oldDist > 1e-6f) {
        const float k = 1.0f - m_dist / oldDist;   // >0 ao aproximar
        m_target[0] += (float(hit.x) - m_target[0]) * k;
        m_target[1] += (float(hit.y) - m_target[1]) * k;
        m_target[2] += (float(hit.z) - m_target[2]) * k;
    }
    update();
}

void Viewport3D::leaveEvent(QEvent*) {
    bool dirty = false;
    if (m_hovMesh >= 0) {
        m_hovMesh = -1;
        m_hovFace = HalfEdgeMesh::kNone;
        m_hlDirty = true;
        dirty = true;
    }
    if (!m_ghost.empty() || !m_snapMark.empty()) {
        m_ghost.clear();
        m_snapMark.clear();
        m_ghostDirty = true;
        dirty = true;
    }
    if (m_infer.kind != 0) {
        m_infer = Infer{};
        dirty = true;
    }
    if (dirty) update();
}

void Viewport3D::keyPressEvent(QKeyEvent* e) {
    // R27/R28: WALKTHROUGH — WASD/setas andam no plano do olho (altura fixa),
    // Esc sai. Vem ANTES da captura do VCB (que senão comeria o 's'). O
    // movimento é CONTÍNUO: a tecla entra num conjunto e o timer (walkStep)
    // integra velocidade×dt enquanto ela estiver pressionada — auto-repeat é
    // ignorado (o conjunto já segura a tecla).
    if (m_walk) {
        if (e->key() == Qt::Key_Escape) { setWalkthrough(false); return; }
        switch (e->key()) {
            case Qt::Key_W: case Qt::Key_Up:
            case Qt::Key_S: case Qt::Key_Down:
            case Qt::Key_D: case Qt::Key_Right:
            case Qt::Key_A: case Qt::Key_Left:
                m_walkKeys.insert(e->key());   // idempotente; autorepeat ok
                return;
            default: return;             // outras teclas: nada no walkthrough
        }
    }
    // VCB: dígitos digitados viram medida; Enter aplica na última operação
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (!m_vcbBuf.isEmpty()) {
            const QString s = m_vcbBuf;
            m_vcbBuf.clear();
            emit vcbText(QString());
            if (!finishGestureExact(s)) vcbApply(s);   // R2: meio do gesto 1º
        } else if (m_tool == Tool::Select &&
                   m_lastToolUsed != Tool::Select) {
            setTool(m_lastToolUsed);       // R4: Enter repete a ferramenta
        }
        return;
    }
    if (e->key() == Qt::Key_Space) {     // R2: Espaço = Selecionar (SketchUp)
        setTool(Tool::Select);
        return;
    }
    if (!m_vcbBuf.isEmpty() && e->key() == Qt::Key_Backspace) {
        m_vcbBuf.chop(1);
        emit vcbText(m_vcbBuf);
        return;
    }
    const QString t = e->text();
    if (!t.isEmpty() && QStringLiteral("0123456789.,;-x/cmrs").contains(t)) {
        m_vcbBuf += t;
        emit vcbText(m_vcbBuf);
        return;
    }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }
    if (e->key() == Qt::Key_Escape &&
        (m_tool == Tool::Move || m_tool == Tool::Circle ||
         m_tool == Tool::Pull || m_tool == Tool::Erase ||
         m_tool == Tool::Tape || m_tool == Tool::Paint ||
         m_tool == Tool::Arc || m_tool == Tool::Rotate ||
         m_tool == Tool::Scale || m_tool == Tool::Offset)) {
        m_tapeStage = 0;
        m_arcStage = 0;
        m_rotStage = 0;
        m_scStage = 0;
        m_offStage = 0;
        m_scAxis = 0;
        setTool(Tool::Select);
        update();
        return;
    }
    // R8: setas no ESCALA travam o eixo (escala não-uniforme)
    if (m_tool == Tool::Scale &&
        (e->key() == Qt::Key_Right || e->key() == Qt::Key_Left ||
         e->key() == Qt::Key_Up)) {
        const int ax = e->key() == Qt::Key_Right ? 1
                       : e->key() == Qt::Key_Left ? 2 : 3;
        m_scAxis = (m_scAxis == ax) ? 0 : ax;
        static const char* kAx[4] = {"uniforme", "só X", "só Y", "só Z"};
        emit pickInfo(QStringLiteral("Escala: %1.")
                          .arg(QString::fromUtf8(kAx[m_scAxis])));
        return;
    }
    // TRAVAS DE EIXO (setas: → X · ← Y · ↑ Z; repetir solta) — Mover e Lápis
    if ((m_tool == Tool::Move || m_tool == Tool::Line) &&
        (e->key() == Qt::Key_Right || e->key() == Qt::Key_Left ||
         e->key() == Qt::Key_Up)) {
        const int ax = e->key() == Qt::Key_Right ? 1
                       : e->key() == Qt::Key_Left ? 2 : 3;
        static const char* kAxName[4] = {"livre", "eixo X", "eixo Y", "eixo Z"};
        if (m_tool == Tool::Move) {
            m_moveAxisLock = (m_moveAxisLock == ax) ? 0 : ax;
            emit pickInfo(QStringLiteral("Mover: direção %1.")
                              .arg(QString::fromUtf8(kAxName[m_moveAxisLock])));
        } else {
            m_axisDrawLock = (m_axisDrawLock == ax) ? 0 : ax;
            emit pickInfo(QStringLiteral("Lápis: direção %1 — desenha no "
                                         "ESPAÇO ao longo do eixo.")
                              .arg(QString::fromUtf8(kAxName[m_axisDrawLock])));
        }
        return;
    }
    // G1: Shift TRAVA a inferência corrente (direção/alinhamento fica presa)
    if (e->key() == Qt::Key_Shift && !m_shiftLock && m_infer.kind != 0 &&
        m_infer.hasRef) {
        const Vec3 d = m_infer.p - m_infer.ref;
        if (d.lengthSq() > 1e-12) {
            m_shiftLock = true;
            m_lockKind = m_infer.kind;
            m_lockP = m_infer.ref;
            m_lockDir = d.normalized();
            emit pickInfo(QStringLiteral("Inferência TRAVADA (%1) — solte o "
                                         "Shift para liberar.")
                              .arg(QString::fromUtf8(inferLabel(m_lockKind))));
            update();
        }
        return;
    }
    if (e->key() == Qt::Key_Escape) {
        if (!m_pendingComp.isEmpty()) {     // R14: solta a inserção armada
            m_pendingComp.clear();
            emit pickInfo(QStringLiteral("Inserção cancelada."));
            return;
        }
        if (m_clipDragging) {              // R34: Esc no MEIO do arrasto volta
            m_clipDragging = false;        // a seção pra onde estava
            if (!m_sections.empty()) m_sections.back().d = m_clipD0;
            disarmGestures();
            emit pickInfo(QStringLiteral("Deslize cancelado."));
            update();
            return;
        }
        if (m_armClipFace || m_armPositionCam || m_armClipDrag || m_armStair ||
            m_armGuard || m_armSlab) {
            disarmGestures();              // R30/R28/R34/R41: desarma o gesto
            emit pickInfo(QStringLiteral("Cancelado."));
            return;
        }
        if (m_tool == Tool::Rect) {
            if (m_rectStage == 1) {      // volta pro 1º canto
                cancelRectStage();
                emit pickInfo(QStringLiteral(
                    "Retângulo: clique o 1º canto sobre uma face (Esc sai)"));
            } else {
                setTool(Tool::Select);
            }
        } else if (m_tool == Tool::Line) {
            if (m_chainActive) {            // encerra o traço do lápis
                m_chainActive = false;
                m_axisDrawLock = 0;
                m_ghost.clear();
                m_ghostDirty = true;
                emit pickInfo(QStringLiteral(
                    "Lápis: traço encerrado (as arestas ficam no rascunho)."));
            } else if (m_lineStage == 1) {
                cancelLineStage();
                emit pickInfo(QStringLiteral(
                    "Linha: clique o 1º ponto numa aresta da face (Esc sai)"));
            } else {
                setTool(Tool::Select);
            }
        } else if (m_selMesh >= 0 || !m_selSolidsMulti.empty() ||
                   !m_selFacesMulti.empty()) {
            m_selMesh = -1;
            m_selFace = HalfEdgeMesh::kNone;
            m_selSolidsMulti.clear();
            m_selFacesMulti.clear();
            m_hlDirty = true;
            emit pickInfo(QString());
        } else if (inEditContext()) {       // G5: Esc sobe do contexto
            exitContext();
        }
        update();
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}

void Viewport3D::keyReleaseEvent(QKeyEvent* e) {
    if (m_walk && !e->isAutoRepeat()) {   // R28: solta a tecla de movimento
        m_walkKeys.remove(e->key());
        return;
    }
    if (e->key() == Qt::Key_Shift && m_shiftLock) {   // solta a trava (G1)
        m_shiftLock = false;
        m_lockKind = 0;
        emit pickInfo(QString());
        update();
        return;
    }
    QOpenGLWidget::keyReleaseEvent(e);
}

// R28: alt-tab/perda de foco solta TODAS as teclas — senão uma tecla presa no
// momento do foco sair "gruda" e o olho anda sozinho ao voltar.
void Viewport3D::focusOutEvent(QFocusEvent* e) {
    m_walkKeys.clear();
    QOpenGLWidget::focusOutEvent(e);
}

// R28: um passo do walkthrough — soma as direções das teclas pressionadas e
// move o olho por velocidade×dt. Altura fixa (anda, não voa).
// R49: o dt agora é MEDIDO, não presumido. Assumir 16 ms fazia o passo mentir
// em máquina lenta ou com o timer atrasado (a velocidade real virava função da
// carga da máquina). O relógio é reiniciado no TOPO e INCONDICIONALMENTE —
// senão o 1º passo depois de um tempo parado viria com o dt acumulado inteiro
// e teleportaria; e o clamp cobre freeze/alt-tab (o focusOutEvent já limpa as
// teclas, mas não o relógio).
void Viewport3D::walkStep() {
    const double medido = m_walkClock.isValid()
                              ? m_walkClock.restart() / 1000.0
                              : (m_walkClock.start(), 0.016);
    walkStep(std::clamp(medido, 0.0, 0.1));
}

void Viewport3D::walkStep(double dt) {
    if (!m_walk || m_walkKeys.isEmpty()) return;
    const float yaw = qDegreesToRadians(m_yaw);
    const QVector3D fwd(std::cos(yaw), std::sin(yaw), 0.0f);
    const QVector3D right(std::sin(yaw), -std::cos(yaw), 0.0f);
    QVector3D d(0, 0, 0);
    if (m_walkKeys.contains(Qt::Key_W) || m_walkKeys.contains(Qt::Key_Up))
        d += fwd;
    if (m_walkKeys.contains(Qt::Key_S) || m_walkKeys.contains(Qt::Key_Down))
        d -= fwd;
    if (m_walkKeys.contains(Qt::Key_D) || m_walkKeys.contains(Qt::Key_Right))
        d += right;
    if (m_walkKeys.contains(Qt::Key_A) || m_walkKeys.contains(Qt::Key_Left))
        d -= right;
    if (d.lengthSquared() < 1e-9f) return;
    d.normalize();                        // diagonal não anda mais rápido
    constexpr float kSpeed = 2.8f;        // m/s (caminhada rápida)
    const float step = kSpeed * float(dt);
    m_walkEye[0] += d.x() * step;
    m_walkEye[1] += d.y() * step;
    update();
}
