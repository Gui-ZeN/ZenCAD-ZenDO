# -*- coding: utf-8 -*-
# A PROVA DE COBERTURA QUE ENTENDE ROTAÇÃO — polígono real, não bbox.
#
# POR QUE ELA EXISTE. Todas as minhas cegueiras nesta casa têm a MESMA raiz:
# eu raciocino por BBOX, e o bbox de uma peça girada é gordo nos dois eixos.
# Uma laje girada 8° tem bbox que "cobre" cantos que a laje não cobre — então
# eu declarava ambiente coberto e o Guilherme via o buraco na tela. O script da
# casa já documenta TRÊS correções desse mesmo tipo (hall, faixa oeste, quina
# NE), sempre achadas a olho, nunca por número. "Girado não cobre reto" não é
# um bug que se conserta uma vez: é uma classe que só morre trocando o
# instrumento.
#
# O QUE MUDA AQUI: em vez do bbox, projeto a FACE de verdade (a lista de
# vértices do polígono) no plano XY e faço ponto-em-polígono. Uma laje girada
# projeta um losango, e o losango não cobre a quina — que é a verdade.
#
# LIMITE DECLARADO: só considera faces quase-horizontais (|nz| > 0.9). Telhado
# muito inclinado não conta como cobertura horizontal — o que é proposital: um
# ambiente sob água de telhado inclinada é coberto pela água, e para "chove
# dentro?" isso deve ser testado com o raio zenital, não com esta prova.
#
# Uso:  python prova_cobertura.py <casa.zendo> [--z0 3.0 --z1 3.6]
#                                 [--reg "nome,x0,y0,x1,y1"]...
import json, sys, io, argparse

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")


def carrega_faces(path, z0, z1):
    """Faces quase-horizontais na faixa de cota, como polígonos XY."""
    d = json.load(open(path, encoding="utf-8"))
    polis = []
    for m in d.get("meshes", []):
        vs = m.get("verts") or []
        if not vs:
            continue
        if not isinstance(vs[0], list):            # achatado -> triplas
            vs = [vs[i:i + 3] for i in range(0, len(vs), 3)]
        for f in m.get("faces") or []:
            if len(f) < 3:
                continue
            pts = [vs[i] for i in f if 0 <= i < len(vs)]
            if len(pts) < 3:
                continue
            # normal por Newell (robusta pra polígono não-triangular)
            nx = ny = nz = 0.0
            for k in range(len(pts)):
                a, b = pts[k], pts[(k + 1) % len(pts)]
                nx += (a[1] - b[1]) * (a[2] + b[2])
                ny += (a[2] - b[2]) * (a[0] + b[0])
                nz += (a[0] - b[0]) * (a[1] + b[1])
            n = (nx * nx + ny * ny + nz * nz) ** 0.5
            if n < 1e-12:
                continue
            if abs(nz / n) < 0.9:                  # não é horizontal
                continue
            zs = [p[2] for p in pts]
            zm = sum(zs) / len(zs)
            if not (z0 <= zm <= z1):
                continue
            polis.append([(p[0], p[1]) for p in pts])
    return polis


def dentro(poli, x, y):
    """Ponto-em-polígono por ray casting. AQUI mora a diferença do bbox."""
    n = len(poli)
    d = False
    j = n - 1
    for i in range(n):
        xi, yi = poli[i]
        xj, yj = poli[j]
        if (yi > y) != (yj > y):
            xint = (xj - xi) * (y - yi) / (yj - yi + 1e-30) + xi
            if x < xint:
                d = not d
        j = i
    return d


def varre(polis, x0, y0, x1, y1, passo=0.15):
    """Devolve as células SEM cobertura, agrupadas em regiões contíguas."""
    sem = []
    x = x0
    while x <= x1:
        y = y0
        while y <= y1:
            if not any(dentro(p, x, y) for p in polis):
                sem.append((round(x, 3), round(y, 3)))
            y += passo
        x += passo
    # agrupa vizinhos (flood fill 8-conectado)
    S = set(sem)
    vis = set()
    comps = []
    for c in sem:
        if c in vis:
            continue
        pilha, comp = [c], []
        while pilha:
            p = pilha.pop()
            if p in vis or p not in S:
                continue
            vis.add(p)
            comp.append(p)
            px, py = p
            for dx in (-passo, 0, passo):
                for dy in (-passo, 0, passo):
                    pilha.append((round(px + dx, 3), round(py + dy, 3)))
        comps.append(comp)
    comps.sort(key=len, reverse=True)
    return comps, passo


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("casa")
    ap.add_argument("--z0", type=float, default=3.0)
    ap.add_argument("--z1", type=float, default=3.6)
    ap.add_argument("--passo", type=float, default=0.15)
    ap.add_argument("--reg", action="append", default=[],
                    help='"nome,x0,y0,x1,y1" — repetível')
    a = ap.parse_args()

    polis = carrega_faces(a.casa, a.z0, a.z1)
    print("  %d faces horizontais na cota %.2f–%.2f" % (len(polis), a.z0, a.z1))
    if not a.reg:
        print("  (informe --reg \"nome,x0,y0,x1,y1\" para testar um ambiente)")
        return 0

    ruim = 0
    for r in a.reg:
        p = r.split(",")
        nome = p[0]
        x0, y0, x1, y1 = (float(v) for v in p[1:5])
        comps, passo = varre(polis, x0, y0, x1, y1, a.passo)
        cel = passo * passo
        # ignora franjas de 1-2 células (borda da malha, irrelevante)
        reais = [c for c in comps if len(c) * cel >= 0.10]
        print()
        print("  ┌─ %s  (x %.2f–%.2f · y %.2f–%.2f)" % (nome, x0, x1, y0, y1))
        if not reais:
            print("  │ ✓ coberto — nenhum furo ≥ 0,10 m²")
        else:
            ruim += len(reais)
            for c in reais[:6]:
                xs = [q[0] for q in c]
                ys = [q[1] for q in c]
                print("  │ ✗ FURO de %.2f m² em x[%.2f,%.2f] y[%.2f,%.2f]"
                      % (len(c) * cel, min(xs), max(xs), min(ys), max(ys)))
        print("  └─")
    print()
    print("  %s" % ("✓ nenhum furo de cobertura" if not ruim
                    else "✗ %d furo(s) de cobertura" % ruim))
    return 1 if ruim else 0


if __name__ == "__main__":
    sys.exit(main())
