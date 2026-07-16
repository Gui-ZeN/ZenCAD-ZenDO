// src/core/edit/ArrayPathOps.hpp
#pragma once
// ============================================================================
//  ArrayPathOps — ARRAYPATH (distribuir cópias ao longo de um caminho).
//
//  Header-only / inline. NÃO depende de Qt/OpenGL.
//  Usa SOMENTE a API real do kernel:
//    - Entity::clone()            -> std::unique_ptr<Entity>  (cópia profunda)
//    - Entity::transform(const Matrix4&)  -> void              (afim, in-place)
//    - Entity::boundingBox()      -> AABB                       (p/ centro do bbox)
//    - Matrix4::translation(Vec3) / Matrix4::rotationZ(double) / operator*
//    - Vec3/Point3 (de core/math/Vec.hpp)
// ============================================================================
#include <cmath>
#include <memory>
#include <vector>

#include "core/geometry/Entity.hpp"   // Entity, clone(), transform(), boundingBox()
#include "core/math/Matrix4.hpp"      // Matrix4::translation/rotationZ/operator*
#include "core/math/Vec.hpp"          // Vec3 / Point3
#include "core/math/AABB.hpp"         // tipo de retorno de boundingBox()

namespace cad {

// ----------------------------------------------------------------------------
//  Centro do bounding box de uma entidade.
//  AABB do kernel expõe min/max (ver core/math/AABB.hpp); o centro é a média.
//  Mantido inline e isolado para não acoplar o resto do header ao layout do AABB.
// ----------------------------------------------------------------------------
inline Point3 bboxCenter(const Entity& e) {
    return e.boundingBox().center();  // AABB::center() = média de min/max
}

// ----------------------------------------------------------------------------
//  Resultado de uma amostragem por comprimento de arco sobre a polilinha-caminho.
//    pos = ponto sobre o caminho
//    tan = direção (tangente) do segmento onde o ponto caiu (não-normalizada-nula
//          só no caso degenerado de caminho de comprimento zero)
// ----------------------------------------------------------------------------
struct PathSample {
    Point3 pos;
    Vec3   tan;
};

// ----------------------------------------------------------------------------
//  Amostra o caminho `path` na distância de arco `dist` (em unidades de mundo),
//  caminhando segmento a segmento. `dist` é fixado ao intervalo [0, L].
//  `path` deve ter >= 2 vértices (garantido pelo chamador).
// ----------------------------------------------------------------------------
inline PathSample sampleAtArcLength(const std::vector<Point3>& path, double dist) {
    // Fixa nos extremos para robustez numérica.
    if (dist <= 0.0) {
        const Vec3 t = path[1] - path[0];
        return PathSample{path.front(), t};
    }

    double acc = 0.0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const Vec3   seg = path[i + 1] - path[i];
        const double segLen = seg.length();
        if (segLen <= 0.0) continue;  // pula vértices repetidos

        if (dist <= acc + segLen) {
            const double local = (dist - acc) / segLen;            // [0,1] dentro do segmento
            const Point3 p = path[i] + seg * local;                // interpolação linear
            return PathSample{p, seg};                             // tangente = direção do segmento
        }
        acc += segLen;
    }

    // dist >= L: cai no último vértice, tangente do último segmento não-nulo.
    Vec3 lastTan = path.back() - path.front();
    for (std::size_t i = path.size(); i-- > 1;) {
        const Vec3 seg = path[i] - path[i - 1];
        if (seg.length() > 0.0) { lastTan = seg; break; }
    }
    return PathSample{path.back(), lastTan};
}

// ----------------------------------------------------------------------------
//  arrayAlongPath — distribui `count` cópias de `src` ao longo de `path`,
//  igualmente espaçadas POR COMPRIMENTO DE ARCO. A original NÃO entra no
//  resultado (devolve apenas as cópias).
//
//  Para i = 0..count-1:
//    * posição de arco  d_i = i * (L / (count-1))   (count==1 => d_0 = 0, no início)
//    * ponto p e tangente t = sampleAtArcLength(path, d_i)
//    * a cópia é transladada de (p - centroDoBBoxDeSrc)  => fica CENTRADA em p
//    * se `align`: rotaciona em torno de p por atan2(t.y, t.x)
//
//  Composição da transformação (Matrix4 é column-major; v' = M * v):
//    sem align:   M = translation(p - c)
//    com align:   M = translation(p) * rotationZ(ang) * translation(-c)
//                 (leva c->origem, gira, e reposiciona em p)
// ----------------------------------------------------------------------------
inline std::vector<std::unique_ptr<Entity>>
arrayAlongPath(const Entity& src,
               const std::vector<Point3>& path,
               int count,
               bool align) {
    std::vector<std::unique_ptr<Entity>> out;
    if (count <= 0 || path.size() < 2) return out;
    out.reserve(static_cast<std::size_t>(count));

    // Centro do bbox da entidade-fonte (ponto de referência das cópias).
    const Point3 c = bboxCenter(src);

    // Comprimento total do caminho = soma dos segmentos.
    double L = 0.0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i)
        L += (path[i + 1] - path[i]).length();

    // Passo de arco entre cópias consecutivas.
    // count==1 => uma cópia no início (d_0 = 0). count>1 => espaçamento uniforme.
    const double step = (count > 1) ? (L / static_cast<double>(count - 1)) : 0.0;

    for (int i = 0; i < count; ++i) {
        const double d = step * static_cast<double>(i);
        const PathSample s = sampleAtArcLength(path, d);

        // Monta a matriz afim conforme `align`.
        Matrix4 M;
        if (align) {
            const double ang = std::atan2(s.tan.y, s.tan.x);
            // M = T(p) * Rz(ang) * T(-c)
            M = Matrix4::translation(Vec3{s.pos.x, s.pos.y, s.pos.z})
              * Matrix4::rotationZ(ang)
              * Matrix4::translation(Vec3{-c.x, -c.y, -c.z});
        } else {
            // Apenas translada c -> p.
            M = Matrix4::translation(s.pos - c);
        }

        std::unique_ptr<Entity> copy = src.clone();  // cópia profunda independente
        copy->transform(M);                          // aplica a transformação afim in-place
        out.push_back(std::move(copy));
    }

    return out;
}

} // namespace cad
