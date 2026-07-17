# As duas provas de PROJETO (R56)

Nasceram porque o Guilherme olhou dois renders da Casa do Briefing por 30
segundos e achou três falhas que **todas** as provas geométricas do projeto
deixaram passar: a casa não tinha escada, o carro não entrava, e a sacada
estava atrás de uma parede.

O diagnóstico: bbox, vértice, flood-fill e render verificam se a geometria bate
com o que eu **mandei desenhar**. Nenhum pergunta se o que mandei desenhar
**serve**.

| prova | pergunta | como |
|---|---|---|
| `prova_programa.py` | **dá pra chegar?** | BFS de pessoa (pé-direito ≥2,00, degrau ≤0,20) e de carro (altura ≥2,10, rampa ≤0,05) da rua até alvos do briefing |
| `prova_aberturas.py` | **dá pra ver?** | do centro de cada vidro, raio horizontal pra cada lado; todos batendo em parede = enterrado |

## A regra inegociável: a prova NASCE VERMELHA

Antes de escrever uma linha de correção, a prova roda contra a casa **quebrada**
e tem que acusar. Se der verde na casa que a motivou, é teatro e volta pra
bancada. Foi assim que as duas se provaram:

    prova_programa  → 3/3 falhas na casa velha, 4/4 verde na nova
    prova_aberturas → 1/7 vidros enterrados na velha, 0/10 na nova

## Uso

    python prova_programa.py "../../entregas/Casa Briefing/Casa Briefing.zendo"
    python prova_aberturas.py "../../entregas/Casa Briefing/Casa Briefing.zendo"

`--alvos alvos.json` aponta os destinos sem tocar no motor:

    {"vaga de carro": [[13.2, 8.0, 0.06], "carro"],
     "piso superior": [[12.0, 13.0, 3.56], "pessoa"]}

## Armadilhas (todas custaram caro)

- **A grade tem que ser menor que o piso do degrau** (0,28 m na escada da R41).
  Com 0,40 o BFS vence 0,25 m por célula e reprova escada que funciona.
- **Piso é a FACE voltada pra cima, não o topo do bbox.** Tratar sólido como
  "footprint + faixa de z" faz a escada virar bloco maciço e sumir.
- **Erosão mede obstáculo, não planura.** Exigir vizinho na mesma altura
  reprova a pessoa numa escada normal.
- **Face vertical não pode ser descartada**: sem ela a prova deixa atravessar
  parede — foi assim que ela deu verde numa casa com a sacada emparedada.
- **bbox não serve num modelo girado**: o bbox de uma parede a 8° tem 2,13 m
  de largura pra 25 cm de espessura. A 1ª `prova_aberturas` deu 26 alarmes na
  casa boa e 29 na quebrada — não separava nada.
- **Guarda-corpo não é janela** (vidro interno; cercar é a função) e **árvore
  não é parede** (balança, some, o cliente derruba).

## O que elas NÃO pegam

Parede cega que devia ter janela; cobogó tapado; porta emparedada; e qualquer
juízo de projeto ("a sacada olha pra quê?"). **Placar da R56: das 7 falhas de
projeto, o cliente achou 5 olhando, eu achei 2, as provas acharam 0.** Elas
pegam o que o olho não pega — desembarque de escada caindo no vão, porta
"entreaberta" com 11 cm de passagem — e nada mais. Não substituem ninguém
olhando.
