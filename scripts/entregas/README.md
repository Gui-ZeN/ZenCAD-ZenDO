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
