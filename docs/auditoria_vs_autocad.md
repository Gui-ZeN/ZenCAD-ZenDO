# CadCore — Auditoria comando-por-comando vs AutoCAD (2026-06-30)

Estado base: **kernel smoke 100%**, **app compila** (`APP_EXIT=0`). Auditoria feita por 4 agentes lendo o código-fonte real (`ToolController`, `MainWindow`, `GeometryOps/ModifyOps`, `SnapEngine`, `Dimension/Hatch/Leader/MLeader`, `DxfWriter/Reader`, `LayersPanel`).

Legenda: ✅ completo · ⚠️ parcial · ❌ ausente.

> **Correções vs rascunho dos agentes**: ciclagem de seleção **existe** (`selectAt` com `m_cycleIndex` + candidatos ordenados) e **lock/frozen são aplicados** em `selectAt` E `selectInBox` (`if (lay->frozen||lay->locked) continue`). Verificado no código.

---

## 1. DESENHO / CRIAÇÃO

| Comando | Status | Falta vs AutoCAD |
|---|---|---|
| Line | ✅ | Close, Undo (vértice), entrada por comprimento/ângulo; cada par vira `Line` separada |
| Circle centro-raio | ✅ | sub-opção Diâmetro / raio digitado |
| Circ 2P / 3P | ✅ | — |
| Circ TTR | ⚠️ | só tangente a Line/Círculo; raio fixado antes |
| Circ TTT | ⚠️ | doc diz "3 retas"; tangência a círculos/arcos não garantida |
| Arc 3P | ✅ | — |
| Arc SCE / CSE | ⚠️ | sentido fixo CCW; sem variantes ângulo/corda |
| Arc SER / SEA | ⚠️ | aceitam valor digitado; SER sem escolher arco maior/menor |
| Arc SED | ✅ | — |
| Rectangle | ⚠️ | faltam Area, Dimensions, Rotation, Width |
| Ret Chanfro / Fillet | ⚠️ | um valor só (reusa dist. global do CHAMFER/FILLET) |
| Polígono | ⚠️ | falta modo **Edge** |
| Ellipse | ⚠️ | só "Center"; falta **Axis,End** e Rotation |
| Arco Elíptico | ⚠️ | ângulos por clique (sem digitar) |
| Polyline | ⚠️ | falta Halfwidth, Length, Undo, largura afilada |
| → modo Arco (bulge) | ⚠️ | **sem toggle de UI** (só API `setPolyArc`); só arco tangente |
| → largura | ⚠️ | largura **global**, sem taper nem por-segmento |
| Spline (fit) | ⚠️ | sem tolerância, tangentes, fechar |
| Spline CV | ⚠️ | sem escolher grau |
| Point | ⚠️ | sem PDMODE/PDSIZE |
| XLINE | ⚠️ | faltam Hor/Ver/Ang/Bisect/Offset (todas) |
| RAY | ✅ | — |
| MLINE | ⚠️ | faltam Justification, Scale, Style (MLSTYLE) |
| Nuvem de Revisão | ⚠️ | só retângulo; faltam Polygonal/Freehand/Object |

**Famílias inteiras ausentes**: DONUT, 2D-SOLID, WIPEOUT, REGION, BOUNDARY, GRADIENT (hatch), TABLE, HELIX, 3DPOLY, SKETCH, SPLINEDIT/PEDIT, MLSTYLE.

**Transversal (P1 #2, 2026-06-30) — ✅ FECHADO**: **entrada numérica** completa — **Círculo/Polígono por raio**, **Retângulo+variantes por `largura,altura`/`LxA`**, **Elipse por eixo menor**, **Texto por altura** (diálogo), Arc SER/SEA; coords `x,y`/`@dx,dy`/`@dist<ang` e distância direta para qualquer ponto.

---

## 2. MODIFICAR / EDIÇÃO

| Comando | Status | Falta vs AutoCAD |
|---|---|---|
| Move | ✅ | Displacement, vetor por teclado |
| Copy | ✅ | **Copy múltiplo** (AutoCAD): ponto-base, depois cada clique cola uma cópia até Enter/Esc |
| Rotate (+Copiar / +Referência) | ✅ | ângulo numérico por teclado |
| Scale (+Copiar / +Referência) | ✅ | fator numérico por teclado |
| Mirror | ⚠️ | sem "apagar origem? Y/N" (sempre mantém) |
| Offset | ⚠️ | Line/Circle/Arc/Polyline só; faltam Through/Erase/Layer; sem Ellipse/Spline |
| **Trim** | ✅* | **Line/Circle/Arc/Polyline**, modelo AutoCAD moderno: **todas as entidades são arestas de corte**, 1 clique apara o trecho clicado (remove só o vão entre os 2 cortes vizinhos; sobra no **máx. 2 peças contínuas**, sem fragmentar nos demais cruzamentos — corrigido 2026-06-30); 1 peça in-place, 2 split. Polyline trata trechos como retas (bulges ignorados na divisão). ✅ **window-select** (arrastar janela) e ✅ **Fence** (tecla F: clica a linha-cerca, Enter apara tudo que ela cruza). |
| **Extend** | ✅* | **Line/Arc → qualquer contorno** (`ExtendOps::extendEntity`). ✅ **Fence** (tecla F). *Falta*: Polyline, Edge/Project |
| **Fillet** | ✅* | **Line/Arc/Circle** em qualquer combinação (`edit/FilletOps::filletGeometry` por loci de offset + `trimToTangent`; círculo mantido inteiro). *Falta*: Polyline, Multiple, no-trim mode |
| **Chamfer** | ⚠️ | **só Line+Line, d1=d2**; sem Distance1≠2, Angle, Polyline, Multiple |
| **Break** | ✅* | **Line/Circle/Arc/Polyline** (reusa `splitEntityAt` do Trim: remove o trecho entre os 2 pontos; círculo→arco; polilinha→pedaços). *Bulges ignorados na divisão* |
| **Join** | ✅* | colineares → 1 Line; **tocando-se → Polilinha** (`joinEntities`). *Falta*: Arc/Spline, encadear vários |
| **Lengthen** | ⚠️ | modo **Delta** em **Line e Arc** (`lengthenArc`: Δcomprimento→Δângulo). *Falta*: Percent/Total/Dynamic |
| Stretch | ⚠️ | depende de Crossing; sem deslocamento numérico |
| Array Retangular | ⚠️ | **dx=dy forçado**; sem ângulo, sem associatividade |
| Array Polar | ⚠️ | **centro = bbox** (não escolhível); sem rotate-items, sem associatividade |
| Array Path | ❌ | ausente |
| Explode | ⚠️ | provável só Polyline/Block |
| Block (criar) | ⚠️ | sem nome/diálogo, sem INSERT, sem atributos |
| Align | ✅ | só 2 pares 2D (sem 3D, sem "scale? Y/N" — sempre escala) |
| Match Properties | ⚠️ | só cor/camada/tipo/espessura; sem Settings (texto/cota/hachura) |
| Erase | ✅ | sem OOPS (só Undo) |
| Seleção Window/Crossing | ✅ | sem WPolygon/CPolygon/Fence/All/Last/Previous |
| **Ciclagem de seleção** | ✅ | existe (clique repetido cicla sobrepostos); sem HUD de escolha |
| Grips | ⚠️ | só Line/Polyline/Circle, **1 grip**, **seleção única**; sem grip-menu, sem Arc/Ellipse/Spline/Dim/Text |

**Ausentes**: ARRAYPATH, arrays associativos/ARRAYEDIT, ALIGN-3D, REVERSE, OVERKILL, BLEND, PEDIT/SPLINEDIT, OOPS, DIVIDE/MEASURE com inserção de bloco, toda edição 3D, SETBYLAYER/NCOPY/COPYBASE.

**Achado-chave (RESOLVIDO em 2026-06-30)**: era "Trim/Extend/Fillet/Break/Join/Lengthen só operavam sobre `Line`". **P1 #1 fechou isso**: Trim/Break → Line/Circle/Arc/Polyline; Extend/Lengthen/Fillet → Line/Arc/Circle; Join → Line/Polyline; + window-trim e Ctrl+Z. (Chamfer é só linha-linha no AutoCAD — não é lacuna.)

---

## 3. ANOTAÇÃO

| Recurso | Status | Falta vs AutoCAD |
|---|---|---|
| Texto / "MText" | ⚠️ | **monolinha de fato** (`strokeText` não trata `\n`); só alinhamento esquerda; sem formatação rica |
| Cota Linear | ✅ | só H/V por eixo dominante; sem rotacionada, override, tolerância |
| Cota Alinhada | ✅ | sem override/tolerância (bom tratamento de leitura) |
| Cota Raio / Diâmetro | ✅ | não medem a entidade círculo automaticamente; sem jogged |
| Cota Angular | ⚠️ | hitTest aproximado; só vértice+2 pontos |
| Cota Contínua / Linha-base | ⚠️ | **só encadeiam LINEAR**; passo de baseline fixo (3·altura) |
| Estilo de Cota | ⚠️ | **não nomeado/persistido** (estado global); seta só "V"; sem DIMSCALE, tolerâncias, unidades alt. |
| Hachura — fronteira | ✅* | pick de **qualquer área fechada** (polilinha, círculo, elipse, polígono — contorno extraído do outline). *Falta*: detecção automática de contorno/ilhas por ponto interno |
| Hachura Lines/ANSI31/ANSI37/Grid | ⚠️ | **aproximações** por famílias de linha (não os .pat reais) |
| Hachura SOLID | ✅ | ear-clipping; furos não vazam no sólido; sem gradiente |
| Hachura associativa | ❌ | não atualiza ao editar a fronteira; sem HATCHEDIT |
| Leader | ✅ | texto definido só no início; monolinha; sem landing/estilo |
| Multileader | ⚠️ | UI cria **1 chamada** (classe suporta N); sem blocos/estilo |
| Nuvem de Revisão | ⚠️ | só retângulo (UI); `revisionCloudFromPath` existe mas não exposta |

**Ausentes**: MTEXT rico, TABLE, FIELD, STYLE (estilos de texto, fontes SHX/TTF), WIPEOUT, estilos de cota nomeados, DIMORDINATE/DIMJOGGED/DIMARC/QDIM, TOLERANCE (GD&T), MLEADERSTYLE, padrões .pat reais + gradientes, REVCLOUD por objeto/freehand.

---

## 4. CONSULTA / INQUIRY

| Recurso | Status | Falta vs AutoCAD |
|---|---|---|
| Consultar (pick entidade) | ⚠️ | comprimento: Line/Circle/Arc/Polyline; área: só Circle e Polyline fechada; não lista coords/camada/cor |
| Área/Perímetro/Comprimento | ⚠️ | sem área por pontos clicados; sem add/subtract; Hatch/Spline/Ellipse = 0 |
| DIST | ⚠️ | **2D** (ignora dZ); sem distância acumulada |
| ID Point | ❌ | ausente |

**Ausentes**: ID, MASSPROP, MEASUREGEOM com add/subtract, distância 3D.

---

## 5. PRECISÃO / ENTRADA / OSNAP

**OSNAP** (8 tipos): Endpoint ✅, Midpoint ✅, Center ✅, Quadrant ✅, Intersection ✅, Nearest ⚠️ (só fallback), Perpendicular ⚠️ (só Line/Circle + base), Tangent ⚠️ (só Circle + base).
✅ **Ligar/desligar por tipo**: menu no botão OSNAP (máscara `snapBit` passada ao `SnapEngine::resolve`). **Faltam**: Node, Insertion, Geometric Center, Extension, Parallel, Apparent Intersection, Mid-Between-2-Points.

| Recurso | Status | Falta vs AutoCAD |
|---|---|---|
| Ortho | ✅ | — |
| Polar Tracking | ✅ | **F10** + botão POLAR com menu de incrementos (5/10/15/18/22.5/30/45/90°); gruda no raio dentro de 4° com guia tracejada; exclui Ortho |
| OTRACK | ⚠️ | só guias **ortogonais** (H/V); até 2 refs; sem cruzamento de 2 guias |
| Entrada dinâmica (HUD) | ✅ | só display; sem Tab entre comp./ângulo; ângulo sempre absoluto |
| Distância direta | ✅ | (inclui ao longo da guia OTRACK) |
| Coords abs `x,y` / rel `@dx,dy` / polar `@d<a` | ✅ | falta polar **absoluta** `d<a` e angle-override `<a` |
| Autocomplete de comandos | ⚠️ | lista hardcoded ~26 (faltam DIST/LEADER/ALIGN etc.) |
| Grips | ⚠️ | Line/Polyline/Circle, 1 grip, seleção única; sem grip-menu/multi |
| Window/Crossing + Shift soma | ✅ | sem Fence/WPolygon/CPolygon |
| Esc em estágios | ✅ | — |
| Repetir último comando | ✅ | sem menu de recentes (botão-direito contextual) |
| Mira curta + pickbox + flyout-arrow discreta | ✅ | — |
| F3/F7/F8/F9/F10/F11 | ✅* | **F3=OSNAP, F9=Grid Snap** (separados), F7 grade, F8 ortho, F10 polar, F11 otrack; botão SNAP faz **grid snap** incremental. *Falta*: F12 (dyn input) |

---

## 6. CAMADAS

Cor ✅, Corrente ✅, Linetype ✅, Lineweight ✅ (render via triângulos), Lock ✅ (+ **esmaece** a travada), **ON/OFF (lâmpada) e FREEZE (floco) distintos** ✅, **Isolar/Mostrar todas** ✅.
**Faltam**: VP-freeze, filtros/busca, transparência, plot on/off, Layer States, renomear/excluir/purge, índice ACI.

---

## 7. PROPRIEDADES / UI

Edição inline (camada/cor/tipo/espessura) ✅, multi-seleção ✅, undo agrupado (MacroCmd) ✅, edição numérica ⚠️ (só Line/Circle/Arc), ribbon de ícones + flyouts ✅, sincronização do botão ativo ✅, tema Aurora ✅ (comentários de cor desatualizados dizem "âmbar"), status bar ✅, zoom ⚠️ (sem Window/Previous/Realtime; pan só botão-meio).
**Faltam**: aba/gerenciador de Estilos, Quick Properties, Tool Palettes, edição de Text/Dim/Hatch/Polyline no painel, ribbon contextual real, Layouts/Paper Space.

---

## 8. I/O

| Recurso | Status | Falta vs AutoCAD |
|---|---|---|
| Exportar DXF | ✅* | LINE/CIRCLE/ARC/LWPOLYLINE(bulge)/TEXT/ELLIPSE + **DIMENSION e HATCH** (round-trip CadCore). *Falta*: HEADER/BLOCKS, true color (ACI aprox.); cota/hachura usam códigos internos (não abrem ricos no AutoCAD) |
| Importar DXF | ✅* | LINE/CIRCLE/ARC/LWPOLYLINE/TEXT + **ELLIPSE** (assimetria fechada) + **DIMENSION/HATCH**. *Falta*: Spline, Point, Block |
| Exportar PDF | ✅* | A4 paisagem fit-to-page; **plota fill/SOLID** (áreas preenchidas) e **lineweight** (mm efetivo ByLayer → largura da caneta). *Falta*: escala/papel/janela/layouts/CTB |

**Ausentes**: DWG nativo, DXF de Dimension/Hatch/Blocks/Spline/MText, true color DXF, Layouts/Paper Space, plot styles, plotagem de preenchimento.

---

## Roadmap priorizado (impacto × esforço)

**P1 — destrava o uso real (alto impacto) — ✅ CONCLUÍDA (2026-06-30)**
1. ✅ **Trim/Extend/Fillet/Break/Join/Lengthen para Arc/Circle/Polyline** (era só Line) + Ctrl+Z.
2. ✅ **Entrada numérica universal** (Círculo/Polígono por raio, Retângulo+variantes por `L,A`, Elipse por eixo, Texto por altura, Arco SER/SEA, coords/distância direta).
3. ✅ **DXF: Dimension e Hatch (round-trip)** + importar Ellipse (assimetria fechada).
4. ✅ **PDF: plota fill/SOLID e lineweight** (mm efetivo). Bônus: **Hachura em qualquer área fechada** (círculo/elipse/polígono, não só polilinha).

**P2 — produtividade e fidelidade — ✅ CONCLUÍDA (2026-06-30)**
5. ✅ **Trim/Extend por Fence** (tecla F); múltiplos contornos (já eram arestas de corte).
6. ✅ **Copy múltiplo** + Polyline modo-arco com toggle A/L/C (já existia). *Resta*: Halfwidth da polilinha.
7. ✅ **Estilos nomeados** — **cota** e **texto** (`StyleTable` no kernel + gerenciadores: criar/editar/excluir/tornar-corrente). Linha = linetypes (já existiam, por camada/entidade).
8. ✅ **Polar tracking** (F10) + **OSNAP por tipo** (menu) + **F3≠F9 / Grid Snap** (F9). *Resta*: snaps Node/Extension/etc.
9. ✅ **Camada ON vs FREEZE distintos** + isolar (LAYISO) / mostrar todas + esmaecer travada.

**P3 — recursos de classe ausente** _(grande leva entregue em 2026-06-30 via 4 agentes paralelos + integração)_
10. ✅ **MTEXT real** — multilinha (`\n`) + justificação (Left/Center/Right); diálogo de texto agora multilinha.
11. ✅ **Array Path** + ✅ **PEDIT** (mover/inserir/remover vértice de polilinha: tecla **I**, **Delete** sobre vértice, menu de contexto do grip) + ✅ **SPLINEDIT** (idem p/ pontos de controle de spline). ⚠️ **Arrays associativos**: arquitetural (entidade-array com params + regen) — deferido.
12. ✅ **Booleanas** + **BOUNDARY por ponto interno** + **REGION** + **WIPEOUT** (✅ máscara real: cor de fundo, desenhada **pós-linhas**) + **TABLE** + ✅ **GRADIENT real** (cor por triângulo interpolada em bandas; diálogo com 2 cores).
13. ✅ **Contorno por ponto interno** + **ilhas/furos na hachura SOLID** (varredura even-odd, área exata). ⚠️ **Hachura associativa**: arquitetural (link à fronteira + regen) — deferido.

**Frente arquitetural deferida** (precisa subsistema de dependências/regeneração): **associatividade** de hachura/arrays e **multi-grip** verdadeiro (Stretch por janela já move vários). Todo o resto da P3 entregue.
14. ✅ **Multileader UI multi-chamada** — cada traço+Enter acumula uma chamada; Enter vazio conclui 1 MLeader com todas.

---

# Atualização 2026-07-01 — 3 grandes entregues + Auditoria de Performance

## Delta de cobertura (lacunas que eram gritantes, agora fechadas)
- ✅ **Paper Space / Pranchas** — abas Modelo/Prancha, viewports do Modelo em escala (`glScissor` + MVP por viewport), selo paramétrico A4-A0, **plotagem PDF em escala verdadeira**. (`core/layout/Layout.hpp`, `ViewportWidget::paintPaper`, `MainWindow` menu Prancha.)
- ✅ **Blocos** — biblioteca nomeada (`BlockTable` no `DrawingManager`), **INSERT** com escala/rotação, **explodir bloco**. (`BlockRef::fromDefinition`, `MakeBlockCmd` registra a def.)
- ✅ **DXF robustecido** — round-trip de **BLOCK/INSERT** + seção **HEADER** (`$ACADVER`/`$EXTMIN`/`$EXTMAX`).
- ✅ **Ganhos p/ arquitetura** — ferramenta **AREA** por pontos, **hachuras de material** (Tijolo/Concreto/Madeira/Areia), **cota com ponta arquitetônica** (tique).
- 🐞 Fix: "X" no canvas ativava XLINE em vez de EXPLODE (autocomplete ignorava aliases das ações) — corrigido.

**Veredito de cobertura:** ZenCAD ≈ **AutoCAD LT no 2D**. Faltas relevantes que restam são de nível "AutoCAD pleno / produtividade": associatividade (cota/hachura), atributos de bloco + blocos dinâmicos + XREF, UCS, escala anotativa, QSELECT/filtros/grupos, LTSCALE, e interop DWG (inviável sem ODA/libredwg).

## Auditoria de Performance / Fluidez (2026-07-01, agente lendo o pipeline real)

**Fluidez por escala (honesto):**
- **~100 entidades** (uso-alvo de estudante): **excelente**, sub-ms, parece CAD nativo.
- **~5k**: pan/zoom/pick ótimos; **edição** com micro-stutter (cada clique re-tessela o doc todo); seleção grande arrastada pesa.
- **~50k**: pan/zoom/pick ok (Quadtree + VBO em cache seguram); **cada edição trava** e seleções grandes deixam o mouse choppy. É um **muro de throughput de EDIÇÃO**, não de render.

**Bem arquitetado (não mexer):** Quadtree usado de verdade em pick/snap/box-select; geometria cometida num **único VBO em cache**; Paper Space **reusa** o VBO do modelo (parte mais bem-feita); comandos/undo sem cópia do documento (mementos só das entidades afetadas); DXF I/O linear/streaming; `ensureLayer` é O(1) (unordered_map), não scan.

**Gargalos (o "muro de edição"):**
1. 🔴 **Regen total por edição** — `uploadFromDoc()` re-tessela TODAS as entidades e re-sobe o VBO inteiro a cada clique/edição (via `rebuild()`, ~27 pontos). Sem dirty-tracking. `ViewportWidget.cpp:682-834`.
2. 🟠 **Re-emit por-frame no `paintGL`** — seleção re-emitida a cada frame (O(seleção) por mouse-move, `:268-291`); linhas grossas re-expandidas em triângulos todo frame (`:241-257`); overlays `drawDynamic/drawFilled` re-alocam o VBO por draw (`:487-510`).
3. 🟠 **`BlockRef` clona todos os membros a cada `emitTo`/`hitTest`/`boundingBox`** (`BlockRef.cpp:40-67`) → multiplicador O(instâncias × membros) por regen/reindex.

**Backlog priorizado de otimização (impacto × esforço):**
| # | Otimização | Impacto | Esforço |
|---|---|---|---|
| 1 | **Dirty-tracking no upload** — re-emitir só entidades alteradas + patch do VBO | 🔴 altíssimo | Alto (refactor) |
| 2 | **Cachear seleção/grips/linhas-grossas entre frames** (rebuild só ao mudar seleção/zoom) | 🟠 alto | Médio |
| 3 | **`BlockRef` sem clone** — transformar vértices, não clonar membros | 🟠 alto (com blocos) | Médio |
| 4 | **Reusar VBO dinâmico** (`glBufferSubData` + scratch) em vez de `allocate()` por draw | 🟡 médio | Baixo |
| 5 | **`std::map`→`unordered_map`** no upload + **limitar pilha de undo** (`CommandStack` é ilimitada) | 🟢 pequeno/seguro | Baixo |

**Faltas de perf "nível AutoCAD":** regen incremental/dirty-regions; cache de tessela por-entidade; **culling de render pela janela visível** (Quadtree usado p/ pick, mas NÃO para pular geometria fora da tela ao desenhar — em 100k desenha off-screen todo frame); LOD (arcos/splines em resolução cheia mesmo com zoom-out); buffers GPU persistentes; undo limitado; espessura de linha via geometry shader.

**Decisão (2026-07-01):** para o uso-alvo (plantas/cortes de estudante, centenas–poucos milhares de entidades) o app está **fluido de sobra**; o gargalo só morde em desenhos grandes. Otimizações **registradas como backlog, NÃO implementadas agora** (a pedido). #4/#5 são quick wins seguros quando quiser; #2/#3 dão o maior ganho perceptível; #1 é o refactor grande de escala.

## Atualização 2026-07-01 (mais tarde) — Backlog EXECUTADO + features novas

**Otimizações — TODAS as 5 implementadas** (smoke 100%, verificado ao vivo):
1. ✅ **Dirty-tracking pragmático** — `DrawingManager` ganhou conjunto de ids sujos (anotado em add/remove/replace/reinsert/markDirty) + `fullDirty`; `uploadFromDoc` mantém **cache de tesselação por entidade** (`m_emitCache`: EntityId→RenderBatch cru) e re-emite SÓ os alterados. Cor/linetype/visibilidade seguem resolvidos por rebuild (fora do cache) → mudanças de camada/tema corretas sem invalidar. A parte cara (tesselar arcos/splines/TEXTO) virou incremental.
2. ✅ **Cache de seleção + linhas grossas entre frames** — destaque re-emitido só quando o CONJUNTO selecionado muda ou pós-rebuild; expansão de linhas grossas em triângulos cacheada por escala de câmera (pan não invalida; zoom sim).
3. ✅ **BlockRef sem clone** em `emitTo` (emite membros em batch local e transforma os VÉRTICES) e `boundingBox` (transforma cantos dos bboxes locais). `hitTest` mantém clone (só roda no clique).
4. ✅ **VBO dinâmico reusado** — `uploadDynamic()` com `write()` (glBufferSubData) quando cabe; realoca só ao crescer (geométrico).
5. ✅ **unordered_map nos buckets** do upload (+ sort de chaves p/ z-order estável) e **undo LIMITADO** (`CommandStack::kMaxHistory = 256`, descarta o mais antigo).

**Features novas vs AutoCAD (2D):**
- ✅ **Atributos de bloco (ATTDEF/ATTRIB-lite)** — entidade `AttDef` (tag/prompt/padrão, header-only `core/geometry/AttDef.hpp`, renderiza a tag); `MakeBlockCmd` extrai ATTDEFs para `BlockDefinition.attdefs` (campos, não geometria fixa) e a inserção criada mostra os VALORES PADRÃO; **INSERT pergunta os valores** (diálogo com o prompt de cada campo) e cada inserção renderiza o próprio valor (`BlockRef::attValues`); explodir materializa o valor como MTEXT. Comando ATTDEF/ATT + menu Modificar→Bloco. *Verificado ao vivo: bloco ROTULO com campo AMBIENTE → inserções "SALA" e "COZINHA".*
- ✅ **QSELECT** (seleção rápida) — filtro por TIPO (lista os tipos presentes no desenho) e/ou CAMADA; `ToolController::selectByFilter`; comando QSELECT/QSE + menu Editar. *Verificado: selecionou exatamente os 2 INSERTs.*
- ✅ **LTSCALE** — escala global dos tipos de linha aplicada na expansão de tracejado; comando LTSCALE/LTS + menu Anotação→Estilos e unidades. *Verificado: centerlines com traços 3× maiores.*

**Segue deferido (arquitetural/inviável):** associatividade de cota/hachura e escala anotativa (exigem subsistema de dependências/regen), UCS, XREF/blocos dinâmicos, DWG nativo, Fields, culling de render por janela visível + LOD (próximos passos de perf se um dia mirar 100k+).

## Atualização 2026-07-02 — Leva pós-varredura (refino)

- ✅ **ATTEDIT** — edita os valores de atributo de um INSERT existente (comando ATTEDIT/ATE, menu Modificar→Bloco, ou **duplo clique no bloco**); prompt de cada campo vem da definição; undo via ReplaceCmd.
- ✅ **Redefinição de bloco** — BLOCK com nome existente pergunta e redefine: a definição é sobrescrita e **todas as inserções existentes atualizam** (transform e valores de atributo preservados por tag; undo/redo simétricos no MakeBlockCmd).
- ✅ **OSNAP Node / Extension / Parallel** — Node = entidade POINT (glifo ⊗, ligado por padrão); Extension = prolongamento colinear de Line/segmentos retos de Polyline (deferred, sonda ampliada 40×tol, opt-in); Parallel = paralela por um segmento vizinho a partir do ponto-base (deferred, opt-in). *Ainda faltam*: Insertion, Geometric Center, Apparent Intersection, M2P.
- ✅ **ZOOM/PAN por comando** — `Z`/`ZOOM` = Zoom Janela, `ZE`/`ZA` = Extensão, `ZP` = Anterior (histórico de 32 vistas), `P`/`PAN` = mão. (As operações já existiam no viewport; agora expostas na CLI + autocomplete.)
- ✅ **Camadas: renomear / excluir / purge** — menu de contexto na linha do painel (renomear remapeia entidades E membros de blocos; excluir em uso oferece mover p/ 0) + botão "Limpar não usadas"; camada 0 intocável; kernel `renameLayer/removeLayer/purgeLayers/layerUsage`.
- ℹ️ Itens da auditoria já fechados antes (constatado agora): autocomplete JÁ era dinâmico (itera a CommandTable) e DIMRADIUS/DIMDIAMETER JÁ medem o círculo clicado (`dimCirclePick`). Zoom Window/Previous/realtime-pan já existiam no viewport.
- Smoke: +13 checks (Node/Extension/Parallel/classes, camadas rename/remove/purge, redefinição com undo/redo) — 100%.

## Atualização 2026-07-02 (tarde) — As 3 GRANDES (antes deferidas)

- ✅ **COTAS ASSOCIATIVAS** (v1 pragmática, sessão): `DimAnchor` (entidade + qual ponto: Start/End/Center/Vertex/Node/OnCurve) em cada cota; o `SnapResult` agora carrega a ENTIDADE que forneceu o ponto e o clique de cota registra a âncora automaticamente (Linear/Alinhada via OSNAP; Raio/Diâmetro via o círculo clicado). `DrawingManager::regenAssociativeDims()` roda após execute/undo/redo: esticou a linha → a cota re-mede; mudou o raio → a cota de raio segue. *Limite honesto*: âncoras valem para a sessão (EntityIds não sobrevivem a salvar/abrir — cota vira estática ao reabrir); linha de cota segue a translação média dos pontos medidos.
- ✅ **TEXTO TTF** — hook plugável `core/text/TtfFont.hpp` (tesselador + medidor; kernel segue headless) + provider Qt (`app/TtfTextProvider.hpp`, QPainterPath→contornos, escala por capHeight, multiline/justificação/rotação preservados). `TextStyle` ganhou **fonte** (combo no gerenciador de Estilos de Texto; item 0 = traços padrão CAD); `MText` ganhou campo `font` por entidade (persistido no .zencad; compat. retro). Cota/selo continuam com a fonte de traços.
- ✅ **GRIPS DE VIEWPORT + MSPACE** (Paper Space): clicar num viewport na prancha SELECIONA (moldura latão + 4 grips de canto); arrastar pelo corpo MOVE, grip REDIMENSIONA (mín. 5mm); **Del** exclui; botão direito → "Escala do viewport..." (1:N verdadeiro pela unidade) / "Excluir". **Duplo clique dentro = MSPACE** (moldura laranja dupla): roda = zoom da vista (mantém o ponto do modelo sob o cursor; rótulo 1:N acompanha), botão do meio = pan da vista; duplo clique fora ou Esc volta ao papel. *Sem undo nas edições de prancha (consistente com o Config. de Página).*
- Smoke: +3 checks (cota segue esticão com undo; cota de raio segue raio novo) — 100%.
</content>
