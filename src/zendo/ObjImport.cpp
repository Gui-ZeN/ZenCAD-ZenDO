// src/zendo/ObjImport.cpp
#include "ObjImport.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

namespace objimp {
namespace {

using cad::HalfEdgeMesh;
using cad::Point3;
using Idx = HalfEdgeMesh::Idx;
using K = std::tuple<long long, long long, long long>;

K chave(const Point3& p) {
    return {std::llround(p.x * 1e6), std::llround(p.y * 1e6),
            std::llround(p.z * 1e6)};
}

// .mtl: só o difuso (Kd) interessa por enquanto
std::map<QString, std::array<float, 3>> lerMtl(const QString& path) {
    std::map<QString, std::array<float, 3>> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    QString atual;
    while (!ts.atEnd()) {
        const QString ln = ts.readLine().trimmed();
        const QStringList tk =
            ln.split(QRegularExpression(QStringLiteral("\\s+")),
                     Qt::SkipEmptyParts);
        if (tk.isEmpty()) continue;
        if (tk[0] == QLatin1String("newmtl") && tk.size() > 1)
            atual = tk[1];
        else if (tk[0] == QLatin1String("Kd") && tk.size() >= 4 &&
                 !atual.isEmpty())
            out[atual] = {tk[1].toFloat(), tk[2].toFloat(), tk[3].toFloat()};
    }
    return out;
}

} // namespace

bool importar(const QString& path, Resultado& out, QString* erro) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (erro) *erro = QStringLiteral("Não consegui abrir o arquivo.");
        return false;
    }
    QTextStream ts(&f);
    const QDir dir = QFileInfo(path).dir();

    std::vector<Point3> vsOrig;          // como vieram (p/ índices)
    std::map<K, Idx> solda;              // posição -> vértice soldado
    std::vector<Idx> soldado;            // índice original -> soldado
    std::map<QString, std::array<float, 3>> mtl;
    std::array<float, 3> corAtual{};
    bool temCor = false;

    auto soldaDe = [&](int iObj) -> Idx {
        // OBJ é 1-based; negativo é relativo ao fim
        const long long n = (long long)vsOrig.size();
        const long long i = iObj > 0 ? iObj - 1 : n + iObj;
        if (i < 0 || i >= n) return HalfEdgeMesh::kNone;
        return soldado[std::size_t(i)];
    };

    const QRegularExpression kWs(QStringLiteral("\\s+"));
    while (!ts.atEnd()) {
        const QString ln = ts.readLine().trimmed();
        if (ln.isEmpty() || ln.startsWith('#')) continue;
        const QStringList tk = ln.split(kWs, Qt::SkipEmptyParts);
        if (tk[0] == QLatin1String("v") && tk.size() >= 4) {
            const Point3 p{tk[1].toDouble(), tk[2].toDouble(),
                           tk[3].toDouble()};
            vsOrig.push_back(p);
            const K k = chave(p);
            const auto it = solda.find(k);
            if (it != solda.end()) {
                soldado.push_back(it->second);
            } else {
                const Idx nv = out.mesh.addVertex(p);
                solda[k] = nv;
                soldado.push_back(nv);
            }
        } else if (tk[0] == QLatin1String("mtllib") && tk.size() > 1) {
            const auto m = lerMtl(dir.absoluteFilePath(tk[1]));
            mtl.insert(m.begin(), m.end());
        } else if (tk[0] == QLatin1String("usemtl") && tk.size() > 1) {
            const auto it = mtl.find(tk[1]);
            temCor = it != mtl.end();
            if (temCor) corAtual = it->second;
        } else if (tk[0] == QLatin1String("f") && tk.size() >= 4) {
            std::vector<Idx> loop;
            for (int i = 1; i < tk.size(); ++i) {
                const QString v = tk[i].section('/', 0, 0);
                const Idx s = soldaDe(v.toInt());
                if (s == HalfEdgeMesh::kNone) { loop.clear(); break; }
                if (loop.empty() || (loop.back() != s && loop.front() != s))
                    loop.push_back(s);
            }
            if (loop.size() < 3) continue;
            Idx face = out.mesh.addFace(loop);
            if (face == HalfEdgeMesh::kNone) {   // winding inconsistente?
                std::reverse(loop.begin(), loop.end());
                face = out.mesh.addFace(loop);
            }
            if (face == HalfEdgeMesh::kNone) {
                ++out.descartadas;
                continue;
            }
            ++out.faces;
            if (temCor) out.cores[face] = corAtual;
        }
    }
    if (out.faces == 0) {
        if (erro)
            *erro = QStringLiteral(
                "Nenhuma face aproveitável no OBJ (%1 descartadas).")
                        .arg(out.descartadas);
        return false;
    }
    return true;
}

} // namespace objimp
