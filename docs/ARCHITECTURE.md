# D-SF — Arquitetura Ativa

Status: arquitetura experimental consolidada até **R5E-HW04**. Nenhum contrato está `FOUNDATIONAL`.

Este documento mantém duas camadas deliberadas:

1. **arquitetura ativa**, refletindo o estado verificado mais recente;
2. **baseline detalhado R0–R2.1 preservado**, para que a evolução arquitetural não apague invariantes, limitações e justificativas anteriores.

Histórico experimental e decisões ficam em `RESEARCH_LEDGER.md`; evidências e fingerprints em `VERIFICATION.md`; roadmap/governança em `PROJECT.md`.

---

## 1. Invariantes ativos

1. `World` é autoritativo; renderer, physics, spatial, geometry e device são views/compilações derivadas.
2. `VisualGeometry != PhysicalGeometry`.
3. workers/sistemas não recebem autoridade irrestrita; mudanças autoritativas passam por contratos validados.
4. otimizações semanticamente equivalentes devem possuir reference/oracle quando aplicável.
5. backend API não deve contaminar os contratos upstream.
6. capability disponível não implica escolha ótima.
7. tecnologia nova não recebe preferência automática por ser nova.
8. `REFERENCE RESULT` e `HARDWARE RESULT` permanecem classes distintas.

---

## 2. Arquitetura ativa consolidada

```text
Authoritative World
  ├─ State / Identity
  ├─ Hybrid Transactions
  └─ Change Journal
        |
        +--> Execution Kernel
        |      └─ dependency waves / private patches / atomic commit
        |
        +--> Shared Spatial Snapshot
        |      ├─ Wide BVH8 view
        |      └─ Morton BVH8 view
        |
        +--> GeometrySet
        |      ├─ Clustered Triangle
        |      └─ Sparse / Analytic SDF
        |
        +--> Device Geometry Packages
               └─ Residency Manager
                      |
                      +--> DeviceWorkPacket DAG
                              |
                              +--> Backend Translation
                                      |
                                      +--> Vulkan hardware gates
```

Separação central:

```text
World != Geometry != Device Package != Device Work != Backend Command Model
```

---

## 3. Transactions / Execution

R2.1 mantém duas lanes sob a mesma autoridade:

- scalar sparse/structural writes;
- typed contiguous ranges para workloads densos/clustered.

R2 Execution Kernel declara resources `reads/writes/after`, constrói waves determinísticas e faz merge canônico em uma transação por wave. `WRITE` não implica `READ`.

Workers observam o mesmo pre-wave state e produzem patches privados. A publicação autoritativa ocorre somente depois do merge/validation.

---

## 4. Spatial Kernel — R3/R3.1/R3.2

`Shared Spatial Snapshot` é um data plane derivado do World e evita que cada backend replique os mesmos bounds autoritativos.

Modelo ativo:

```text
Authoritative World
      ↓ R2.1 SpatialChangeSet
Shared Spatial Snapshot
      ├─ WideBVH8 SAH/refit
      └─ MortonBVH8 deterministic radix
```

Propriedades verificadas no CPU/reference scope:

- dense/sparse identity mapping;
- 24 bytes/object de bounds densos no modelo estrutural medido;
- version + structure_revision para rejeitar deltas stale/skipped;
- create/destroy força structural rebuild;
- dirty slots/ranges propagam updates sem transformar Spatial em autoridade;
- query equivalence comparada a oracle.

A política não escolhe backend por churn isolado. O custo relevante é:

```text
FrameSpatialCost = UpdateCost + QueryCount * QueryCost + Rebuild/SwitchCost
```

R3.2 adicionou manutenção cooperativa/budgeted e backlog explícito. Se produção de trabalho excede service budget, backlog cresce; quando produção cai abaixo do service rate, ele drena.

**Não promovido como solução universal:** fixed region partition, MortonClusterBVH8 e background work irrestrito.

---

## 5. Geometry Kernel — R4A–R4F

`GeometryHandle {ProviderId, Generation, ResourceId}` identifica representação. Providers são capability-oriented.

`GeometrySet` pode conter múltiplas representações do mesmo source revision, com:

- capability set;
- source revision;
- `max_geometric_error`;
- storage/cost telemetry externa ao contrato de verdade.

Representações verificadas incluem:

- TriangleReference;
- Analytic SDF;
- Sparse Implicit Geometry;
- Clustered Triangle Surface.

Seleção usa:

```text
hard constraints
+ explicit objective
+ Pareto frontier
```

Não existe score mágico universal.

R4E/R4F adicionaram telemetria workload/device/batch scoped e exploração segura opt-in. Inactive provider improvement é não observável sem exploration/shadow work; por isso freshness é política/SLA, não verdade dedutível pelo kernel.

Sparse e clustered permanecem candidatos com tradeoffs diferentes; nenhum é universal.

---

## 6. Device Residency / Geometry Packages — R5A/R5B

`DeviceResourceKey` separa identidade de recurso de geometria:

```text
owner { namespace, object_id, revision }
resource_class
subresource
```

Namespaces incluem Geometry, Work e Global.

Residency Manager de referência possui:

- budget explícito;
- eviction LRU entre unpinned;
- pinning;
- immutable key/content;
- generational handles;
- key→slot lookup;
- free-list;
- rollback/restoration após upload failure.

Geometry packages nascem de `RepresentationArchive` canônico little-endian. `ensure_group` publica grupos atomicamente: validate → plan → stage → publish, ou restaura estado anterior em failure.

---

## 7. Device Work — R5C

`DeviceWorkPacket` é coarse execution node e contém:

- packet id;
- domain Compute/Graphics/Ray;
- opaque program key;
- launch mode Direct/Indirect/DeviceGenerated;
- launch dimensions ou control resource;
- resources com Read/Write/ReadWrite;
- explicit `after` dependencies;
- immutable parameters.

Planner usa hazard tracking resource-centric (`last_writer + active_readers`) e Kahn frontier determinístico.

Fine-grained GPU work deve residir em work-items/indirect/device-generated data. O teste de 100k host packets demonstrou custo inadequado para tratar cada item fino como `DeviceWorkPacket`.

Launch-control resource de Indirect/DeviceGenerated precisa ser explicitamente declarado como readable resource; dependência oculta é rejeitada.

---

## 8. Backend Translation — R5D

Backend-neutral model:

```text
DeviceWorkPacket
      ↓
BackendCapabilityProfile
+ BackendTranslationPolicy
      ↓
BackendTranslatedStream
```

Semântica de lowering é assimétrica:

- Direct/static pode permanecer Direct ou ser promovido quando backend/policy permitem;
- Indirect dinâmico não pode ser silenciosamente demovido para Direct;
- DeviceGenerated não pode ser silenciosamente demovido para host-driven mode;
- Indirect pode ser promovido para device-generated quando suportado.

`semantic_digest` é separado de `backend_digest`.

Descriptor-memory reference model separa:

```text
BackendDescriptor   // unique table
BackendResourceUse  // per command use/access
```

Barrier lowering representa hazards cross-wave. Um plan manualmente inválido com hazard na mesma wave é rejeitado em vez de ser “reparado” pelo translator.

Nenhuma estrutura de descriptor/barrier de R5D é declarada layout final de hardware.

---

## 9. Vulkan Hardware State — R5E-HW01 a HW04

Hardware de teste atual:

```text
NVIDIA GeForce RTX 3070 Ti
Vendor/Device 10DE:2482
VRAM física 8192 MiB
Driver 610.47
Vulkan device API 1.4.341
```

Verificado em hardware real:

- Vulkan capability discovery;
- feature bits relevantes, incluindo DGC/descriptor heap/buffer, BDA e synchronization2;
- real `VkDevice`;
- real `VkDeviceMemory`;
- HOST → DEVICE_LOCAL → HOST roundtrip byte-exact;
- Direct compute por `vkCmdDispatch`;
- Indirect compute por `vkCmdDispatchIndirect` com resident control buffer;
- exact CPU oracle equality para 1.048.576 `uint32` no workload HW03/HW04;
- zero validation errors/warnings nos gates aceitos.

Isso prova correção funcional no escopo testado. **Ainda não prova:** Direct vs Indirect performance, DGC runtime/performance, descriptor heap runtime, PCIe bandwidth, final barrier policy ou production residency behavior.

---

## 10. Próxima fronteira

**R5E-HW05 — GPU Timestamp & Direct/Indirect Characterization.**

O gate deve manter shader, input, output oracle, binding baseline e element count equivalentes a HW03/HW04, acrescentando timestamps e custos CPU relevantes. Performance não pode ser inferida antes dessa medição.

---

# APÊNDICE A — BASELINE DETALHADO R0–R2.1 PRESERVADO

> Este apêndice preserva o conteúdo arquitetural detalhado do último snapshot íntegro anterior a R3. Afirmações de “futuro” contidas nele são históricas e foram supersedidas pelas seções ativas acima quando R3–R5E produziram evidência.

## A.1 Escopo do baseline

Este baseline descrevia somente a arquitetura sustentada até R2.1 e separava hipóteses futuras.

## A.2 Estado arquitetural no fechamento R2.1

| Área | Estado naquele snapshot |
|---|---|
| World authority | `VERIFIED` — reference scope |
| Transactional mutation | `VERIFIED` — reference scope |
| Journal / replay / rollback | `VERIFIED` — same-architecture reference scope |
| Dependency-derived execution | `VERIFIED` — correctness scope |
| Hybrid scalar/range patches | `VERIFIED` — tested correctness/performance scope |
| Spatial Kernel | `NOT YET SELECTED` naquele momento |
| Geometry Kernel | `NOT YET IMPLEMENTED` naquele momento |
| GPU authoritative execution | `NOT VERIFIED` naquele momento |
| Renderer production | `NOT IMPLEMENTED` |
| Physics production | `NOT IMPLEMENTED` |
| FOUNDATIONAL contracts | `NONE` |

## A.3 Arquitetura R0–R2.1

```text
                            ┌─────────────────────────────┐
                            │ AUTHORITATIVE WORLD STATE   │
                            │ identity / state / tx cursor│
                            └──────────────┬──────────────┘
                                           │ immutable pre-wave view
                                           ▼
                            ┌─────────────────────────────┐
                            │ EXECUTION KERNEL            │
                            │ declared read/write sets    │
                            │ explicit dependencies       │
                            │ deterministic DAG / waves   │
                            └──────────────┬──────────────┘
                                           │
                          ┌────────────────┼────────────────┐
                          ▼                ▼                ▼
                    System A         System B         System N
                    private work     private work     private work
                          │                │                │
                          └────────────────┼────────────────┘
                                           ▼
                            ┌─────────────────────────────┐
                            │ PRIVATE PATCH OUTPUTS       │
                            │ scalar + typed ranges       │
                            └──────────────┬──────────────┘
                                           │ stable SystemId ordering
                                           ▼
                            ┌─────────────────────────────┐
                            │ HYBRID TRANSACTION          │
                            │ one TransactionId           │
                            │ full validation             │
                            └──────────────┬──────────────┘
                                           │
                                           ▼
                            ┌─────────────────────────────┐
                            │ PATCH JOURNAL / COMMIT      │
                            │ forward history             │
                            │ ephemeral undo              │
                            └──────────────┬──────────────┘
                                           │
                                           ▼
                            ┌─────────────────────────────┐
                            │ NEW AUTHORITATIVE WORLD     │
                            └──────────────┬──────────────┘
                                           │
                                           ▼
                            ┌─────────────────────────────┐
                            │ CANONICAL SHA-256           │
                            └─────────────────────────────┘
```

## A.4 World Kernel — contratos verificados

### A.4.1 World state é autoridade

O `World` de referência mantém o estado que os testes consideram autoritativo.

Renderer, física e neural representations não faziam parte do núcleo e não eram necessários para reconstruir o estado testado.

### A.4.2 Identidade

Propriedades testadas:

- Entity ID `0` reservado para mutações de escopo do mundo;
- IDs criados sequencialmente;
- identidade não depende de endereço de memória;
- criação de identidade ocorre dentro de `CreateEntity`;
- não existe reserva externa que avance secretamente o cursor de IDs;
- `next_entity_id()` é leitura no modelo corrigido.

### A.4.3 Transaction ID

Transaction IDs são monotônicos no modelo de referência. Uma transação com ID inválido é rejeitada.

### A.4.4 Atomicidade de validação

A transação inteira é validada antes de a alteração autoritativa ser publicada. Uma operação inválida após operações válidas não produz commit parcial.

### A.4.5 Estado numérico

Valores vetoriais não finitos (`NaN`/`Infinity`) são rejeitados. `AdvanceReference` exige `dt` finito e não negativo.

## A.5 Canonical State Hash

O hash de referência é SHA-256 sobre serialização canônica little-endian contendo:

- schema version;
- next entity ID;
- last transaction ID;
- living entity count;
- IDs alocados em ordem;
- alive flag;
- health;
- bits IEEE-754 brutos de position e velocity.

Consequência deliberada:

```text
+0.0 != -0.0
```

para o hash, pois os bit patterns diferem. O hash detecta estado exato, não equivalência perceptual.

Limitação: o hash R1 percorre estado completo; incremental/Merkle permanece aberto.

## A.6 Journal e rollback

O journal persistente armazena forward transactions. Isso permite representar integração como `AdvanceReference(dt)` em vez de persistir todas as posições resultantes.

Rollback exato usa pre-state efêmero separado do formato forward persistente.

Antes de rollback, o journal compara o hash esperado com o World atual. Estado divergente fora da história controlada é rejeitado.

Limitações registradas:

- crash-safe record recovery;
- checksums por record;
- rollback storage comprimido/bounded;
- hashing incremental.

## A.7 Execution Kernel detalhado

Cada sistema possui stable `SystemId`, read set, write set, optional `after` e função.

Recursos iniciais:

- Identity;
- EntityState;
- Position;
- Velocity;
- Health.

Regras:

```text
READ X  + READ X  → no hazard
READ X  + WRITE X → serialize
WRITE X + READ X  → serialize
WRITE X + WRITE X → serialize
```

Write permission não implica read permission. Acesso não declarado rejeita o sistema antes do commit da wave.

Kahn topological ordering forma waves determinísticas; systems da mesma wave observam o mesmo pre-wave World.

Workers não têm autoridade. Eles produzem private patches; somente depois de completion, canonical ordering, merge e validation ocorre publicação no World.

A primeira implementação criava worker pool por `execute()`. O benchmark revelou contaminação por lifecycle; `ExecutionRuntime` persistente passou a ser o path de benchmark.

## A.8 Hybrid Transaction — R2.1

R2 mostrou que `Mutation` por entity podia dominar materialization/merge/commit em dense updates.

```text
PatchTransaction
├── scalar_mutations
├── vec3_patches
└── u32_patches
```

Uso:

- scalar: create/destroy, structure, scattered sparse writes;
- Vec3 ranges: Position/Velocity contiguous;
- U32 ranges: Health contiguous.

Scalar e range pertencem ao mesmo `TransactionId` e são publicados atomicamente.

Se scalar e range tentam escrever ambiguamente o mesmo componente/entidade, a transaction é rejeitada. Não existe silent precedence.

Fixed page 256 foi falsificado como representação universal e não faz parte da semântica do World.

`execute_patched()` preserva scheduling semantics:

```text
DAG execution
→ private scalar/range outputs
→ stable SystemId merge
→ one hybrid PatchTransaction per wave
→ validate
→ publish
```

## A.9 Determinismo — escopo do baseline

Comprovado naquele escopo:

- replay exact-state em x86-64 Linux;
- GCC 14.2 e Clang 17 com hashes idênticos nos cenários registrados;
- worker counts diferentes com hash idêntico em R2;
- serial oracle e R2.1 com hash idêntico nos workloads registrados.

Não comprovado naquele snapshot:

- Windows ↔ Linux;
- x86-64 ↔ ARM;
- diferentes FPU modes;
- CPU ↔ GPU;
- todos os sistemas futuros;
- rede distribuída.

Nenhum documento deve chamar esses resultados de determinismo universal.

## A.10 Hipóteses futuras registradas naquele snapshot

### Spatial Kernel

```text
World spatial data
    ↓
SpatialBackend contract
    ├── flat grid
    ├── hash grid
    ├── BVH
    ├── sparse brick hierarchy
    └── octree
```

Essa hipótese foi posteriormente executada em R3 e supersedida pelas seções ativas deste documento.

### Geometry Kernel

```text
World Object / Region
    ↓
GeometryProvider
    ├── Triangle
    ├── SDF
    ├── Voxel
    ├── Gaussian/Splat
    └── future provider
```

Posteriormente parcialmente demonstrado em R4 com múltiplas representações concretas sob contrato comum.

### GPU Laboratory

```text
Execution Graph
    ├── CPU workers
    ├── GPU compute
    └── future accelerators
```

Posteriormente iniciado em hardware real no R5E.

### Heterogeneous World

```text
Authoritative object/region
    ├── Render representation A
    ├── Physics representation B
    ├── destruction representation C
    └── distant representation D
```

Continua não fechado de ponta a ponta e permanece alvo de R6.

## A.11 Candidatos a contrato preservados

Itens que sobreviveram ao baseline R2.1 e continuam candidatos, sem status `FOUNDATIONAL`:

1. World state é autoridade; derived views não devem ser autoridade.
2. Identidade não depende de memória/renderer/scene graph.
3. Mudança autoritativa cruza fronteira validada.
4. Optimized path deve ser comparável a oracle/reference.
5. Workers calculam propostas; publicação autoritativa é separada.
6. Declared access deve ser machine-checkable.
7. Granularidade de patch é adaptativa e não semântica.
8. Overlap ambíguo não deve ser resolvido por precedência silenciosa.
