# -*- coding: utf-8 -*-
# Gera "Casa Pátio" — projeto arquitetônico completo em formato .zencad (JSON):
# implantação, planta térreo, planta superior, elevação frontal, corte AA e
# 5 pranchas A2 com viewports em escala real. Unidades: METROS.
import json, math, os

ENTS = []

# ---------------------------------------------------------------- helpers ---
def common(o, layer, color=None, ltype="ByLayer", lw=-1.0):
    o["layer"] = layer
    if color is None:
        o["colorMode"] = "byLayer"
    else:
        o["colorMode"] = "rgb"
        o["color"] = [color[0], color[1], color[2], 255]
    o["lineType"] = ltype
    o["lineWeightMm"] = lw
    o["visible"] = True
    return o

def P(x, y): return [round(x, 6), round(y, 6), 0.0]

def line(layer, a, b, **kw):
    ENTS.append(common({"type": "LINE", "start": P(*a), "end": P(*b)}, layer, **kw))

def pline(layer, pts, closed=False, **kw):
    ENTS.append(common({"type": "LWPOLYLINE",
                        "vertices": [P(*p) for p in pts],
                        "bulges": [0.0] * len(pts),
                        "closed": closed, "width": 0.0}, layer, **kw))

def rect(layer, x0, y0, x1, y1, **kw):
    pline(layer, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], True, **kw)

def circle(layer, c, r, **kw):
    ENTS.append(common({"type": "CIRCLE", "center": P(*c), "radius": r}, layer, **kw))

def arc(layer, c, r, a0, a1, **kw):
    ENTS.append(common({"type": "ARC", "center": P(*c), "radius": r,
                        "startAngle": math.radians(a0), "endAngle": math.radians(a1)},
                       layer, **kw))

def text(layer, pos, s, h, just=1, rot=0.0, bold=False, italic=False,
         font="Arial", **kw):
    o = {"type": "MTEXT", "position": P(*pos), "text": s, "height": h,
         "rotation": math.radians(rot), "justify": just, "font": font}
    if bold: o["bold"] = True
    if italic: o["italic"] = True
    ENTS.append(common(o, layer, **kw))

def hatch(layer, loops, pattern, angle=45.0, scale=1.0, spacing=0.15, **kw):
    ENTS.append(common({"type": "HATCH",
                        "loops": [[P(*p) for p in lp] for lp in loops],
                        "pattern": pattern, "angleDeg": angle, "scale": scale,
                        "spacing": spacing,
                        "gradientColor1": [0, 0, 0, 255],
                        "gradientColor2": [255, 255, 255, 255]}, layer, **kw))

HL, HANSI31, HGRID, HSOLID = 0, 1, 3, 4       # HatchPattern: Lines/ANSI31/Grid/Solid
HCONC, HWOOD, HSAND = 7, 8, 9

def dim(p1, p2, p3, layer="COTAS", h=0.18, dec=2):
    ENTS.append(common({"type": "DIMENSION", "kind": 0,
                        "p1": P(*p1), "p2": P(*p2), "p3": P(*p3),
                        "textHeight": h, "arrowSize": 0.10, "decimals": dec,
                        "suffix": "", "arrowType": 1, "font": "Arial"}, layer))

def dimh(x0, x1, y, yline):   # cota horizontal
    dim((x0, y), (x1, y), ((x0 + x1) / 2, yline))

def dimv(y0, y1, x, xline):   # cota vertical
    dim((x, y0), (x, y1), (xline, (y0 + y1) / 2))

# ------------------------------------------------------------ parede/vãos ---
TH = 0.15   # espessura padrão de parede

def hwall(y, x0, x1, gaps=()):
    # parede HORIZONTAL: faces y..y+TH; gaps = [(xa, xb), ...] em x
    xs = sorted(gaps)
    cur = x0
    for (a, b) in xs:
        if a > cur:
            rect("PAREDES", cur, y, a, y + TH)
            hatch("HACH-PAR", [[(cur, y), (a, y), (a, y + TH), (cur, y + TH)]], HSOLID)
        cur = b
    if cur < x1:
        rect("PAREDES", cur, y, x1, y + TH)
        hatch("HACH-PAR", [[(cur, y), (x1, y), (x1, y + TH), (cur, y + TH)]], HSOLID)

def vwall(x, y0, y1, gaps=()):
    ys = sorted(gaps)
    cur = y0
    for (a, b) in ys:
        if a > cur:
            rect("PAREDES", x, cur, x + TH, a)
            hatch("HACH-PAR", [[(x, cur), (x + TH, cur), (x + TH, a), (x, a)]], HSOLID)
        cur = b
    if cur < y1:
        rect("PAREDES", x, cur, x + TH, y1)
        hatch("HACH-PAR", [[(x, cur), (x + TH, cur), (x + TH, y1), (x, y1)]], HSOLID)

def window_h(y, xa, xb):    # janela em parede horizontal (3 linhas + ombreiras)
    ym = y + TH / 2
    for yy in (y, ym, y + TH):
        line("ESQUADRIAS", (xa, yy), (xb, yy))
    line("ESQUADRIAS", (xa, y), (xa, y + TH))
    line("ESQUADRIAS", (xb, y), (xb, y + TH))

def window_v(x, ya, yb):
    xm = x + TH / 2
    for xx in (x, xm, x + TH):
        line("ESQUADRIAS", (xx, ya), (xx, yb))
    line("ESQUADRIAS", (x, ya), (x + TH, ya))
    line("ESQUADRIAS", (x, yb), (x + TH, yb))

def door(hx, hy, w, a_wall, a_leaf):
    # folha aberta em a_leaf (graus) + arco de giro entre a_wall e a_leaf
    lx = hx + w * math.cos(math.radians(a_leaf))
    ly = hy + w * math.sin(math.radians(a_leaf))
    line("ESQUADRIAS", (hx, hy), (lx, ly))
    a0, a1 = min(a_wall, a_leaf), max(a_wall, a_leaf)
    arc("ESQUADRIAS", (hx, hy), w, a0, a1)

def room(x, y, nome, area=None, h=0.22):
    s = nome if area is None else nome + "\n" + area
    text("TEXTO", (x, y), s, h, just=1, bold=True)

def titulo(x, y, t, esc):
    text("TITULO", (x, y), t, 0.45, just=1, bold=True)
    text("TITULO", (x, y - 0.55), esc, 0.25, just=1)

def nivel(x, y, rotulo):     # marcador de nível (triângulo + texto)
    pline("SIMBOLOS", [(x - 0.18, y + 0.18), (x + 0.18, y + 0.18), (x, y)], True)
    text("TEXTO", (x + 0.28, y + 0.06), rotulo, 0.18, just=0)

# ================================================================== TÉRREO ===
TX, TY = 30.0, 0.0
def t(x, y): return (TX + x, TY + y)

def planta_terreo():
    # paredes externas (12.00 x 8.00, th 0.15)
    hwall(TY + 0.0,  TX + 0, TX + 12, gaps=[(TX + 2.2, TX + 3.2), (TX + 4.2, TX + 7.0)])
    hwall(TY + 7.85, TX + 0, TX + 12, gaps=[(TX + 8.0, TX + 10.0), (TX + 10.6, TX + 11.4)])
    vwall(TX + 0.0,   TY + 0.15, TY + 7.85, gaps=[(TY + 2.6, TY + 5.4)])
    vwall(TX + 11.85, TY + 0.15, TY + 7.85, gaps=[(TY + 0.9, TY + 1.5)])
    # internas
    vwall(TX + 6.85, TY + 0.15, TY + 7.85, gaps=[(TY + 2.3, TY + 3.5), (TY + 5.6, TY + 7.0)])
    hwall(TY + 3.85, TX + 7.0, TX + 11.85, gaps=[(TX + 10.6, TX + 11.5)])
    vwall(TX + 9.75, TY + 0.15, TY + 2.35, gaps=[(TY + 1.5, TY + 2.2)])
    hwall(TY + 2.35, TX + 9.75, TX + 11.85)

    # esquadrias — janelas
    window_h(TY + 0.0,  TX + 4.2, TX + 7.0)     # J1 sala (frente)
    window_h(TY + 7.85, TX + 8.0, TX + 10.0)    # J2 cozinha (fundos)
    window_v(TX + 0.0,  TY + 2.6, TY + 5.4)     # J3 sala (lateral)
    window_v(TX + 11.85, TY + 0.9, TY + 1.5)    # J4 lavabo
    # portas
    door(TX + 2.2, TY + 0.15, 1.0, 0, 90)                  # P1 entrada
    door(TX + 10.6, TY + 8.0, 0.8, 0, 90)                  # P2 fundos: abre p/ o quintal
    door(TX + 9.9, TY + 2.2, 0.7, -90, 0)                  # lavabo: gira p/ dentro do lavabo
    line("ESQUADRIAS", t(6.85, 2.3), t(7.0, 2.3))          # vão livre sala->hall
    line("ESQUADRIAS", t(6.85, 3.5), t(7.0, 3.5))
    line("ESQUADRIAS", t(6.85, 5.6), t(7.0, 5.6))          # vão livre sala->cozinha
    line("ESQUADRIAS", t(6.85, 7.0), t(7.0, 7.0))

    # escada (sobe): 14 pisadas de 0.25 a partir de y=0.35, largura 1.0
    ex0, ex1 = TX + 7.15, TX + 8.15
    for i in range(15):
        yy = TY + 0.35 + i * 0.25
        line("ESQUADRIAS", (ex0, yy), (ex1, yy))
    line("ESQUADRIAS", (ex0, TY + 0.35), (ex0, TY + 0.35 + 14 * 0.25))
    line("ESQUADRIAS", (ex1, TY + 0.35), (ex1, TY + 0.35 + 14 * 0.25))
    pline("SIMBOLOS", [t(7.65, 0.5), t(7.65, 3.6)])
    pline("SIMBOLOS", [t(7.55, 3.4), t(7.65, 3.6), t(7.75, 3.4)])
    text("TEXTO", t(7.65, 0.18), "SOBE", 0.14, just=1)

    # mobiliário essencial
    line("MOBILIARIO", t(7.15, 7.25), t(10.45, 7.25))      # bancada cozinha (até a porta)
    line("MOBILIARIO", t(11.25, 4.15), t(11.25, 7.0))      # bancada lateral
    circle("MOBILIARIO", t(8.6, 7.5), 0.20)                # cuba
    circle("MOBILIARIO", t(9.2, 7.5), 0.20)
    rect("MOBILIARIO", TX + 10.3, TY + 4.3, TX + 11.1, TY + 5.0)   # fogão/ilha
    circle("MOBILIARIO", t(11.35, 1.9), 0.18)              # cuba lavabo (fora do giro)
    rect("MOBILIARIO", TX + 11.1, TY + 0.5, TX + 11.7, TY + 1.15)  # vaso
    circle("MOBILIARIO", t(11.4, 1.0), 0.16)

    # nomes dos ambientes
    room(*t(3.5, 4.6), "ESTAR / JANTAR", "51,5 m²")
    room(*t(9.4, 5.8), "COZINHA", "18,7 m²")
    room(*t(10.8, 1.6), "LAVABO")
    room(*t(9.0, 3.1), "HALL", h=0.18)
    nivel(*t(1.0, 0.6), "+0,05")

    # cotas externas
    dimh(TX + 0, TX + 12, TY + 0, TY - 1.5)
    for a, b in [(0, 2.2), (2.2, 3.2), (3.2, 4.2), (4.2, 7.0), (7.0, 12.0)]:
        dimh(TX + a, TX + b, TY + 0, TY - 0.8)
    dimv(TY + 0, TY + 8, TX + 0, TX - 1.5)
    for a, b in [(0, 2.6), (2.6, 5.4), (5.4, 8.0)]:
        dimv(TY + a, TY + b, TX + 0, TX - 0.8)

    # linha de corte AA (atravessa a escada)
    pline("SIMBOLOS", [t(7.65, -0.6), t(7.65, 8.6)], ltype="DASHED")
    for yy in (-0.6, 8.6):
        circle("SIMBOLOS", t(7.65, yy), 0.28)
        text("SIMBOLOS", t(7.65, yy - 0.09), "A", 0.2, just=1, bold=True)
        line("SIMBOLOS", t(7.65 - 0.28, yy), t(7.65 - 0.7, yy))   # seta: olha p/ -x
        pline("SIMBOLOS", [t(7.65 - 0.55, yy + 0.1), t(7.65 - 0.7, yy), t(7.65 - 0.55, yy - 0.1)])

    titulo(*t(6.0, -2.6), "PLANTA BAIXA — PAVIMENTO TÉRREO", "ESCALA 1:50")

# ================================================================ SUPERIOR ===
SX, SY = 60.0, 0.0
def s(x, y): return (SX + x, SY + y)

def planta_superior():
    hwall(SY + 0.0,  SX + 0, SX + 12,
          gaps=[(SX + 1.2, SX + 3.8), (SX + 5.4, SX + 6.3), (SX + 9.3, SX + 10.3)])
    hwall(SY + 7.85, SX + 0, SX + 12, gaps=[(SX + 1.5, SX + 4.0), (SX + 7.5, SX + 10.0)])
    vwall(SX + 0.0,   SY + 0.15, SY + 7.85)
    vwall(SX + 11.85, SY + 0.15, SY + 7.85)
    # internas
    hwall(SY + 3.15, SX + 0.15, SX + 11.85,
          gaps=[(SX + 2.5, SX + 3.3), (SX + 7.0, SX + 8.3), (SX + 9.8, SX + 10.6)])
    hwall(SY + 4.55, SX + 0.15, SX + 11.85,
          gaps=[(SX + 2.6, SX + 3.4), (SX + 8.9, SX + 9.7), (SX + 7.0, SX + 8.15)])
    vwall(SX + 4.75, SY + 0.15, SY + 3.15, gaps=[(SY + 1.2, SY + 1.9)])
    vwall(SX + 7.0,  SY + 0.15, SY + 3.15)   # banho suíte <-> escada
    vwall(SX + 8.15, SY + 0.15, SY + 3.15)
    vwall(SX + 5.85, SY + 4.70, SY + 7.85)

    # janelas
    window_h(SY + 0.0,  SX + 1.2, SX + 3.8)      # suíte
    window_h(SY + 0.0,  SX + 5.4, SX + 6.3)      # banho suíte
    window_h(SY + 0.0,  SX + 9.3, SX + 10.3)     # banho social
    window_h(SY + 7.85, SX + 1.5, SX + 4.0)      # quarto 2
    window_h(SY + 7.85, SX + 7.5, SX + 10.0)     # quarto 3
    # portas (folhas varrendo pro lado LIVRE, dentro do próprio vão)
    door(SX + 2.5, SY + 3.15, 0.8, 0, -90)       # suíte: abre p/ baixo-direita
    door(SX + 4.9, SY + 1.9, 0.7, -90, 0)        # banho suíte
    door(SX + 9.8, SY + 3.15, 0.8, 0, -90)       # banho social: gira DENTRO do vão
    door(SX + 2.6, SY + 4.70, 0.8, 0, 90)        # quarto 2: abre p/ cima-direita
    door(SX + 9.7, SY + 4.70, 0.8, 180, 90)      # quarto 3

    # escada (desce) + vazio
    ex0, ex1 = SX + 7.15, SX + 8.15
    for i in range(15):
        yy = SY + 0.35 + i * 0.25
        line("ESQUADRIAS", (ex0, yy), (ex1, yy))
    line("ESQUADRIAS", (ex0, SY + 0.35), (ex0, SY + 3.85))
    line("ESQUADRIAS", (ex1, SY + 0.35), (ex1, SY + 3.85))
    pline("SIMBOLOS", [s(7.65, 3.5), s(7.65, 0.7)])
    pline("SIMBOLOS", [s(7.55, 0.9), s(7.65, 0.7), s(7.75, 0.9)])
    text("TEXTO", s(7.65, 3.65), "DESCE", 0.14, just=1)
    line("PAREDES", s(8.15, 0.35), s(8.15, 3.15))          # guarda-corpo

    # mobiliário mínimo (camas + louças)
    rect("MOBILIARIO", SX + 0.5, SY + 0.7, SX + 2.3, SY + 2.7)     # cama casal suíte
    rect("MOBILIARIO", SX + 0.6, SY + 5.4, SX + 2.0, SY + 7.4)     # cama q2
    rect("MOBILIARIO", SX + 9.9, SY + 5.4, SX + 11.3, SY + 7.4)    # cama q3
    circle("MOBILIARIO", s(5.6, 0.7), 0.18)                        # cuba b.suíte
    rect("MOBILIARIO", SX + 6.3, SY + 0.4, SX + 6.75, SY + 1.0)    # vaso
    rect("MOBILIARIO", SX + 5.0, SY + 2.3, SX + 6.6, SY + 3.0)     # box
    circle("MOBILIARIO", s(9.0, 0.7), 0.18)                        # cuba b.social
    rect("MOBILIARIO", SX + 10.8, SY + 0.4, SX + 11.25, SY + 1.0)  # vaso
    rect("MOBILIARIO", SX + 10.7, SY + 2.3, SX + 11.7, SY + 3.0)   # box (fora do giro da porta)

    room(*s(2.4, 1.7), "SUÍTE", "14,0 m²")
    room(*s(5.8, 2.2), "B. SUÍTE", h=0.16)
    room(*s(10.0, 1.8), "BANHO", "10,0 m²")
    room(*s(3.0, 6.2), "QUARTO 2", "17,9 m²")
    room(*s(8.9, 6.2), "QUARTO 3", "18,4 m²")
    room(*s(4.0, 3.8), "CORREDOR", h=0.16)
    nivel(*s(1.0, 4.0), "+3,20")

    dimh(SX + 0, SX + 12, SY + 0, SY - 1.5)
    for a, b in [(0, 1.2), (1.2, 3.8), (3.8, 5.4), (5.4, 6.3), (6.3, 9.3), (9.3, 10.3), (10.3, 12.0)]:
        dimh(SX + a, SX + b, SY + 0, SY - 0.8)
    dimv(SY + 0, SY + 8, SX + 12, SX + 13.5)

    titulo(*s(6.0, -2.6), "PLANTA BAIXA — PAVIMENTO SUPERIOR", "ESCALA 1:50")

# ======================================================== ELEVAÇÃO FRONTAL ===
EX, EY = 30.0, -22.0
def e(x, y): return (EX + x, EY + y)

def elevacao():
    # terreno
    line("TERRENO", e(-2.0, 0), e(14.0, 0), lw=0.6)
    hatch("TERRENO", [[e(-2.0, -0.35), e(14.0, -0.35), e(14.0, 0), e(-2.0, 0)]],
          HSAND, spacing=0.12)
    # volume + platibanda
    rect("PAREDES", EX + 0, EY + 0, EX + 12, EY + 6.6, lw=0.5)
    line("PAREDES", e(0, 6.35), e(12, 6.35))               # pingadeira da platibanda
    line("PAREDES", e(0, 3.1), e(12, 3.1))                 # friso da laje
    line("PAREDES", e(0, 3.3), e(12, 3.3))
    # painel ripado de madeira (térreo, à direita)
    rect("PAREDES", EX + 7.6, EY + 0, EX + 12, EY + 3.1)
    hatch("HACH-VIS", [[e(7.6, 0), e(12, 0), e(12, 3.1), e(7.6, 3.1)]],
          HWOOD, angle=90, spacing=0.18)
    # porta de entrada
    rect("ESQUADRIAS", EX + 2.2, EY + 0, EX + 3.2, EY + 2.4)
    rect("ESQUADRIAS", EX + 2.3, EY + 0, EX + 3.1, EY + 2.3)
    # janela da sala
    rect("ESQUADRIAS", EX + 4.2, EY + 0.9, EX + 7.0, EY + 2.5)
    for i in range(1, 4):
        line("ESQUADRIAS", e(4.2 + i * 0.7, 0.9), e(4.2 + i * 0.7, 2.5))
    # pavimento superior: janela suíte com brises
    rect("ESQUADRIAS", EX + 1.2, EY + 4.0, EX + 3.8, EY + 5.5)
    x = 1.2
    while x < 3.8 - 1e-9:
        line("ESQUADRIAS", e(x, 4.0), e(x, 5.5))
        x += 0.26
    rect("ESQUADRIAS", EX + 5.4, EY + 4.5, EX + 6.3, EY + 5.3)     # banho suíte
    rect("ESQUADRIAS", EX + 9.3, EY + 4.5, EX + 10.3, EY + 5.3)    # banho social
    # marquise sobre a porta
    rect("PAREDES", EX + 1.9, EY + 2.55, EX + 3.5, EY + 2.7)
    # níveis
    for yy, r in [(0.0, "+0,00"), (3.2, "+3,20"), (6.6, "+6,60")]:
        line("SIMBOLOS", e(12, yy), e(13.2, yy))
        nivel(*e(13.2, yy), r)
    titulo(*e(6.0, -1.8), "ELEVAÇÃO FRONTAL", "ESCALA 1:50")

# ================================================================ CORTE AA ===
CX, CY = 60.0, -22.0
def c(x, y): return (CX + x, CY + y)

def corte():
    # eixo horizontal = y da planta (0..8); vertical = alturas
    # fundação/baldrame + solo
    hatch("TERRENO", [[c(-1.0, -0.5), c(9.0, -0.5), c(9.0, 0.0), c(-1.0, 0.0)]],
          HSAND, spacing=0.12)
    line("TERRENO", c(-1.0, 0), c(9.0, 0), lw=0.6)
    # lajes (piso 0..0.10, entrepiso 3.10..3.20+, cobertura 6.20..6.40)
    for (y0, y1) in [(0.0, 0.10), (3.10, 3.30), (6.20, 6.40)]:
        rect("ESTRUTURA", CX + 0, CY + y0, CX + 8, CY + y1)
        hatch("HACH-PAR", [[c(0, y0), c(8, y0), c(8, y1), c(0, y1)]], HSOLID)
    # paredes cortadas: frente (0..0.15) e fundos (7.85..8.0) até a platibanda
    for (x0, x1, y0, y1) in [(0.0, 0.15, 0.10, 7.00), (7.85, 8.0, 0.10, 7.00),
                             (3.85, 4.0, 0.10, 3.10),          # parede hall/cozinha térreo
                             (3.15, 3.30, 3.30, 6.20),         # corredor sul (sup)
                             (4.55, 4.70, 3.30, 6.20)]:        # corredor norte (sup)
        rect("PAREDES", CX + x0, CY + y0, CX + x1, CY + y1)
        hatch("HACH-PAR", [[c(x0, y0), c(x1, y0), c(x1, y1), c(x0, y1)]], HSOLID)
    # platibanda: paredes externas sobem até 7.0 (0.6 acima da laje)
    line("PAREDES", c(0, 7.0), c(8, 7.0), ltype="DASHED")   # topo platibanda (fundo)
    # escada em corte: 14 pisadas 0.25 / 15 espelhos 0.2067, sobe de y=0.35 p/ fundos
    pts = [(0.35, 0.10)]
    xcur, ycur = 0.35, 0.10
    for i in range(15):
        ycur += 3.10 / 15.0
        pts.append((xcur, ycur))
        if i < 14:
            xcur += 0.25
            pts.append((xcur, ycur))
    pts.append((3.85, 3.20))
    pline("PAREDES", [c(px, py) for (px, py) in pts])
    # guarda-corpo da escada
    line("ESQUADRIAS", c(0.6, 1.35), c(3.7, 4.05))
    for i in range(7):
        px = 0.6 + i * 0.5
        py = 0.10 + (px - 0.35) / 0.25 * (3.10 / 15.0)
        line("ESQUADRIAS", c(px, py + 0.05), c(px, py + 0.95 + (px - 0.6) * 0.0))
    # guarda-corpo do vazio (pav. superior)
    line("ESQUADRIAS", c(0.35, 3.30), c(0.35, 4.40))
    line("ESQUADRIAS", c(0.35, 4.40), c(3.85, 4.40))
    # portas em vista (corredor superior / hall térreo)
    rect("ESQUADRIAS", CX + 4.0, CY + 0.10, CX + 4.9, CY + 2.20)
    # pé-direito (cotas verticais)
    dimv(CY + 0.10, CY + 3.10, CX + 8.0, CX + 9.2)
    dimv(CY + 3.30, CY + 6.20, CX + 8.0, CX + 9.2)
    dimv(CY + 0.0, CY + 7.0, CX + 0.0, CX - 1.2)
    for yy, r in [(0.10, "+0,05"), (3.20, "+3,20"), (6.40, "+6,40")]:
        nivel(*c(8.6, yy), r)
    text("TEXTO", c(2.0, 1.9), "ESCADA\n15 × 20,7/25 cm", 0.16, just=1)
    text("TEXTO", c(6.0, 1.5), "ESTAR", 0.2, just=1, bold=True)
    text("TEXTO", c(6.0, 4.6), "CORREDOR / QUARTOS", 0.16, just=1)
    titulo(*c(4.0, -1.8), "CORTE AA", "ESCALA 1:50")

# ============================================================= IMPLANTAÇÃO ===
IX, IY = 0.0, 0.0
def i2(x, y): return (IX + x, IY + y)

def implantacao():
    # lote 15 x 20 (muro)
    rect("TERRENO", IX + 0, IY + 0, IX + 15, IY + 20, lw=0.5)
    # rua + calçada
    line("TERRENO", i2(-2, -2.2), i2(17, -2.2))
    line("TERRENO", i2(-2, -6.0), i2(17, -6.0))
    text("TEXTO", i2(7.5, -4.4), "RUA DAS PALMEIRAS", 0.6, just=1, italic=True)
    hatch("PISO", [[i2(-2, -2.2), i2(17, -2.2), i2(17, 0), i2(-2, 0)]],
          HGRID, angle=0, spacing=0.8)                      # calçada
    # projeção da casa (recuos: frente 5.0, laterais 1.5, fundos 7.0)
    rect("PAREDES", IX + 1.5, IY + 5.0, IX + 13.5, IY + 13.0, lw=0.5)
    hatch("HACH-VIS", [[i2(1.5, 5.0), i2(13.5, 5.0), i2(13.5, 13.0), i2(1.5, 13.0)]],
          HANSI31, angle=45, spacing=0.5)
    text("TEXTO", i2(7.5, 9.3), "RESIDÊNCIA\n2 PAVIMENTOS", 0.45, just=1, bold=True)
    # acesso pavimentado (portão -> porta)
    rect("PISO", IX + 3.7, IY + 0, IX + 6.7, IY + 5.0)
    hatch("PISO", [[i2(3.7, 0), i2(6.7, 0), i2(6.7, 5.0), i2(3.7, 5.0)]],
          HGRID, angle=0, spacing=0.5)
    # piscina + deck (fundos)
    rect("PISCINA", IX + 2.0, IY + 14.2, IX + 7.0, IY + 18.6)
    hatch("PISCINA", [[i2(2.0, 14.2), i2(7.0, 14.2), i2(7.0, 18.6), i2(2.0, 18.6)]],
          HL, angle=0, spacing=0.28)
    text("TEXTO", i2(4.5, 16.3), "PISCINA", 0.35, just=1)
    rect("PISO", IX + 8.0, IY + 13.8, IX + 14.5, IY + 19.4)
    hatch("HACH-VIS", [[i2(8.0, 13.8), i2(14.5, 13.8), i2(14.5, 19.4), i2(8.0, 19.4)]],
          HWOOD, angle=90, spacing=0.3)
    text("TEXTO", i2(11.2, 16.4), "DECK", 0.35, just=1)
    # vegetação
    for (vx, vy, r) in [(13.8, 2.2, 1.0), (1.6, 2.6, 0.8), (14.0, 10.5, 0.9),
                        (0.9, 18.8, 0.7), (7.6, 19.2, 0.6)]:
        circle("VEGETACAO", i2(vx, vy), r)
        circle("VEGETACAO", i2(vx, vy), r * 0.55)
        ENTS.append(common({"type": "POINT", "position": P(*i2(vx, vy))}, "VEGETACAO"))
    # norte (topo-direito, fora do lote)
    nx, ny = 17.3, 17.5
    circle("SIMBOLOS", i2(nx, ny), 1.0)
    pline("SIMBOLOS", [i2(nx - 0.35, ny - 0.55), i2(nx, ny + 0.75), i2(nx + 0.35, ny - 0.55), i2(nx, ny - 0.2)], True)
    hatch("SIMBOLOS", [[i2(nx - 0.35, ny - 0.55), i2(nx, ny + 0.75), i2(nx, ny - 0.2)]], HSOLID)
    text("TEXTO", i2(nx, ny + 1.15), "N", 0.4, just=1, bold=True)
    # cotas do lote e recuos
    dimh(IX + 0, IX + 15, IY + 20, IY + 21.4)
    dimv(IY + 0, IY + 20, IX + 0, IX - 1.4)
    dimv(IY + 0, IY + 5.0, IX + 1.5, IX + 0.7)      # recuo frontal
    dimh(IX + 0, IX + 1.5, IY + 13.0, IY + 13.8)    # recuo lateral esq
    dimv(IY + 13.0, IY + 20.0, IX + 13.5, IX + 14.3)  # recuo fundos
    titulo(*i2(7.5, -8.0), "IMPLANTAÇÃO", "ESCALA 1:100")

# ------------------------------------------------------------------ build ---
planta_terreo()
planta_superior()
elevacao()
corte()
implantacao()

layers = [
    ("0",           (255, 255, 255), "CONTINUOUS", -1.0),
    ("PAREDES",     (232, 232, 232), "CONTINUOUS", 0.50),
    ("HACH-PAR",    (96, 100, 108),  "CONTINUOUS", -1.0),
    ("HACH-VIS",    (120, 104, 82),  "CONTINUOUS", -1.0),
    ("ESQUADRIAS",  (95, 180, 196),  "CONTINUOUS", 0.25),
    ("MOBILIARIO",  (128, 133, 143), "CONTINUOUS", 0.18),
    ("COTAS",       (225, 200, 90),  "CONTINUOUS", -1.0),
    ("TEXTO",       (235, 235, 235), "CONTINUOUS", -1.0),
    ("TITULO",      (194, 160, 99),  "CONTINUOUS", 0.35),
    ("SIMBOLOS",    (220, 82, 82),   "CONTINUOUS", 0.30),
    ("TERRENO",     (124, 178, 124), "CONTINUOUS", 0.35),
    ("VEGETACAO",   (96, 165, 96),   "CONTINUOUS", -1.0),
    ("PISO",        (110, 114, 122), "CONTINUOUS", -1.0),
    ("PISCINA",     (95, 155, 205),  "CONTINUOUS", -1.0),
    ("ESTRUTURA",   (165, 165, 170), "CONTINUOUS", 0.50),
]
layers_json = [{"name": n, "color": [c[0], c[1], c[2], 255],
                "lineType": lt, "lineWeightMm": lw,
                "on": True, "frozen": False, "locked": False}
               for (n, c, lt, lw) in layers]

def prancha(nome, titulo_, cx, cy, mm_per_unit, denom, vw, vh):
    return {"name": nome, "paper": 2, "landscape": True, "marginMm": 10.0,
            "title": titulo_, "project": "CASA PÁTIO — RESID. UNIFAMILIAR",
            "author": "ZenCAD · Claude", "date": "02/07/2026",
            "scaleLabel": ("1:%d" % denom),
            "viewports": [{"xMm": (594 - vw) / 2.0 - 40, "yMm": (420 - vh) / 2.0 + 14,
                           "wMm": vw, "hMm": vh,
                           "modelCx": cx, "modelCy": cy,
                           "mmPerUnit": mm_per_unit, "scaleDenom": float(denom)}]}

layouts = [
    prancha("01 IMPLANTAÇÃO", "IMPLANTAÇÃO", 7.5, 7.0, 10.0, 100, 260, 330),
    prancha("02 PLANTA TÉRREO", "PLANTA BAIXA — TÉRREO", 36.0, 2.8, 20.0, 50, 400, 300),
    prancha("03 PLANTA SUPERIOR", "PLANTA BAIXA — SUPERIOR", 66.0, 2.8, 20.0, 50, 400, 300),
    prancha("04 ELEVAÇÃO FRONTAL", "ELEVAÇÃO FRONTAL", 36.0, -18.8, 20.0, 50, 400, 260),
    prancha("05 CORTE AA", "CORTE AA", 64.0, -18.6, 20.0, 50, 340, 260),
]

root = {
    "app": "ZenCAD", "version": 1,
    "settings": {"ltScale": 0.05, "unitIndex": 3, "unitDecimals": 2,
                 "unitSuffix": " m", "currentLayer": "PAREDES"},
    "layers": layers_json,
    "dimStyles": [{"name": "Standard", "textHeight": 0.18, "arrowSize": 0.10,
                   "decimals": 2, "suffix": "", "arrowType": 1}],
    "currentDim": "Standard",
    "textStyles": [{"name": "Standard", "height": 0.22, "font": "Arial"}],
    "currentText": "Standard",
    "blocks": [],
    "layouts": layouts,
    "currentLayout": 1,
    "entities": ENTS,
}

# R49: relativo ao script (antes: absoluto da máquina do dev).
import sys
REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
out = (sys.argv[1] if len(sys.argv) > 1
       else os.path.join(REPO, "projetos", "Casa Patio.zencad"))
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w", encoding="utf-8") as f:
    json.dump(root, f, ensure_ascii=False, indent=1)
print("OK:", out, "| entidades:", len(ENTS))
