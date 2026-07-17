# -*- coding: utf-8 -*-
# R56 — A PROVA DAS ABERTURAS: o objeto existe, mas o VAZIO dele existe?
#
# POR QUE ISTO EXISTE. Numa sessão só, o Guilherme achou o MESMO defeito quatro
# vezes, sempre olhando, sempre em 30 segundos:
#   R53 · o vidro das janelas 1 cm ATRÁS da parede (invisível de fora)
#   R55 · a porta de correr deslizando na frente de um pano de vidro CONTÍNUO
#   R56 · a parede maciça construída SOB a parede com vãos ("não tem janela?
#         sacada é uma parede seca?") — 3 paredes norte e 2 sul no mesmo lugar
#   R56 · a grelha do cobogó colada numa parede maciça ("um vão pra o vazio?")
#
# É sempre a mesma doença: EU CONSTRUO O ELEMENTO E ESQUEÇO DE ABRIR O LUGAR
# DELE. O objeto existe; o vazio não. E nenhuma prova minha pegava: bbox,
# vértice, flood-fill e render provam que a peça está lá — e ela ESTÁ. Até a
# prova de caminhamento (prova_programa.py) passa batido, porque ela pergunta
# "dá pra chegar?" e não "dá pra VER/ENTRAR?".
#
# Esta prova não anda pela casa: ela olha CADA peça que só faz sentido num vazio
# (vidro, esquadria, grelha, folha de porta) e pergunta se tem um sólido opaco
# em cima dela. Se tem, aquilo é ornamento enterrado, não abertura.
#
# Uso:  python prova_aberturas.py <casa.zendo>
import json, sys, io, argparse

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

VIDRO = [168, 199, 208]
GRAFITE = [52, 54, 58]
LATAO = [168, 140, 92]
# Peças que SÓ existem pra ocupar um vazio. Se estiverem dentro de um opaco,
# alguém esqueceu de abrir a parede.
DELICADAS = (tuple(VIDRO), tuple(GRAFITE), tuple(LATAO))

def bbox(m):
    vs = m["verts"]
    pts = ([vs[i:i+3] for i in range(0, len(vs), 3)]
           if not isinstance(vs[0], list) else vs)
    return [(min(p[k] for p in pts), max(p[k] for p in pts)) for k in range(3)]

def cor(m):
    c = (m.get("colors") or [{}])[0].get("c")
    return tuple(c) if c else None

def opaco(m):
    return cor(m) not in DELICADAS

def raio_bate(bbs, ox, oy, oz, dx, dy, dz=0.0, alcance=14.0):
    """Marcha um raio e devolve True se bater em algum opaco.

    LIMITE CONHECIDO — o teste e por BBOX. Pra parede (prisma reto) o bbox e a
    peca. Pra TELHADO INCLINADO E GIRADO o bbox e MUITO maior que a agua, e
    engole a fresta: o raio zenital da claraboia "bate" no telhado mesmo
    passando pelo vao. Por isso a claraboia aparece como falso-positivo aqui.
    Consertar exige trocar bbox por FACE (como o prova_programa ja faz) — fica
    declarado, nao escondido: um alarme conhecido e melhor que um alarme mudo.

    dz != 0 permite mirar pra CIMA: sem isso a prova e cega a abertura
    ZENITAL — e a claraboia da fresta (a peca que esta leva criou pra tapar o
    poco vertical) nunca era testada. Uma prova que so olha pros lados nao ve
    o telhado."""
    t = 0.06
    while t < alcance:
        x, y, z = ox + dx*t, oy + dy*t, oz + dz*t
        for b in bbs:
            if (b[0][0] <= x <= b[0][1] and b[1][0] <= y <= b[1][1]
                    and b[2][0] <= z <= b[2][1]):
                return True
        t += 0.05
    return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("casa")
    a = ap.parse_args()
    d = json.load(open(a.casa, encoding="utf-8"))
    ms = d["meshes"]
    # Só os opacos ESPESSOS podem tapar: descarto laje/piso (achatados) porque
    # o raio é horizontal e nunca sai por eles.
    # QUEM PODE MURAR: só PAREDE. Não árvore, não montante de brise.
    # A 1ª versão contava tudo e acusou o pano do social de "murado" porque
    # havia uma ÁRVORE do lado de fora, a 1 m da fachada — e árvore não é
    # parede: ela balança, some no inverno, e o cliente derruba se quiser.
    bbs = []
    for m in ms:
        if not opaco(m) or m.get("comp"):        # comp = árvore/móvel
            continue
        b = bbox(m)
        alt = b[2][1] - b[2][0]
        larg = max(b[0][1]-b[0][0], b[1][1]-b[1][0])
        if alt < 0.35 or larg < 0.30:            # ripa/montante vaza
            continue
        bbs.append(b)
    print("  %d sólidos · %d opacos espessos podem tapar" % (len(ms), len(bbs)))
    print()
    print("  ┌─ VIDRO que não se vê de lugar nenhum ────────────────────────")
    n = 0; tot = 0
    for i, m in enumerate(ms):
        if cor(m) != tuple(VIDRO):
            continue
        b0 = bbox(m)
        # Guarda-corpo NÃO é janela (vidro interno cercando um vazio; estar
        # cercado é a função dele). Reconheço pela altura: 1,00 m é peitoril.
        # MAS o filtro por altura excluía TODO vidro horizontal — inclusive a
        # CLARABOIA (6 cm), que é a abertura mais importante do superior.
        # Separo pela FORMA: peça baixa E estreita é guarda-corpo; peça baixa e
        # LARGA nos dois eixos é claraboia, e claraboia é janela do teto.
        alt = b0[2][1] - b0[2][0]
        largura_min = min(b0[0][1] - b0[0][0], b0[1][1] - b0[1][0])
        zenital = alt < 0.25 and largura_min > 0.30
        if alt <= 1.05 and not zenital:
            continue
        tot += 1
        b = bbox(m)
        cx = (b[0][0]+b[0][1])/2; cy = (b[1][0]+b[1][1])/2
        cz = (b[2][0]+b[2][1])/2
        # 5 direções: os 4 lados + PRA CIMA. A claraboia só tem a de cima.
        livre = [d_ for d_ in ((1,0,0), (-1,0,0), (0,1,0), (0,-1,0), (0,0,1))
                 if not raio_bate(bbs, cx, cy, cz, d_[0], d_[1], d_[2])]
        if not livre:
            print("  │ ✗ vidro #%d em (%.2f, %.2f, %.2f) — MURADO nos 4 lados"
                  % (i, cx, cy, cz))
            n += 1
    if not n:
        print("  │ ✓ os %d vidros têm pelo menos uma direção livre" % tot)
    print("  └──────────────────────────────────────────────────────────────")
    print()
    print("  %s" % ("✓ nenhum vidro enterrado" if not n
                    else "✗ %d de %d vidros enterrados" % (n, tot)))
    if n:
        print("  (claraboia sobre telhado inclinado/girado e falso-positivo")
        print("   conhecido do teste por bbox — ver docstring de raio_bate)")
    return 1 if n else 0

if __name__ == "__main__":
    sys.exit(main())
