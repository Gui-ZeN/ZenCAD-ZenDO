# -*- coding: utf-8 -*-
# R56 — A PROVA DA CHUVA: todo lugar onde se pisa tem teto?
#
# POR QUE EXISTE. O Guilherme olhou o hall e disse: "NAO TEM LAJE KKKK CHOVEU
# MOLHOU O TERREO INTEIRO". Ele estava certo: eu dei laje pra garagem e esqueci
# o hall — sobravam 1,3 a 2,1 m de ceu aberto bem sobre a porta de entrada,
# porque eu contava com o balanco do superior e nunca conferi ATE ONDE ele
# chega depois de girar 8 graus.
#
# E a varredura achou pior: a FRESTA DE LUZ da cobertura, que eu alinhei DE
# PROPOSITO com o vao da escada, formava um POCO VERTICAL do telhado ao terreo.
# Fresta de luz e ABERTURA — precisa do vidro, senao e buraco. E a mesma
# doenca das outras quatro: o objeto existe, o vazio (ou a tampa dele) nao.
#
# SEM TOLERANCIA. A 1a versao aceitava ate 5% de celulas abertas e imprimia
# "todos os ambientes tem cobertura" com 20 buracos na tabela logo acima. Um
# teste que arredonda o proprio veredito e a doenca escrita em Python.
#
# Uso: python prova_chuva.py <casa.zendo>
import sys, types, io, argparse

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

# ambientes INTERNOS do briefing: (nome, x0,x1, y0,y1, z do piso)
# ATE A BORDA, sem recuo. A 1a versao usava retangulos *inset* (hall ate 10,2
# quando o hall vai a 10,4) — e uma varredura que recua da borda e cega
# justamente onde moram os erros de borda, que sao a familia inteira desta
# leva (laje que nao alcanca, volume girado que nao cobre o reto na quina).
AMBIENTES = [
    ("garagem",          10.5, 15.9,  5.1, 10.5, 0.06),
    ("hall / entrada",    6.9, 10.3,  5.1, 10.5, 0.06),
    ("social",            1.7,  6.9, 10.7, 18.9, 0.06),
    ("cozinha/servico",   7.1, 14.7, 10.7, 16.9, 0.06),
    ("suites (superior)", 8.4, 14.6,  5.8, 16.8, 3.56),
]
# O que conta como TETO. Sem filtro nenhum a prova aceita BRISE DE 6 CM como
# cobertura (um patio a ceu aberto com uma lamina a 2,6 m passava como SECO).
# Mas filtrar so por espessura joga fora a CLARABOIA, que tem 6 cm e E teto —
# e era a peca que esta leva criou pra tapar o poco da fresta. O criterio nao
# e espessura, e MATERIAL: vidro veda, ripa de madeira nao. Laje de concreto
# (espessa) e vidro (fino) cobrem; brise, guarda-corpo e ripado nao.
TETO_MIN_ESPESSURA = 0.15
VIDRO_COR = [168, 199, 208]

def carrega_motor():
    src = open("scripts/entregas/prova_programa.py", encoding="utf-8").read()
    src = src.split("# ------------------------------------------------------"
                    "---------- o gabarito")[1]
    src = src.split("# ------------------------------------------------------"
                    "------------ o laudo")[0]
    m = types.ModuleType("pp")
    exec(compile("import json, sys, math\nfrom collections import deque\n"
                 "import numpy as np\n" + src, "pp", "exec"), m.__dict__)
    return m

_PECAS = {}

def _espessa(f, mod):
    """Guarda so faces de solidos com >= 15 cm de espessura vertical."""
    if not _PECAS:
        return True
    return _PECAS.get(f.mi, 0.0) >= TETO_MIN_ESPESSURA

def _medir(casa, mod):
    """Espessura vertical de cada mesh; vidro entra como teto em qualquer
    espessura (uma claraboia de 6 mm veda tanto quanto uma laje de 20 cm)."""
    import json as _j
    d = _j.load(open(casa, encoding="utf-8"))
    for mi, m in enumerate(d["meshes"]):
        vs = m["verts"]
        pts = ([vs[k:k+3] for k in range(0, len(vs), 3)]
               if not isinstance(vs[0], list) else vs)
        zs = [p[2] for p in pts]
        c = (m.get("colors") or [{}])[0].get("c")
        _PECAS[mi] = 99.0 if c == VIDRO_COR else (max(zs) - min(zs))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("casa")
    ap.add_argument("--grade", type=float, default=0.4)
    a = ap.parse_args()
    mod = carrega_motor()
    _medir(a.casa, mod)
    # so PECAS ESPESSAS podem ser teto — o carregador do prova_programa aceita
    # toda face horizontal, inclusive a lamina de 6 cm de um brise.
    faces = [f for f in mod.carregar(a.casa) if _espessa(f, mod)]
    idx = mod.Indice(faces)
    print("  ambiente             piso   com teto   A CEU ABERTO")
    print("  " + "-" * 52)
    total_aberto = 0
    for nome, x0, x1, y0, y1, zp in AMBIENTES:
        tot = seco = 0
        abertas = []
        x = x0
        while x <= x1:
            y = y0
            while y <= y1:
                piso = [(z, v) for z, v in mod.superficies(idx, x, y)
                        if abs(z - zp) < 0.25]
                if piso:
                    tot += 1
                    if piso[0][1] < 40:      # tem laje acima; 40 = o ceu
                        seco += 1
                    else:
                        abertas.append((x, y))
                y += a.grade
            x += a.grade
        n = len(abertas)
        total_aberto += n
        print("  %-18s %6d %10d %14d %s"
              % (nome, tot, seco, n, "" if not n else "<-- CHOVE"))
        for (bx, by) in abertas[:6]:
            print("        buraco em (%.1f, %.1f)" % (bx, by))
    print()
    print("  %s" % ("TODO ambiente interno tem cobertura" if not total_aberto
                    else "%d celulas A CEU ABERTO dentro de casa" % total_aberto))
    return 1 if total_aberto else 0

if __name__ == "__main__":
    sys.exit(main())
