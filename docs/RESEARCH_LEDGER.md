# D-SF — Research Ledger

## 0. Função e regras deste ledger

Este é o registro cronológico canônico da pesquisa D-SF. Ele preserva a origem do projeto, hipóteses, decisões, evidências, falsificações e mudanças de roadmap.

O ledger não é apagado quando a arquitetura muda. Novas entradas **supersedem** decisões antigas quando necessário, mas não reescrevem silenciosamente o passado.

### Origem permitida para o baseline inicial

O baseline R0–R2.1 foi reconstruído usando exclusivamente:

1. mensagens desta conversa;
2. código e relatórios de sandbox produzidos nesta conversa;
3. resultados de compilação/testes/benchmarks executados durante esta conversa.

Nenhum histórico de outro repositório foi usado para reconstruir aquele passado.

---

# PARTE I — ORIGEM DA PESQUISA

## L-001 — Análise inicial das tecnologias não convencionais

### Entrada de origem

O primeiro texto apresentado para análise propunha substituir ou superar pipelines tradicionais baseados em malhas/rasterização por cinco famílias:

1. motores de renderização neural — NeRF e 3D Gaussian Splatting;
2. Neural World Simulators;
3. Signed Distance Fields + raymarching;
4. Sparse Voxel Octrees;
5. arquiteturas compute-first com ECS em VRAM.

As vantagens alegadas incluíam:

- eliminar modelagem/retopologia/UV;
- inferir física, colisão e iluminação em uma única inferência neural;
- “detalhamento infinito” e booleanas “perfeitas” em SDF;
- eliminar LOD manual em SVO;
- executar ECS, física e interações totalmente em VRAM;
- manipular centenas de milhões de objetos com overhead muito baixo.

### Avaliação registrada na conversa

A conclusão não foi “as tecnologias são falsas”. A conclusão foi:

> As tecnologias são reais, porém o texto mistura pesquisa legítima com extrapolações muito mais fortes do que os resultados sustentam.

#### NeRF / 3D Gaussian Splatting

Conclusões registradas:

- NeRF é representação neural implícita real.
- 3D Gaussian Splatting não deve ser descrito como “geometria latente” genérica.
- 3DGS trabalha com primitivas gaussianas explícitas contendo posição/forma/orientação/opacidade/cor ou parâmetros equivalentes.
- differentiable rasterization é importante sobretudo para otimização/training; “renderizar via diferenciação” é descrição imprecisa.
- manipular gaussianas por transforms espaciais não equivale a “manipular objetos em espaço latente”.
- 3DGS pode reduzir muito etapas de captura/reconstrução para certos ambientes.
- 3DGS não fornece automaticamente superfície física, topologia, collider, UV, interior/exterior, rigging, destruição ou material PBR tradicional.

Decisão conceitual inicial:

> 3DGS parece mais forte como novo tipo de asset/renderer dentro de uma engine híbrida do que como substituto universal da geometria de gameplay.

#### Neural World Simulators

Conclusões registradas:

- modelos que predizem frames futuros a partir de ações são reais e pesquisa relevante;
- um modelo pode gerar dinâmica visual plausível sem possuir explicitamente `mass`, `collider`, `health`, `door_locked` etc.;
- aparência de física plausível não equivale a estado físico autoritativo;
- jogos competitivos, replay, networking e lógica de estado exigem controle/determinismo que um gerador probabilístico de frames não garante por padrão.

Decisão conceitual inicial:

> Neural world models devem ser considerados sistemas opcionais/propositivos, não autoridade do mundo, até que seja demonstrado o contrário.

#### SDF + Raymarching

Conclusões registradas:

- SDF é tecnologia real e forte para CSG, edição geométrica e destruição;
- `f(x,y,z)=d` fornece uma forma natural de representar distância à superfície;
- `union`, `intersection`, `subtract` e blends podem ser expressos diretamente;
- “detalhamento infinito” foi rejeitado como afirmação literal porque precisão numérica, resolução, VRAM, bandwidth e número de passos continuam finitos;
- “booleanas perfeitas” foi reduzido para “booleanas muito mais naturais”, pois materiais, detalhe fino, sampling e normal estimation continuam problemáticos;
- destruição por subtração de volumes foi considerada uma direção de pesquisa especialmente promissora.

Decisão conceitual inicial:

> SDF foi classificado como uma das melhores candidatas para uma engine experimental, principalmente para geometria procedural, terreno e destruição.

#### Sparse Voxel Octree

Conclusões registradas:

- SVO é estrutura real e útil para volumes esparsos/hierárquicos;
- LOD pode emergir naturalmente da hierarquia, mas streaming, nível a carregar/renderizar e transições continuam sendo decisões de sistema;
- editar density/voxel não cria automaticamente um solver de física de deformação;
- hardware RT não “entende SVO automaticamente”; aceleração ainda exige representação/intersection path adequado.

Decisão conceitual inicial:

> Sparse volumes permanecem candidatos fortes de infraestrutura, especialmente combinados com SDF, mas sem promessa de eliminar todos os problemas de LOD/física.

#### ECS / Compute-first em VRAM

Conclusões registradas:

- GPU-driven culling, LOD, compaction e indirect drawing são reais;
- grandes workloads uniformes podem se beneficiar muito da GPU;
- “eliminar a CPU” foi rejeitado;
- OS, input, filesystem, network, editor, audio, scripting e workloads branch-heavy/irregulares continuam fortes candidatos a CPU;
- “centenas de milhões de objetos” só pode ser defendido quando “objeto” é uma unidade extremamente simples; não como ator completo com IA/física/animação.

Decisão conceitual inicial:

> O alvo correto é minimizar sincronização e movimentação desnecessária CPU↔GPU, não transformar “CPU zero” em dogma.

### D-001 — Primeira direção arquitetural

**Problema:** nenhuma das cinco tecnologias vence universalmente todos os workloads.

**Alternativas:** escolher uma representação única vs engine híbrida.

**Decisão:** investigar engine híbrida com múltiplas representações nativas.

**Razão:** meshes, SDF, voxels, splats e futuros métodos possuem perfis de vantagem diferentes.

**Estado naquele momento:** `HYPOTHESIS`.

---

## L-002 — Reformulação do objetivo: não “matar polígonos”, mas desacoplar o mundo da representação

A conversa avançou para a pergunta: se fosse designado um programa de pesquisa capaz de tentar redesenhar a indústria, qual seria o ponto de partida?

A resposta registrada alterou o foco:

> Não começar pelo renderer. Começar definindo o que é o mundo e como sua verdade sobrevive à substituição de representações.

A arquitetura conceitual proposta foi:

```text
WORLD STATE
  ├── Semantic State
  └── Spatial State
          ↓
      WORLD FIELD / world description
          ↓
representation/compiler layer
  ├── Mesh
  ├── SDF
  ├── Voxel
  ├── Gaussian
  └── future representation
          ↓
derived views
  ├── Render
  ├── Physics
  └── Audio
```

### D-002 — Geometria visual não é autoridade

**Decisão conceitual:** o objeto não deve ser definido pelo mesh.

Exemplo discutido:

```text
Object 73942
Semantic:
    material = rock
    density = ...
    hardness = ...
Spatial:
    region = ...
Representations:
    MeshRepresentation
    SDFRepresentation
    SparseVolumeRepresentation
```

Um mesmo objeto poderia futuramente usar:

- mesh perto;
- SDF para destruição;
- SDF coarse para colisão;
- splat distante;
- voxel/volume para GI.

**Estado até R2.1:** direção arquitetural; Geometry Kernel ainda não existia.

---

# PARTE II — CONSTITUIÇÃO CONCEITUAL ANTES DO CÓDIGO

## L-003 — Proposta dos cinco núcleos iniciais

Antes do LAB-0, a conversa propôs um `World Kernel` mínimo com cinco responsabilidades pesquisáveis.

### State Kernel

- verdade lógica do mundo;
- estado como `health`, `locked`, `closed` etc.;
- renderer não pode alterar essa verdade diretamente.

### Space Kernel

Hipótese proposta:

- evitar scene graph como autoridade espacial;
- experimentar `Hierarchical Sparse Space` ou estruturas similares;
- separar organização espacial de hierarquia lógica.

Adiada até R3.

### Memory Kernel

Hipótese proposta:

```text
Virtual World Memory
resource can be:
CPU resident
GPU resident
duplicated
compressed
streamed
generated
discarded
```

Interface imaginada:

```text
require(resource, GPU, deadline = frame + 1)
```

Nenhum Memory Kernel foi implementado até R2.1.

### Execution Kernel

Hipótese:

- sistema declara `reads`, `writes`, constraints;
- scheduler decide ordem/paralelismo;
- CPU/GPU placement deve ser implementação onde possível.

Virou R2 e foi parcialmente comprovada para CPU worker pool.

### Change Kernel

Hipótese:

- mudanças autoritativas como transactions;
- infraestrutura compartilhável para undo/redo, replay, replication, save, rollback e debug temporal.

R0/R1 comprovaram transaction, journal, replay e rollback no escopo de referência. Networking/editor undo permaneceram abertos.

---

## L-004 — Geometry Kernel conceitual

Contrato futuro proposto:

```text
GeometryProvider
├── TriangleProvider
├── SDFProvider
├── VoxelProvider
├── GaussianProvider
├── CurveProvider
├── ParticleProvider
└── NeuralProvider
```

Capacidades seriam diferentes por provider. Nenhum `GeometryProvider` existia até R2.1.

---

## L-005 — Renderer heterogêneo conceitual

Renderer deveria receber `RenderView`, não entities diretamente:

```text
World
→ Visibility
→ Representation Selection
→ Render Compilation
→ GPU command graph
```

Um frame poderia combinar rasterization, ray tracing, raymarching, Gaussian splatting, volume rendering e neural reconstruction.

Foi proposta convergência para um `Surface Buffer` comum:

```text
position
normal
material
depth
velocity
semantic ID
```

Permaneceu hipótese.

---

## L-006 — Física como derived view

Regra registrada:

```text
VisualGeometry != PhysicalGeometry
```

Exemplos:

- Gaussian visual + SDF physical proxy;
- detailed mesh visual + convex hull physical;
- destructible voxel visual + coarse volume physical.

Nenhum Physics View production existia até R2.1.

---

## L-007 — Política de IA

```text
AI proposes.
Kernel validates.
```

Modelo pode propor destino/geometria, mas navigation/physics/Geometry Kernel valida. IA não vira autoridade apenas por gerar aparência plausível.

---

## L-008 — Método do laboratório

```text
Theory
Experiment
Data
Conclusion
Specification
```

Resultados possíveis:

```text
PASS
PARTIAL
FAIL
```

Uma ideia deveria poder morrer mesmo se inicialmente defendida.

### D-003 — Oracle/reference implementation

Para cada kernel importante, manter caminho simples/correto contra o qual optimized path possa ser comparado.

Implementado em R2/R2.1 como serial/per-entity oracle vs parallel/range paths.

---

## L-009 — Estados de maturidade

```text
IDEA
→ HYPOTHESIS
→ EXPERIMENTAL
→ VERIFIED
→ FOUNDATIONAL
```

`FOUNDATIONAL` estabiliza contrato, não congela implementação.

Até R2.1: nenhum contrato foi promovido.

---

## L-010 — Testes adversariais conceituais

Foram sugeridos:

- 1 bilhão de entidades vazias;
- 10 milhões de objetos móveis;
- explosão destruindo 1 km³;
- teleporte instantâneo;
- alteração de 1 milhão de materiais;
- GPU com pouca VRAM;
- CPU com poucos threads;
- alta latência CPU↔GPU;
- streaming loss;
- destruição durante ray tracing;
- mundo procedural amplo/infinito.

Não executados até R2.1; eram exemplos de metodologia adversarial.

---

## L-011 — Deterministic vs non-deterministic domains

Deterministic domain proposto:

- gameplay;
- multiplayer;
- física autoritativa relevante;
- replay;
- transactions.

Non-deterministic domain:

- particles;
- visual AI;
- GI;
- denoising;
- procedural decoration.

R1/R2 demonstraram apenas exact-state determinism nos workloads registrados.

---

## L-012 — Error Budget e Computational Economy

### Error Budget

Em vez de apenas LODs fixos:

```text
max_screen_error = 0.25 pixels
```

A engine escolheria representação adequada ao erro/orçamento.

### Computational Economy

Frame teria orçamento dinâmico redistribuível entre sistemas.

Nenhuma das duas ideias existia implementada até R2.1.

---

## L-013 — Ordem de construção escolhida

Rejeitado começar pelo editor:

```text
Engine Lab
→ Runtime
→ Developer Tools
→ Editor
```

### D-004 — Primeiro gate real

> Um `World State` pode existir sem conhecer renderer, mesh, collider, scene graph ou GPU?

Isso levou ao LAB-0/R0.

---

# PARTE III — CAPACIDADE DA SANDBOX E LAB-0

## L-014 — Limitações da sandbox

Disponível:

- GCC 14.2;
- Clang 17;
- CMake 3.31;
- Python 3.13;
- C/C++/multithreading/SIMD/data structures/tests/benchmarks CPU.

Não disponível para evidência real:

- GPU NVIDIA exposta;
- Vulkan device de produção;
- VRAM/timestamps/occupancy reais.

### D-005 — Política de GPU evidence

Código GPU pode ser preparado na sandbox, mas nenhum número é chamado de GPU measurement sem execução real.

---

## L-015 — LAB-0 criado

C++23 com World Kernel, EntityId, Position, Velocity, Health, transactions, SoA storage, reference simulation, tests, benchmarks e constitution.

Build inicial:

- GCC 14.2;
- C++23;
- CMake 3.31;
- Linux x86-64.

```text
100% tests passed
0 tests failed
```

Atomicidade foi testada com transaction contendo operação válida seguida de inválida.

---

## L-016 — LAB-0 performance baseline

| Entidades | Tempo CPU |
|---:|---:|
| 100.000 | ~0.140 ms/frame |
| 1.000.000 | ~1.357 ms/frame |
| 3.000.000 | ~3.917 ms/frame |

Uma execução intermediária citou ~1.22 ms para 1M.

Workload = integração mínima position/velocity; não NPC completo.

---

# PARTE IV — R0/R1

## L-017 — Diretiva para concluir R0 e entrar em R1

```text
R0 completion
→ R1 Change Journal
→ State Hash
→ deterministic replay
→ rollback
```

Gate: milhares de frames, persistir transactions, reconstruir pristine World, obter same hash.

---

## L-018 — Falha encontrada: `reserve_entity_id()`

`reserve_entity_id()` alterava cursor fora das transactions e podia quebrar reconstrução exata.

### D-006 — Identity allocation transaction-derived

- `CreateEntity` aloca dentro da transaction;
- `next_entity_id()` read-only.

---

## L-019 — `AdvanceReference` virou mutação autoritativa

Antes `world.integrate_reference(dt)` podia escrever diretamente. Corrigido para:

```text
Transaction N
└── AdvanceReference(dt)
```

---

## L-020 — Forward journal vs ephemeral undo

Persistente: forward transactions.

Temporário: pre-state necessário para exact undo.

### D-007 — Separar formato persistente de undo

Journal compacto/reproduzível não precisa usar o mesmo formato de rollback em memória.

---

## L-021 — R1 replay proof

256 entities, 5.000 frames, 5.001 transactions, health changes, destroy/create, `AdvanceReference`.

Hash final:

```text
9e6b6a3bac5a0564e2f3100bcf7eed9d0e48ef44615382f27931d5dcb9960c57
```

Original e replay pristine produziram mesmo hash.

---

## L-022 — Cross-compiler R1

GCC 14.2 e Clang 17 em x86-64 Linux produziram same workload hash.

Não prova Windows/Linux/ARM/GPU determinism universal.

---

## L-023 — SHA-256 cross-check

C++ vs Python hashlib:

```text
9052d221ad22d52fb0a43dbec4410a9546d7bbb968642614ee0deb55758e7c33
```

---

## L-024 — R1 rollback proof

Rollback para checkpoint, reapply tail e full rollback de spawn journal-owned reproduziram hashes/cursor corretos.

---

## L-025 — Rollback divergence guard

World divergente do journal → rollback rejeitado.

### D-008 — Não aplicar stale undo

Recusar rollback é preferível a corrupção silenciosa.

---

## L-026 — R1 performance

- commit total ~272.054 ms;
- ~0.054 ms/frame;
- journal 224.236 bytes;
- save ~0.812 ms;
- load ~0.720 ms;
- replay ~1.504 ms;
- rollback all ~275.413 ms.

Limites: full World hash/commit e full touched-state undo.

### D-009 — Não congelar implementação R1

Contrato funcionou; implementação não foi promovida como final.

---

## L-027 — R0 baseline após integração transacional

1M entities / 120 frames:

- spawn 3M mutations;
- spawn commit ~39.873 ms;
- simulation ~1.229 ms/frame;
- ~813.5M minimal updates/s.

---

## L-028 — Status após R1

```text
R0 VERIFIED — reference scope
R1 VERIFIED — same-architecture reference scope
FOUNDATIONAL NONE
```

---

# PARTE V — R2 EXECUTION KERNEL

## L-029 — Hipótese R2

> Sistemas declaram dependências; kernel determina ordem/paralelismo seguro sem fixed frame phases e mantém commit determinístico.

Gate:

```text
Serial World Hash == Parallel World Hash
```

---

## L-030 — Private patches, não direct World writes

```text
immutable pre-wave World
→ private computation
→ private patches
→ canonical merge
→ transaction
→ World
```

### D-010 — Workers compute proposals; transaction owns authority

Scheduling não pode tornar truth dependente de thread timing.

---

## L-031 — Resource access model

Identity, EntityState, Position, Velocity, Health; cada system declara stable SystemId, reads, writes, optional `after`, function.

---

## L-032 — Hazard rules

```text
READ+READ   parallel-safe
READ+WRITE  serialize
WRITE+READ  serialize
WRITE+WRITE serialize
```

WRITE não implica READ. Undeclared access rejeitado.

---

## L-033 — DAG e cycles

Explicit dependencies entram no graph; cycles rejeitados; stable SystemId tie-break no reference model.

---

## L-034 — Deterministic waves

Kahn ordering; same-wave systems observam same pre-wave World; canonical merge e one transaction/wave.

---

## L-035 — Correção de lifecycle

Primeiro prototype criava worker pool por `execute()`.

### D-011 — Persistent `ExecutionRuntime`

Workers persistem; benchmark usa runtime persistente.

---

## L-036 — R2 correctness proof

Hash 4.096 entities / 120 frames:

```text
057e4f9d9e4921cb93da1a0c5b1245fafbadb49ce8755f572ebff34b558e53ca
```

GCC/Clang/TSan paths reproduziram correctness scope.

---

## L-037 — Cross-worker R2

8.192 entities / 60 frames / 4 systems / 2 waves / workers 1,2,4,5:

```text
71ccbd8aaaed14974c7c70ab4879f099f42195dd1ed6d312d93fa8642cf4218c
```

---

## L-038 — Compute-only benchmark

| Workers | Median | Speedup |
|---:|---:|---:|
| 1 | 80.508 ms | 1.000× |
| 2 | 41.275 ms | 1.951× |
| 4 | 24.551 ms | 3.279× |
| 5 | 23.186 ms | 3.472× |

Checksum `3462961269496396242`.

---

## L-039 — R2 authoritative benchmark

| Workers | Median | Speedup |
|---:|---:|---:|
| 1 | 49.192 ms | 1.000× |
| 2 | 41.956 ms | 1.172× |
| 4 | 46.826 ms | 1.051× |
| 5 | 45.700 ms | 1.076× |

Scheduler funciona; world workload não escala proporcionalmente.

---

## L-040 — Gargalo R2

Millions of Mutation records → materialization, metadata, memory bandwidth, merge, validation, serial publication.

### D-012 — R2 performance = PARTIAL

Correctness verified; performance partial.

---

## L-041 — Sanitizers R2

ASan pass, UBSan pass, TSan concurrent test sem reported race.

---

## L-042 — Mudança de roadmap

### D-013 — Inserir R2.1 antes de R3

Resolver authority-boundary bottleneck antes de construir Spatial Kernel.

---

# PARTE VI — R2.1 HYBRID TRANSACTION PATCHES

## L-043 — Hipótese R2.1

Manter exact semantics sem uma `Mutation` por entity/component.

Candidatos: oracle scalar, contiguous ranges, fixed pages, COW pages, disjoint parallel commit.

---

## L-044 — Dense e structural writes não exigem mesma granularidade

### D-014 — Uma authority boundary, múltiplas granularidades

```text
PatchTransaction
├── scalar_mutations
├── vec3 ranges
└── u32 ranges
```

---

## L-045 — Patch equivalence proof

4.096 entities / 120 frames, candidatos reproduziram:

```text
a073236582885e8cd53f22aa4825ed539a00c74c7c026e61d9e1db9940ada47e
```

Replay/rollback/save/load/overlap/non-finite checks passaram.

---

## L-046 — Early falsifications

Pages256 não eram automaticamente melhores; thread creation dentro de commit tornava parallel path artificialmente caro.

Dense 1M payload ~96 MB scalar → ~28 MB range.

### D-015 — Persistent parallel publisher

Evitar lifecycle artificial.

---

## L-047 — Sparse workload contra “one page fits all”

100k entities / 200 frames / 1% writes.

Clustered:

- scalar 16.087 ms / 32.512 B frame;
- exact ranges 17.213 ms / 12.400 B;
- pages256 18.931 ms / 31.120 B;
- full range 249.793 ms / 1.200.040 B.

Scattered:

- scalar 12.036 ms / 32.512 B;
- one-value ranges 17.018 ms / 52.000 B;
- pages256 285.824 ms / 1.215.640 B;
- full range 249.986 ms / 1.200.040 B.

### D-016 — Rejeitar fixed page universal

Scalar para structural/sparse; exact ranges clustered quando úteis; large ranges dense; pages/COW optional/not promoted.

---

## L-048 — No silent precedence

Scalar Position + overlapping PositionRange → REJECT.

### D-017 — Overlap ambíguo é erro

Sem regra implícita range/scalar winner.

---

## L-049 — Hybrid scalar + range transaction proof

Health scalar + Destroy + dense Position range em same transaction passaram commit/serialize/load/replay/rollback.

---

## L-050 — R2 Execution integration

`set_position_range`, `set_velocity_range`, `set_health_range`; legacy `execute()` rejeita range producer; `execute_patched()` preserva DAG semantics.

### D-018 — Scheduling separado de patch storage

Granularidade de transaction não deve redesenhar dependency logic.

---

## L-051 — R2.1 execution equivalence

4.096 entities / 120 frames:

```text
657f7bd1092e03c74acf7a38b7a70243f3a8decef268fcce0c552b4195f34a94
```

240 wave transactions; replay same hash.

---

## L-052 — Integrated 8.192 entities

Hash `71ccbd8...`; ranges ajudaram, worker overhead podia piorar small/medium workload.

## L-053 — Integrated 100.000 entities

Hash:

```text
e6803f6411816d3e2261f091e7eb82718262ee9969b33dce9135467c9072c2c4
```

Ranges + workers chegaram ~1.889× vs scalar serial naquele workload.

## L-054 — Integrated 1.000.000 entities

Hash:

```text
61d624a0af70729626dafebd3b3bea4cb5a074e625ec7f17ac981f6eef5a2c60
```

Scalar serial 740.970 ms; range+4 workers+persistent commit 150.455 ms (~4.925× naquele workload).

Não comparar esse número a engines externas.

---

## L-055 — Dense payload evidence

Scalar 3M records ~96 MB vector capacity; contiguous range 3 records ~28.000.120 B; pages256 11.721 records ~28.468.840 B.

---

## L-056 — Cross-compiler e sanitizers R2.1

GCC/Clang same hashes; ASan/UBSan dedicated tests pass; TSan Execution Kernel sem race reportada; full patch TSan não reivindicado quando timeout impediu conclusão.

### D-019 — Timeout não é aprovação

---

## L-057 — Release hygiene R2.1

GCC/Clang 4/4 tests, zero warnings configured set, clean ZIP rebuild/test.

ZIP SHA-256:

```text
a4d0bcdef114e84456758c6c0067df50b1aaf599bda7686945d433aa9137cd63
```

---

## L-058 — Status após R2.1

```text
R0 VERIFIED
R1 VERIFIED
R2 VERIFIED correctness / performance partial
R2.1 VERIFIED
FOUNDATIONAL NONE
```

### D-020 — R3 liberado

---

# PARTE VII — R3 DIREÇÃO DE PESQUISA

## L-059 — Spatial Kernel Bake-Off definido

Candidatos: grid, hash grid, BVH, sparse brick, octree, possibly hierarchical hash grid.

Workloads: static dense/sparse, moving, teleports, regional changes, streaming, range/ray/large queries, million-object updates.

### D-021 — R3 sem preferência tecnológica

Mesmo contrato/workload decide.

---

# PARTE VIII — CENTRALIZAÇÃO NO GITHUB D-SF

## L-060 — Mandato de repositório oficial

Foi criado:

```text
git@github.com:AiltonSantanaReis/D-SF.git
```

No baseline inicial decidiu-se centralizar planos, decisões, evidência e documentação nesse repositório.

### D-022 — D-SF como fonte oficial

Decisão original: GitHub D-SF como system of record.

### D-023 — Cinco documentos canônicos

```text
README.md
PROJECT.md
ARCHITECTURE.md
RESEARCH_LEDGER.md
VERIFICATION.md
```

### D-024 — Preservar namespace `aion` no baseline

Evitar rename cosmético durante importação do código já testado.

---

# PARTE IX — DECISION REGISTER DO BASELINE R0–R2.1

| ID | Decisão | Estado no baseline |
|---|---|---|
| D-001 | Engine híbrida em vez de representação universal. | HYPOTHESIS |
| D-002 | Geometria visual como derived representation. | direção |
| D-003 | Oracle/reference antes de otimização. | método ativo |
| D-004 | Começar por World independente. | executado R0 |
| D-005 | Não fabricar GPU evidence. | regra ativa |
| D-006 | Identity allocation dentro de transaction. | VERIFIED R1 |
| D-007 | Forward journal separado de undo. | VERIFIED R1 |
| D-008 | Rollback rejeita divergence. | VERIFIED R1 |
| D-009 | Não congelar R1 full-hash/full-undo. | regra ativa |
| D-010 | Workers produzem proposals/patches. | VERIFIED R2 |
| D-011 | Persistent runtime. | VERIFIED implementation |
| D-012 | R2 performance PARTIAL. | evidência |
| D-013 | Inserir R2.1. | concluído |
| D-014 | Hybrid transaction, uma authority boundary. | VERIFIED R2.1 |
| D-015 | Persistent parallel publisher. | implementation |
| D-016 | Fixed page 256 não universal. | FALSIFIED universal |
| D-017 | No silent precedence. | VERIFIED |
| D-018 | Scheduling separado de patch storage. | VERIFIED integration |
| D-019 | Timeout não é pass. | regra ativa |
| D-020 | R3 liberado. | concluído |
| D-021 | R3 por bake-off. | método |
| D-022 | GitHub como source oficial. | posteriormente refinado por D-044 |
| D-023 | Cinco documentos canônicos. | ativo |
| D-024 | Preservar `aion` no baseline. | ativo |

---

# PARTE X — R3 SPATIAL KERNEL EXECUTADO

## L-061 — R3 começou pelo oracle e candidatos

BruteForceOracle foi usado como referência de correctness. HGrid mostrou limitações para AABB generalista; WideBVH8 SAH/refit ficou forte em low churn; Morton wide BVH rebuild ficou forte em high churn.

Fixed region partition foi rejeitado. MortonClusterBVH8 não foi promovido devido a memory/update duplication.

## L-062 — Shared Spatial Snapshot

Problema identificado: cada spatial backend duplicar center/half bounds desperdiçava memória e publication work.

### D-025 — Shared Spatial Snapshot como data plane derivado

```text
Authoritative World
→ R2.1 Hybrid Transaction
→ Shared Spatial Snapshot
→ WideBVH8 / MortonBVH8 views
```

Snapshot SoA center/half, dense/sparse identity, ~24 B/object dense no modelo estrutural.

R2.1 propagation:

```text
SpatialChangeSet { dirty_slots, dirty_ranges }
```

Create/destroy → structural rebuild. Version + structure_revision rejeitam stale/skipped deltas.

## L-063 — 1M spatial medians

```text
snapshot        40.986 ms
SAH build     1782.978 ms
Morton radix    55.231 ms
publish2         0.924 ms
SAH2            13.960 ms
Morton2         51.575 ms
publish50        2.516 ms
SAH50         1664.696 ms
Morton50        54.426 ms
```

Shared structural memory ~66.0 MB vs duplicated model ~106.0 MB (~37.73% reduction). Não RSS.

## L-064 — Churn-only policy falsificada

`changed_fraction` isolado não explicava winner. Topology debt ajudava, mas não era suficiente.

### D-026 — Spatial policy inclui query demand

```text
FrameSpatialCost = UpdateCost + QueryCount * QueryCost + Rebuild/SwitchCost
```

Query count pode inverter o winner mesmo com mesmo churn.

## L-065 — R3.1 async SAH e compact catchup

Initial full catchup ~52.833 ms foi rejeitado.

Compact dirty catchup:

- snapshot copy ~3.508 ms;
- background SAH ~2969.335 ms;
- catchup ~9.041 ms.

Mas background interference elevou active Morton update ~43.5%.

### D-027 — Background work não é “grátis”

Unrestricted background scheduling não foi promovido.

Cost policy:

```text
predicted = update_ms + sampled_query_ms/sample_count * expected_queries
```

Matched subsequent winner 23/23 synthetic AABB scenarios no escopo testado.

## L-066 — R3.2 Budgeted Spatial Build Scheduler

Cooperative/resumable SAH state machine:

capture → catchup → bounds → binning → scatter/partition → nodes → cleanup.

Execution Budget Scheduler concede somente slack; zero slack → zero maintenance.

100k promoveu ~92 frames com ~1 ms slices; 250k ~292 frames.

1M Morton critical ~54.8 ms/frame → 80/80 frames starved, maintenance 0, correctness preservada.

Dirty-refit queue contínua substituiu batch catchup que podia starvation.

Extreme 1M: 50% dirty/frame ×30 → peak ~10.83M pending; após production stop drenou em 181 nominal 0.5 ms slices; final backlog 0, query equivalence/promotion PASS.

### D-028 — R3 fechado

`VERIFIED — CPU/reference`. Limite: mandatory 1M Morton rebuild/frame ainda >16.67 ms na CPU da sandbox.

---

# PARTE XI — R4 GEOMETRY KERNEL

## L-067 — Neutral math layer antes de Geometry

`Vec3`, `Aabb`, `Ray` extraídos para math neutral, mantendo World/Spatial/Geometry como siblings.

## L-068 — R4A Representation Contract

Capabilities:

- Bounds;
- RaySurface;
- SignedDistance.

TriangleReference: Bounds/Ray. AnalyticSdf: all three.

Cube 12 triangles vs analytic box SDF: exact bounds, 882 ray hits equivalent, inside/non-normalized rays testados.

### D-029 — GeometryProvider capability-oriented

Provider não é escolhido por nome/tipo hard-coded no consumer.

## L-069 — R4B Sparse Implicit Geometry

Sparse narrow-band hierarchy, int16 quantized payload, conservative error certificate.

Capped sphere tracing tornou-se resolution-dependent e foi rejeitado. Naive hierarchical child testing também piorou.

### D-030 — Brick DDA + local sphere tracing promovido

100k rays ~138/135/145 ms em 1/32,1/64,1/128 no set testado.

Sphere sparse/dense-int16: 96.9%,55.3%,31.7%; Box: 191.5%,133.1%,80.0%.

Conclusão: sparse não é universalmente menor.

## L-070 — R4C Clustered Triangle

Adjacency-aware clusters, uint8 local indices, contiguous payload, BVH8.

Per-cluster quantization abriu cracks no torus.

### D-031 — Shared resource quantization frame

Shared source vertex decodes identicamente entre clusters. Quantized BVH bounds recebem one-unit outward padding.

64 vertices/124 triangles tornou-se experimental CPU default por tradeoff do sweep, não contrato universal.

## L-071 — R4D Heterogeneous Selection

### D-032 — Hard constraints + explicit objective + Pareto

Weighted magic score rejeitado.

Sphere example:

- Sparse error ~0.0135324, storage ~2,842,910 B;
- Clustered error ~0.0192412, storage ~46,288 B.

Sparse venceu ray latency nos workloads CPU testados; Clustered venceu memória. Pareto contém ambos.

## L-072 — R4E Online Runtime Telemetry

Observations keyed por handle + revision + workload + device + batch class.

Median/MAD/P90 recent window. Outlier `1,1,1,1,1,1,50` → median1, P90=50.

Always-on per-call timing de microbatch foi falsificado por overhead; sampling foi introduzido.

## L-073 — R4F Safe Online Exploration

Inactive provider improvement não é observável sem executar alternativa/shadow work ou permanecer ignorante.

Exploration opt-in, sem hidden duplicate work.

Refresh age 1/2 com min_observed=3 podia entrar em exploração quase permanente.

### D-033 — Freshness é workload SLA

Não existe universal refresh age ótimo; freshness horizon não deve ser menor que confidence sample requirement.

### D-034 — R4 fechado

R4A–F `VERIFIED — CPU/reference`; nenhuma representação universal promovida.

---

# PARTE XII — R5 DEVICE / BACKEND REFERENCE ARCHITECTURE

## L-074 — Separação central de R5

```text
World != Geometry != Device Package != Device Work != Backend Command Model
```

## L-075 — R5A Device Residency Contract

Initial geometry-shaped key foi generalizado.

### D-035 — Generic DeviceResourceKey

```text
owner { namespace, object_id, revision }
resource_class
subresource
```

Namespaces Geometry/Work/Global. Reference backend para semantics, sem VRAM claim.

Residency: explicit budget, LRU unpinned, pinning, immutable content/key, generational handles, key→slot map, free-list, restore after failed upload.

## L-076 — R5B Geometry Device Packages

`RepresentationArchive` canonical little-endian, sem `memcpy(struct)` ABI.

Sparse/Clustered compilers produzem same generic package model.

Bug real encontrado: staged new subresources usavam sentinel `entries.size()` e podiam sobrescrever slots posteriores. Corrigido com `existed_before` explícito.

### D-036 — Atomic ensure_group

Validate all → plan evictions → stage → publish all; failure restaura tudo.

## L-077 — R5C Device Work Contract

`DeviceWorkPacket`: id, domain, program key, launch mode, dimensions/control resource, resources/access, explicit after, immutable parameters.

Resource-centric hazard tracker substituiu O(P²) packet comparisons; frontier Kahn substituiu rescans.

### D-037 — DeviceWorkPacket é coarse node

100k host packets mostraram ~1s reference planner cost. Fine work deve residir em work-items/indirect/device-generated buffers.

Planner digest:

```text
9229187388161744994
```

Launch-control dependency tornou-se explicitamente readable resource.

## L-078 — R5D Backend Capability & Translation

Backend-neutral capability profiles e launch kinds Direct/Indirect/GeneratedSequence/WorkGraph.

### D-038 — Launch semantic lowering assimétrico

- Direct static pode preserve/promote;
- dynamic Indirect não demote silently para Direct;
- DeviceGenerated não demote silently para host-driven;
- Indirect pode promote para generated quando suportado.

Semantic digest separado de backend digest.

Initial descriptor model duplicava binding por use; rejeitado. Unique descriptor table + per-command resource uses promovido.

Reference R5D fingerprints:

```text
semantic 2894648114337488996
direct   1822164511422550589
generated-sequence 14902490109153104665
work-graph 9850775312450346422
```

7-run CPU translation medians 100/1k/5k documentados; não representam GPU speed.

R5D final: GCC/Clang 17/17, warnings clean configured set, ASan+UBSan dedicated A-D pass.

---

# PARTE XIII — R5E REAL HARDWARE BRING-UP

## L-079 — Gate philosophy

### D-039 — HARDWARE RESULT requer execução física

Sandbox pode escrever/reference-test adapters, mas VRAM, GPU sync, timestamps, DGC e backend performance só recebem hardware classification após execução fora da sandbox.

Hardware target atual:

```text
NVIDIA GeForce RTX 3070 Ti
Driver 610.47
Vulkan loader 1.4.357
Device API 1.4.341
```

## L-080 — HW01 gate iterations e metodologia

Runner v1/v2/v3/v4/v5 expôs problemas de prerequisite discovery, optional CMake assumption, stderr warning handling, MSVC invocation e process lifetime.

Essas falhas foram classificadas como gate/toolchain failures, não GPU failures.

HW01 v6 removeu `--show-all`, adicionou timeouts/Job Object/process-tree safety e isolou implicit layers.

### D-040 — HW01 VERIFIED

Real feature bits confirmaram BDA, synchronization2, timeline semaphore, descriptor indexing/buffer/heap, DGC, mesh/task shader, acceleration structure/ray tracing.

Capability availability ≠ optimality/performance.

## L-081 — HW02 real memory roundtrip

Toolchain gates foram corrigidos até direct `cl.exe /c` + `link.exe` separate response files.

Final hardware path:

```text
HOST upload 16 MiB
→ DEVICE_LOCAL
→ synchronization2
→ HOST readback 16 MiB
→ full byte compare
```

Hashes:

```text
input  0xc0dd6ba4a0e044c2
output 0xc0dd6ba4a0e044c2
```

Validation 0 errors / 0 warnings.

### D-041 — HW02 VERIFIED

Real `VkDevice` + memory roundtrip funcionam no target.

## L-082 — HW03 Direct Compute

Workload:

```text
1,048,576 uint32
local_size_x 256
4096 workgroups
out[i] = in[i] * 3u + 7u
```

CPU oracle:

```text
0x8e2eef1faffc414f
```

GPU Direct output same; full element compare PASS; validation clean.

### D-042 — HW03 VERIFIED

Primeiro real compute work executado fisicamente pela GPU e comparado a oracle CPU.

## L-083 — HW04 Indirect Compute

Mudança controlada: mesmo input/shader/binding/operation/oracle; apenas launch mechanism trocado.

Device-resident 12-byte control buffer:

```text
VkDispatchIndirectCommand {4096,1,1}
```

`vkCmdDispatchIndirect` output:

```text
0x8e2eef1faffc414f
```

Same CPU oracle; validation 0/0.

Evidence ZIP SHA-256:

```text
da327a30d50ebfcc90431d06cd14be585efb0d6b29cb16dadbe308a3eb1faa31
```

### D-043 — Functional equivalence Direct/Indirect no workload testado

Conclusão permitida: same semantics/output no gate.

Conclusão proibida: performance equivalence/superiority. Timestamps ainda não medidos.

---

# PARTE XIV — GOVERNANÇA REFINADA APÓS R5E-HW04

## L-084 — Correção da política de repositório

A prática real de pesquisa demonstrou que publicar cada experimento diretamente no GitHub durante exploração cria ruído e pode congelar estados não comprovados.

### D-044 — GitHub é registro oficial de milestones; sandbox/local é laboratório ativo

Esta decisão **supersede parcialmente D-022**:

- D-SF GitHub continua sendo registro público oficial dos estados consolidados;
- desenvolvimento/experimentos permanecem local/sandbox até fechamento de hipótese/gate;
- `main` recebe snapshots coerentes/verificados, não cada tentativa;
- failed runners/toolchains podem ser preservados em evidence/ledger quando metodologicamente relevantes, mas não entram como “GPU failure”.

## L-085 — Auditoria documental após HW04

Foi detectado que uma atualização de GitHub havia resumido acidentalmente `ARCHITECTURE.md`, `PROJECT.md`, `RESEARCH_LEDGER.md` e `VERIFICATION.md`.

A redução foi tratada como regressão documental. Conteúdo detalhado foi restaurado e os estados novos foram adicionados sem apagar o baseline.

### D-045 — Documentação canônica não pode ser encurtada por substituição destrutiva

Atualizações futuras devem ser append/supersede orientadas quando o conteúdo antigo ainda tem valor de auditoria. Resumo pertence ao README/estado, não deve apagar ledger/evidence detalhados.

---

# PARTE XV — PRÓXIMA ENTRADA ESPERADA

## L-086 — R5E-HW05 autorizado

**GPU Timestamp & Direct/Indirect Characterization.**

Variáveis que devem permanecer comparáveis a HW03/HW04:

- input;
- operation;
- element count;
- shader;
- binding baseline;
- CPU oracle;
- target GPU.

Adicionar:

- GPU timestamps;
- repeated samples;
- warm/cold policy explícita;
- CPU preparation/record/submit/synchronization quando relevante;
- median/tail characterization.

### D-046 — Não escolher Direct/Indirect/DGC por preferência

HW05 mede Direct vs Indirect. DGC e descriptor modern paths entram depois como candidatos controlados. O vencedor, se houver, é workload/hardware-specific.

---

# DECISION REGISTER — CONTINUAÇÃO R3–R5E

| ID | Decisão | Estado |
|---|---|---|
| D-025 | Shared Spatial Snapshot como data plane derivado. | VERIFIED R3 |
| D-026 | Spatial policy inclui query demand/cost, não churn isolado. | VERIFIED tested scope |
| D-027 | Background work irrestrito não é grátis. | FALSIFIED universal |
| D-028 | R3 fechado CPU/reference, com 1M rebuild limitation explícita. | VERIFIED |
| D-029 | GeometryProvider capability-oriented. | VERIFIED R4A |
| D-030 | Brick DDA + local sphere tracing no Sparse SDF tested path. | VERIFIED R4B scope |
| D-031 | Shared resource quantization frame para clustered triangles. | VERIFIED R4C |
| D-032 | Hard constraints + explicit objective + Pareto. | VERIFIED R4D |
| D-033 | Freshness/exploration é workload SLA. | VERIFIED mechanism |
| D-034 | R4 fechado CPU/reference; nenhuma representação universal. | VERIFIED |
| D-035 | DeviceResourceKey genérico por namespace/object/revision. | VERIFIED reference |
| D-036 | Atomic `ensure_group`. | VERIFIED reference |
| D-037 | DeviceWorkPacket é coarse execution node. | VERIFIED reference conclusion |
| D-038 | Backend lowering assimétrico preserva autonomy semantics. | VERIFIED reference |
| D-039 | Hardware claim exige hardware execution. | regra ativa |
| D-040 | HW01 real capability fingerprint. | VERIFIED hardware |
| D-041 | HW02 real VkDevice/memory roundtrip. | VERIFIED hardware |
| D-042 | HW03 Direct compute exact CPU oracle. | VERIFIED hardware |
| D-043 | HW04 Indirect compute functionally equivalent no workload. | VERIFIED hardware correctness |
| D-044 | Sandbox/local = laboratório; GitHub = milestone record. | regra ativa |
| D-045 | Não substituir documentos canônicos detalhados por resumo destrutivo. | regra ativa |
| D-046 | HW05 mede antes de escolher launch path. | próxima fase |
