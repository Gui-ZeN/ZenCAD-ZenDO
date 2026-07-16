// src/core/edit/ModifyOps.hpp
#pragma once
#include "core/geometry/Line.hpp"
#include "core/geometry/Arc.hpp"
#include "core/geometry/Entity.hpp"
#include "core/math/Vec.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace cad {

// ============================================================================
//  ModifyOps — operações de modificação de linhas (break, join, lengthen).
//  Funções livres, puras, sem estado, todas no plano XY em precisão dupla
//  (double). A coordenada Z dos pontos de entrada é tratada como geometria 2D
//  (apenas X/Y participam dos cálculos); os resultados são emitidos com z = 0.
// ============================================================================

// "Break" (quebrar) entre dois pontos: projeta p1 e p2 sobre a linha l (fixando
// as projeções ao segmento) e retorna as partes que SOBRAM, isto é, os trechos
// FORA do intervalo [p1, p2]. Pode devolver 0, 1 ou 2 linhas:
//   - 2 linhas: quando o intervalo removido fica no meio da linha;
//   - 1 linha:  quando o intervalo encosta numa das pontas;
//   - 0 linhas: quando o intervalo cobre a linha inteira.
// Se p1 ≈ p2 (quebra num único ponto), devolve as duas metades da linha.
// Trechos de comprimento ~0 são descartados.
std::vector<Line> breakLine(const Line& l, const Point3& p1, const Point3& p2);

// "Join" (unir): se as linhas a e b forem COLINEARES (pertencem à mesma reta,
// dentro da tolerância angular tol) e se tocarem/encostarem nos extremos (ou
// estiverem sobrepostas), devolve UMA única linha que vai do extremo mais
// distante de uma ao extremo mais distante da outra (projeta os quatro pontos
// no eixo comum e toma mínimo/máximo). Caso contrário, devolve nullopt.
std::optional<Line> joinLines(const Line& a, const Line& b, double tol = 1e-6);

// "Join" GENÉRICO de duas linhas: se COLINEARES → uma única Line (via joinLines);
// senão, se compartilham um extremo (dentro de tol) → uma POLILINHA aberta de 3
// vértices (o extremo comum vira o vértice do meio). Devolve nullptr se não der.
std::unique_ptr<Entity> joinEntities(const Entity& a, const Entity& b, double tol = 1e-6);

// "Lengthen" (alongar/aparar): estende (delta > 0) ou apara (delta < 0) o
// comprimento da linha l em delta, mantendo a direção. A alteração ocorre na
// ponta end() quando fromEnd == true, ou na ponta start() caso contrário.
// O comprimento nunca inverte: é fixado num piso ~0 (a linha não passa a
// apontar para o lado oposto).
Line lengthenLine(const Line& l, double delta, bool fromEnd = true);

// "Lengthen" para ARCO: alonga (delta > 0) ou apara (delta < 0) o comprimento de
// arco em delta, convertendo para ângulo (Δângulo = delta / raio). Cresce na ponta
// final (fromEnd == true) avançando o endAngle, ou na ponta inicial recuando o
// startAngle. Centro e raio preservados.
Arc lengthenArc(const Arc& a, double delta, bool fromEnd = true);

} // namespace cad
