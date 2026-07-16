# -*- coding: utf-8 -*-
# R53 — CASA DO BRIEFING: o "Anti-Caixote" do programa do Guilherme.
#
# A TESE (Fable aprovou, com 2 ressalvas): "anti-caixote" != "não-ortogonal".
# Planos desencontrados, balanços, platibandas em alturas diferentes, brises,
# cobogó, pátio interno — TUDO isto é prisma reto. O que mata o caixote é
# profundidade de sombra + ritmo + material, não curvatura (é o brutalismo
# paulista inteiro). MAS: o briefing PEDIU "não-ortogonal" com todas as letras,
# então o volume superior GIRA 8° em planta. É o pagamento dessa palavra —
# entregar só caixas deslocadas seria fingir que atendi.
#
# A prainha ficou de fora pelo argumento de PROJETO (o briefing oferece
# "prainha OU raia"; a raia dialoga com o eixo do deck), NÃO por ser curva —
# o Zendo faz curva em planta (círculo/arco/polígono/Follow Me). O que ele não
# faz é superfície EMPENADA, que é o fundo em rampa de uma prainha de verdade.
#
# Frente do lote = y−. Lote 16 × 32.
import json, copy, os, math, io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

AQUI = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(AQUI))          # .../CadCore
DEST = (sys.argv[1] if len(sys.argv) > 1
        else os.path.join(REPO, "entregas", "Casa Briefing"))

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

REBOCO = "Reboco branco.jpg";   PEDRA = "Rocha 058.jpg"
DECK   = "Piso madeira 062.jpg"; PISOSUP = "Piso madeira 040.jpg"
MAD    = "Madeira 095.jpg";     RIPA = "Madeira 092.jpg"
# "Grama.png" NAO existe nos assets: e PROCEDURAL (R9), mora no
# %APPDATA%/Zendo/materiais e so a UI a resolve. A copia falhava CALADA
# e o terreno saia cinza. Aqui uso a fotografica do ambientCG (R10/R11).
CONC   = "Concreto 031.jpg";    GRAMA = "Grama 004.jpg"
GRAFITE = [52, 54, 58]
VIDRO   = [168, 199, 208]     # R39/R43: a cor EXATA vira vidro físico no Cycles
AGUA    = [86, 141, 155]      # R35: não existe textura de água

# ---------------------------------------------------------------- primitivas
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

# O GIRO. Rotação própria (det=+1) preserva o winding — por isso posso girar
# os vértices DEPOIS de montar a caixa, sem tocar nas faces.
# Aplicado a TODAS as peças do volume superior com o MESMO pivô: girar peça a
# peça (o que o rotateSelected da UI faria — ele usa o bboxCenter de cada
# sólido) desmontaria a composição, cada peça orbitando o próprio centro.
GIRO = math.radians(8.0)
PIV = (10.75, 12.9)

def gira(m, ang=GIRO, piv=PIV):
    c, s = math.cos(ang), math.sin(ang)
    for v in m["verts"]:
        dx, dy = v[0] - piv[0], v[1] - piv[1]
        v[0] = piv[0] + dx * c - dy * s
        v[1] = piv[1] + dx * s + dy * c
    return m

def boxg(*a, **kw):
    return gira(box(*a, **kw))

def agua(x0, x1, y0, y1, z0, zA, zB, **kw):
    """Prisma de topo inclinado (UMA água): z do topo vai de zA (em y0) a zB.
    Manual de propósito: a ferramenta Telhado só faz 2/4 águas, sobre o bbox
    ALINHADO AO MUNDO — sobre um volume girado ela sairia não-girada e com
    beiral fantasma de até 1,2 m (Viewport3D.cpp:6116-6184)."""
    return _mk([[[x0, x1][ix], [y0, y1][iy],
                 z0 if iz == 0 else [zA, zB][iy]] for (ix, iy, iz) in slots],
               **kw)

E = 0.005   # R43: a penetração de 5 mm que mata o z-fighting entre coplanares

# ---------------------------------------------------------------- o terreno
box(0, 16, 0, 32, -0.3, 0, tex=GRAMA, scale=2.0)

# ------------------------------------------------- LAZER (a transição suave)
# Piscina RAIA 3 × 9, no eixo do deck.
# O padrão da R35: bacia rasa + lâmina 1 cm ACIMA dela. Minha 1ª versão pôs a
# água DENTRO de um bloco maciço de pedra (z 0,30–0,34 num bloco 0–0,35): a
# piscina saiu cinza, com a água enterrada na borda. A prova visual pegou.
box(2.2, 5.8, 2.7, 12.3, 0, 0.05, tex=PEDRA, scale=1.0)      # bacia
box(2.5, 5.5, 3.0, 12.0, 0, 0.06, color=AGUA)                # lâmina d'água
for (bx0, bx1, by0, by1) in [(2.2, 2.5, 2.7, 12.3), (5.5, 5.8, 2.7, 12.3),
                             (2.2, 5.8, 2.7, 3.0), (2.2, 5.8, 12.0, 12.3)]:
    box(bx0, bx1, by0, by1, 0, 0.14, tex=PEDRA, scale=1.0)   # borda (anel)
box(5.8, 10.4, 2.7, 12.3, 0, 0.12, tex=DECK, scale=1.2)      # deck

# ------------------------------------- TÉRREO · volume A (social, vidro)
# Pé-direito DUPLO: 5,6 m — e por isso ele fica AO LADO do superior, nunca
# embaixo. Minha 1ª versão passou o volume superior (z 3,2–6,3) por cima de um
# social de 5,6 m: os dois se interpenetravam. Pé-direito duplo é um VAZIO —
# o pavimento de cima tem que desviar dele. Dois volumes altos lado a lado (o
# de vidro e o girado) é justamente o que dá o "movimento" do briefing.
AX0, AX1, AY0, AY1, AZ = 2.0, 7.0, 13.0, 19.0, 5.6
box(AX0, AX1, AY0, AY1 - 0.2, AZ - 0.25, AZ, tex=REBOCO, scale=3)   # bandeira
box(AX0, AX0 + 0.25, AY0, AY1, 0, AZ, tex=REBOCO, scale=3)          # oeste
box(AX1 - 0.25, AX1, AY0, AY1, 0, AZ, tex=PEDRA, scale=1.4)         # pedra
box(AX0, AX1, AY1 - 0.25, AY1, 0, AZ, tex=REBOCO, scale=3)          # fundo
box(AX0, AX1, AY0, AY1, 0, 0.06, tex="Piso travertino.jpg", scale=1)
# GRANDE PANO DE VIDRO (a fachada sul inteira, pro deck) — esquadria + vidro
box(AX0 + 0.25, AX1 - 0.25, AY0, AY0 + 0.06, 0.06, AZ - 0.25, color=VIDRO)
for xv in [AX0 + 0.25, 3.4, 4.5, 5.6, AX1 - 0.25]:                # montantes
    box(xv - 0.04, xv + 0.04, AY0 - 0.02, AY0 + 0.08, 0.06, AZ - 0.25,
        color=GRAFITE)
box(AX0 + 0.25, AX1 - 0.25, AY0 - 0.02, AY0 + 0.08, 2.85, 2.93, color=GRAFITE)

# ------------------------- TÉRREO · volume B (garagem + serviço, concreto)
# DESENCONTRADO do A: avança 0,6 m à frente e para 1 m antes no fundo.
BX0, BX1, BY0, BY1, BZ = 7.0, 13.6, 9.0, 19.0, 3.2
box(BX0, BX1, BY0, BY1, 0, 0.06, tex="Piso travertino.jpg", scale=1)  # piso
box(BX1 - 0.25, BX1, BY0, BY1, 0, BZ, tex=CONC, scale=2.2)            # leste
box(BX0, BX1, BY1 - 0.25, BY1, 0, BZ, tex=CONC, scale=2.2)            # fundo
box(BX0, BX0 + 0.25, 12.6, BY1, 0, BZ, tex=REBOCO, scale=3)           # miolo
# CONCRETO RIPADO: geométrico, não textura. A biblioteca só tem concreto liso,
# e nenhuma textura entrega o "jogo de sombra e luz" que 4 cm de relevo dão.
x = BX0 + 0.15
while x < BX1 - 0.2:
    box(x, x + 0.06, BY1 - 0.29, BY1 - 0.25 + E, 0.1, BZ - 0.1, tex=CONC,
        scale=0.5)
    x += 0.16
# PLATIBANDAS EM DUAS ALTURAS (quebra a horizontal estática)
box(BX0, 10.6, BY0, BY1, BZ, 3.9, tex=CONC, scale=2.2)
box(10.6, BX1, BY0, BY1, BZ, 3.42, tex=CONC, scale=2.2)
# COBOGÓ: grelha de ripas cruzadas (vazio geométrico REAL = sombra real).
# 48 subtrações fariam o mesmo, mais caro e sem prova.
cy = 15.4
while cy < 18.8:
    box(BX0 - 0.04, BX0 + 0.29, cy, cy + 0.09, 0.9, 2.7, tex=CONC, scale=0.4)
    cy += 0.28
cz = 0.9
while cz < 2.7:
    box(BX0 - 0.04, BX0 + 0.29, 15.4, 18.8, cz, cz + 0.09, tex=CONC, scale=0.4)
    cz += 0.28

# ------------------------------------------ PÁTIO INTERNO (o vazio + verde)
# Recorta a estrutura entre o social e o serviço: traz o verde pra dentro.
box(7.4, 9.8, 19.0, 21.4, -0.3, 0.02, tex=GRAMA, scale=1.0)
box(7.4, 7.65, 19.0, 21.4, 0, 2.9, tex=REBOCO, scale=3)
box(9.55, 9.8, 19.0, 21.4, 0, 2.9, tex=PEDRA, scale=1.4)

# ------------------------ SUPERIOR: o volume GIRADO 8°, em BALANÇO de 1,6 m
# Avança sobre o deck E sobre a garagem, sem pilar: é a sombra que o briefing
# pede ("volumes que avançam criando áreas sombreadas naturais").
SX0, SX1, SY0, SY1 = 7.5, 14.0, 7.4, 18.4
SZ0, SZ1 = 3.2 - E, 6.3          # penetra 5 mm no térreo: nada de coplanar
boxg(SX0, SX1, SY0, SY1, SZ0, SZ0 + 0.3, tex=CONC, scale=2.2)     # a laje-viga
boxg(SX0, SX1, SY0, SY0 + 0.25, SZ0 + 0.3, SZ1, tex=REBOCO, scale=3)
boxg(SX0, SX1, SY1 - 0.25, SY1, SZ0 + 0.3, SZ1, tex=REBOCO, scale=3)
boxg(SX0, SX0 + 0.25, SY0, SY1, SZ0 + 0.3, SZ1, tex=REBOCO, scale=3)
boxg(SX1 - 0.25, SX1, SY0, SY1, SZ0 + 0.3, SZ1, tex=PEDRA, scale=1.4)
boxg(SX0, SX1, SY0, SY1, SZ0 + 0.3, SZ0 + 0.36, tex=PISOSUP, scale=1.2)
# janelas das suítes (fachada norte/fundo)
for jx in [8.1, 10.3, 12.0]:
    boxg(jx, jx + 1.7, SY1 - 0.26, SY1 - 0.20, SZ0 + 1.3, SZ1 - 0.45,
         color=VIDRO)
    boxg(jx - 0.05, jx + 1.75, SY1 - 0.28, SY1 - 0.18, SZ0 + 1.26, SZ0 + 1.32,
         color=GRAFITE)

# VARANDA PRIVATIVA DA MASTER: recorte construído DIRETO no volume (o Fable
# cortou a subtração booleana: alvo girado é caminho jamais exercitado —
# experimento não se mistura com entrega).
VX0, VX1 = 11.2, 14.0
boxg(VX0, VX1, SY0, SY0 + 2.4, SZ0 + 0.3, SZ0 + 0.36, tex=PISOSUP, scale=1.2)
boxg(VX0, VX0 + 0.25, SY0, SY0 + 2.4, SZ0 + 0.3, SZ1, tex=REBOCO, scale=3)
boxg(VX0, VX1, SY0 + 2.4, SY0 + 2.65, SZ0 + 0.36, SZ1 - 0.9, color=VIDRO)
boxg(VX0, VX1, SY0, SY0 + 0.1, SZ0 + 0.36, SZ0 + 1.36, color=VIDRO)  # guarda
boxg(VX0, VX1, SY0 - 0.02, SY0 + 0.12, SZ0 + 1.36, SZ0 + 1.42, tex=MAD,
     scale=0.6)

# BRISES VERTICAIS — fachada OESTE (o sol da tarde). 14 lâminas de madeira.
by = SY0 + 0.2
while by < SY1 - 0.3:
    boxg(SX0 - 0.32, SX0 - 0.24, by, by + 0.3, SZ0 + 0.4, SZ1 - 0.15,
         tex=RIPA, scale=0.5)
    by += 0.58
boxg(SX0 - 0.36, SX0 - 0.20, SY0 + 0.1, SY1 - 0.1, SZ1 - 0.15, SZ1 - 0.07,
     tex=MAD, scale=0.6)

# COBERTURA: UMA água de ~12°, girada JUNTO com o volume. Sangra 0,45 m além
# da platibanda — a sombra na fachada é o "ritmo" que o briefing pede.
gira(agua(SX0 - 0.45, SX1 + 0.45, SY0 - 0.45, SY1 + 0.45, SZ1,
          SZ1 + 0.10, SZ1 + 2.05, tex=CONC, scale=2.4))

# --------------------------------------------------- verde e gente (escala)
# As árvores da R44 e o Ensō-san vêm da Casa do Dogfooding — que foi construída
# PELA INTERFACE na R51 e por isso tem os componentes plantados nela.
# ATENÇÃO: `components` está VAZIO em todo .zendo; as instâncias moram nas
# MESHES, marcadas por `comp`. Procurar em components (o nome óbvio) não acha
# nada e a cena sai sem árvore nenhuma — foi o que aconteceu na 1ª tentativa.
FONTE = os.path.join(REPO, "entregas", "Casa do Dogfooding.zendo")
dog = json.load(open(FONTE, encoding="utf-8"))

def inst(nome, x, y, ang=0.0):
    ms = [m for m in dog["meshes"] if m.get("comp") == nome]
    if not ms:
        return
    m0 = ms[0]                       # 1 malha por instância (árvore = 62 faces)
    vs = m0["verts"]
    cx = (min(v[0] for v in vs) + max(v[0] for v in vs)) / 2
    cy0 = (min(v[1] for v in vs) + max(v[1] for v in vs)) / 2
    zmin = min(v[2] for v in vs)
    c, s = math.cos(ang), math.sin(ang)
    novo = {"wallNo": 0, "comp": nome, "faces": copy.deepcopy(m0["faces"]),
            "verts": []}
    for v in vs:
        dx, dy = v[0] - cx, v[1] - cy0
        novo["verts"].append([x + dx * c - dy * s, y + dx * s + dy * c,
                              v[2] - zmin])
    if "colors" in m0:
        novo["colors"] = copy.deepcopy(m0["colors"])
    M.append(novo)

for (tx, ty, nome, ta) in [(1.0, 5.4, "Árvore alta", 0.0),
                           (14.8, 7.0, "Árvore", 1.1),
                           (14.2, 23.0, "Árvore alta", 2.2),
                           (1.4, 24.5, "Árvore", 0.5),
                           (8.6, 20.4, "Árvore", 1.7),
                           (12.2, 4.2, "Árvore", 2.8)]:
    inst(nome, tx, ty, ta)
inst("Ensō-san", 8.2, 6.0, 0.0)      # a escala humana, no deck

# ------------------------------------------------------------------ gravar
os.makedirs(DEST, exist_ok=True)
doc = {"app": "Zendo", "version": base.get("version", 1),
       "meshes": M, "components": [], "scenes": [], "sections": [],
       "dimensions": [], "guides": [], "sketch": [],
       "day": base.get("day", {}), "wallHeight": 3.0,
       "textures": [{"name": t, "file": t} for t in
                    sorted({REBOCO, PEDRA, DECK, PISOSUP, MAD, RIPA, CONC,
                            GRAMA, "Piso travertino.jpg"})],
       "camera": base.get("camera", {})}
out = os.path.join(DEST, "Casa Briefing.zendo")
json.dump(doc, open(out, "w", encoding="utf-8"), ensure_ascii=False)

# copia as texturas usadas pro lado do .zendo (o pacote da R38)
ASSETS = os.path.join(REPO, "build-app", "src", "zendo", "assets", "materiais")
import shutil
for t in doc["textures"]:
    for raiz, _, arqs in os.walk(ASSETS):
        if t["file"] in arqs:
            shutil.copy(os.path.join(raiz, t["file"]), DEST)
            break

print("Casa Briefing: %d sólidos -> %s" % (len(M), out))
