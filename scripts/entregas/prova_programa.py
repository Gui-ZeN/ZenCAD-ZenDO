# -*- coding: utf-8 -*-
# R56 — A PROVA DE PROGRAMA: a casa FUNCIONA, ou só está bonita?
#
# POR QUE ISTO EXISTE. O Guilherme olhou dois renders por 30 segundos e
# perguntou: "como um carro entraria aqui, como a pessoa acessaria o segundo
# piso, tem uma sacada para uma parede vazia?". As três procediam. Nenhuma das
# minhas provas — bbox, vértices, flood-fill, render — pegaria qualquer uma
# delas: todas verificam se a geometria bate com o que eu MANDEI desenhar,
# nenhuma pergunta se o que mandei desenhar SERVE.
#
# Esta pergunta "quem usa isso, vindo de onde, indo pra onde?" é o que este
# script automatiza. Ele não olha a forma: ele ANDA pela casa.
#
# REGRA DO FABLE, inegociável: esta prova NASCE VERMELHA. Rodada contra a
# `Casa Briefing.zendo` de hoje ela tem que reportar exatamente as 3 falhas que
# o Guilherme achou no olho. Se der verde contra a casa quebrada, a prova é
# teatro e volta pra bancada — antes de UMA linha de reprojeto ser escrita.
#
# Uso:  python prova_programa.py <casa.zendo> [--grade 0.25]
import json, sys, io, math, argparse
from collections import deque
import numpy as np

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

# ---------------------------------------------------------------- o gabarito
# Os alvos vêm do BRIEFING, não da geometria — é isso que quebra a
# circularidade. Se eu extraísse os alvos do .zendo, provaria que a casa é
# igual a si mesma.
LOTE = (0.0, 16.0, 0.0, 32.0)
PESSOA_LARG = 0.55          # ombro; erosão de meia-largura
CARRO_LARG = 2.30           # corpo + espelhos: meia-largura 1.15
PE_DIREITO_MIN = 2.00       # vão livre pra pessoa passar em pé
CARRO_ALT_MIN = 2.10        # SUV/picape
DEGRAU_MAX = 0.20           # o que um pé vence
RAMPA_CARRO_MAX = 0.05      # o que uma roda vence sem rampa modelada

class Face:
    """Uma FACE do .zendo, não um sólido.

    A 1ª versão reduzia cada mesh a "footprint + faixa de z". Funciona pra
    caixa e MENTE pra escada: os 20 degraus viram um bloco maciço de z=0 a
    3,56, e a prova respondia "piso 3,56" na corrida inteira — a escada
    desaparecia e o percurso pro andar de cima ficava impossível mesmo com ela
    construída. A prova não sabia SUBIR escada porque achava que escada é
    caixa. Sólido é o que a geometria tem; PISO é o que a face horizontal
    voltada pra cima oferece — é a face que importa pra quem anda."""
    __slots__ = ("poly", "nz", "a", "b", "c", "d", "bb", "mi")

    def __init__(self, pts, mi=-1):
        self.mi = mi
        v = np.asarray(pts, dtype=float)
        # normal por Newell: robusta pra polígono de N lados, côncavo ou não
        n = np.zeros(3)
        for i in range(len(v)):
            p, q = v[i], v[(i + 1) % len(v)]
            n[0] += (p[1] - q[1]) * (p[2] + q[2])
            n[1] += (p[2] - q[2]) * (p[0] + q[0])
            n[2] += (p[0] - q[0]) * (p[1] + q[1])
        ln = np.linalg.norm(n)
        if ln < 1e-12:
            self.nz = 0.0
            self.poly = []
            return
        n /= ln
        self.a, self.b, self.c = float(n[0]), float(n[1]), float(n[2])
        self.d = float(np.dot(n, v[0]))
        self.nz = self.c
        self.poly = [(float(p[0]), float(p[1])) for p in v]
        self.bb = (float(v[:, 0].min()), float(v[:, 0].max()),
                   float(v[:, 1].min()), float(v[:, 1].max()))

    def z_em(self, x, y):
        """z do plano da face em (x,y). Só faz sentido pra face não-vertical."""
        return (self.d - self.a * x - self.b * y) / self.c

    def cobre(self, x, y):
        if abs(self.nz) < 0.34:      # face vertical/muito inclinada: não é piso
            return False
        if not (self.bb[0] - 1e-9 <= x <= self.bb[1] + 1e-9
                and self.bb[2] - 1e-9 <= y <= self.bb[3] + 1e-9):
            return False
        return dentro_poly(self.poly, x, y)

def casco(pts):
    """Casco convexo (monotone chain). Uma caixa girada vira o retângulo
    girado — não o bbox alinhado, que é o erro que engordaria a casa."""
    p = sorted(set(map(tuple, np.round(pts, 6))))
    if len(p) <= 2:
        return p
    def meia(pontos):
        h = []
        for q in pontos:
            while len(h) >= 2 and cruz(h[-2], h[-1], q) <= 0:
                h.pop()
            h.append(q)
        return h
    return meia(p)[:-1] + meia(reversed(p))[:-1]

def cruz(o, a, b):
    return (a[0]-o[0])*(b[1]-o[1]) - (a[1]-o[1])*(b[0]-o[0])

def dentro_poly(poly, x, y):
    """Ponto-em-polígono por ray casting. Fecha em cima da borda: uma parede
    de 25 cm precisa bloquear a célula que ela toca."""
    n = len(poly)
    if n < 3:
        return False
    d = False
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > y) != (yj > y):
            xc = (xj - xi) * (y - yi) / (yj - yi + 1e-300) + xi
            if x < xc:
                d = not d
        j = i
    return d

# ------------------------------------------------- o mundo em camadas por z
def carregar(path):
    doc = json.load(open(path, encoding="utf-8"))
    faces = []
    for mi, m in enumerate(doc["meshes"]):
        vs = m["verts"]
        pts = ([vs[k:k+3] for k in range(0, len(vs), 3)]
               if not isinstance(vs[0], list) else vs)
        for f in m["faces"]:
            idx = f if isinstance(f, list) else f.get("v", [])
            if len(idx) < 3:
                continue
            fc = Face([pts[i] for i in idx], mi)
            if fc.poly and abs(fc.nz) >= 0.34:
                faces.append(fc)
    return faces

class Indice:
    """Balde de faces por célula de 1 m. Sem isto, uma grade de 0,20 m sobre o
    lote 16×32 são 12.800 pontos × ~2.000 faces = 25 milhões de testes."""
    def __init__(self, faces, celula=1.0):
        self.c = celula
        self.b = {}
        for f in faces:
            i0, i1 = int(f.bb[0] // celula), int(f.bb[1] // celula)
            j0, j1 = int(f.bb[2] // celula), int(f.bb[3] // celula)
            for i in range(i0, i1 + 1):
                for j in range(j0, j1 + 1):
                    self.b.setdefault((i, j), []).append(f)

    def perto(self, x, y):
        return self.b.get((int(x // self.c), int(y // self.c)), ())

def superficies(faces, x, y):
    """Onde dá pra POUSAR o pé em (x,y), e quanto vão livre tem acima.
    Devolve [(z_piso, vao_livre)], de baixo pra cima.

    Trabalha por FACE, não por sólido: cada face voltada pra CIMA (nz>0) sob o
    ponto é um piso candidato; cada face voltada pra BAIXO (nz<0) acima dele é
    um teto. É o que faz o degrau existir — e é o que a versão por-sólido não
    conseguia enxergar. Um piso sem vão livre não é piso: é laje que esmaga a
    cabeça, e é assim que uma escada sem vão reprova."""
    # Agrupa por SÓLIDO. Em (x,y), as faces horizontais de um mesmo mesh dão o
    # intervalo [base, topo] que ELE ocupa naquela vertical — e é por (x,y),
    # não pelo bbox do mesh inteiro: na escada, as faces que cobrem um ponto
    # são a base e o degrau daquele ponto, então o intervalo sai [0, 0.53] e
    # não [0, 3.56]. Resolve escada E parede com a mesma conta.
    porsol = {}
    for f in (faces.perto(x, y) if isinstance(faces, Indice) else faces):
        if not f.cobre(x, y):
            continue
        z = f.z_em(x, y)
        a, b = porsol.get(f.mi, (1e9, -1e9))
        porsol[f.mi] = (min(a, z), max(b, z))
    if not porsol:
        return []
    vol = sorted(porsol.values())        # os volumes ocupados nesta vertical
    out = []
    for a, b in vol:
        # O TOPO DE UM SÓLIDO SÓ É PISO SE NÃO ESTIVER DENTRO DE OUTRO.
        # Sem isto a prova deixa a pessoa ATRAVESSAR PAREDE: uma parede de
        # z 3,5-6,3 sobre um piso 3,56 tem a base ABAIXO do piso, então a busca
        # ingênua por "primeiro teto acima" não a encontrava e declarava 2,74 m
        # de vão livre — dentro do reboco. Foi assim que esta prova deu VERDE
        # numa casa cuja sacada estava atrás de uma parede maciça, e cujas
        # janelas idem. Prova que não vê parede não prova percurso nenhum.
        # A COMPARAÇÃO DA BASE É INCLUSIVA (<=), e isto é a diferença entre a
        # prova funcionar e ser teatro. Com `o[0] < b` estrito, uma parede que
        # ASSENTA no piso (base z=0 = topo do terreno, que é como nasce TODA
        # parede do térreo) não engolia o piso de baixo: a prova reportava
        # 5,60 m de vão livre DENTRO de uma parede de pedra de 5,60 m, e
        # deixava a pessoa atravessar. O Fable provou por MUTAÇÃO: emparedou o
        # portão da garagem com um bloco maciço e a prova disse "carro CHEGA";
        # emparedou a porta de entrada e ela disse "pessoa CHEGA no social".
        # Eu tinha consertado só o caso em que a parede PENETRA o piso (o E de
        # 5 mm da R43, que só uso no superior) e li o verde como "consertei a
        # casa" — era igualmente compatível com "quebrei a prova". Era isso.
        dentro = any(o[0] <= b + 1e-6 and b < o[1] - 1e-6 for o in vol)
        if dentro:
            continue
        teto = 1e9
        for c, _d in vol:
            if c > b + 1e-6:
                teto = c
                break
        out.append((b, teto - b))
    fus = []
    for z, v in out:
        if fus and abs(z - fus[-1][0]) < 0.02:
            fus[-1] = (max(z, fus[-1][0]), min(v, fus[-1][1]))
        else:
            fus.append((z, v))
    return fus

def malha_andavel(faces, passo, alt_min, degrau_max, meia_larg, tol_ero=0.6):
    """Grade 3D esparsa: nó = (ix, iy, z_piso). Só entra quem tem pé-direito.
    A erosão morfológica pela meia-largura é o que impede o corredor de 3,2 m
    com árvore no meio de contar como passagem.

    `tol_ero` é a folga VERTICAL da erosão, e ela existe por um erro meu: a 1ª
    versão erodia com o mesmo `degrau_max` do passo, exigindo que os vizinhos
    tivessem piso na MESMA altura. Numa escada, a 0,4 m de distância o degrau
    já subiu 0,25 m — então a erosão concluía que a pessoa "não cabe" numa
    escada perfeitamente normal, e o percurso pro andar de cima reprovava com a
    escada construída e funcionando. Erosão mede OBSTÁCULO (parede, árvore,
    buraco), não planura: o corpo atravessa um lance inclinado sem reclamar.
    0,6 m cobre um lance sob o corpo; o carro, que é rígido, usa 0,10."""
    x0, x1, y0, y1 = LOTE
    nx = int((x1 - x0) / passo) + 1
    ny = int((y1 - y0) / passo) + 1
    nos = {}
    for ix in range(nx):
        for iy in range(ny):
            x, y = x0 + ix * passo, y0 + iy * passo
            for z, vao in superficies(faces, x, y):
                if vao >= alt_min:
                    nos.setdefault((ix, iy), []).append(z)
    # erosão: uma célula só serve se o corpo inteiro cabe nela e nos vizinhos
    # O RAIO É MEDIDO EM METROS, NÃO EM ÍNDICE DE CÉLULA. Com
    # `r = ceil(meia_larg/passo)` e depois `dx²+dy² > r²`, o teste virava um
    # disco de r CÉLULAS: com passo 0,20 e meia-largura 0,275 dava r=2 = 0,40 m
    # — a pessoa engordava 45% e não passava numa porta de 1,10 m. O BFS dava
    # a volta na casa inteira pelo gramado (7.590 células, até o fundo do lote)
    # e nunca entrava pela porta da frente. Arredondar pra cima o corpo de quem
    # anda é reprovar a casa por um erro do método.
    r = int(math.ceil(meia_larg / passo))
    if r > 0:
        livre = {}
        for (ix, iy), zs in nos.items():
            for z in zs:
                ok = True
                for dx in range(-r, r + 1):
                    for dy in range(-r, r + 1):
                        if (dx * passo) ** 2 + (dy * passo) ** 2 > meia_larg ** 2:
                            continue
                        viz = nos.get((ix + dx, iy + dy))
                        if not viz or not any(abs(zz - z) <= tol_ero
                                              for zz in viz):
                            ok = False
                            break
                    if not ok:
                        break
                if ok:
                    livre.setdefault((ix, iy), []).append(z)
        nos = livre
    return nos, nx, ny

def alcanca(nos, passo, origem, alvo, degrau_max, raio_alvo=0.6):
    """BFS do ponto de partida até o alvo. Devolve (achou, celulas_visitadas)."""
    x0, y0lote = LOTE[0], LOTE[2]
    def cel(p):
        return (int(round((p[0] - x0) / passo)), int(round((p[1] - y0lote) / passo)))
    ci = cel(origem)
    zs = nos.get(ci)
    if not zs:
        return False, 0, ("partida (%.1f, %.1f) não é andável"
                          % (origem[0], origem[1]))
    ini = (ci[0], ci[1], min(zs, key=lambda z: abs(z - origem[2])))
    ca = cel(alvo)
    r = int(math.ceil(raio_alvo / passo))
    vis = {ini}
    fila = deque([ini])
    while fila:
        ix, iy, z = fila.popleft()
        # O ALVO É O NÓ, NÃO A CÉLULA. A 1ª versão testava se a célula do alvo
        # tinha ALGUMA superfície na altura certa — sem exigir que o BFS
        # tivesse CHEGADO nela. Resultado: quem andasse no gramado embaixo do
        # sobrado era declarado no quarto de cima. Teletransporte. A prova
        # nasceu VERDE numa casa sem escada — exatamente o teatro que ela
        # existe pra caçar. O z do nó tem que ser o z do alvo.
        if (abs(ix - ca[0]) <= r and abs(iy - ca[1]) <= r
                and abs(z - alvo[2]) <= 0.35):
            return True, len(vis), ""
        for dx, dy in ((1,0), (-1,0), (0,1), (0,-1)):
            k = (ix + dx, iy + dy)
            for zz in nos.get(k, []):
                if abs(zz - z) <= degrau_max and (k[0], k[1], zz) not in vis:
                    vis.add((k[0], k[1], zz))
                    fila.append((k[0], k[1], zz))
    return False, len(vis), "sem caminho"

# ------------------------------------------------------------------ o laudo
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("casa")
    # A GRADE TEM QUE SER MENOR QUE O PISO DO DEGRAU (0,28 m na escada da
    # R41). Com 0,40 a prova pula de degrau em degrau vencendo 0,25 m por
    # célula — acima do 0,20 que um pé alcança — e reprova uma escada que
    # EXISTE e funciona. O default 0,20 resolve o degrau com folga.
    ap.add_argument("--grade", type=float, default=0.20)
    ap.add_argument("--alvos", default="")
    a = ap.parse_args()

    sol = Indice(carregar(a.casa))
    nf = sum(len(v) for v in sol.b.values())
    print("  %d faces indexadas · grade %.2f m" % (nf, a.grade))
    print()

    # Os alvos: coordenada, altura do piso, e o que a falha SIGNIFICA.
    # Editáveis por --alvos pra apontar pra casa nova sem tocar no motor.
    alvos = {
        "vaga de carro":   ((11.5, 12.0, 0.06), "carro"),
        "social":          (( 4.5, 16.0, 0.06), "pessoa"),
        "piso superior":   ((11.0, 13.0, 3.56), "pessoa"),
        "varanda master":  ((13.2,  8.9, 3.56), "pessoa"),
    }
    if a.alvos:
        alvos = json.loads(open(a.alvos, encoding="utf-8").read())
        alvos = {k: (tuple(v[0]), v[1]) for k, v in alvos.items()}

    print("  construindo a malha da PESSOA (pé-direito ≥ %.2f, degrau ≤ %.2f)…"
          % (PE_DIREITO_MIN, DEGRAU_MAX))
    nos_p, nx, ny = malha_andavel(sol, a.grade, PE_DIREITO_MIN, DEGRAU_MAX,
                                  PESSOA_LARG / 2)
    print("  construindo a malha do CARRO (altura ≥ %.2f, rampa ≤ %.2f)…"
          % (CARRO_ALT_MIN, RAMPA_CARRO_MAX))
    nos_c, _, _ = malha_andavel(sol, a.grade, CARRO_ALT_MIN, RAMPA_CARRO_MAX,
                                CARRO_LARG / 2, tol_ero=0.10)   # carro é rígido
    print()

    # A partida entra 1,5 m no lote: em y=0,25 a erosão morfológica procura
    # vizinhos ATRÁS da divisa, não acha nós, e reprova a calçada — artefato do
    # método, não defeito da casa. Quem começa do portão já está no terreno.
    rua_p = (8.0, 1.5, 0.0)
    rua_c = (11.5, 1.5, 0.0)
    falhas = []
    print("  ┌─ PERCURSO ─────────────────────────┬────────┬──────────────────")
    for nome, (alvo, quem) in alvos.items():
        nos = nos_c if quem == "carro" else nos_p
        org = rua_c if quem == "carro" else rua_p
        dmax = RAMPA_CARRO_MAX if quem == "carro" else DEGRAU_MAX
        ok, n, err = alcanca(nos, a.grade, org, alvo, dmax)
        print("  │ %-6s: rua → %-22s │ %-6s │ %s"
              % (quem, nome, "CHEGA" if ok else "NÃO", err or "%d células" % n))
        if not ok:
            falhas.append(nome)
    print("  └────────────────────────────────────┴────────┴──────────────────")
    print()
    if falhas:
        print("  ✗ %d de %d percursos IMPOSSÍVEIS: %s"
              % (len(falhas), len(alvos), ", ".join(falhas)))
    else:
        print("  ✓ todos os %d percursos do programa funcionam." % len(alvos))
    return 1 if falhas else 0

if __name__ == "__main__":
    sys.exit(main())
