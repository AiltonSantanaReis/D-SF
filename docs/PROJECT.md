# D-SF — Projeto, Governança e Critérios de Fechamento

> **Estado ativo:** R5E-HW04 fechado como `VERIFIED — HARDWARE RESULT`; próxima ação autorizada: **R5E-HW05 — GPU Timestamp & Direct/Indirect Characterization**.

Este documento contém primeiro a governança/roadmap **ativos** e, ao final, preserva o baseline detalhado R0–R2.1 como apêndice histórico. O apêndice não é descartado quando o roadmap avança.

---

# PARTE I — GOVERNANÇA E ROADMAP ATIVOS

## 1. Propósito

Este arquivo é a fonte canônica para missão, escopo, antiobjetivos, método de pesquisa, regras de evidência/promoção, roadmap ativo e critérios de fechamento do D-SF.

Resultados detalhados pertencem ao `RESEARCH_LEDGER.md`; evidência reproduzível e fingerprints ao `VERIFICATION.md`; contratos e arquitetura ativa ao `ARCHITECTURE.md`; o `README.md` é a porta de entrada e resumo navegável.

## 2. Intenção de origem

D-SF não nasceu como tentativa de reproduzir Unreal, Unity ou qualquer engine existente. A pergunta é se uma arquitetura de engine pode ser redesenhada a partir de invariantes mais fundamentais, aceitando que ideias atraentes sejam rejeitadas quando os dados não as sustentarem.

Princípios de origem:

1. começar pelo núcleo, não pelo editor;
2. não tornar renderer, física, mesh ou backend autoridade sobre o mundo;
3. testar hipóteses antes de promover contratos;
4. preservar oracle/reference paths;
5. avaliar o sistema inteiro, não apenas microbenchmarks;
6. registrar resultados negativos e falsificações;
7. não escolher tecnologia por popularidade, novidade ou preferência;
8. não transformar estimativa em evidência.

Formulação central:

> **O mundo não deve depender de polígonos, renderer, física ou processador específico. Essas tecnologias devem ser representações ou views derivadas de um estado autoritativo mais fundamental.**

## 3. Missão

Descobrir, por experimentos reproduzíveis, uma arquitetura que maximize capacidade de mundo, qualidade, previsibilidade, auditabilidade, paralelização e liberdade de representação/backend por unidade de tempo, memória, bandwidth e complexidade.

Lens conceitual:

```text
(capacity + quality + scale)
----------------------------
(time + memory + bandwidth + complexity)
```

Não é benchmark único; impede otimizações locais que pioram o sistema total.

## 4. Separação arquitetural ativa

```text
World != Geometry != Device Package != Device Work != Backend Command Model
```

E:

```text
VisualGeometry != PhysicalGeometry
```

## 5. Hipóteses de alto nível — estado atual

### H-A — Representation independence — `PARTIALLY VERIFIED`

R4 demonstrou Sparse Implicit/SDF e Clustered Triangle sob contratos comuns, com `GeometrySet`, capability/revision/error filtering e selection explícita. Ainda não é world production heterogêneo completo.

### H-B — Execution independence — `PARTIALLY VERIFIED`

R2 demonstrou dependency waves em CPU/reference. R5C/R5D separaram DeviceWork de backend. HW03/HW04 demonstraram Direct e Indirect compute reais. Unified heterogeneous CPU/GPU scheduler ainda não foi demonstrado.

### H-C — Transaction-derived history — `PARTIALLY VERIFIED`

Replay/rollback/hash/journal comprovados em referência. Networking, editor undo e production persistence permanecem abertos.

### H-D — Adaptive representation by error/budget — `PARTIALLY VERIFIED`

R4D–F demonstraram hard constraints + Pareto + telemetry/exploration. Global error budget integrado a renderer/physics/streaming ainda não existe.

### H-E — Heterogeneous world — `PARTIALLY VERIFIED`

Representações distintas coexistem sob Geometry layer; integrated heterogeneous demonstrator permanece R6.

## 6. Método permanente

```text
Theory / Hypothesis
        → Reference / Oracle
        → Controlled Experiment
        → Data
        → Conclusion
        → Specification / Promotion / Rejection
```

Regras:

- **AI proposes; Kernel validates.**
- correctness antes de performance;
- optimized path comparável ao oracle quando semanticamente equivalente;
- hardware claim exige hardware execution;
- benchmark nomeia o trabalho real;
- falhas são evidência;
- implementation detail não vira contrato automaticamente;
- tecnologia nova não recebe preferência por ser nova.

Classes:

- `REFERENCE RESULT`;
- `HARDWARE RESULT`;
- `PARTIAL`;
- `FALSIFIED`;
- `NOT MEASURED`.

## 7. Estados de promoção

```text
IDEA → HYPOTHESIS → EXPERIMENTAL → VERIFIED → FOUNDATIONAL
```

`VERIFIED` sempre declara escopo.

**Até R5E-HW04 nenhum contrato está `FOUNDATIONAL`.**

## 8. Governança documental

| Informação | Arquivo canônico |
|---|---|
| Missão, escopo, regras, roadmap, fechamento | `docs/PROJECT.md` |
| Arquitetura ativa e contratos | `docs/ARCHITECTURE.md` |
| História, decisões, falsificações | `docs/RESEARCH_LEDGER.md` |
| Evidência, hashes, ambientes, comandos | `docs/VERIFICATION.md` |
| Navegação e estado resumido | `README.md` |

Relatórios de closeout aprofundam uma fase, mas não substituem as fontes canônicas.

### Regra anti-regressão documental

Documentos canônicos detalhados **não podem ser substituídos por resumos destrutivos**. Resumos pertencem ao README/current-state. Histórico e evidência detalhados devem ser preservados por append/supersede ou apêndice claramente marcado.

## 9. Política de repositório

`AiltonSantanaReis/D-SF` é o registro público oficial de milestones consolidados, **não o laboratório ativo**.

- experimentação/desenvolvimento continuam localmente/sandbox;
- GitHub recebe snapshots após hipóteses/gates estarem coerentes;
- `main` representa estados consolidados;
- logs brutos podem ficar fora quando hashes/fingerprints/closeouts preservam auditabilidade;
- runner/toolchain failure não é reportado como GPU/kernel failure;
- source snapshot publicado deve corresponder ao milestone que a documentação declara.

## 10. Roadmap e estado verificado

### R0 — `VERIFIED / REFERENCE`
Minimal authoritative world, identity e transactions.

### R1 — `VERIFIED / REFERENCE`
Journal, canonical hash, replay, rollback, divergence guard.

### R2 — `VERIFIED correctness / PARTIAL performance`
Dependency execution graph, declared access, deterministic waves, persistent worker pool.

### R2.1 — `VERIFIED / REFERENCE`
Hybrid scalar/range transaction patches; universal fixed-page strategy falsificada.

### R3 / R3.1 / R3.2 — `VERIFIED / CPU REFERENCE`
Shared Spatial Snapshot, WideBVH8/MortonBVH8, cost-aware policy, budgeted maintenance. 1M mandatory Morton rebuild/frame permanece >60 Hz budget na CPU sandbox.

### R4A–R4F — `VERIFIED / CPU REFERENCE`
Representation Contract, Sparse Implicit, Clustered Triangle, Pareto selection, online telemetry e safe exploration. Nenhuma representação universal.

### R5A — `VERIFIED / REFERENCE`
Device Residency Contract.

### R5B — `VERIFIED / REFERENCE`
Geometry Device Packages + atomic residency.

### R5C — `VERIFIED / CPU REFERENCE`
DeviceWorkPacket/planner; coarse execution node.

### R5D — `VERIFIED / CPU REFERENCE`
Backend capability/translation, asymmetric launch semantics, descriptor table/use split, barriers e semantic/backend digests.

### R5E — `IN PROGRESS / HARDWARE`

Target atual:

```text
NVIDIA GeForce RTX 3070 Ti
Driver 610.47
Vulkan device API 1.4.341
```

- HW01 `VERIFIED`: real capability fingerprint;
- HW02 `VERIFIED`: real VkDevice/VkDeviceMemory + 16 MiB roundtrip byte-exact;
- HW03 `VERIFIED`: real Direct compute, exact CPU oracle;
- HW04 `VERIFIED`: real `vkCmdDispatchIndirect`, resident `{4096,1,1}` control buffer, same CPU oracle;
- HW05 `NEXT`: GPU timestamps + Direct/Indirect characterization.

### R6 — `PLANNED / NOT VERIFIED`
Integrated World + Execution + Spatial + Geometry + Device demonstrator.

## 11. Critério de fechamento de fase

Uma fase só fecha quando, conforme aplicável:

1. hipótese falsificável;
2. oracle/reference definido;
3. candidatos comparáveis;
4. correctness tests passam;
5. adversarial/negative tests passam;
6. ambiente/resultados/hashes registrados;
7. limitações/non-claims explícitos;
8. decisão e razão registradas;
9. documentação canônica atualizada sem perda histórica;
10. snapshot limpo reconstrói a regressão relevante.

“Funcionou uma vez” não fecha fase.

## 12. Research Closure Gate — RC-1

RC-1 exige, no mínimo:

1. World authority;
2. scalable history;
3. execution policy;
4. spatial policy;
5. pelo menos duas geometry representations sob identidade comum;
6. derived renderer/physics views sem autoridade;
7. pelo menos um backend GPU medido em hardware real;
8. cross-platform evidence compatível com alegações;
9. integrated demonstrator;
10. comparative baselines;
11. auditabilidade;
12. nenhum critical unknown mascarado como VERIFIED/FOUNDATIONAL.

R5E começou a satisfazer GPU evidence; RC-1 **não está fechado**.

## 13. Antiobjetivos permanentes

- substituir triangles apenas por novidade;
- GPU-only como ideologia;
- SDF/voxel/Gaussian/neural universal sem dados;
- congelar algoritmo por um benchmark;
- esconder regressões;
- confundir visual quality com physical truth;
- IA como world authority sem validation;
- editor completo antes do núcleo;
- hardware numbers não medidos;
- fontes documentais concorrentes.

## 14. Próxima ação autorizada

**R5E-HW05 — GPU Timestamp & Direct/Indirect Characterization.**

Manter input, shader, operation, element count, binding baseline, oracle e target GPU equivalentes a HW03/HW04. Adicionar GPU timestamps, repeated samples e CPU prep/record/submit/sync quando relevante. Nenhuma superioridade Direct/Indirect antes desses dados.

---

# PARTE II — BASELINE DETALHADO R0–R2.1 PRESERVADO

> O texto abaixo preserva a governança e o roadmap detalhados do snapshot anterior a R3. Estados como “R3 NEXT” são históricos e foram supersedidos pela PARTE I; permanecem para auditoria da evolução do projeto.

## A1. Propósito original detalhado

Este arquivo foi definido como fonte canônica para:

- intenção original do projeto;
- problema que o laboratório tenta resolver;
- escopo e antiobjetivos;
- método de pesquisa;
- regras de evidência e promoção;
- roadmap ativo;
- definição de “feito” para cada etapa e para o programa de pesquisa;
- governança documental e de alterações.

Resultados experimentais detalhados pertencem ao `RESEARCH_LEDGER.md` e `VERIFICATION.md`; arquitetura ativa ao `ARCHITECTURE.md`.

## A2. Intenção de origem detalhada

A intenção inicial foi:

1. começar pelo núcleo, não pelo editor ou demonstração visual;
2. dividir responsabilidades em núcleos independentes quando comprovadamente vantajoso;
3. testar teorias em laboratório antes de promover;
4. estabilizar contratos que sobrevivem aos testes, não congelar implementações cedo;
5. pensar no sistema inteiro;
6. documentar resultados/falhas/razões com qualidade de auditoria;
7. explorar arquiteturas plausíveis além do convencional;
8. nunca transformar hipótese atraente em fato sem evidência.

Formulação central já registrada:

> **O mundo não deve depender de polígonos, renderer, física ou processador específico. Essas tecnologias devem poder ser representações ou views derivadas de um estado autoritativo mais fundamental.**

## A3. Missão original detalhada

Maximizar:

- complexidade de mundo;
- qualidade perceptual;
- previsibilidade/auditabilidade;
- paralelização;
- liberdade de trocar representações/backends;

por unidade de:

- CPU/GPU time;
- memória;
- bandwidth;
- latency;
- complexidade de produção/manutenção.

Não existe uma métrica única de eficiência; toda melhoria deve declarar custo, workload e condições.

## A4. Problema arquitetural investigado

Acoplamentos tradicionais considerados:

- scene graph;
- mesh dominante;
- object direct state writes;
- fixed frame phases;
- rigid CPU-engine/GPU-renderer split;
- LOD escolhido pelo conteúdo em vez de budget/error;
- replay/rollback/network/editor undo/persistence separados.

Direção:

```text
Authoritative World State
        ↓
Derived execution / spatial / geometry views
        ↓
CPU / GPU / future accelerators
        ↓
Render / Physics / Audio / AI representations
```

## A5. Hipóteses abertas no baseline

### H-A — Representation independence
Mesh/SDF/voxel/Gaussian/neural providers coexistindo sem definir identidade.

### H-B — Execution independence
Dependency declaration permitindo placement/scheduling em CPU/GPU/future accelerators.

### H-C — Transaction-derived history
Replay/rollback/networking/editor undo/persistence compartilhando change model.

R1 só comprovava replay/rollback.

### H-D — Adaptive representation by error/budget
Escolha por budget/error em vez de fixed LOD. Não implementado até R2.1.

### H-E — Heterogeneous world
Representações diferentes por render/physics/destruction/distance sob identidade comum. Ainda não demonstrado naquele snapshot.

## A6. Princípios de pesquisa detalhados

### Referência antes de otimização
Optimized path divergiu do oracle quando deveria ser equivalente → optimized path falhou.

### Correção antes de performance

```text
correctness
→ reproducibility
→ adversarial tests
→ measurement
→ optimization
→ promotion
```

### Dados podem matar uma ideia
Nenhuma tecnologia favorita.

### Não fabricar hardware evidence
CPU benchmark ≠ GPU benchmark; estimativa ≠ medição.

### Contratos estabilizam; algoritmos continuam substituíveis
`FOUNDATIONAL` é contrato/invariante versionado, não implementação congelada.

### Falhas/regressões são resultados
Não apagar.

### Não otimizar propaganda
Entity-minimal benchmark não vira “NPC completo”.

## A7. Estados de promoção detalhados

### IDEA
Possibilidade sem proposição falsificável.

### HYPOTHESIS
Proposição com success/failure planejado.

### EXPERIMENTAL
Código/protótipo sem compatibility guarantee.

### VERIFIED
Passou verificações definidas com scope explícito.

### FOUNDATIONAL
Exige specification, oracle quando aplicável, regression/adversarial tests, múltiplos compiladores, multiplataforma quando alegado, limitations, formal ledger decision e nenhum critical open contradiction.

Até R2.1: nenhum contrato foundational.

## A8. Regra de atualização documental original

Informação comprovada somente com suporte de:

- automated reproducible test;
- benchmark com workload/ambiente;
- hash/replay;
- direct code inspection;
- build/sanitizer result;
- explicit governance decision.

Informação não comprovada marcada IDEA/HYPOTHESIS/OPEN/NOT VERIFIED/DEFERRED.

Toda decisão registra problema, alternativas, evidência, decisão, razão, limitações e condição de revisão.

Não apagar evidência contraditória: manter old ledger result, registrar novo, explicar mudança, atualizar active state.

## A9. Governança dos arquivos original

| Informação | Arquivo canônico |
|---|---|
| Missão/escopo/regras/roadmap | `docs/PROJECT.md` |
| Arquitetura ativa | `docs/ARCHITECTURE.md` |
| História/decisões | `docs/RESEARCH_LEDGER.md` |
| Evidência/hashes/ambientes | `docs/VERIFICATION.md` |
| Navegação/resumo | `README.md` |

Quando experimento promove/rejeita:

1. ledger detalhado;
2. verification;
3. architecture se mudou;
4. project se roadmap/regra mudou;
5. README stage summary.

Uma fase VERIFIED deve possuir regression tests permanentes da propriedade demonstrada.

## A10. Política de repositório original e supersessão

O baseline dizia que trabalho futuro deveria ser feito/registrado no repo e que `main` deveria conter estados coerentes/testados.

A experiência posterior refinou essa política: **o laboratório ativo é local/sandbox; o GitHub registra milestones consolidados**. Isso está formalizado na PARTE I e no ledger como decisão D-044.

## A11. Roadmap detalhado no baseline

### R0 — Minimal Authoritative World — VERIFIED

Pergunta: menor state model válido enquanto renderer/physics/backend mudam.

Comprovado:

- stable/sequential identity;
- transaction identity allocation;
- full validation before mutation;
- non-finite rejection;
- simulation advance como authoritative mutation;
- 1M lightweight baseline.

### R1 — Journal/Hash/Replay/Rollback — VERIFIED

- forward journal;
- canonical SHA-256;
- replay pristine;
- exact rollback via ephemeral undo;
- divergence protection;
- binary save/load;
- GCC/Clang hash equality x86-64 Linux.

### R2 — Dependency Execution Graph — VERIFIED correctness / PARTIAL performance

- read/read same wave;
- hazards serialize;
- cycles rejected;
- undeclared access rejected;
- worker counts same hash;
- persistent worker pool;
- patch-per-entity bottleneck deixou performance PARTIAL.

### R2.1 — Hybrid Transaction Patches — VERIFIED

- scalar + typed ranges same transaction authority;
- overlap ambiguous rejected;
- replay/rollback/save/load exact;
- fixed page 256 not universal;
- sparse can favor scalar;
- dense strongly favors ranges in tested workload;
- Execution can emit ranges.

### R3 — Spatial Kernel — era NEXT/HYPOTHESIS

Planejava grid/hash/BVH/sparse bricks/octree e workloads static/moving/teleport/range/ray/large/update/streaming/destruction. Essa etapa foi posteriormente executada.

### R4 — Geometry Kernel — era PLANNED

Planejava GeometryProvider e visual/physical separation. Posteriormente R4A–F executado.

### R5 — GPU Laboratory — era PLANNED

Planejava Vulkan/DX12 compute, resident data, indirect, patches, SDF, sparse residency, splats, timestamps. R5A-D foram reference; R5E está em hardware real.

### R6 — Heterogeneous World — permanece PLANNED

Integrar Spatial + Geometry + Execution e demonstrar representation switching sem perder identity/state.

## A12. Critério de fechamento original

1. falsifiable hypothesis;
2. oracle/reference;
3. comparable candidates;
4. correctness;
5. adversarial/negative;
6. environment/results recorded;
7. limitations;
8. decision/reason;
9. docs updated;
10. clean package/repo rebuild.

## A13. RC-1 original

1. World authority;
2. scalable history;
3. real execution policy;
4. spatial choice by data;
5. at least two geometry representations;
6. renderer/physics as derived views;
7. real GPU evidence;
8. cross-platform;
9. integrated demonstrator;
10. comparative baseline;
11. auditability;
12. no hidden critical unknown.

RC-1 não exige “vencer todas as engines”; exige demonstrar onde/por quê/sob quais workloads existe vantagem e onde não existe.

## A14. Antiobjetivos originais

- novelty-only triangle replacement;
- GPU-only ideology;
- universal SDF/voxel/Gaussian/neural claims;
- algorithm freeze por single benchmark;
- esconder regressão;
- visual quality = physical correctness;
- generative AI como authority sem deterministic validation;
- full editor antes do core;
- non-measured hardware numbers;
- competing state documents.

## A15. Próxima ação no baseline

Era **R3 Spatial Kernel Bake-Off** com common interface, oracle/query validation e comparable benchmarks antes de qualquer promotion. Essa ação foi concluída e supersedida pelo roadmap ativo da PARTE I.
