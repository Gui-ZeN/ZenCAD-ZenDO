# -*- coding: utf-8 -*-
# CASA ZEN — a casa refeita, no mesmo partido, MOBILIADA DESDE O NASCIMENTO.
#
# POR QUE UMA CASA NOVA. A Casa do Briefing chegou a 248 sólidos sendo uma
# maquete de volumes: social, cozinha e "3 suítes" eram caixas com nome. Os 7
# componentes do arquivo inteiro eram 4 árvores, 2 árvores altas e o Ensō-san —
# zero bancada, zero pia, zero cama, zero louça. O Guilherme perguntou "essa
# casa não tem cozinha, sala de estar não?" e a resposta honesta era NÃO: tinha
# uma sala vazia com esse nome.
#
# A DOENÇA, nomeada de vez: eu construo o RECIPIENTE e escrevo o nome da coisa
# nele. Foi assim que saíram "3 suítes" que eram 3 janelas, cobogó colado em
# reboco, sacada sem piso e cozinha sem cozinha. Refazer com o mesmo método
# daria a mesma casa.
#
# O MÉTODO NOVO — CÔMODO PRIMEIRO, e um cômodo só existe quando tem os SEIS:
#   1. fechamento (parede nos lados que precisam)
#   2. piso
#   3. teto (conferido pela prova_cobertura, que enxerga POLÍGONO, não bbox)
#   4. acesso (porta de verdade, com vão na parede)
#   5. abertura (janela pra fora — luz e ventilação)
#   6. AS PEÇAS QUE DÃO O NOME: bancada+pia+fogão = cozinha; cama = quarto;
#      vaso+cuba+box = banheiro; sofá+mesa = estar. Sem elas é uma sala vazia.
# A função `comodo()` no fim do arquivo confere os seis e ABORTA se faltar —
# o script não consegue gravar uma casa com cômodo oco.
#
# O GIRO DE 8° FICA (decisão do Guilherme: é a identidade do partido). Ele
# custou 4 buracos na casa anterior — mas agora existe a `prova_cobertura`, que
# mede polígono girado de verdade. O giro deixou de ser cego.
import copy, json, math, os, sys, io

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEST = (sys.argv[1] if len(sys.argv) > 1
        else os.path.join(REPO, "entregas", "Casa Zen"))

# winding roubado de um box que o kernel JÁ aprovou (lição R35: nunca inventar
# a ordem de vértice/face — copiar de um real)
base = json.load(open(os.path.join(REPO, "entregas", "Casa Enso",
                                   "Casa Enso.zendo"), encoding="utf-8"))
tpl = next(m for m in base["meshes"] if len(m["verts"]) == 8)
txs = sorted(set(round(v[0], 6) for v in tpl["verts"]))
tys = sorted(set(round(v[1], 6) for v in tpl["verts"]))
tzs = sorted(set(round(v[2], 6) for v in tpl["verts"]))
slots = [(txs.index(round(v[0], 6)), tys.index(round(v[1], 6)),
          tzs.index(round(v[2], 6))) for v in tpl["verts"]]
TPLF = copy.deepcopy(tpl["faces"])

M = []
REBOCO = "Reboco branco.jpg";    PEDRA = "Rocha 058.jpg"
DECK   = "Piso madeira 062.jpg"; PISOSUP = "Piso madeira 040.jpg"
MAD    = "Madeira 095.jpg";      RIPA = "Madeira 092.jpg"
CONC   = "Concreto 031.jpg";     GRAMA = "Grama 004.jpg"
PISO   = "Piso travertino.jpg";  TIJOLO = "Tijolo 059.jpg"
GRAFITE = [52, 54, 58]
VIDRO   = [168, 199, 208]      # a cor EXATA vira vidro físico no Cycles
AGUA    = [86, 141, 155]
LATAO   = [168, 140, 92]
BRANCO  = [232, 230, 224]      # louça
ESTOFADO= [104, 106, 102]      # tecido do sofá
INOX    = [176, 180, 184]

def _mk(verts, tex=None, scale=1.0, color=None):
    m = {"wallNo": 0, "verts": verts, "faces": copy.deepcopy(TPLF)}
    if tex:
        m["faceTex"] = [{"f": i, "t": tex} for i in range(len(TPLF))]
        m["texScale"] = scale
    if color:
        m["colors"] = [{"f": i, "c": color} for i in range(len(TPLF))]
    M.append(m)
    return m

def box(x0, x1, y0, y1, z0, z1, **kw):
    return _mk([[[x0, x1][ix], [y0, y1][iy], [z0, z1][iz]]
                for (ix, iy, iz) in slots], **kw)

GIRO = math.radians(8.0)
PIV = (11.5, 10.25)

def gira(m, ang=GIRO, piv=PIV):
    c, s = math.cos(ang), math.sin(ang)
    for v in m["verts"]:
        dx, dy = v[0] - piv[0], v[1] - piv[1]
        v[0] = piv[0] + dx * c - dy * s
        v[1] = piv[1] + dx * s + dy * c
    return m

def girados(desde):
    for m in M[desde:]:
        gira(m)

E = 0.02   # folga anti-z-fight

# ------------------------------------------------- paredes com vão de verdade
def esquadria_x(vx0, vx1, y0, y1, vz0, vz1):
    t, e = 0.05, 0.01
    ym = (y0 + y1) / 2
    box(vx0 + 0.02, vx1 - 0.02, ym - 0.015, ym + 0.015, vz0 + 0.04, vz1 - 0.04,
        color=VIDRO)
    box(vx0 - t, vx1 + t, y0 - e, y1 + e, vz1, vz1 + t, color=GRAFITE)
    box(vx0 - t, vx1 + t, y0 - e, y1 + e, vz0 - t, vz0, color=GRAFITE)
    box(vx0 - t, vx0, y0 - e, y1 + e, vz0, vz1, color=GRAFITE)
    box(vx1, vx1 + t, y0 - e, y1 + e, vz0, vz1, color=GRAFITE)

def esquadria_y(x0, x1, vy0, vy1, vz0, vz1):
    t, e = 0.05, 0.01
    xm = (x0 + x1) / 2
    box(xm - 0.015, xm + 0.015, vy0 + 0.02, vy1 - 0.02, vz0 + 0.04, vz1 - 0.04,
        color=VIDRO)
    box(x0 - e, x1 + e, vy0 - t, vy1 + t, vz1, vz1 + t, color=GRAFITE)
    box(x0 - e, x1 + e, vy0 - t, vy1 + t, vz0 - t, vz0, color=GRAFITE)
    box(x0 - e, x1 + e, vy0 - t, vy0, vz0, vz1, color=GRAFITE)
    box(x0 - e, x1 + e, vy1, vy1 + t, vz0, vz1, color=GRAFITE)

def parede_x(x0, x1, y0, y1, z0, z1, vaos=(), tex=REBOCO, scale=3, esq=True):
    """Parede que corre em X. O vão SUBSTITUI o trecho — não convive com ele."""
    xs = x0
    for (vx0, vx1, vz0, vz1) in sorted(vaos):
        if vx0 > xs: box(xs, vx0, y0, y1, z0, z1, tex=tex, scale=scale)
        if vz0 > z0: box(vx0, vx1, y0, y1, z0, vz0, tex=tex, scale=scale)
        if vz1 < z1: box(vx0, vx1, y0, y1, vz1, z1, tex=tex, scale=scale)
        if esq and vz0 > z0 + 0.2:
            esquadria_x(vx0, vx1, y0, y1, vz0, vz1)
        xs = vx1
    if xs < x1: box(xs, x1, y0, y1, z0, z1, tex=tex, scale=scale)

def parede_y(x0, x1, y0, y1, z0, z1, vaos=(), tex=REBOCO, scale=3, esq=True):
    ys = y0
    for (vy0, vy1, vz0, vz1) in sorted(vaos):
        if vy0 > ys: box(x0, x1, ys, vy0, z0, z1, tex=tex, scale=scale)
        if vz0 > z0: box(x0, x1, vy0, vy1, z0, vz0, tex=tex, scale=scale)
        if vz1 < z1: box(x0, x1, vy0, vy1, vz1, z1, tex=tex, scale=scale)
        if esq and vz0 > z0 + 0.2:
            esquadria_y(x0, x1, vy0, vy1, vz0, vz1)
        ys = vy1
    if ys < y1: box(x0, x1, ys, y1, z0, z1, tex=tex, scale=scale)

# ============================================================ O MOBILIÁRIO
# É ISTO que a casa anterior não tinha. Cada função devolve o nº de peças que
# criou, e o `comodo()` exige que os móveis-chave existam.
def cama(cx, y0, larg=1.6, comp=2.0, z=0.0):
    """Cama: estrado + colchão + cabeceira + 2 criados-mudos."""
    x0, x1 = cx - larg / 2, cx + larg / 2
    box(x0, x1, y0, y0 + comp, z + 0.20, z + 0.45, tex=MAD, scale=0.5)   # colchão
    box(x0, x1, y0, y0 + comp, z, z + 0.20, color=GRAFITE)              # estrado
    box(x0, x1, y0 - 0.06, y0, z + 0.20, z + 0.95, tex=MAD, scale=0.5)  # cabeceira
    for cx2 in (x0 - 0.50, x1 + 0.05):
        box(cx2, cx2 + 0.45, y0 + 0.05, y0 + 0.50, z, z + 0.50,
            tex=MAD, scale=0.5)
    return 5

def armario(x0, x1, y0, y1, z=0.0, h=2.30):
    """Guarda-roupa: corpo + 3 frestas verticais (as portas)."""
    box(x0, x1, y0, y1, z, z + h, tex=MAD, scale=0.6)
    n = max(2, int((x1 - x0) / 0.6))
    for k in range(1, n):
        xk = x0 + (x1 - x0) * k / n
        box(xk - 0.008, xk + 0.008, y0 - 0.01, y0, z + 0.05, z + h - 0.05,
            color=GRAFITE)
    return 1 + n - 1

def sofa(x0, x1, y0, y1, z=0.0):
    """Sofá: assento + encosto + 2 braços + almofadas."""
    box(x0, x1, y0, y1, z, z + 0.40, color=ESTOFADO)                    # assento
    box(x0, x1, y1 - 0.20, y1, z + 0.40, z + 0.85, color=ESTOFADO)      # encosto
    box(x0, x0 + 0.18, y0, y1, z + 0.40, z + 0.62, color=ESTOFADO)
    box(x1 - 0.18, x1, y0, y1, z + 0.40, z + 0.62, color=ESTOFADO)
    return 4

def mesa(x0, x1, y0, y1, z=0.0, h=0.75):
    """Mesa: tampo + 4 pés."""
    box(x0, x1, y0, y1, z + h - 0.05, z + h, tex=MAD, scale=0.4)
    for (px, py) in ((x0 + 0.06, y0 + 0.06), (x1 - 0.12, y0 + 0.06),
                     (x0 + 0.06, y1 - 0.12), (x1 - 0.12, y1 - 0.12)):
        box(px, px + 0.06, py, py + 0.06, z, z + h - 0.05, tex=MAD, scale=0.4)
    return 5

def cadeira(cx, cy, z=0.0, giroY=False):
    """Cadeira: assento + encosto + 4 pés."""
    s = 0.44
    x0, x1 = cx - s / 2, cx + s / 2
    y0, y1 = cy - s / 2, cy + s / 2
    box(x0, x1, y0, y1, z + 0.44, z + 0.48, tex=MAD, scale=0.35)
    if giroY:
        box(x0, x0 + 0.04, y0, y1, z + 0.48, z + 0.92, tex=MAD, scale=0.35)
    else:
        box(x0, x1, y1 - 0.04, y1, z + 0.48, z + 0.92, tex=MAD, scale=0.35)
    for (px, py) in ((x0 + 0.02, y0 + 0.02), (x1 - 0.06, y0 + 0.02),
                     (x0 + 0.02, y1 - 0.06), (x1 - 0.06, y1 - 0.06)):
        box(px, px + 0.04, py, py + 0.04, z, z + 0.44, color=GRAFITE)
    return 6

def bancada(x0, x1, y0, y1, z=0.0, h=0.90):
    """Bancada: base marcenaria + tampo de pedra + rodapé recuado."""
    box(x0, x1, y0, y1, z + 0.08, z + h - 0.04, tex=MAD, scale=0.5)
    box(x0 - 0.02, x1 + 0.02, y0 - 0.02, y1 + 0.02, z + h - 0.04, z + h,
        tex=PEDRA, scale=0.8)
    box(x0 + 0.05, x1 - 0.05, y0 + 0.05, y1, z, z + 0.08, color=GRAFITE)
    return 3

def pia(cx, cy, z=0.90):
    """Cuba de inox embutida + torneira."""
    box(cx - 0.28, cx + 0.28, cy - 0.22, cy + 0.22, z - 0.16, z - 0.005,
        color=INOX)
    box(cx - 0.03, cx + 0.03, cy + 0.26, cy + 0.32, z, z + 0.28, color=INOX)
    box(cx - 0.03, cx + 0.14, cy + 0.16, cy + 0.29, z + 0.24, z + 0.28,
        color=INOX)
    return 3

def cooktop(cx, cy, z=0.90):
    """Cooktop: chapa + 4 bocas."""
    box(cx - 0.35, cx + 0.35, cy - 0.25, cy + 0.25, z, z + 0.02, color=GRAFITE)
    for dx in (-0.17, 0.17):
        for dy in (-0.12, 0.12):
            box(cx + dx - 0.08, cx + dx + 0.08, cy + dy - 0.08, cy + dy + 0.08,
                z + 0.02, z + 0.04, color=[38, 40, 44])
    return 5

def geladeira(x0, y0, larg=0.75, prof=0.70, alt=1.85, z=0.0):
    box(x0, x0 + larg, y0, y0 + prof, z, z + alt, color=INOX)
    box(x0 + 0.02, x0 + larg - 0.02, y0 - 0.015, y0, z + 1.20, z + 1.24,
        color=GRAFITE)                                   # fresta freezer
    box(x0 + larg - 0.10, x0 + larg - 0.06, y0 - 0.03, y0, z + 0.85, z + 1.55,
        color=GRAFITE)                                   # puxador
    return 3

def vaso(cx, y0, z=0.0):
    """Vaso sanitário: base + caixa acoplada + tampo."""
    box(cx - 0.19, cx + 0.19, y0 + 0.10, y0 + 0.62, z, z + 0.38, color=BRANCO)
    box(cx - 0.19, cx + 0.19, y0, y0 + 0.18, z, z + 0.72, color=BRANCO)
    box(cx - 0.20, cx + 0.20, y0 + 0.08, y0 + 0.64, z + 0.38, z + 0.42,
        color=[240, 240, 236])
    return 3

def cuba(cx, y0, z=0.80):
    """Lavatório suspenso + torneira."""
    box(cx - 0.28, cx + 0.28, y0, y0 + 0.42, z, z + 0.14, color=BRANCO)
    box(cx - 0.03, cx + 0.03, y0 + 0.03, y0 + 0.09, z + 0.14, z + 0.36,
        color=INOX)
    return 2

def box_banho(x0, x1, y0, y1, z=0.0, h=1.90):
    """Box: 2 panos de vidro + chuveiro + ralo."""
    box(x0, x1, y0 - 0.01, y0 + 0.01, z, z + h, color=VIDRO)
    box(x1 - 0.01, x1 + 0.01, y0, y1, z, z + h, color=VIDRO)
    cx = (x0 + x1) / 2
    box(cx - 0.11, cx + 0.11, y1 - 0.30, y1 - 0.08, z + 2.05, z + 2.09,
        color=INOX)                                       # ducha
    box(cx - 0.10, cx + 0.10, y1 - 0.28, y1 - 0.10, z + 0.002, z + 0.02,
        color=INOX)                                       # ralo
    return 4

# ============================================================ REGISTRO DE CÔMODOS
# Cada cômodo se declara AQUI, com as peças que o nomeiam. O `confere()` no fim
# aborta a gravação se faltar alguma — é isto que impede a "cozinha" vazia.
COMODOS = []
def comodo(nome, x0, y0, x1, y1, piso_z, teto_z, precisa):
    COMODOS.append(dict(nome=nome, x0=x0, y0=y0, x1=x1, y1=y1,
                        piso=piso_z, teto=teto_z, precisa=set(precisa),
                        tem=set()))
def poe(nome, *itens):
    for c in COMODOS:
        if c["nome"] == nome:
            c["tem"].update(itens)
            return
    raise SystemExit("cômodo inexistente: " + nome)

# ================================================================ O TERRENO
box(0, 16, 0, 32, -0.30, 0, tex=GRAMA, scale=6)                    # terreno
box(-4, 20, -6, 0, -0.30, -0.02, tex=CONC, scale=4)                # rua
box(0, 16, -0.6, 0, -0.02, 0.02, tex=CONC, scale=2)                # calçada

# ============================================================== TÉRREO: ACESSO
# GARAGEM — 2 vagas. Piso de concreto da rua até o fundo dela.
box(10.4, 16.0, 0, 10.6, 0, 0.06, tex=CONC, scale=2.2)
comodo("garagem", 10.4, 5.0, 16.0, 10.6, 0.06, 3.20, ["piso", "teto", "acesso"])
poe("garagem", "piso", "acesso")
box(10.4, 16.0, 5.0, 10.6, 3.20, 3.44, tex=CONC, scale=2.2)        # laje
poe("garagem", "teto")
# portão: vão de 4,8 m na fachada (y=5,0) com ripado vertical
parede_x(10.4, 16.0, 4.94, 5.06, 0, 3.0,
         vaos=[(10.9, 15.7, 0, 2.60)], tex=CONC, scale=2.2, esq=False)
for i in range(26):
    xr = 10.95 + i * 0.185
    box(xr, xr + 0.055, 4.96, 5.04, 0.02, 2.55, tex=RIPA, scale=0.35)

# HALL / ENTRADA
box(7.0, 10.4, 5.0, 10.6, 0, 0.06, tex=PISO, scale=1)
comodo("hall", 7.0, 5.2, 10.4, 10.5, 0.06, 3.20,
       ["piso", "teto", "acesso", "abertura"])
poe("hall", "piso")
box(7.0, 10.4, 5.0, 10.6, 3.20, 3.44, tex=CONC, scale=2.2)         # laje do hall
poe("hall", "teto")
# porta de entrada (vão real) + folha entreaberta
parede_x(7.0, 10.4, 4.94, 5.06, 0, 3.0,
         vaos=[(8.30, 9.40, 0, 2.30)], tex=REBOCO, scale=3, esq=False)
box(8.34, 8.38, 5.02, 6.08, 0.06, 2.26, tex=MAD, scale=0.5)        # folha aberta
poe("hall", "acesso")
# janela alta do hall (fachada oeste do hall dá pro jardim lateral)
parede_y(6.94, 7.06, 5.0, 10.6, 0, 3.0,
         vaos=[(7.10, 9.40, 1.90, 2.70)], tex=REBOCO, scale=3)
poe("hall", "abertura")

# LAVABO — sob a escada, com louça de verdade
box(9.10, 10.35, 8.60, 10.50, 0, 0.06, tex=PISO, scale=0.8)
comodo("lavabo", 9.10, 8.60, 10.35, 10.50, 0.06, 2.60,
       ["piso", "teto", "acesso", "vaso", "cuba"])
poe("lavabo", "piso")
box(9.10, 10.35, 8.60, 10.50, 2.60, 2.72, tex=CONC, scale=2)
poe("lavabo", "teto")
parede_y(9.04, 9.16, 8.60, 10.50, 0, 2.60,
         vaos=[(9.30, 10.10, 0, 2.10)], tex=REBOCO, scale=3, esq=False)
poe("lavabo", "acesso")
box(9.10, 10.35, 8.54, 8.66, 0, 2.60, tex=REBOCO, scale=3)
vaso(9.75, 8.70);            poe("lavabo", "vaso")
cuba(9.75, 10.05, z=0.82);   poe("lavabo", "cuba")

# ============================================================ TÉRREO: SOCIAL
# Pé-direito DUPLO (5,6) — fica AO LADO do volume superior, nunca sob ele.
AZ = 5.60
box(1.5, 7.0, 11.0, 19.0, 0, 0.06, tex=PISO, scale=1)
comodo("social", 1.5, 11.0, 7.0, 19.0, 0.06, AZ,
       ["piso", "teto", "acesso", "abertura", "sofa", "mesa"])
poe("social", "piso")
box(1.5, 7.0, 11.0, 19.0, AZ, AZ + 0.25, tex=CONC, scale=2.2)      # cobertura
poe("social", "teto")
# oeste: janelas altas (recuo lateral 1,5 m — luz sem expor ao vizinho)
parede_y(1.50, 1.75, 11.0, 19.0, 0, AZ,
         vaos=[(12.2, 14.0, 3.60, 4.90), (15.4, 17.2, 3.60, 4.90)],
         tex=REBOCO, scale=3)
poe("social", "abertura")
# leste: parede pro hall/cozinha COM a porta
parede_y(6.75, 7.00, 11.0, 19.0, 0, AZ,
         vaos=[(12.4, 13.5, 0, 2.30)], tex=REBOCO, scale=3, esq=False)
poe("social", "acesso")
box(1.5, 7.0, 10.88, 11.0, 0, AZ, tex=REBOCO, scale=3)             # sul
# norte: pano de vidro pro pátio/jardim
parede_y(1.5, 7.0, 18.88, 19.0, 0, AZ, tex=REBOCO, scale=3) if False else None
box(1.5, 1.75, 18.88, 19.0, 0, AZ, tex=REBOCO, scale=3)
box(6.75, 7.0, 18.88, 19.0, 0, AZ, tex=REBOCO, scale=3)
box(1.75, 6.75, 18.90, 18.98, 0.06, 3.10, color=VIDRO)             # pano N
box(1.75, 6.75, 18.88, 19.00, 3.10, 3.18, color=GRAFITE)
for xv in (1.75, 3.42, 5.08, 6.71):
    box(xv, xv + 0.04, 18.87, 19.01, 0.06, 3.10, color=GRAFITE)
# --- A MOBÍLIA DO SOCIAL (é isto que faz dele um estar) ---
sofa(2.30, 4.70, 12.10, 13.00);              poe("social", "sofa")
mesa(2.90, 4.10, 13.50, 14.30, h=0.42)                              # centro
box(1.85, 5.30, 11.90, 15.00, 0.061, 0.075, tex=MAD, scale=0.9)     # tapete
mesa(2.60, 5.90, 15.90, 17.30)                                      # jantar
poe("social", "mesa")
for cy in (16.10, 17.10):
    for cx in (3.20, 4.25, 5.30):
        cadeira(cx, cy)
box(6.20, 6.70, 12.20, 14.60, 0, 0.45, tex=MAD, scale=0.5)          # rack
box(6.35, 6.45, 12.60, 14.20, 0.75, 1.42, color=GRAFITE)            # TV

# ============================================================ TÉRREO: COZINHA
box(7.2, 14.8, 11.0, 17.0, 0, 0.06, tex=PISO, scale=1)
comodo("cozinha", 7.2, 11.0, 14.8, 17.0, 0.06, 3.20,
       ["piso", "teto", "acesso", "abertura", "bancada", "pia", "fogao",
        "geladeira"])
poe("cozinha", "piso")
# TETO: a laje do superior é GIRADA e não cobre o retângulo reto da cozinha —
# a lição das 4 falhas anteriores. Aqui a cozinha tem laje PRÓPRIA, ortogonal,
# cobrindo os 7,6 × 6,0 inteiros. O volume girado passa POR CIMA, decorativo.
box(7.2, 14.8, 11.0, 17.0, 3.20, 3.44, tex=CONC, scale=2.2)
poe("cozinha", "teto")
box(7.2, 14.8, 16.88, 17.0, 0, 3.20, tex=CONC, scale=2.2)          # fundo
# leste (divisa): o COBOGÓ — a parede TEM o vão e a grelha mora DENTRO dele
parede_y(14.55, 14.80, 11.0, 17.0, 0, 3.20,
         vaos=[(12.0, 15.6, 1.00, 2.60)], tex=CONC, scale=2.2, esq=False)
for i in range(11):
    yc = 12.05 + i * 0.325
    box(14.60, 14.75, yc, yc + 0.20, 1.05, 2.55, tex=TIJOLO, scale=0.35)
for k in range(4):
    zc = 1.05 + k * 0.40
    box(14.60, 14.75, 12.05, 15.55, zc, zc + 0.06, tex=TIJOLO, scale=0.35)
poe("cozinha", "abertura")
poe("cozinha", "acesso")            # a porta pro social (vão na parede leste dele)
# --- A MOBÍLIA DA COZINHA (é isto que faz dela uma cozinha) ---
bancada(7.45, 12.60, 11.15, 11.80);   poe("cozinha", "bancada")     # bancada N
bancada(7.45, 7.95, 11.80, 15.40)                                   # bancada L
pia(8.60, 11.48);                     poe("cozinha", "pia")
cooktop(11.20, 11.48);                poe("cozinha", "fogao")
box(10.60, 11.85, 11.10, 11.20, 1.60, 2.20, color=INOX)             # coifa
geladeira(13.30, 11.15);              poe("cozinha", "geladeira")
armario(7.45, 12.60, 11.10, 11.45, z=1.70, h=0.80)                  # aéreos
box(9.30, 12.30, 12.60, 13.60, 0.74, 0.80, tex=PEDRA, scale=0.8)    # ilha tampo
box(9.45, 12.15, 12.70, 13.50, 0, 0.74, tex=MAD, scale=0.5)         # ilha base
for cx in (9.90, 10.80, 11.70):
    cadeira(cx, 13.95, giroY=False)

# ========================================== SUPERIOR: girado 8°, 3 SUÍTES REAIS
# Tudo em coordenadas LOCAIS (pré-giro) e girado no fim, com pivô único.
# A cozinha abaixo já tem laje ORTOGONAL própria — o volume girado passa por
# cima como gesto, não como estrutura. Foi a inversão dessa dependência que
# gerou os 4 buracos da casa anterior.
SX0, SX1, SY0, SY1 = 8.2, 14.8, 5.6, 17.0
SZ0, SZ1 = 3.44, 6.30                 # piso do superior · teto
ZP, ZT = SZ0 + 0.06, SZ1              # piso acabado · teto
_s = len(M)
# laje + piso do superior (com o vão da escada recortado)
VX0, VX1, VY0, VY1 = 8.2, 10.5, 7.00, 11.2      # o vão da escada
for (a, b, c, d) in ((SX0, SX1, SY0, VY0), (SX0, SX1, VY1, SY1),
                     (VX1, SX1, VY0, VY1)):
    box(a, b, c, d, SZ0 - 0.24, SZ0, tex=CONC, scale=2.2)
    box(a, b, c, d, SZ0, ZP, tex=PISOSUP, scale=1.2)
# guarda-corpo do vão (leste e sul; o norte é por onde a escada chega)
box(VX1, VX1 + 0.06, VY0, VY1, ZP, ZP + 1.00, color=VIDRO)
box(VX0, VX1, VY0 - 0.06, VY0, ZP, ZP + 1.00, color=VIDRO)
# envoltória: oeste cega, leste pedra (divisa), norte com as 3 janelas
box(SX0, SX0 + 0.25, SY0, SY1, ZP, ZT, tex=REBOCO, scale=3)
box(SX1 - 0.25, SX1, SY0, SY1, ZP, ZT, tex=PEDRA, scale=1.4)
parede_x(SX0, SX1, SY1 - 0.25, SY1, ZP, ZT,
         vaos=[(jx, jx + 1.6, ZP + 0.95, ZT - 0.45)
               for jx in (8.6, 10.7, 12.8)], tex=REBOCO, scale=3)
# fachada sul: porta da varanda da master
RX0, RX1 = 11.6, 14.55
parede_x(SX0, SX1, SY0, SY0 + 0.25, ZP, ZT,
         vaos=[(RX0, RX1, ZP, ZP + 2.30)], tex=REBOCO, scale=3, esq=False)
box(RX0 + 1.5, RX1, SY0 + 0.10, SY0 + 0.16, ZP, ZP + 2.26, color=VIDRO)
# A VARANDA: piso SOB o parapeito (o erro da casa anterior era o sinal trocado)
box(RX0, RX1, SY0 - 1.96, SY0, SZ0 - 0.24, SZ0, tex=CONC, scale=2.2)
box(RX0, RX1, SY0 - 1.96, SY0, SZ0, ZP, tex=PISOSUP, scale=1.2)
box(RX0, RX1, SY0 - 1.96, SY0 - 1.90, ZP, ZP + 1.00, color=VIDRO)
box(RX0 - 0.06, RX0, SY0 - 1.96, SY0, ZP, ZP + 1.00, color=VIDRO)
box(RX1, RX1 + 0.06, SY0 - 1.96, SY0, ZP, ZP + 1.00, color=VIDRO)

# --- corredor + as 3 suítes ---
COR1, DIV = 11.62, 0.12
parede_y(COR1, COR1 + DIV, SY0, SY1, ZP, ZT,
         vaos=[(7.10, 8.00, ZP, ZP + 2.10), (11.60, 12.50, ZP, ZP + 2.10),
               (15.10, 16.00, ZP, ZP + 2.10)], tex=REBOCO, scale=3, esq=False)
for dy in (9.40, 13.20):
    box(COR1 + DIV, SX1 - 0.25, dy, dy + DIV, ZP, ZT, tex=REBOCO, scale=3)

SUITES = [("master", SY0 + 0.25, 9.40, 11.6, 14.55),
          ("suite 2", 9.52, 13.20, 11.6, 14.55),
          ("suite 3", 13.32, SY1 - 0.25, 11.6, 14.55)]
for (nome, ya, yb, xa, xb) in SUITES:
    comodo(nome, xa, ya, xb, yb, ZP, ZT,
           ["piso", "teto", "acesso", "abertura", "cama", "armario"])
    poe(nome, "piso", "teto", "acesso", "abertura")
    ymeio = (ya + yb) / 2
    cama(13.30, ymeio - 1.0, z=ZP);       poe(nome, "cama")
    armario(11.80, 12.80, ya + 0.15, ya + 0.75, z=ZP, h=2.30)
    poe(nome, "armario")

# --- os 3 BANHEIROS (a oeste do corredor, com louça de verdade) ---
BANHOS = [("banho master", SY0 + 0.30, 8.30), ("banho 2", 9.60, 11.60),
          ("banho 3", 13.40, 15.40)]
for (nome, ya, yb) in BANHOS:
    xa, xb = 8.45, 11.62
    comodo(nome, xa, ya, xb, yb, ZP, ZP + 2.60,
           ["piso", "teto", "acesso", "vaso", "cuba", "box"])
    box(xa, xb, ya, yb, SZ0, ZP, tex=PISO, scale=0.7)
    poe(nome, "piso")
    box(xa, xb, ya, yb, ZP + 2.60, ZP + 2.72, tex=CONC, scale=2)
    poe(nome, "teto")
    poe(nome, "acesso")                       # porta na parede do corredor
    box(xa, xb, yb - 0.12, yb, ZP, ZP + 2.60, tex=REBOCO, scale=3)
    vaso(9.10, ya + 0.25, z=ZP);              poe(nome, "vaso")
    cuba(10.60, ya + 0.20, z=ZP + 0.82);      poe(nome, "cuba")
    box_banho(8.55, 9.95, ya + 1.10, yb - 0.20, z=ZP, h=1.90)
    poe(nome, "box")
girados(_s)

# ================================================================ A COBERTURA
_c = len(M)
FRESTA = (9.50, 10.05)
box(SX0 - 0.9, FRESTA[0], SY0 - 1.2, SY1 + 0.9, SZ1, SZ1 + 0.30,
    tex=CONC, scale=2.4)
box(FRESTA[1], SX1 + 0.9, SY0 - 1.2, SY1 + 0.9, SZ1 + 0.34, SZ1 + 0.64,
    tex=CONC, scale=2.4)
box(FRESTA[0], FRESTA[1], SY0 - 1.2, SY1 + 0.9, SZ1 + 0.30, SZ1 + 0.36,
    color=VIDRO)                                   # a fresta É vidro, não buraco
girados(_c)
box(1.3, 7.2, 10.8, 19.2, AZ + 0.25, AZ + 0.50, tex=CONC, scale=2.4)  # social

# ==================================================================== O LAZER
# O DECK CONTORNA a piscina, nao passa por cima dela. A 1a versao era um
# retangulo unico de 1,5-14,5 x 21-30 e a piscina (8,5-12,5 x 22,5-28,5)
# ficava ENTERRADA sob a tabua. Vi renderizando: nao havia agua nenhuma.
# E o mesmo erro do vidro atras da parede, invertido: em vez de esquecer
# de abrir o vao, eu TAPEI um que existia. Quatro faixas em volta.
PX0, PX1, PY0, PY1 = 8.5, 12.5, 22.5, 28.5     # a piscina
box(1.5, PX0 - 0.2, 21.0, 30.0, 0, 0.06, tex=DECK, scale=1.1)   # oeste
box(PX1 + 0.2, 14.5, 21.0, 30.0, 0, 0.06, tex=DECK, scale=1.1)  # leste
box(PX0 - 0.2, PX1 + 0.2, 21.0, PY0 - 0.2, 0, 0.06, tex=DECK, scale=1.1)
box(PX0 - 0.2, PX1 + 0.2, PY1 + 0.2, 30.0, 0, 0.06, tex=DECK, scale=1.1)
box(8.5, 12.5, 22.5, 28.5, -0.9, 0.02, color=AGUA)                # piscina
box(8.3, 12.7, 22.3, 22.5, 0, 0.10, tex=PEDRA, scale=0.9)
box(8.3, 12.7, 28.5, 28.7, 0, 0.10, tex=PEDRA, scale=0.9)
box(8.3, 8.5, 22.3, 28.7, 0, 0.10, tex=PEDRA, scale=0.9)
box(12.5, 12.7, 22.3, 28.7, 0, 0.10, tex=PEDRA, scale=0.9)
box(2.0, 5.0, 21.5, 23.3, 0, 1.05, tex=TIJOLO, scale=0.6)         # churrasqueira
box(1.9, 5.1, 21.4, 23.4, 1.05, 1.15, tex=PEDRA, scale=0.8)
box(2.6, 4.4, 21.6, 22.4, 0.55, 1.05, color=GRAFITE)             # a BOCA
box(3.2, 3.8, 21.9, 22.1, 1.15, 2.60, tex=TIJOLO, scale=0.5)     # chaminé
mesa(2.4, 4.6, 24.5, 26.3)                                        # mesa externa
for cy in (24.7, 26.1):
    for cx in (2.9, 4.1):
        cadeira(cx, cy)

# ============================================== O VALIDADOR: aborta cômodo oco
def confere():
    ruim = []
    for c in COMODOS:
        falta = c["precisa"] - c["tem"]
        if falta:
            ruim.append((c["nome"], sorted(falta)))
    print("  %d cômodos declarados" % len(COMODOS))
    for c in COMODOS:
        a = (c["x1"] - c["x0"]) * (c["y1"] - c["y0"])
        print("    %-14s %5.1f m²  %s" % (c["nome"], a, "·".join(sorted(c["tem"]))))
    if ruim:
        print()
        for n, f in ruim:
            print("  ✗ %s SEM: %s" % (n, ", ".join(f)))
        raise SystemExit("ABORTADO: cômodo oco — a casa não é gravada assim.")
    print("  ✓ todos os cômodos têm piso, teto, acesso e as peças que os nomeiam")

confere()
os.makedirs(DEST, exist_ok=True)
# O ENVELOPE do .zendo herda da base: sem "app"/"version"/"wallHeight" o
# app abre o arquivo e desenha NADA — e sem erro nenhum. Descoberto
# renderizando: 5 KB de PNG vazio, dump "sem selecao". Falha calada.
tex = sorted({t["t"] for m in M for t in m.get("faceTex", [])})
doc = {"app": "Zendo", "version": base.get("version", 1),
       "meshes": M, "sketch": [], "source": "",
       "components": [], "dimensions": [], "guides": [], "scenes": [],
       "sections": [], "day": base.get("day", {}), "wallHeight": 3.0,
       "textures": [{"name": t, "file": t} for t in tex],
       "camera": base.get("camera", {})}
out = os.path.join(DEST, "Casa Zen.zendo")
json.dump(doc, open(out, "w", encoding="utf-8"), ensure_ascii=False)
print("  Casa Zen: %d sólidos · %d texturas" % (len(M), len(tex)))
print("  -> %s" % out)
