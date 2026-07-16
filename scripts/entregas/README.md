# scripts/entregas — como as vitrines são construídas

Os projetos-vitrine do ecossistema Zen não foram desenhados na UI: eles são
**escritos direto no JSON** do `.zendo`/`.zencad` por estes scripts (o arquivo
é a interface). Até a R49 eles moravam no diretório temporário da sessão —
ou seja, as entregas **não eram reconstruíveis**. Agora vivem aqui.

Todos aceitam um **destino alternativo como argumento** (`script.py <destino>`)
para gerar sem sobrescrever a entrega real — foi assim que a R49 se provou.

| Script | Produz | Estado |
|---|---|---|
| `build_sobrado.py` | `entregas/Sobrado Zen/` (184 sólidos + 2 plantas + assets) | **Reconstrói 100%** — provado por re-run + diff contra o entregue |
| `build_casa.py` | `entregas/Casa Enso/Casa Enso.zendo` (80 sólidos) | **Parcial** — ver abaixo |
| `gera_casa.py` | `projetos/Casa Patio.zencad` (375 entidades: plantas, elevação, corte, 5 pranchas A2) | Reconstrói 100% |
| `casa-semente.zendo` | insumo do `build_casa.py` | A casa já extrudada da planta 2D pelo próprio app |

## A cadeia (não é um botão só)

O Sobrado nasce inteiro do script. A Casa Ensō não:

```
Casa Enso 3D.zencad  --(app: abre e extruda as paredes)-->  casa-semente.zendo
casa-semente.zendo   --(build_casa.py)------------------->  Casa Enso.zendo  (80 sólidos)
                     --(camadas perdidas)---------------->  a entregue (133 sólidos)
```

**Honestidade sobre o estado**: a Casa Ensō *entregue* tem 133 sólidos; o
script reconstrói o estágio de 80. As camadas que vieram depois (as 45 peças
de esquadria da R37, o paisagismo, o terreno) foram aplicadas por scripts
avulsos que **não sobreviveram** ao scratchpad. Rodar `build_casa.py` no
destino padrão **rebaixa** a entrega — use o argumento de destino. Reconstruir
essas camadas é trabalho de uma leva; enquanto não for feito, isto fica
escrito aqui em vez de fingirmos reprodutibilidade.

O `build_sobrado.py` já absorveu tudo que veio depois da R39 (forro R43,
árvores orgânicas R44, entorno + grama fotográfica R45, o terreno que era a
flag `--terrain`, e as duas plantas humanizadas) — por isso ele fecha.

## O que NÃO mora aqui (critério de corte)

Só entra script que **produz um artefato versionado** (uma entrega ou um
projeto). Sondas, sniffers de QA, recortes de imagem e scripts de uma leva só
morrem com a leva — se voltarem, voltam nomeados. Sem esse critério, esta
pasta vira um segundo scratchpad.

## Casa Briefing (R53) — `build_casa_briefing.py`

Responde a um briefing de arquitetura REAL do Guilherme ("anti-caixote":
contemporâneo não-linear, planos desencontrados, balanços, platibandas
dinâmicas, brises/cobogó, pátio interno, pé-direito duplo, piscina).

**A tese**: "anti-caixote" ≠ "não-ortogonal". Tudo que o briefing enumera
(planos desencontrados, balanço, platibanda em duas alturas, brise, cobogó,
pátio, estratificação) é prisma reto — o que mata o caixote é sombra + ritmo
+ material, não curvatura. **MAS** o briefing pediu "não-ortogonal" com todas
as letras, então o volume superior **gira 8° em planta**: é o pagamento dessa
palavra. Entregar só caixas deslocadas seria fingir que atendeu.

**Reconstrói**: 100%. `python build_casa_briefing.py [destino-de-teste]`.

**O que NÃO saiu pela ferramenta e por quê** (honestidade, não desculpa):
- **Telhado de uma água girado** → manual. O `roofSelected` monta o telhado
  sobre o bbox **alinhado ao MUNDO**: sobre volume girado sai não-girado e com
  beiral fantasma de até 1,2 m. E "uma água" não existe na ferramenta (só 2/4).
- **Prainha da piscina** → cortada pelo argumento de PROJETO (o briefing
  oferece "prainha OU raia"; a raia dialoga com o eixo do deck). **Não** por
  ser curva: o Zendo faz curva em planta (círculo/arco/polígono/Follow Me).
  O que ele não faz é superfície EMPENADA — o fundo em rampa.
- **Varanda da master** → construída direto no volume, não por subtração
  booleana: alvo girado é caminho jamais exercitado (R21-R25 foram todas
  axis-aligned). Experimento não se mistura com entrega.
- **Concreto ripado** → geométrico (ripas de 4 cm), não textura: a biblioteca
  só tem concreto liso e nenhuma textura dá o jogo de sombra do relevo.
- **Cobogó** → grelha de ripas cruzadas, não 48 subtrações: vazio real projeta
  sombra real no Cycles, e sai mais barato.

**Armadilhas que a prova visual pegou** (as 4 na 1ª rodada):
1. `components` está **vazio** em todo .zendo — as instâncias moram nas MESHES
   com `comp`. Procurar em `components` não acha nada e a cena sai sem árvore.
2. **"Grama.png" não existe nos assets**: é PROCEDURAL (R9), mora no %APPDATA%
   e só a UI resolve. A cópia falha calada e o terreno sai cinza. Use as
   fotográficas ("Grama 004.jpg").
3. Piscina: bacia rasa + lâmina **1 cm acima** (padrão R35). Pôr a água dentro
   de um bloco maciço a enterra e a piscina sai cinza.
4. Pé-direito duplo é um **VAZIO**: o pavimento superior tem que desviar dele,
   nunca passar por cima. Volume alto vai AO LADO.

**Sol**: renderize com **Nishita** (sem `--hdri`). Com o HDRI a cena sai
escura — a foto de céu traz o sol DELA, em posição fixa, e ignora a hora da
Bandeja. Achado da R53, aberto.
