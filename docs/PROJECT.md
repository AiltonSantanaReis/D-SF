# D-SF — Projeto, Governança e Critérios de Fechamento

## 1. Propósito

Este arquivo é a fonte canônica para missão, escopo, antiobjetivos, método de pesquisa, regras de evidência/promoção, roadmap ativo e critérios de fechamento do D-SF.

Resultados detalhados pertencem ao `RESEARCH_LEDGER.md`; evidência reproduzível e fingerprints pertencem ao `VERIFICATION.md`; contratos e arquitetura ativa pertencem ao `ARCHITECTURE.md`; o `README.md` resume o estado navegável.

---

## 2. Intenção de origem

D-SF não nasceu como tentativa de reproduzir Unreal, Unity ou qualquer engine existente. A pergunta de origem é se uma arquitetura de engine pode ser redesenhada a partir de invariantes mais fundamentais, aceitando que ideias atraentes sejam rejeitadas quando os dados não as sustentarem.

Princípios de origem:

1. começar pelo núcleo, não pelo editor;
2. não tornar renderer, física, mesh ou backend autoridade sobre o mundo;
3. testar hipóteses antes de promover contratos;
4. preservar oracle/reference paths para conferir otimizações;
5. avaliar o sistema inteiro, não apenas microbenchmarks isolados;
6. registrar resultados negativos e falsificações;
7. não escolher tecnologia por popularidade, novidade ou preferência;
8. não transformar estimativa em evidência.

Formulação central:

> **O mundo não deve depender de polígonos, renderer, física ou processador específico. Essas tecnologias devem ser representações ou views derivadas de um estado autoritativo mais fundamental.**

---

## 3. Missão

Descobrir, por experimentos reproduzíveis, uma arquitetura que maximize capacidade de mundo, qualidade, previsibilidade, auditabilidade, paralelização e liberdade de representação/backend por unidade de tempo, memória, largura de banda e complexidade.

Não existe uma métrica única chamada eficiência. Toda afirmação deve identificar workload, ambiente, custo observado e baseline relevante.

Uma forma conceitual útil é:

```text
(capacity + quality + scale)
----------------------------
(time + memory + bandwidth + complexity)
```

Ela não é uma métrica de benchmark pronta; serve para impedir otimizações locais que pioram o sistema total.

---

## 4. Arquitetura investigada

```text
Authoritative World State
        |
        +--> Transactions / Change Journal
        +--> Execution views
        +--> Shared Spatial Snapshot
        +--> GeometrySet / representations
        +--> Device Geometry Packages
        +--> DeviceWorkPacket graph
                 |
                 +--> backend-neutral translation
                          |
                          +--> real backends / hardware
```

Separação ativa:

```text
World != Geometry != Device Package != Device Work != Backend Command Model
```

`VisualGeometry != PhysicalGeometry` continua regra arquitetural; ambas são views derivadas quando aplicável.

---

## 5. Hipóteses de alto nível

### H-A — Representation independence — `PARTIALLY VERIFIED`

R4 demonstrou duas famílias concretas sob contratos comuns: Sparse Implicit/SDF e Clustered Triangle, com `GeometrySet`, capability/revision/error filtering e seleção explícita. Isso suporta a hipótese de independência de representação, mas ainda não prova um mundo de produção com todas as famílias planejadas.

### H-B — Execution independence — `PARTIALLY VERIFIED`

R2 demonstrou dependency waves e worker execution em CPU/reference. R5C/R5D separaram `DeviceWorkPacket` da tradução de backend. R5E-HW03/HW04 demonstraram Direct e Indirect compute reais no Vulkan. A integração automática CPU/GPU como um único scheduler heterogêneo ainda não foi demonstrada.

### H-C — Transaction-derived history — `PARTIALLY VERIFIED`

Replay, rollback, hashing e journal foram demonstrados no escopo de referência. Networking, editor undo e persistência incremental de produção permanecem abertos.

### H-D — Adaptive representation by error/budget — `PARTIALLY VERIFIED`

R4D demonstrou seleção por hard constraints + objetivo explícito + fronteira de Pareto. R4E/R4F adicionaram telemetria e exploração controlada. Um error-budget global integrado a renderer/physics/streaming ainda não foi demonstrado.

### H-E — Heterogeneous world — `PARTIALLY VERIFIED`

Representações distintas coexistem sob a mesma camada geométrica e podem ser selecionadas sem transformar uma delas em autoridade. O demonstrador de mundo heterogêneo integrado permanece R6.

---

## 6. Método permanente

```text
Theory / Hypothesis
        -> Reference / Oracle
        -> Controlled experiment
        -> Data
        -> Conclusion
        -> Specification / promotion or rejection
```

Regras:

- **AI proposes; Kernel validates.**
- correção antes de performance;
- otimização importante deve possuir oracle quando semanticamente comparável;
- falso resultado de hardware é proibido;
- benchmark sintético deve ser nomeado pelo trabalho que realmente executa;
- falha experimental é evidência e deve ser preservada;
- implementation detail não vira contrato apenas porque venceu um teste;
- nova tecnologia não recebe preferência automática por ser nova.

### Classes de evidência

- `REFERENCE RESULT` — semântica/medição em caminho CPU/reference.
- `HARDWARE RESULT` — realmente executado no hardware alvo identificado.
- `PARTIAL` — evidência útil com limitação explícita.
- `FALSIFIED` — hipótese/caminho rejeitado no escopo testado.
- `NOT MEASURED` — nenhuma afirmação de custo permitida.

---

## 7. Estados de promoção

`IDEA -> HYPOTHESIS -> EXPERIMENTAL -> VERIFIED -> FOUNDATIONAL`

`VERIFIED` sempre deve declarar o escopo. `FOUNDATIONAL` exige estabilidade significativamente maior: contrato especificado, regressão permanente, adversarial tests, múltiplos compiladores quando aplicável, plataforma/hardware compatível com a alegação, limitações conhecidas e decisão formal.

**Até R5E-HW04 nenhum contrato do D-SF está classificado como `FOUNDATIONAL`.**

---

## 8. Governança documental

| Informação | Arquivo canônico |
|---|---|
| Missão, escopo, regras, roadmap, fechamento | `docs/PROJECT.md` |
| Arquitetura ativa e contratos | `docs/ARCHITECTURE.md` |
| História, decisões, falsificações | `docs/RESEARCH_LEDGER.md` |
| Evidência, hashes, ambientes, comandos | `docs/VERIFICATION.md` |
| Navegação e estado resumido | `README.md` |

Relatórios de fase (`R3_CPU_CLOSEOUT.md`, `R4_CPU_CLOSEOUT.md`, `R5_DEVICE_EXECUTION_CLOSEOUT.md`, `R5E_HWxx_CLOSEOUT.md`, etc.) podem aprofundar a evidência, mas não substituem as fontes canônicas.

---

## 9. Política de repositório

`AiltonSantanaReis/D-SF` é o registro público oficial dos estados consolidados do projeto, **não o laboratório ativo**.

Política atual:

- desenvolvimento e experimentação continuam localmente/sandbox;
- GitHub recebe snapshots somente depois que hipóteses, contratos ou gates relevantes foram testados e o estado ficou coerente;
- `main` deve representar apenas snapshots verificados/consolidados;
- resultados locais não são considerados publicados até entrarem no snapshot correspondente;
- hardware evidence deve identificar configuração e escopo; logs brutos podem permanecer fora do repositório quando o closeout registra hashes e resultados necessários;
- caminhos falhos de runner/toolchain não são promovidos como resultados negativos do kernel/backend, mas podem ser mencionados quando alteram a metodologia do gate.

---

## 10. Roadmap e estado verificado

### R0 — Minimal Authoritative World — `VERIFIED / REFERENCE`

Estado autoritativo mínimo, identidade, transações validadas e integração de referência.

### R1 — Change Journal / Hash / Replay / Rollback — `VERIFIED / REFERENCE`

Journal forward, SHA-256 canônico, save/load, replay, rollback e divergence guard.

### R2 — Dependency Execution Graph — `VERIFIED correctness / PARTIAL performance`

Resource access declarations, waves determinísticas, worker pool e merge transacional.

### R2.1 — Hybrid Transaction Patches — `VERIFIED / REFERENCE`

Scalar lane + typed range lane sob a mesma autoridade transacional. Dense ranges e sparse scalars coexistem; uma granularidade universal foi rejeitada.

### R3 / R3.1 / R3.2 — Spatial Kernel — `VERIFIED / CPU REFERENCE`

Fechado com Shared Spatial Snapshot, WideBVH8/MortonBVH8 derived views, cost-aware policy e budgeted/cooperative maintenance. Fixed region partition e outras alternativas que perderam nos testes não foram promovidas.

Limite explícito: rebuild Morton de 1M objetos por frame não atende 60 Hz na CPU da sandbox; não foi tratado como problema resolvido por estimativa.

### R4A–R4F — Geometry Kernel — `VERIFIED / CPU REFERENCE`

- Representation Contract;
- Sparse Implicit Geometry;
- Clustered Triangle Surface;
- heterogeneous selection por constraints/objective/Pareto;
- runtime telemetry robusta;
- safe exploration/routing.

Sparse e clustered possuem vantagens diferentes; nenhuma representação foi promovida como universal.

### R5A — Device Residency Contract — `VERIFIED / REFERENCE`

Budget, LRU de unpinned, pinning, generational handles e rollback de upload no reference backend.

### R5B — Geometry Device Packages — `VERIFIED / REFERENCE`

Canonical `RepresentationArchive`, package fingerprints e atomic `ensure_group`.

### R5C — Device Work Contract — `VERIFIED / CPU REFERENCE`

`DeviceWorkPacket`, resource-centric hazard tracking, deterministic waves e launch-control resource explícito.

### R5D — Backend Capability & Translation — `VERIFIED / CPU REFERENCE`

Backend-neutral capability lattice, Direct/Indirect/DeviceGenerated semantics, descriptor table/use split, barrier lowering e semantic/backend digests.

### R5E — Real Backend Bring-up — `IN PROGRESS / HARDWARE`

Hardware alvo atual: NVIDIA GeForce RTX 3070 Ti, driver 610.47, Vulkan device API 1.4.341.

- **HW01 — VERIFIED:** hardware/runtime fingerprint; DGC, descriptor heap/buffer, synchronization2, BDA e outras capabilities confirmadas por feature bits reais.
- **HW02 — VERIFIED:** `VkDevice`, real `VkDeviceMemory`, 16 MiB HOST -> DEVICE_LOCAL -> HOST roundtrip; byte-exact; validation clean.
- **HW03 — VERIFIED:** real Direct compute, 1,048,576 `uint32`, exact CPU oracle, validation clean.
- **HW04 — VERIFIED:** real `vkCmdDispatchIndirect` com control buffer device-resident `{4096,1,1}`, mesmo exact CPU oracle, validation clean.
- **HW05 — NEXT:** GPU timestamps e caracterização controlada Direct vs Indirect. Performance ainda não foi afirmada.

Depois de timestamps/sync characterization, DGC e modelos modernos de descriptor serão testados somente como candidatos, não como escolhas predeterminadas.

### R6 — Integrated Heterogeneous Demonstrator — `PLANNED / NOT VERIFIED`

Integrar World + Execution + Spatial + Geometry + Device em cenário reproduzível e comparar benefícios/custos de ponta a ponta.

---

## 11. Critério de fechamento de fase

Uma fase fecha quando, conforme aplicável:

1. hipótese é falsificável;
2. oracle/reference é definido;
3. candidatos usam contrato comparável;
4. correctness tests passam;
5. adversarial/negative tests relevantes passam;
6. ambiente/resultados/hashes são registrados;
7. limitações e non-claims são explícitos;
8. decisão e razão são registradas;
9. documentação canônica é atualizada;
10. snapshot limpo reconstrói a regressão relevante.

“Funcionou uma vez” não fecha uma fase.

---

## 12. Research Closure Gate — RC-1

O programa inicial só entra em productization depois de evidência de ponta a ponta para:

1. World authority;
2. scalable history;
3. execution policy;
4. spatial policy;
5. ao menos duas geometry representations sob identidade comum;
6. derived renderer/physics views sem autoridade sobre World;
7. pelo menos um backend GPU medido em hardware real;
8. requisitos multiplataforma compatíveis com as alegações;
9. integrated demonstrator;
10. comparative baselines;
11. auditabilidade completa;
12. nenhum critical unknown mascarado como `VERIFIED`/`FOUNDATIONAL`.

R5E já começou a satisfazer o item de hardware real, mas RC-1 **não está fechado**.

---

## 13. Antiobjetivos permanentes

- substituir triângulos apenas por novidade;
- tornar GPU-only uma ideologia;
- declarar SDF, voxel, Gaussian ou neural representation solução universal sem dados;
- congelar algoritmo porque venceu um único benchmark;
- esconder regressões/falsificações;
- confundir qualidade visual com estado físico correto;
- usar IA como autoridade do mundo sem validação;
- construir editor completo antes do núcleo justificar o investimento;
- produzir números de hardware não medidos;
- proliferar fontes de verdade documentais concorrentes.

---

## 14. Próxima ação autorizada

**R5E-HW05 — GPU Timestamp & Direct/Indirect Characterization.**

O próximo gate deve manter workload/shader/binding comparáveis ao HW03/HW04 e adicionar medição de timestamps GPU e custos CPU relevantes, distinguindo execução, preparação, submit e sincronização. Nenhuma conclusão de superioridade Direct/Indirect deve existir antes desses dados.
