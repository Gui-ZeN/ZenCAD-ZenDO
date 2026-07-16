# -*- coding: utf-8 -*-
# R35 — A Casinha do Cliente: constrói telhado, piso, esquadrias, portas,
# árvores e texturas DIRETO no JSON do .zendo (o arquivo é a interface).
import json, sys, io, copy
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

import os
# R49: caminhos RELATIVOS ao próprio script — isto morava no scratchpad
# (temporário) com tudo hard-coded, e as entregas-vitrine não seriam
# reconstruíveis se ele evaporasse.
AQUI = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(AQUI))          # .../CadCore
ASSETS = os.path.join(REPO, "build-app", "src", "zendo", "assets", "materiais")
import sys as _sys
# argv[1] gera num destino de teste sem tocar a entrega real
DEST = (_sys.argv[1] if len(_sys.argv) > 1
        else os.path.join(REPO, "entregas", "Casa Enso"))

# a SEMENTE: a casa extrudada da planta 2D pelo próprio app
d = json.load(open(os.path.join(AQUI, "casa-semente.zendo"),
                   encoding="utf-8"))
meshes = d["meshes"]

# ---- template de caixa: rouba a ordem de vértices/faces de um box real ----
tpl = meshes[0]
txs = sorted(set(round(v[0], 6) for v in tpl["verts"]))
tys = sorted(set(round(v[1], 6) for v in tpl["verts"]))
tzs = sorted(set(round(v[2], 6) for v in tpl["verts"]))
slots = []   # p/ cada vértice do template: (ix, iy, iz) em {0,1}
for v in tpl["verts"]:
    slots.append((txs.index(round(v[0], 6)), tys.index(round(v[1], 6)),
                  tzs.index(round(v[2], 6))))
TPL_FACES = copy.deepcopy(tpl["faces"])

def box(x0, x1, y0, y1, z0, z1, tex=None, scale=1.0, color=None):
    xs, ys, zs = [x0, x1], [y0, y1], [z0, z1]
    verts = [[xs[ix], ys[iy], zs[iz]] for (ix, iy, iz) in slots]
    m = {"wallNo": 0, "verts": verts, "faces": copy.deepcopy(TPL_FACES)}
    if tex:
        m["faceTex"] = [{"f": i, "t": tex} for i in range(len(TPL_FACES))]
        m["texScale"] = scale
    if color:
        m["colors"] = [{"f": i, "c": color} for i in range(len(TPL_FACES))]
    return m

# ---- 1) TEXTURA nas paredes (todas as 62 caixas existentes) ----
REBOCO = "Reboco branco.jpg"
for m in meshes:
    n = len(m["faces"])
    m["faceTex"] = [{"f": i, "t": REBOCO} for i in range(n)]
    m["texScale"] = 2.0

# ---- 2) TELHADO 4 águas com beiral 0.5 (casa 16×11, topo z=3.1) ----
TELHA = "Telha de barro.jpg"
bx0, bx1, by0, by1, bz = -0.6, 16.6, -0.6, 11.6, 3.1
h = 1.8
half = (by1 - by0) / 2.0            # 6.1
r1 = [bx0 + half, (by0 + by1) / 2, bz + h]
r2 = [bx1 - half, (by0 + by1) / 2, bz + h]
A = [bx0, by0, bz]; B = [bx1, by0, bz]; C = [bx1, by1, bz]; D = [bx0, by1, bz]
roof = {"wallNo": 0,
        "verts": [A, B, C, D, r1, r2],       # 0..5
        "faces": [[0, 3, 2, 1],              # fundo (normal -z)
                  [0, 1, 5, 4],              # água sul
                  [2, 3, 4, 5],              # água norte
                  [3, 0, 4],                 # água oeste
                  [1, 2, 5]],                # água leste
        "faceTex": [{"f": i, "t": TELHA} for i in range(1, 5)],
        "texScale": 1.5}
meshes.append(roof)

# ---- 3) PISO interno (madeira) ----
PISO = "Piso madeira 040.jpg"
meshes.append(box(0.05, 15.95, 0.05, 10.95, 0.0, 0.02, tex=PISO, scale=1.2))

# ---- 4) JANELAS: vidro (pane fina) nos vãos com peitoril ----
VIDRO = [168, 199, 208]              # azul-acinzentado
janelas_y = [  # (x0,x1, y_parede0,y_parede1) — frente y[0,0.1] e fundo y[10.9,11]
    (1.2, 3.0, 0.0, 0.1), (11.6, 14.6, 0.0, 0.1),
    (0.5, 1.3, 10.9, 11.0), (2.4, 4.4, 10.9, 11.0),
    (6.8, 8.8, 10.9, 11.0), (12.0, 15.0, 10.9, 11.0)]
for (x0, x1, y0, y1) in janelas_y:
    ym = (y0 + y1) / 2
    meshes.append(box(x0 + 0.02, x1 - 0.02, ym - 0.015, ym + 0.015,
                      0.92, 2.38, color=VIDRO))
janelas_x = [(0.0, 0.1, 4.2, 6.8), (15.9, 16.0, 4.5, 6.5),
             (15.9, 16.0, 8.2, 9.4)]
for (x0, x1, y0, y1) in janelas_x:
    xm = (x0 + x1) / 2
    meshes.append(box(xm - 0.015, xm + 0.015, y0 + 0.02, y1 - 0.02,
                      0.92, 2.38, color=VIDRO))

# ---- 5) PORTAS: folha de madeira na entrada + 2 internas ----
PORTA = "Madeira 095.jpg"
meshes.append(box(7.62, 8.58, 0.02, 0.06, 0.0, 2.08, tex=PORTA, scale=2.2))
meshes.append(box(12.24, 13.06, 3.36, 3.40, 0.0, 2.08, tex=PORTA, scale=2.2))
meshes.append(box(4.94, 5.76, 7.56, 7.60, 0.0, 2.08, tex=PORTA, scale=2.2))

# ---- 6) ÁRVORES-PAGODE: tronco + 3 copas empilhadas ----
# ---- ÁRVORES ORGÂNICAS (R44) ----
# As caixotes da R35 morreram no feedback "tirar essas arvores minecraft".
# Tronco = frustum hexagonal; copa = 3 blobs irregulares. Jitter por LCG com
# SEED FIXA: a mesma árvore em toda reconstrução.
import math

TRONCO = [92, 64, 42]
COPAS = [[85, 106, 71], [104, 128, 82], [74, 114, 76]]

def arvore(cx, cy, s, seed, gname):
    st = [seed]
    def rnd():
        st[0] = (st[0] * 1103515245 + 12345) & 0xffffffff
        return ((st[0] >> 16) & 0x7fff) / 32767.0
    verts, faces, cols = [], [], []
    def anel(x, y, z, r, a0, jit):
        idx = []
        for k in range(6):
            a = a0 + k * math.pi / 3
            rr = r * (0.80 + 0.38 * rnd()) if jit else r
            idx.append(len(verts))
            verts.append([x + rr * math.cos(a), y + rr * math.sin(a), z])
        return idx
    def face(loop, c):
        cols.append({"f": len(faces), "c": c})
        faces.append(loop)
    lo = anel(cx, cy, 0.0, 0.15 * s, 0.26, False)
    hi = anel(cx, cy, 1.55 * s, 0.10 * s, 0.26, False)
    face(lo[::-1], TRONCO)
    face(list(hi), TRONCO)
    for k in range(6):
        k2 = (k + 1) % 6
        face([lo[k], lo[k2], hi[k2], hi[k]], TRONCO)
    def blob(bx, by, bz, r, c):
        a0 = rnd() * math.pi
        p0 = len(verts); verts.append([bx, by, bz - 0.92 * r])
        r1 = anel(bx, by, bz - 0.36 * r, 0.94 * r, a0, True)
        r2 = anel(bx, by, bz + 0.38 * r, 0.86 * r, a0 + 0.5, True)
        pT = len(verts); verts.append([bx, by, bz + 0.90 * r])
        for k in range(6):
            k2 = (k + 1) % 6
            face([p0, r1[k2], r1[k]], c)
            face([r1[k], r1[k2], r2[k2], r2[k]], c)
            face([pT, r2[k], r2[k2]], c)
    blob(cx, cy, 2.15 * s, 1.00 * s, COPAS[0])
    blob(cx + 0.48 * s, cy + 0.30 * s, 2.72 * s, 0.60 * s, COPAS[1])
    blob(cx - 0.52 * s, cy - 0.26 * s, 2.55 * s, 0.54 * s, COPAS[2])
    return {"verts": verts, "faces": faces, "colors": cols, "wallNo": 0,
            "hidden": False, "group": gname}

for k, (cx, cy) in enumerate([(-1.9, 1.6), (-1.9, 9.2), (17.9, 9.8),
                              (17.9, 2.2)]):
    meshes.append(arvore(cx, cy, 0.95 + 0.13 * ((k * 7) % 4), 17 + k * 101,
                         f"Árvore {k+1}"))

# ---- 7) biblioteca de texturas (caminhos absolutos) ----
libnames = [REBOCO, TELHA, PISO, PORTA, "Gramado.jpg"]
seen = {t["name"] for t in d.get("textures", [])}
d.setdefault("textures", [])
for n in libnames:
    if n not in seen:
        d["textures"].append({"name": n, "file": ASSETS + "/" + n})

json.dump(d, open(os.path.join(DEST, "Casa Enso.zendo"), "w",
                  encoding="utf-8"))
print(f"casa_v1.zendo: {len(meshes)} sólidos, {len(d['textures'])} texturas na lib")
