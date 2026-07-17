# -*- coding: utf-8 -*-
# R57 — O TESTE DAS PROVAS: sabota a casa APROVADA e exige que a prova reprove.
#
# POR QUE EXISTE. A R56 fechou com "3 provas verdes". O Fable pegou a casa
# aprovada, emparedou o portao da garagem com um bloco macico e rodou a
# prova_programa: **"carro CHEGA na vaga"**. A prova estava morta (uma
# desigualdade estrita deixava atravessar parede) e as luzes verdes eram ruido.
#
# A REGRA QUE FALTAVA, na ordem dele:
#   1. caso minimo sintetico, resposta conhecida A PRIORI, antes do caso real
#   2. TESTE DE MUTACAO sobre o aprovado — se um mutante obvio passa, e teatro
#   3. a prova roda contra o caso doente E contra o caso sao
#
# "'Nasce vermelha' mede SENSIBILIDADE, nao especificidade. Sem um caso
#  negativo independente, VERDE e SABOTAGEM sao indistinguiveis."
#
# Uso:  python prova_mutacao.py <pasta_saida>
#       depois rode a prova_programa contra cada mut_*.zendo gerado:
#       mut_portao  -> tem que REPROVAR o carro
#
# NOTA HONESTA: o mutante "porta de entrada tapada" NAO reprova mais, e esta
# CERTO — depois que a R57 abriu a circulacao interna, a casa tem DUAS entradas
# (a da frente e o vao do patio). Um mutante que para de reprovar pode
# significar "a prova quebrou" OU "a casa melhorou"; so olhando da pra saber.
import json, copy, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
src = "entregas/Casa Briefing/Casa Briefing.zendo"
d = json.load(open(src, encoding='utf-8'))
tpl = next(m for m in d['meshes'] if len(m['verts']) == 8)
def box(x0,x1,y0,y1,z0,z1):
    vs = tpl['verts']
    xs=sorted(set(round(v[0],6) for v in vs)); ys=sorted(set(round(v[1],6) for v in vs))
    zs=sorted(set(round(v[2],6) for v in vs))
    slots=[(xs.index(round(v[0],6)), ys.index(round(v[1],6)), zs.index(round(v[2],6))) for v in vs]
    return {"wallNo":0, "faces":copy.deepcopy(tpl['faces']),
            "verts":[[[x0,x1][a],[y0,y1][b],[z0,z1][c]] for (a,b,c) in slots],
            "colors":[{"f":i,"c":[90,90,90]} for i in range(len(tpl['faces']))]}
muts = {
 "portao emparedado":     box(10.65, 15.95, 4.90, 5.10, 0.06, 3.00),
 "porta de entrada tapada": box(7.55, 8.75, 4.70, 5.05, 0.06, 2.35),
}
for nome, blk in muts.items():
    m = copy.deepcopy(d); m['meshes'].append(blk)
    p = "%s/mut_%s.zendo" % (sys.argv[1], nome.split()[0])
    json.dump(m, open(p, "w", encoding='utf-8'), ensure_ascii=False)
    print(p)
