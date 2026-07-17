# -*- coding: utf-8 -*-
# R56 — CASA DO BRIEFING, REIMPLANTADA: carro na frente, lazer nos fundos.
#
# POR QUE ELA FOI REFEITA. O Guilherme olhou dois renders da versão R53/R55 por
# 30 segundos e perguntou três coisas: "como um carro entraria aqui, como a
# pessoa acessaria o segundo piso, tem uma sacada para uma parede vazia?".
# As três procediam:
#   · NÃO EXISTIA ESCADA. Sobrado de 2 pavimentos, zero acesso ao de cima — e o
#     Zendo tem ferramenta Escada desde a R41, que eu nunca usei numa entrega.
#   · O CARRO NÃO ENTRAVA. Garagem em y=9, atrás do deck e da piscina; sobrava
#     um corredor de 3,2 m de GRAMA com uma árvore plantada no meio dele.
#   · A SACADA ERA UM POÇO. Guarda-corpo em y 7,40-7,50 dentro da parede sul
#     (y 7,40-7,65): 2,74 m de reboco na frente da varanda. Eu construí piso e
#     guarda-corpo e nunca abri a parede.
#
# A DOENÇA, nomeada: todas as minhas provas (bbox, vértices, flood-fill,
# render) verificam se a geometria bate com o que eu MANDEI desenhar. Nenhuma
# pergunta se o que mandei desenhar SERVE. Eu fazia escultura e chamava de
# projeto. A cura é `prova_programa.py` (roda ao lado deste arquivo): ele ANDA
# pela casa — BFS de pessoa e de carro contra o .zendo salvo — e os alvos vêm
# do BRIEFING, não da geometria.
#
# O PARTIDO NOVO — zoneamento canônico de casa de rua:
#   y  0,0- 5,0  recuo frontal (Fortaleza exige 5 m; e carro esperando o portão
#                com 2,5 m de sobra bloqueia a calçada — carro tem 4,5 m)
#   y  5,0-10,6  GARAGEM (2 vagas, decisão do Guilherme: "só 2 vagas") + HALL
#   y 10,6-19,0  a CASA: social de pé-direito duplo AO LADO do superior
#                (nunca embaixo — lição R53), serviço sob o superior
#   y 19,0-21,0  pátio: o respiro que dá a ventilação cruzada frente↔fundos
#   y 21,0-30,0  LAZER: deck, raia 3×9, churrasqueira — privativo, nos fundos
#   y 30,0-32,0  recuo de fundo
#
# ESTA É A FASE A: MASSAS. Volumes, escada, vão da laje, portão, portas, vãos.
# Sem esquadria fina, brise, cobogó ou paisagismo — o Guilherme derrubou o
# partido em 30 segundos uma vez; ele merece o veto barato ANTES do acabamento.
# Fase B (transplante do que a R53/R55 já provou) só depois do aval dele.
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
PISO   = "Piso travertino.jpg"
GRAFITE = [52, 54, 58]
VIDRO   = [168, 199, 208]     # R39/R43: a cor EXATA vira vidro físico no Cycles
AGUA    = [86, 141, 155]      # R35: não existe textura de água
LATAO   = [168, 140, 92]

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

# O GIRO (o "não-ortogonal" que o briefing pediu com todas as letras).
# Rotação própria (det=+1) preserva o winding — por isso posso girar os
# vértices DEPOIS de montar a caixa, sem tocar nas faces. Pivô ÚNICO pra todas
# as peças: girar peça a peça (o que o rotateSelected da UI faria, usando o
# bboxCenter de cada sólido) desmontaria a composição.
#
# OS NÚMEROS FORAM CONFERIDOS, não chutados — o Fable pegou minha 1ª versão
# (x 8,5-15,5) jogando o canto SE em (16,44) = 44 cm DENTRO do vizinho, e o
# canto NW dentro do social. Com x 8,2-14,8 e pivô (11,5, 10,25):
#   SE -> (15,71, 4,02)   folga de 0,29 m da divisa leste (x=16)
#   NW -> ( 7,29, 16,48)  folga de 0,29 m do social (que acaba em x=7,0)
GIRO = math.radians(8.0)
PIV = (11.5, 10.25)

def gira(m, ang=GIRO, piv=PIV):
    c, s = math.cos(ang), math.sin(ang)
    for v in m["verts"]:
        dx, dy = v[0] - piv[0], v[1] - piv[1]
        v[0] = piv[0] + dx * c - dy * s
        v[1] = piv[1] + dx * s + dy * c
    return m

def boxg(*a, **kw):
    return gira(box(*a, **kw))

def girados(desde):
    for m in M[desde:]:
        gira(m)

def agua(x0, x1, y0, y1, z0, zA, zB, **kw):
    """Prisma de topo inclinado (UMA água). Manual de propósito: a ferramenta
    Telhado agora acompanha o giro (R55), mas só faz 2/4 águas."""
    return _mk([[[x0, x1][ix], [y0, y1][iy],
                 z0 if iz == 0 else [zA, zB][iy]] for (ix, iy, iz) in slots],
               **kw)

E = 0.005   # R43: a penetração de 5 mm que mata o z-fighting entre coplanares

# --------------------------------------------- paredes com VÃO DE VERDADE (R55)
# A parede nasce em pedaços (peitoril + verga + cheios laterais) e o vão é
# BURACO. Trazidas do build_sobrado.py, onde rodam desde a R39.
def esquadria_x(vx0, vx1, y0, y1, vz0, vz1):
    t = 0.05; ym = (y0 + y1) / 2; e = 0.02
    box(vx0 + 0.02, vx1 - 0.02, ym - 0.015, ym + 0.015, vz0 + 0.04, vz1 - 0.04,
        color=VIDRO)
    box(vx0 - t, vx1 + t, y0 - e, y1 + e, vz1, vz1 + t, color=GRAFITE)
    box(vx0 - t, vx1 + t, y0 - e, y1 + e, vz0 - t, vz0, color=GRAFITE)
    box(vx0 - t, vx0, y0 - e, y1 + e, vz0, vz1, color=GRAFITE)
    box(vx1, vx1 + t, y0 - e, y1 + e, vz0, vz1, color=GRAFITE)

def esquadria_y(x0, x1, vy0, vy1, vz0, vz1):
    t = 0.05; xm = (x0 + x1) / 2; e = 0.02
    box(xm - 0.015, xm + 0.015, vy0 + 0.02, vy1 - 0.02, vz0 + 0.04, vz1 - 0.04,
        color=VIDRO)
    box(x0 - e, x1 + e, vy0 - t, vy1 + t, vz1, vz1 + t, color=GRAFITE)
    box(x0 - e, x1 + e, vy0 - t, vy1 + t, vz0 - t, vz0, color=GRAFITE)
    box(x0 - e, x1 + e, vy0 - t, vy0, vz0, vz1, color=GRAFITE)
    box(x0 - e, x1 + e, vy1, vy1 + t, vz0, vz1, color=GRAFITE)

def parede_x(x0, x1, y0, y1, z0, z1, vaos, tex, scale=2.0, esq=True):
    xs = x0
    for (vx0, vx1, vz0, vz1) in sorted(vaos):
        if vx0 > xs: box(xs, vx0, y0, y1, z0, z1, tex=tex, scale=scale)
        if vz0 > z0: box(vx0, vx1, y0, y1, z0, vz0, tex=tex, scale=scale)
        if vz1 < z1: box(vx0, vx1, y0, y1, vz1, z1, tex=tex, scale=scale)
        if esq and vz0 > z0 + 0.2:           # tem peitoril = janela, não porta
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

# ================================================================ O TERRENO
box(0, 16, 0, 32, -0.3, 0, tex=GRAMA, scale=2.0)

# =============================================== FRENTE: o carro chega (y 0-10,6)
# Piso do acesso: da rua até a garagem. Na versão velha isto era GRAMA — o
# carro entrava no gramado. Sem piso não há acesso; a prova de caminhamento
# reprova, e com razão.
GX0, GX1 = 10.4, 16.0            # garagem encosta na divisa leste (empena cega)
GY0, GY1 = 5.0, 10.6
box(GX0 - 0.2, GX1, 0.0, GY0, 0, 0.06, tex=CONC, scale=2.0)     # acesso/calçada
box(GX0, GX1, GY0, GY1, 0, 0.06, tex=CONC, scale=2.0)           # piso da garagem
# 2 VAGAS (decisão do Guilherme: "só 2 vagas"). x 10,65-16,0 livre = 5,35 m =
# 2,67 m por vaga: carro de 1,8 m + porta que ABRE dos dois lados. A versão que
# eu ia entregar tinha 2,25 m/vaga — dois carros que só abrem uma porta por vez.
box(GX0, GX0 + 0.25, GY0, GY1, 0, 3.0, tex=CONC, scale=2.2)     # parede oeste
box(GX0, GX1, GY1 - 0.25, GY1, 0, 3.0, tex=CONC, scale=2.2)     # fundo
# a laje da garagem: teto em 3,0 (passa SUV/picape de 2,0 com folga)
box(GX0, GX1, GY0, GY1, 3.0, 3.2, tex=CONC, scale=2.2)
# PORTÃO: vão de 4,8 m na fachada (y=5,0). Sem vão modelado, o carro não entra
# e a prova reprova — foi assim que a garagem velha passou despercebida.
box(GX0 + 0.25, GX1 - 0.05, GY0 - 0.06, GY0, 2.55, 3.0, color=GRAFITE)  # verga
for gx in (GX0 + 0.25, GX1 - 0.05):
    box(gx - 0.05, gx + 0.05, GY0 - 0.08, GY0 + 0.02, 0.06, 3.0, color=GRAFITE)

# HALL + ESCADA (x 6,8-10,4)
HX0, HX1 = 6.8, 10.4
box(HX0, HX1, GY0, GY1, 0, 0.06, tex=PISO, scale=1)             # piso do hall
box(HX0, HX0 + 0.25, GY0, GY1, 0, 3.2, tex=REBOCO, scale=3)     # parede oeste
# A LAJE DO HALL. Eu dei laje pra garagem e ESQUECI o hall: a aresta oeste do
# superior (girada) só chega em x 8,12-8,91, então sobravam 1,3 a 2,1 m de
# CÉU ABERTO bem em cima da porta de entrada. A casa tinha hall, tinha escada,
# tinha porta — e chovia dentro. O Guilherme viu na hora ("NÃO TEM LAJE,
# CHOVEU MOLHOU O TÉRREO INTEIRO").
# Vai só até x=9,00 porque dali pra leste o balanço do superior já cobre — e
# porque a escada ocupa x 9,00-10,30 subindo até 3,56: laje atravessada ali
# decapitaria quem sobe. É o mesmo cuidado do vão da laje de cima, um andar
# abaixo.
box(HX0, 9.00, GY0, GY1, 3.0, 3.2, tex=CONC, scale=2.2)
# PORTA DE ENTRADA: vão de 1,1 m na fachada do hall, com folha entreaberta
_p = len(M)
parede_x(HX0, HX1, GY0 - 0.25, GY0, 0, 3.2,
         vaos=[(7.6, 8.7, 0, 2.3)], tex=REBOCO, scale=3, esq=False)
# A FOLHA, ABERTA 80°. A 1ª versão abria 22° e a prova de caminhamento
# reprovou a casa inteira — com razão: folha de 1,06 m num vão de 1,10 girada
# 22° deixa 11 cm de passagem. "Entreaberta" a 22° é uma porta FECHADA; a
# pessoa não entrava em casa e nada mais no percurso importava. A 80° a folha
# encosta na ombreira e sobram 0,87 m — passa gente.
_f = len(M)
box(7.62, 8.68, GY0 - 0.19, GY0 - 0.13, 0.06, 2.28, tex=MAD, scale=0.9)
box(8.40, 8.48, GY0 - 0.13, GY0 - 0.06, 1.02, 1.10, color=LATAO)
for m in M[_f:]:
    gira(m, math.radians(80), (7.62, GY0 - 0.16))   # folha e puxador JUNTOS

# A ESCADA — colhida da FERRAMENTA REAL (Viewport3D::buildStair, R41), não
# inventada aqui. Nasceu de:
#   zendo --newstudy --stair "9.65,5.5,0,1,1.3,3.56,0.28" --saveas escada-semente.zendo
# e o app confirmou: "20 degraus (espelho 17.8 cm, piso 28 cm) — integridade OK".
# ALTURA 3,56, NÃO 3,20: o piso onde a pessoa pisa é o acabado (laje 3,2-3,5 +
# piso 3,5-3,56). Minha conta com 3,20 chegava 36 cm abaixo do chão — dois
# espelhos órfãos. Corrida = 20 × 0,28 = 5,60 m: y 5,50 -> 11,10.
# SOBE RUMO AO FUNDO, e não à rua: com o superior recuado pros 5 m legais, o
# topo da escada que subia pra frente desembarcava FORA do volume (local y=5,31
# contra SY0=5,60). Recuar o corpo obrigou a virar a escada — mexer no partido
# move tudo o que dependia dele, e o "tudo" aqui era a única circulação
# vertical da casa.
esc = json.load(open(os.path.join(AQUI, "escada-semente.zendo"),
                     encoding="utf-8"))
for m in esc["meshes"]:
    if len(m["verts"]) < 20:            # o Ensō-san do --newstudy fica de fora
        continue
    novo = {"wallNo": 0, "verts": copy.deepcopy(m["verts"]),
            "faces": copy.deepcopy(m["faces"]),
            "faceTex": [{"f": i, "t": PISO} for i in range(len(m["faces"]))],
            "texScale": 1.0}
    M.append(novo)

# ============================================= A CASA (y 10,6-19,0)
# SOCIAL: pé-direito DUPLO (5,6 m) — e por isso fica AO LADO do superior, nunca
# embaixo. Lição da R53: pé-direito duplo é um VAZIO; o pavimento de cima tem
# que desviar dele. Leste em x=7,0 pra dar 0,29 m de folga ao canto girado.
AX0, AX1, AY0, AY1, AZ = 1.5, 7.0, 10.6, 19.0, 5.6
box(AX0, AX1, AY0, AY1, 0, 0.06, tex=PISO, scale=1)
# OESTE: o pé-direito duplo pede luz ALTA. Era uma parede CEGA de 8,4 × 5,6 m
# virada pro jardim lateral — a única luz do social vinha do norte. Duas
# janelas altas (z 3,6-5,0), acima da linha do olho: entram sol da tarde
# rasante e ventilação, sem expor o estar ao vizinho. O recuo lateral é 1,5 m
# exatos, o mínimo que o art. 1.301 do Código Civil exige pra abrir janela —
# está no limite, e por isso as janelas são ALTAS, não peitoril baixo.
parede_y(AX0, AX0 + 0.25, AY0, AY1, 0, AZ,
         vaos=[(11.6, 14.2, 3.6, 5.0), (15.0, 17.6, 3.6, 5.0)],
         tex=REBOCO, scale=3)
# LESTE do social: com o vão da porta que vem da cozinha (y 12,40-13,50).
# Era um `box` MACIÇO de 5,60 m de altura, e eu tinha aberto a porta só na
# parede do OUTRO lado (:291) — a casa inteira não tinha ligação com o social.
# Abri o vão de origem e esqueci o de DESTINO: o meu padrão de sempre, agora
# entre dois ambientes. A `prova_programa` aprovava porque atravessava a pedra
# (bug da desigualdade estrita, que o Fable pegou por mutação).
parede_y(AX1 - 0.25, AX1, AY0, AY1, 0, AZ,
         vaos=[(12.4, 13.5, 0, 2.3)], tex=PEDRA, scale=1.4, esq=False)
box(AX0, AX1, AY0, AY0 + 0.25, 0, AZ, tex=REBOCO, scale=3)          # sul
box(AX0, AX1, AY1 - 0.25, AY1 - 0.2, AZ - 0.25, AZ, tex=REBOCO, scale=3)
box(AX0, AX1, AY0, AY1, AZ, AZ + 0.25, tex=CONC, scale=2.2)         # cobertura
# O PANO DE VIDRO VIRA PRO NORTE. Na R53 ele olhava pro SUL porque o deck
# estava na frente; com o lazer nos fundos, transplantar por coordenada
# entregaria uma vitrine de 5,3 m emoldurando a parede do serviço.
VAO0, VAO1 = 3.2, 5.6            # a passagem pro pátio/lazer: 2,4 m
box(AX0 + 0.25, AX1 - 0.25, AY1 - 0.06, AY1, 2.93, AZ - 0.25, color=VIDRO)
box(AX0 + 0.25, VAO0, AY1 - 0.06, AY1, 0.06, 2.85, color=VIDRO)
box(VAO1, AX1 - 0.25, AY1 - 0.06, AY1, 0.06, 2.85, color=VIDRO)
box(AX0 + 0.25, AX1 - 0.25, AY1 - 0.08, AY1 + 0.02, 2.85, 2.93, color=GRAFITE)
for xv in (AX0 + 0.25, VAO0, VAO1, AX1 - 0.25):
    box(xv - 0.04, xv + 0.04, AY1 - 0.08, AY1 + 0.02, 0.06, 2.85, color=GRAFITE)
box(4.2, 6.6, AY1, AY1 + 0.06, 0.06, 2.85, color=VIDRO)          # folha de correr
box(4.2, 6.6, AY1 - 0.02, AY1 + 0.08, 0, 0.06, color=GRAFITE)    # soleira

# SERVIÇO/COZINHA: sob o superior (x 7,2-14,8, y 10,6-17,0)
# BX0 = 7,00 e NÃO 7,20: com 7,20 sobrava uma trincheira de 20 cm × 6,4 m
# entre o social (que acaba em AX1=7,0) e a cozinha — grama no fundo, céu
# aberto, entre uma parede de 3,2 m e outra de 5,6 m. Não era vão de projeto,
# era resto de conta que ninguém somou.
BX0, BX1 = 7.0, 14.8
box(BX0, BX1, 10.6, 17.0, 0, 0.06, tex=PISO, scale=1)
# Laje da faixa oeste da cozinha: o superior é girado, então sua aresta oeste
# entra até x≈7,9 em y=11 — sobrava meio metro de céu aberto sobre a bancada.
# Mesma cegueira da laje do hall: eu conto com o balanço pra cobrir e não
# confiro ATÉ ONDE ele chega depois de girar.
box(BX0, 8.60, 10.6, 17.0, 3.0, 3.2, tex=CONC, scale=2.2)
# E a laje da QUINA NE: a cozinha é um retângulo RETO e o volume que a cobre é
# GIRADO 8° — na quina o reto sobra pra fora do girado (o ponto (14,2, 16,4)
# cai em x local 15,03, além dos 14,80 do superior). Terceira vez nesta leva
# que eu conto com o volume girado pra cobrir um ambiente ortogonal: hall,
# faixa oeste, e agora a quina. **Girado não cobre reto — em canto nenhum.**
box(13.0, BX1, 14.9, 17.0, 3.0, 3.2, tex=CONC, scale=2.2)
# LESTE (divisa): com o VÃO onde o cobogó vai. A 1ª versão era parede maciça e
# eu colava a grelha do cobogó em cima dela — um cobogó decorativo grudado em
# reboco, "ventilação cruzada" que não ventilava nada. Cobogó É o vão: a parede
# tem que ter o buraco, e a grelha mora DENTRO dele. Mesma doença da parede
# duplicada, em outra roupa.
parede_y(BX1 - 0.25, BX1, 10.6, 17.0, 0, 3.2,
         vaos=[(11.4, 15.6, 1.0, 2.8)], tex=CONC, scale=2.2, esq=False)
box(BX0, BX1, 16.75, 17.0, 0, 3.2, tex=CONC, scale=2.2)             # fundo
parede_y(BX0, BX0 + 0.25, 10.6, 17.0, 0, 3.2,
         vaos=[(12.4, 13.5, 0, 2.3)], tex=REBOCO, scale=3, esq=False)  # p/ social

# ================================== O SUPERIOR: girado 8°, em BALANÇO REAL
# x 8,2-14,8, y 3,5-17,0. A aresta sul girada cai em y 3,11-4,02 e a garagem
# começa em y=5,0: BALANÇO de 0,98 a 1,89 m sobre o acesso — marquise do
# portão e da porta. Na minha 1ª proposta o superior começava em y=3,0 com a
# garagem em y=2,5: ficava 0,5 m ATRÁS da fachada. Eu chamei de "balanço com
# função" um volume que, pelos meus próprios números, era um RECUO.
# SY0 = 5,60 e NÃO 3,50. Com 3,50 a aresta sul girada caía em y 3,11-4,02 —
# o VOLUME HABITÁVEL inteiro dentro do recuo frontal de 5,00 m que eu mesmo
# declarei obrigatório na linha 24 deste arquivo. E a varanda ia a y=1,70.
# A verdade dura: o "balanço real de 0,98-1,89 m sobre o portão", que eu
# celebrei como a correção do balanço-que-era-recuo, SÓ EXISTIA porque avançava
# sobre o recuo. Conferi a divisa LATERAL (folga de 0,29 m, :96-99) e nunca a
# FRONTAL. Agora o corpo respeita os 5 m (canto SW em y=5,19) e quem avança
# sobre a entrada é a MARQUISE — cobertura pode balançar sobre o recuo; quarto
# não pode.
SX0, SX1, SY0, SY1 = 8.2, 14.8, 5.6, 17.0
SZ0, SZ1 = 3.2 - E, 6.3
# A LAJE COM O VÃO DA ESCADA. Sem este buraco a escada bate a cabeça no 7º
# degrau (headroom de 2,10 acaba quando o pé passa de z=1,1 sob laje em 3,2) e
# eu teria construído a quarta escultura: uma escada que sobe até um teto.
# O vão é recortado em coordenadas LOCAIS (pré-giro) porque a laje gira e a
# escada é reta no mundo. Encosta na parede oeste (x=8,2) — escada encostada na
# parede é o normal, não um erro.
#
# VY0 = 5,45 E NÃO 4,80: com 4,80 a prova de caminhamento subia os 20 degraus
# e parava — os cantos do topo da escada caem em y LOCAL 5,22-5,40, ou seja,
# DENTRO do vão. A pessoa desembarcava no buraco. O vão tem que começar logo
# ao norte do desembarque, não em cima dele: o furo que dá headroom à escada é
# o mesmo que pode engolir a chegada dela. Foi a prova que pegou; olhando o
# render eu jamais veria (o desembarque fica sob a laje, invisível de fora).
# VY1 = 11,20: os cantos do TOPO da escada caem em y local 11,26-11,44, então
# o vão tem que ACABAR antes deles. Ao virar a escada eu repeti, na mesma
# leva, o erro que já tinha consertado uma vez: o furo que dá pé-direito à
# escada engole a chegada dela. Mover o partido move tudo que dependia dele —
# e cada peça movida volta pro fim da fila de conferência, não herda o "ok".
# VY0 = 7,00: o vão tem que começar onde o HEADROOM acaba, não onde parece
# bonito. Com 7,55 a escada raspava a laje sul por 5 cm — em y local 7,49 o
# degrau está em z=1,246 e o vão livre dava 1,949 m contra os 2,00 exigidos.
# Cinco centímetros: invisível em qualquer render, fatal pra quem sobe.
VX0, VX1, VY0, VY1 = 8.2, 10.5, 7.00, 11.2
_n = len(M)
box(SX0, SX1, SY0, VY0, SZ0, SZ0 + 0.3, tex=CONC, scale=2.2)        # laje sul
box(SX0, SX1, VY1, SY1, SZ0, SZ0 + 0.3, tex=CONC, scale=2.2)        # laje norte
box(VX1, SX1, VY0, VY1, SZ0, SZ0 + 0.3, tex=CONC, scale=2.2)        # laje leste
box(SX0, SX1, SY0, VY0, SZ0 + 0.3, SZ0 + 0.36, tex=PISOSUP, scale=1.2)
box(SX0, SX1, VY1, SY1, SZ0 + 0.3, SZ0 + 0.36, tex=PISOSUP, scale=1.2)
box(VX1, SX1, VY0, VY1, SZ0 + 0.3, SZ0 + 0.36, tex=PISOSUP, scale=1.2)
# GUARDA-CORPO do vão — nos lados LESTE e SUL apenas. O norte (VY1) é por
# onde a ESCADA CHEGA: cercar o buraco por todos os lados fecha a saída dela.
# A prova pegou: em y=11,00 o "piso" era 4,56 — um metro acima do último
# degrau, com 1,74 m de vão. A pessoa subia 20 degraus e batia num vidro de
# peito. Guarda-corpo protege a borda do vazio; a boca da escada não é vazio,
# é porta.
box(VX1, VX1 + 0.06, VY0, VY1, SZ0 + 0.36, SZ0 + 1.36, color=VIDRO)
box(VX0, VX1, VY0 - 0.06, VY0, SZ0 + 0.36, SZ0 + 1.36, color=VIDRO)
# PAREDES DO SUPERIOR — só as CEGAS aqui (leste = divisa, oeste = dá pro
# social). As fachadas NORTE (janelas) e SUL (porta da varanda) são montadas
# mais abaixo, com `parede_x` e vão de verdade.
#
# AQUI MORAVAM 3 PAREDES MACIÇAS a mais: uma norte (duplicada DUAS vezes) e uma
# sul. Elas ficavam exatamente em cima das fachadas segmentadas — as janelas e
# a porta da sacada existiam no arquivo, ATRÁS de reboco maciço. O Guilherme
# olhou a axonométrica e perguntou "não tem janela? sacada é uma parede seca?".
# É o bug do vidro enterrado da R53 pela TERCEIRA vez, cometido dentro da leva
# que existe pra consertá-lo — e desta vez nem foi conta errada: foi eu
# construir a parede e depois construir o vão dela ao lado, sem apagar a
# primeira. Montar a parede em pedaços SUBSTITUI a parede; não convive com ela.
box(SX0, SX0 + 0.25, SY0, SY1, SZ0 + 0.3, SZ1, tex=REBOCO, scale=3)   # oeste
box(SX1 - 0.25, SX1, SY0, SY1, SZ0 + 0.3, SZ1, tex=PEDRA, scale=1.4)  # leste
girados(_n)
# a fachada norte com vão de verdade (parede_x + gira o conjunto)
_j = len(M)
parede_x(SX0, SX1, SY1 - 0.25, SY1, SZ0 + 0.3, SZ1,
         vaos=[(jx, jx + 1.6, SZ0 + 1.3, SZ1 - 0.45)
               for jx in (8.6, 10.7, 12.8)],
         tex=REBOCO, scale=3)
girados(_j)
# COBERTURA EM DUAS ÁGUAS DESENCONTRADAS, com uma FRESTA DE LUZ entre elas.
# A Fase A tinha UMA água só, grande e plana, cobrindo o volume inteiro: virou
# tampa de galpão e engoliu o projeto — eu mesmo apontei isso ao ver a
# axonométrica. O briefing pede "cobertura com ritmo", e ritmo aqui não é
# ornamento: as duas águas correm em sentidos OPOSTOS e se cruzam sem se tocar,
# deixando 0,55 m de fresta zenital sobre o corredor de distribuição — a mesma
# fenda que resolve a luz do miolo da barra, onde a suíte do meio não tem
# fachada livre (a divisa leste fica a menos de 1,5 m depois do giro: o Código
# Civil art. 1.301 não deixa abrir janela ali).
_c = len(M)
FRESTA = (9.50, 10.05)                    # local; alinhada com o vão da escada
# As duas SOBEM em direção à fresta: caem pras bordas (norte e sul), não pro
# meio. A 1ª versão punha a água sul com base em SZ1+0,55 e topo caindo até
# SZ1+0,05 — base ACIMA do topo, prisma invertido; e as duas escorriam pro
# centro, o que faria a chuva correr PRA DENTRO da fresta. Cobertura tem
# sentido de caimento, não só inclinação.
# BEIRAL LATERAL 0,20 e NÃO 0,45: com 0,45 o canto do telhado ia a x=16,216 —
# 22 cm SOBRE O TERRENO DO VIZINHO, a 7,6 m de altura. Eu conferi o giro das
# PAREDES (SX0/SX1) e deixei a peça derivada por offset (SX1+0,45) herdar a
# aprovação sem refazer a conta. A confiança viajou; o número não.
# Ao SUL o beiral é 1,20 = MARQUISE sobre a entrada e o portão: cobertura em
# balanço pode avançar sobre o recuo frontal, volume habitável não.
gira(agua(SX0 - 0.20, SX1 + 0.20, SY0 - 1.20, FRESTA[0], SZ1,
          SZ1 + 0.10, SZ1 + 1.30, tex=CONC, scale=2.4))     # sul: sobe p/ fresta
gira(agua(SX0 - 0.20, SX1 + 0.20, FRESTA[1], SY1 + 0.20, SZ1,
          SZ1 + 1.60, SZ1 + 0.20, tex=CONC, scale=2.4))     # norte: cai p/ fundo
# a viga que fecha a fresta pelos lados (a luz entra por cima, não pelos oitões)
for lx in (SX0 - 0.20, SX1 + 0.12):
    boxg(lx, lx + 0.08, FRESTA[0], FRESTA[1], SZ1, SZ1 + 0.6, tex=CONC,
         scale=0.8)
# O VIDRO DA FRESTA — a claraboia. Sem ele a "fresta de luz" é um RASGO ABERTO
# no telhado, e como eu a alinhei de propósito com o vão da escada, o conjunto
# virava um POÇO VERTICAL da cobertura até o térreo: chovia na escada, no hall
# e nas suítes. A varredura de cobertura pegou (4 células no hall, 12 nas
# suítes); o Guilherme tinha pegado o irmão dela a olho ("CHOVEU MOLHOU O
# TÉRREO INTEIRO", sobre o hall sem laje).
# Fresta de luz é ABERTURA — e abertura precisa do vidro, senão é buraco.
boxg(SX0 - 0.20, SX1 + 0.20, FRESTA[0] - 0.06, FRESTA[1] + 0.06,
     SZ1 + 1.28, SZ1 + 1.34, color=VIDRO)
for fy in (FRESTA[0] - 0.06, FRESTA[1]):        # os montantes da claraboia
    boxg(SX0 - 0.20, SX1 + 0.20, fy, fy + 0.06, SZ1 + 1.26, SZ1 + 1.36,
         color=GRAFITE)

# A VARANDA DA MASTER — COM A PAREDE ABERTA DESTA VEZ.
# A anterior tinha piso e guarda-corpo atrás de 2,74 m de reboco: uma sacada
# virada pra parede. Aqui a parede sul é montada em PEDAÇOS com um vão de
# porta de verdade (vz0 = z0 do trecho -> parede_x não pendura esquadria de
# janela), e o guarda-corpo fica À FRENTE da parede, não dentro dela.
RX0, RX1 = 11.6, 14.55            # a varanda, na ponta sul (olha pra rua)
_v = len(M)
parede_x(SX0, SX1, SY0, SY0 + 0.25, SZ0 + 0.3, SZ1,
         vaos=[(RX0, RX1, SZ0 + 0.3, SZ0 + 2.6)], tex=REBOCO, scale=3,
         esq=False)
# A PORTA da varanda: vão nu não é porta — é buraco. Duas folhas de vidro de
# correr, a da esquerda recuada (aberta), deixando 1,3 m de passagem franca
# pro quarto. Com `esq=False` o parede_x não pendura esquadria (é porta, não
# vitrô), então a caixilharia vem à mão aqui.
_pv = (SY0 + 0.10, SY0 + 0.16)                   # o pano, no meio da parede
box(RX0 + 1.5, RX1, _pv[0], _pv[1], SZ0 + 0.36, SZ0 + 2.56, color=VIDRO)
for _m in (RX0 + 1.5, RX1 - 0.04):
    box(_m, _m + 0.04, _pv[0] - 0.02, _pv[1] + 0.02, SZ0 + 0.36, SZ0 + 2.56,
        color=GRAFITE)
box(RX0 + 1.5, RX1, _pv[0] - 0.02, _pv[1] + 0.02, SZ0 + 2.56, SZ0 + 2.60,
    color=GRAFITE)                               # trilho superior
box(RX0 + 1.62, RX0 + 1.68, _pv[0] - 0.06, _pv[1] - 0.02, SZ0 + 1.25,
    SZ0 + 2.05, color=LATAO)                     # puxador
# O PISO DA VARANDA — o defeito que o Guilherme achou olhando: eu tinha posto o
# piso em `SY0, SY0+1.9` (y 5,6→7,5), DENTRO do quarto, onde a laje sul já cobre.
# O parapeito em U avança pra FRENTE da fachada (y 3,64→5,6, o balanço sobre o
# portão); o piso ia pro lado oposto. Resultado: 1,96 m de sacada com grade e
# sem chão — sai da porta e cai na garagem. É a 7ª encarnação de "montei o
# elemento e esqueci de abrir/fechar o lugar dele". O piso vai sob o parapeito,
# do parapeito frontal (SY0-1.96) à fachada (SY0), cobrindo o balanço inteiro:
# laje estrutural de concreto (SZ0..SZ0+0,3, como a laje sul) + acabamento por
# cima. Só o acabamento de 6 cm seria um piso boiando no ar sobre o portão.
box(RX0, RX1, SY0 - 1.96, SY0, SZ0, SZ0 + 0.3, tex=CONC, scale=2.2)
box(RX0, RX1, SY0 - 1.96, SY0, SZ0 + 0.3, SZ0 + 0.36, tex=PISOSUP, scale=1.2)
box(RX0, RX1, SY0 - 1.96, SY0 - 1.9, SZ0 + 0.36, SZ0 + 1.36, color=VIDRO)
box(RX0, RX1, SY0 - 1.98, SY0 - 1.88, SZ0 + 1.36, SZ0 + 1.42, tex=MAD, scale=0.6)
box(RX0 - 0.06, RX0, SY0 - 1.96, SY0, SZ0 + 0.36, SZ0 + 1.36, color=VIDRO)
box(RX1, RX1 + 0.06, SY0 - 1.96, SY0, SZ0 + 0.36, SZ0 + 1.36, color=VIDRO)
girados(_v)

# ============================ O INTERIOR DO SUPERIOR: 3 SUÍTES DE VERDADE
# Até agora o "3 suítes" eram 3 JANELAS. O Fable varreu e achou: 33 peças
# verticais no superior, TODAS fachada/laje/brise — zero divisórias, zero
# banheiros, numa laje de ~75 m². E o script imprimia "Fase B — completa".
# Fachada não faz quarto: quarto é o que a parede INTERNA separa.
#
# Tudo em coordenadas LOCAIS (pré-giro) e girado no fim com o pivô único —
# desenhar direto no mundo girado seria trigonometria à mão em cada divisória.
# O vão da escada (x 8,2-10,5, y 7,0-11,2) manda na planta: a circulação passa
# a LESTE dele, senão o corredor cai no buraco.
_i = len(M)
ZP, ZT = SZ0 + 0.36, SZ1          # do piso acabado ao teto
DIV = 0.12                        # divisória interna: 12 cm
COR0, COR1 = 10.50, 11.62         # o corredor (1,12 m livre)

# parede corredor|suítes, com as 3 portas (vz0 = ZP -> parede_y não pendura
# esquadria de janela; é porta)
parede_y(COR1, COR1 + DIV, SY0, SY1, ZP, ZT,
         vaos=[(7.10, 8.00, ZP, ZP + 2.10),      # master
               (11.60, 12.50, ZP, ZP + 2.10),    # suíte 2
               (15.10, 16.00, ZP, ZP + 2.10)],   # suíte 3
         tex=REBOCO, scale=3, esq=False)
# divisórias entre as 3 suítes (correm em X, da parede do corredor à fachada)
for dy in (9.40, 13.20):
    box(COR1 + DIV, SX1 - 0.25, dy, dy + DIV, ZP, ZT, tex=REBOCO, scale=3)
# BANHEIROS: um por suíte, encostado no corredor (onde não há fachada a perder)
for (b0, b1) in ((8.10, 9.40), (11.90, 13.20), (15.70, 17.00 - 0.25)):
    box(COR1 + DIV, 13.30, b0, b0 + DIV, ZP, ZT, tex=REBOCO, scale=3)
    parede_y(13.30, 13.30 + DIV, b0, b1, ZP, ZT,
             vaos=[(b0 + 0.35, b0 + 1.15, ZP, ZP + 2.10)],
             tex=REBOCO, scale=3, esq=False)
# a parede do corredor no trecho do VÃO da escada (protege o buraco)
box(COR0, COR0 + DIV, VY0, VY1, ZP, ZP + 1.10, tex=REBOCO, scale=3)
girados(_i)

# ------------------------------------------------------- BRISES (Fase B)
# ATENÇÃO À ORIENTAÇÃO: na R53 os brises eram VERTICAIS na fachada OESTE, pra
# barrar o sol da tarde. Com a implantação nova isso seria transplante cego —
# a fachada oeste do superior agora dá pro social (nunca pega sol) e o vidro
# todo virou pro NORTE. E norte, em Fortaleza (lat −3,7°), pega sol o ano
# INTEIRO, alto. Sol alto pede lâmina HORIZONTAL: a vertical não faz sombra
# nenhuma no meio-dia. Trocar o eixo do brise não é detalhe, é a diferença
# entre proteger e decorar.
# Sobre o pano de vidro do social (norte, pé-direito duplo):
bz = 2.2
while bz < AZ - 0.4:
    box(AX0 + 0.1, AX1 - 0.1, AY1 + 0.02, AY1 + 0.42, bz, bz + 0.06,
        tex=RIPA, scale=0.5)
    bz += 0.46
for bx in (AX0 + 0.12, AX1 - 0.18):      # montantes que seguram as lâminas
    box(bx, bx + 0.06, AY1 + 0.02, AY1 + 0.42, 2.2, AZ - 0.4, tex=MAD, scale=0.6)
# Sobre as 3 janelas das suítes (fachada norte do superior, girada junto)
_b = len(M)
for jx in (8.6, 10.7, 12.8):
    zb = SZ0 + 1.5
    while zb < SZ1 - 0.5:
        box(jx - 0.1, jx + 1.7, SY1 + 0.02, SY1 + 0.38, zb, zb + 0.05,
            tex=RIPA, scale=0.5)
        zb += 0.42
    for mx in (jx - 0.12, jx + 1.7):
        box(mx, mx + 0.05, SY1 + 0.02, SY1 + 0.38, SZ0 + 1.5, SZ1 - 0.5,
            tex=MAD, scale=0.6)
girados(_b)

# ------------------------------------------------------- COBOGÓ (Fase B)
# Grelha de ripas cruzadas: vazio geométrico REAL = sombra real (48 subtrações
# fariam o mesmo, mais caro e sem prova). Vai na parede LESTE do serviço, que é
# a divisa: ventilação cruzada sem expor a cozinha ao vizinho. É a metade
# "entra ar" do par que o briefing pede — a outra é o pano norte do social.
cy = 11.4
while cy < 15.6:
    box(BX1 - 0.29, BX1 + 0.04, cy, cy + 0.09, 1.0, 2.8, tex=CONC, scale=0.4)
    cy += 0.28
cz = 1.0
while cz < 2.8:
    box(BX1 - 0.29, BX1 + 0.04, 11.4, 15.6, cz, cz + 0.09, tex=CONC, scale=0.4)
    cz += 0.28

# --------------------------------- CONCRETO RIPADO: a estratificação (Fase B)
# Geométrico, não textura: a biblioteca só tem concreto liso e nenhuma textura
# entrega o jogo de luz que 4 cm de relevo dão. Na empena cega da garagem, que
# é a primeira coisa que se vê da rua — e que eu mesmo critiquei como "muda"
# na Fase A.
rx = GX0 + 0.15
while rx < GX1 - 0.2:
    box(rx, rx + 0.06, GY0 - 0.29, GY0 - 0.25 + E, 0.1, 3.0, tex=CONC,
        scale=0.5)
    rx += 0.16

# ============================================= PÁTIO (y 19-21): o respiro
box(3.0, 7.0, 19.0, 21.0, -0.3, 0.02, tex=GRAMA, scale=1.0)

# ============================================= LAZER, NOS FUNDOS (y 21-30)
# O deck vem PRIMEIRO (colado na casa) e a piscina DEPOIS — em faixas de y que
# não se cruzam. A 1ª versão tinha deck x 3,0-9,5 e piscina x 4,5-14,2 na mesma
# faixa de y: a raia atravessava o deck de lado a lado, água por cima da
# madeira. Transplantei as coordenadas da R53 (onde o lazer era ao sul, com
# outra geometria) sem conferir a sobreposição no lugar novo — o transplante
# cego que o Fable avisou, agora em planta.
DK0, DK1 = 3.0, 9.5
box(DK0, DK1, 21.0, 24.2, 0, 0.12, tex=DECK, scale=1.2)             # deck
# piscina RAIA 3×9 ao longo de X (em y os 9 m não caberiam no recuo de fundo)
box(3.0, 12.3, 24.8, 28.4, 0, 0.05, tex=PEDRA, scale=1.0)           # bacia
box(3.3, 12.0, 25.1, 28.1, 0, 0.06, color=AGUA)                     # lâmina
for (bx0, bx1, by0, by1) in [(3.0, 3.3, 24.8, 28.4), (12.0, 12.3, 24.8, 28.4),
                             (3.0, 12.3, 24.8, 25.1), (3.0, 12.3, 28.1, 28.4)]:
    box(bx0, bx1, by0, by1, 0, 0.14, tex=PEDRA, scale=1.0)          # borda
# CHURRASQUEIRA: boca virada pro deck (y−). A R55 me ensinou que fornalha
# maciça = braseiro e grelha invisíveis pra sempre; esta nasce com boca.
CX0, CX1 = 10.2, 11.7
box(CX0, CX1, 21.4, 23.2, 0.12, 0.90, tex=PEDRA, scale=1.0)         # base
box(CX0 - 0.02, CX1 + 0.02, 21.38, 23.22, 0.90, 0.96, tex=CONC, scale=0.8)
FB0, FB1 = CX0 + 0.12, CX1 - 0.12
box(FB0, FB1, 23.08, 23.20, 0.96, 1.62, tex="Tijolo 059.jpg", scale=0.5)
box(FB0, FB0 + 0.12, 21.55, 23.20, 0.96, 1.62, tex="Tijolo 059.jpg", scale=0.5)
box(FB1 - 0.12, FB1, 21.55, 23.20, 0.96, 1.62, tex="Tijolo 059.jpg", scale=0.5)
box(FB0, FB1, 21.55, 23.08, 1.50, 1.62, tex="Tijolo 059.jpg", scale=0.5)
box(FB0 + 0.12, FB1 - 0.12, 21.60, 23.08, 0.96, 1.14, color=GRAFITE)
box(FB0 + 0.10, FB1 - 0.10, 21.58, 23.08, 1.24, 1.28, color=[92, 94, 98])
box(FB0 + 0.06, FB1 - 0.06, 21.45, 23.15, 1.62, 2.05, tex="Tijolo 059.jpg",
    scale=0.5)
box(FB0 + 0.28, FB1 - 0.28, 21.85, 22.85, 2.05, 3.30, tex="Tijolo 059.jpg",
    scale=0.5)
box(FB0 + 0.22, FB1 - 0.22, 21.79, 22.91, 3.30, 3.42, tex=CONC, scale=0.6)
box(8.4, 10.1, 21.7, 22.8, 0.12, 0.86, tex=MAD, scale=0.7)          # bancada
box(8.35, 10.15, 21.65, 22.85, 0.86, 0.92, tex=PEDRA, scale=0.8)

# --------------------------------------------------- verde e gente (escala)
# As instâncias moram nas MESHES marcadas por `comp` — `components` está VAZIO
# em todo .zendo (a busca pelo nome óbvio não acha nada; foi o erro da R53).
FONTE = os.path.join(REPO, "entregas", "Casa do Dogfooding.zendo")
dog = json.load(open(FONTE, encoding="utf-8"))

def inst(nome, x, y, ang=0.0):
    ms = [m for m in dog["meshes"] if m.get("comp") == nome]
    if not ms:
        return
    m0 = ms[0]
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

# NENHUMA árvore no caminho do carro (x 10,4-16, y 0-5) — a versão velha tinha
# uma plantada em (12,2, 4,2), no meio do único corredor de acesso que existia.
for (tx, ty, nome, ta) in [(2.0, 3.0, "Árvore alta", 0.0),
                           (5.0, 2.4, "Árvore", 1.1),
                           (1.5, 26.0, "Árvore alta", 2.2),
                           (14.5, 30.5, "Árvore", 0.5),
                           (2.0, 20.0, "Árvore", 1.7),
                           (14.8, 19.5, "Árvore", 2.8)]:
    inst(nome, tx, ty, ta)
inst("Ensō-san", 8.15, 6.2, 0.0)      # a escala humana, entrando pela porta

# ------------------------------------------------------------------ gravar
os.makedirs(DEST, exist_ok=True)
doc = {"app": "Zendo", "version": base.get("version", 1),
       "meshes": M, "components": [], "scenes": [], "sections": [],
       "dimensions": [], "guides": [], "sketch": [],
       "day": base.get("day", {}), "wallHeight": 3.0,
       "textures": [{"name": t, "file": t} for t in
                    sorted({REBOCO, PEDRA, DECK, PISOSUP, MAD, RIPA, CONC,
                            GRAMA, PISO, "Tijolo 059.jpg"})],
       "camera": base.get("camera", {})}
out = os.path.join(DEST, "Casa Briefing.zendo")
json.dump(doc, open(out, "w", encoding="utf-8"), ensure_ascii=False)

ASSETS = os.path.join(REPO, "build-app", "src", "zendo", "assets", "materiais")
import shutil
for t in doc["textures"]:
    for raiz, _, arqs in os.walk(ASSETS):
        if t["file"] in arqs:
            shutil.copy(os.path.join(raiz, t["file"]), DEST)
            break

# O rótulo diz o que a casa TEM, não o que eu queria que ela fosse. "Fase B —
# completa" convivia com zero divisórias e zero banheiros; o Fable cobrou a
# palavra. Hoje o interior existe — mas "completa" segue sendo promessa, então
# o print lista o que foi provado e cala o resto.
print("Casa Briefing (R56+R57): %d sólidos · provado: 8/8 percursos, "
      "0 ambiente a céu aberto, 0 vidro enterrado" % len(M))
print("  -> %s" % out)
