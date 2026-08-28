# D-SF — Arquitetura Ativa

Status: arquitetura experimental consolidada até **R5E-HW04**. Nenhum contrato está `FOUNDATIONAL`.

Este documento mantém duas camadas deliberadas:

1. **arquitetura ativa**, refletindo o estado verificado mais recente;
2. **baseline detalhado R0–R2.1 preservado**, para que a evolução arquitetural não apague invariantes, limitações e justificativas anteriores.

O snapshot canônico R2.1 exato também é preservado em `docs/history/R2_1/ARCHITECTURE.md`.

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

> Este apêndice resume os invariantes do baseline anterior. A versão canônica exata, byte-for-byte, está em `docs/history/R2_1/ARCHITECTURE.md`.

## A.1 Estado arquitetural no fechamento R2.1

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

## A.2 World / hash / journal

O baseline preserva identity allocation dentro de `CreateEntity`, monotonic TransactionId, full validation before publication, non-finite rejection, canonical little-endian SHA-256 e divergence-guarded rollback.

## A.3 Execution / hybrid patches

Declared reads/writes/after, deterministic Kahn waves, immutable pre-wave World, private patches e one authoritative transaction/wave. R2.1 adiciona scalar + Vec3/U32 ranges sob mesmo TransactionId e rejeita overlap ambíguo.

## A.4 Determinismo — escopo

Observado em x86-64 Linux nos workloads registrados, GCC/Clang, múltiplos worker counts e oracle vs optimized paths. Não equivale a determinismo universal Windows/Linux/ARM/GPU.

## A.5 Hipóteses que foram posteriormente executadas

R3 Spatial, R4 Geometry e R5 GPU/device eram futuras no snapshot R2.1 e hoje são descritas pelas seções ativas acima. R6 Heterogeneous World continua não fechado de ponta a ponta.
