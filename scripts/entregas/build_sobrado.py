# -*- coding: utf-8 -*-
# R39 — O SOBRADO ZEN: 2 pavimentos contemporâneo, construído direto no JSON.
# Frente = y−. Térreo y[0,8]; superior em BALANÇO y[−1.5,5]; terraço y[5,8].
import json, copy, os, shutil, io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

import os
# R49: caminhos RELATIVOS ao próprio script — isto morava no scratchpad
# (temporário) com tudo hard-coded, e as entregas-vitrine não seriam
# reconstruíveis se ele evaporasse.
AQUI = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(AQUI))          # .../CadCore
ASSETS = os.path.join(REPO, "build-app", "src", "zendo", "assets", "materiais")
import sys as _sys
# argv[1] permite gerar num destino de teste sem sobrescrever a
# entrega boa (foi assim que a R49 provou este script).
DEST = (_sys.argv[1] if len(_sys.argv) > 1
        else os.path.join(REPO, "entregas", "Sobrado Zen"))
SRC = ASSETS

# rouba o winding de vértice/face de um box PRONTO (lição da R35: nunca
# inventar a ordem — copiar de um que o kernel já aprovou)
base = json.load(open(os.path.join(REPO, "entregas", "Casa Enso",
                                   "Casa Enso.zendo"), encoding="utf-8"))

# ---- template de box (winding roubado de um box real) ----
tpl = next(m for m in base["meshes"] if len(m["verts"]) == 8)
txs = sorted(set(round(v[0], 6) for v in tpl["verts"]))
tys = sorted(set(round(v[1], 6) for v in tpl["verts"]))
tzs = sorted(set(round(v[2], 6) for v in tpl["verts"]))
slots = [(txs.index(round(v[0], 6)), tys.index(round(v[1], 6)),
          tzs.index(round(v[2], 6))) for v in tpl["verts"]]
TPLF = copy.deepcopy(tpl["faces"])

M = []          # a lista de sólidos do sobrado

def box(x0, x1, y0, y1, z0, z1, tex=None, scale=1.0, color=None):
    m = {"wallNo": 0,
         "verts": [[[x0, x1][ix], [y0, y1][iy], [z0, z1][iz]]
                   for (ix, iy, iz) in slots],
         "faces": copy.deepcopy(TPLF)}
    if tex:
        m["faceTex"] = [{"f": i, "t": tex} for i in range(len(TPLF))]
        m["texScale"] = scale
    if color:
        m["colors"] = [{"f": i, "c": color} for i in range(len(TPLF))]
    M.append(m)
    return m

REBOCO = "Reboco branco.jpg";  PEDRA = "Rocha 058.jpg"
DECK = "Piso madeira 062.jpg"; PISOSUP = "Piso madeira 040.jpg"
TRAV = "Piso travertino.jpg";  MAD = "Madeira 095.jpg"
RIPA = "Madeira 092.jpg";      GRAN = "Granito 002A.jpg"
CONC = "Concreto 031.jpg"
GRAFITE = [52, 54, 58]; VIDRO = [168, 199, 208]; AGUA = [86, 141, 155]

# ---- paredes com vãos (esquadria grafite + vidro de brinde nos vãos-janela) --
def esquadria_x(vx0, vx1, y0, y1, vz0, vz1):   # janela numa parede que corre em X
    t = 0.05; ym = (y0 + y1) / 2; e = 0.02
    box(vx0 + 0.02, vx1 - 0.02, ym - 0.015, ym + 0.015, vz0 + 0.04, vz1 - 0.04,
        color=VIDRO)
    box(vx0 - t, vx1 + t, y0 - e, y1 + e, vz1, vz1 + t, color=GRAFITE)
    box(vx0 - t, vx1 + t, y0 - e, y1 + e, vz0 - t, vz0, color=GRAFITE)
    box(vx0 - t, vx0, y0 - e, y1 + e, vz0, vz1, color=GRAFITE)
    box(vx1, vx1 + t, y0 - e, y1 + e, vz0, vz1, color=GRAFITE)
    box((vx0+vx1)/2 - 0.018, (vx0+vx1)/2 + 0.018, y0 - e/2, y1 + e/2,
        vz0, vz1, color=GRAFITE)

def esquadria_y(x0, x1, vy0, vy1, vz0, vz1):   # janela numa parede que corre em Y
    t = 0.05; xm = (x0 + x1) / 2; e = 0.02
    box(xm - 0.015, xm + 0.015, vy0 + 0.02, vy1 - 0.02, vz0 + 0.04, vz1 - 0.04,
        color=VIDRO)
    box(x0 - e, x1 + e, vy0 - t, vy1 + t, vz1, vz1 + t, color=GRAFITE)
    box(x0 - e, x1 + e, vy0 - t, vy1 + t, vz0 - t, vz0, color=GRAFITE)
    box(x0 - e, x1 + e, vy0 - t, vy0, vz0, vz1, color=GRAFITE)
    box(x0 - e, x1 + e, vy1, vy1 + t, vz0, vz1, color=GRAFITE)
    box(x0 - e/2, x1 + e/2, (vy0+vy1)/2 - 0.018, (vy0+vy1)/2 + 0.018,
        vz0, vz1, color=GRAFITE)

def parede_x(x0, x1, y0, y1, z0, z1, vaos, tex, scale=2.0, esq=True):
    xs = x0
    for (vx0, vx1, vz0, vz1) in sorted(vaos):
        if vx0 > xs: box(xs, vx0, y0, y1, z0, z1, tex=tex, scale=scale)
        if vz0 > z0: box(vx0, vx1, y0, y1, z0, vz0, tex=tex, scale=scale)
        if vz1 < z1: box(vx0, vx1, y0, y1, vz1, z1, tex=tex, scale=scale)
        if esq and vz0 > z0 + 0.2:           # tem peitoril = janela
            esquadria_x(vx0, vx1, y0, y1, vz0, vz1)
        xs = vx1
    if xs < x1: box(xs, x1, y0, y1, z0, z1, tex=tex, scale=scale)

def parede_y(x0, x1, y0, y1, z0, z1, vaos, tex, scale=2.0, esq=True):
    ys = y0
    for (vy0, vy1, vz0, vz1) in sorted(vaos):
        if vy0 > ys: box(x0, x1, ys, vy0, z0, z1, tex=tex, scale=scale)
        if vz0 > z0: box(x0, x1, vy0, vy1, z0, vz0, tex=tex, scale=scale)
        if vz1 < z1: box(x0, x1, vy0, vy1, vz1, z1, tex=tex, scale=scale)
        if esq and vz0 > z0 + 0.2:
            esquadria_y(x0, x1, vy0, vy1, vz0, vz1)
        ys = vy1
    if ys < y1: box(x0, x1, ys, y1, z0, z1, tex=tex, scale=scale)

E = 0.18            # espessura de parede
ZT0, ZT1 = 0.0, 2.90            # térreo
ZL0, ZL1 = 2.90, 3.08           # laje
ZS0, ZS1 = 3.08, 5.98           # superior
ZC0, ZC1 = 5.98, 6.14           # cobertura

# ================= TÉRREO =================
# frente em DOIS materiais: reboco (com a porta) + volume de PEDRA (com janela)
parede_x(0.0, 5.2, 0.0, E, ZT0, ZT1,
         [(4.2, 5.2, 0.0, 2.40)], REBOCO)                       # porta
parede_x(5.2, 10.0, 0.0, E, ZT0, ZT1,
         [(6.6, 9.4, 0.9, 2.30)], PEDRA, scale=1.6)             # janela na pedra
# fundo: porta-vidro da sala + janela da cozinha
parede_x(0.0, 10.0, 8.0 - E, 8.0, ZT0, ZT1,
         [(1.0, 4.6, 0.0, 2.60), (6.0, 7.4, 1.0, 2.30)], REBOCO)
# porta-vidro da sala (vidro + esquadria, vão sem peitoril)
esquadria_x(1.0, 4.6, 8.0 - E, 8.0, 0.02, 2.58)
# laterais
parede_y(0.0, E, E, 8.0 - E, ZT0, ZT1, [(2.2, 4.2, 0.9, 2.30)], REBOCO)
parede_y(10.0 - E, 10.0, E, 8.0 - E, ZT0, ZT1, [(5.8, 7.0, 1.0, 2.20)], REBOCO)
# divisória interna com passagem
parede_x(0.18, 6.4, 5.0, 5.0 + 0.14, ZT0, ZT1,
         [(2.3, 3.2, 0.0, 2.20)], REBOCO, esq=False)
# porta principal (madeira) + piso térreo
box(4.22, 5.18, 0.03, 0.09, 0.0, 2.36, tex=MAD, scale=2.2)
box(E, 10 - E, E, 8 - E, 0.0, 0.02, tex=TRAV, scale=1.4)

# ================= LAJE com buraco da escada =================
# buraco x[8.5,9.5] y[0.5,5.0]
box(0.0, 10.0, -1.5, 0.5, ZL0, ZL1, tex=CONC, scale=1.5)
box(0.0, 8.5, 0.5, 5.0, ZL0, ZL1, tex=CONC, scale=1.5)
box(9.5, 10.0, 0.5, 5.0, ZL0, ZL1, tex=CONC, scale=1.5)
box(0.0, 10.0, 5.0, 8.0, ZL0, ZL1, tex=CONC, scale=1.5)

# ================= ESCADA maciça (16 degraus, madeira) =================
ND = 16; RISE = ZS0 / ND; RUN = 0.27
for i in range(ND):
    y1 = 4.90 - i * RUN
    box(8.55, 9.45, y1 - RUN, y1, 0.0, (i + 1) * RISE, tex=MAD, scale=1.2)

# ================= SUPERIOR (balanço frontal) =================
# frente com FITA de janela (atrás do brise)
parede_x(0.0, 10.0, -1.5, -1.5 + E, ZS0, ZS1,
         [(0.8, 9.2, 3.98, 5.38)], REBOCO)
# fundo: porta do terraço + janela
parede_x(0.0, 10.0, 5.0 - E, 5.0, ZS0, ZS1,
         [(2.6, 4.4, ZS0, 5.28), (6.5, 8.2, 4.0, 5.20)], REBOCO)
esquadria_x(2.6, 4.4, 5.0 - E, 5.0, ZS0 + 0.02, 5.26)           # porta-vidro
# laterais do superior
parede_y(0.0, E, -1.5 + E, 5.0 - E, ZS0, ZS1, [(0.0, 2.0, 4.0, 5.20)], REBOCO)
parede_y(10.0 - E, 10.0, -1.5 + E, 5.0 - E, ZS0, ZS1, [], REBOCO)
# pisos: interno (madeira) com buraco da escada + terraço (deck)
box(E, 8.5, -1.5 + E, 5.0 - E, ZS0, ZS0 + 0.02, tex=PISOSUP, scale=1.2)
box(8.5, 10 - E, -1.5 + E, 0.5, ZS0, ZS0 + 0.02, tex=PISOSUP, scale=1.2)
box(0.15, 9.85, 5.0, 7.92, ZS0, ZS0 + 0.04, tex=DECK, scale=1.0)

# ================= COBERTURA + platibanda =================
box(0.0, 10.0, -1.5, 5.0, ZC0, ZC1, tex=CONC, scale=1.5)
PB = 0.15
box(0.0, 10.0, -1.5, -1.5 + PB, ZC0, 6.55, tex=REBOCO, scale=2.0)
box(0.0, 10.0, 5.0 - PB, 5.0, ZC0, 6.55, tex=REBOCO, scale=2.0)
box(0.0, PB, -1.5 + PB, 5.0 - PB, ZC0, 6.55, tex=REBOCO, scale=2.0)
box(10.0 - PB, 10.0, -1.5 + PB, 5.0 - PB, ZC0, 6.55, tex=REBOCO, scale=2.0)

# ================= BRISE ripado na frente do superior =================
x = 0.9
while x < 9.15:
    box(x, x + 0.07, -1.64, -1.54, 3.80, 5.55, tex=RIPA, scale=1.0)
    x += 0.40

# ================= TERRAÇO: guarda-corpo de vidro =================
def guarda_vidro_x(x0, x1, y):
    box(x0, x1, y, y + 0.035, ZS0 + 0.04, ZS0 + 1.05, color=VIDRO)
    box(x0, x1, y - 0.012, y + 0.047, ZS0 + 1.05, ZS0 + 1.11, color=GRAFITE)
def guarda_vidro_y(x, y0, y1):
    box(x, x + 0.035, y0, y1, ZS0 + 0.04, ZS0 + 1.05, color=VIDRO)
    box(x - 0.012, x + 0.047, y0, y1, ZS0 + 1.05, ZS0 + 1.11, color=GRAFITE)
guarda_vidro_x(0.12, 9.88, 7.90)            # fundo do terraço
guarda_vidro_y(0.12, 5.0, 7.90)             # lado esquerdo
guarda_vidro_y(9.845, 5.0, 7.90)            # lado direito

# ================= PÉRGOLA da entrada =================
for px in (3.92, 5.40):
    box(px, px + 0.14, -2.62, -2.48, 0.0, 2.35, tex=RIPA, scale=1.0)
box(3.86, 4.02, -2.75, 0.0, 2.35, 2.47, tex=RIPA, scale=1.0)
box(5.34, 5.50, -2.75, 0.0, 2.35, 2.47, tex=RIPA, scale=1.0)
yy = -2.62
while yy < -0.1:
    box(3.78, 5.60, yy, yy + 0.09, 2.47, 2.55, tex=RIPA, scale=1.0)
    yy += 0.50

# ================= caminho + espelho d'água (frente) =================
box(4.05, 5.35, -3.0, -0.02, 0.0, 0.06, tex=DECK, scale=0.8)
wx0, wx1, wy0, wy1, wb = 0.8, 3.4, -2.9, -0.9, 0.14
box(wx0, wx1, wy0, wy0 + wb, 0.0, 0.10, tex=GRAN, scale=0.6)
box(wx0, wx1, wy1 - wb, wy1, 0.0, 0.10, tex=GRAN, scale=0.6)
box(wx0, wx0 + wb, wy0 + wb, wy1 - wb, 0.0, 0.10, tex=GRAN, scale=0.6)
box(wx1 - wb, wx1, wy0 + wb, wy1 - wb, 0.0, 0.10, tex=GRAN, scale=0.6)
box(wx0 + wb, wx1 - wb, wy0 + wb, wy1 - wb, 0.0, 0.06, color=AGUA)

# ================= PISCINA com deck (fundos) =================
box(1.8, 8.4, 8.3, 11.15, 0.0, 0.05, tex=DECK, scale=1.0)       # deck geral
px0, px1, py0, py1, pb = 2.8, 7.4, 8.7, 10.9, 0.18
box(px0, px1, py0, py0 + pb, 0.0, 0.14, tex=GRAN, scale=0.6)
box(px0, px1, py1 - pb, py1, 0.0, 0.14, tex=GRAN, scale=0.6)
box(px0, px0 + pb, py0 + pb, py1 - pb, 0.0, 0.14, tex=GRAN, scale=0.6)
box(px1 - pb, px1, py0 + pb, py1 - pb, 0.0, 0.14, tex=GRAN, scale=0.6)
box(px0 + pb, px1 - pb, py0 + pb, py1 - pb, 0.0, 0.09, color=AGUA)

# ================= ÁRVORES ORGÂNICAS (R44) =================
# As caixotes-pagode da R39 morreram no feedback "tirar essas arvores
# minecraft". Tronco = frustum hexagonal; copa = 3 blobs irregulares.
# Jitter por LCG com SEED FIXA: a mesma árvore em toda reconstrução.
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
    M.append({"verts": verts, "faces": faces, "colors": cols, "wallNo": 0,
              "hidden": False, "group": gname})

for k, (cx, cy) in enumerate([(-2.1, 1.2), (-2.1, 6.8), (12.1, 6.5),
                              (12.1, -0.5), (0.5, 10.4), (9.6, 10.6)]):
    arvore(cx, cy, 0.95 + 0.13 * ((k * 7) % 4), 17 + k * 101, f"Árvore {k+1}")

# ================= FORRO BRANCO (R43) =================
# O teto era a laje de concreto aparente e o render interior saía escuro.
# 3 painéis 5 mm ENTRADOS na laje (sem fresta nem z-fight).
BRANCO = [245, 244, 240]
for (x1, y1, x2, y2) in [(0.05, -1.45, 9.95, 0.50), (0.05, 0.50, 8.45, 4.95),
                         (0.05, 4.95, 9.95, 7.95)]:
    box(x1, x2, y1, y2, 2.87, 2.905, color=BRANCO)

# ================= TERRENO (era a flag --terrain do app) =================
# O platô de grama sob o lote. Vinha de um comando separado do Zendo — o que
# tornava o script incapaz de reconstruir a entrega sozinho.
box(-6.8, 16.8, -6.6, 15.3, -0.30, 0.0, tex="Grama 008.jpg", scale=2.0)

# ================= ENTORNO (R45) =================
# Sem isto o lote flutua num vazio bege — era o que gritava "Minecraft",
# não o material. A rua sai do quadro (66 m); mais que isso infla o bbox e
# a câmera de órbita joga a casa pra longe.
box(-28.0, 38.0, -9.0, -6.6, -0.45, -0.10, tex="Concreto 036.jpg", scale=1.2)
box(-28.0, 38.0, -16.0, -9.0, -0.45, -0.24, tex="Asfalto 012A.jpg", scale=2.8)
box(-28.0, 38.0, -18.4, -16.0, -0.45, -0.10, tex="Concreto 036.jpg", scale=1.2)


# ================= monta o documento =================
d = {k: base[k] for k in ("app", "version") if k in base}
d.update({
    "wallHeight": 6.5, "day": True, "source": "", "sketch": [],
    "guides": [], "dimensions": [], "sections": [], "scenes": [],
    "components": [], "meshes": M,
})
need = sorted({e["t"] for m in M for e in m.get("faceTex", [])})
os.makedirs(os.path.join(DEST, "assets"), exist_ok=True)
for n in need:
    shutil.copy(os.path.join(SRC, n), os.path.join(DEST, "assets", n))
d["textures"] = [{"name": n, "file": "assets/" + n} for n in need]
json.dump(d, open(os.path.join(DEST, "Sobrado Zen.zendo"), "w",
                  encoding="utf-8"))
print(f"Sobrado Zen: {len(M)} sólidos, {len(need)} texturas embutidas")
# ================= PLANTAS HUMANIZADAS (R39) =================
# "Planta" aqui = o modelo visto de cima com o que está ACIMA do pavimento
# escondido (hidden por faixa de z) — não um desenho 2D. Era feito por script
# solto no terminal; agora é parte da entrega reproduzível.
for nome, corte in (("planta terreo", 2.89), ("planta superior", 5.9)):
    v = json.loads(json.dumps(d))          # cópia funda
    for m in v["meshes"]:
        if min(p[2] for p in m["verts"]) >= corte - 1e-6:
            m["hidden"] = True
    json.dump(v, open(os.path.join(DEST, f"Sobrado Zen ({nome}).zendo"), "w",
                      encoding="utf-8"))
print(f"  + variantes: planta terreo (corte 2,89 m) e superior (5,9 m)")

