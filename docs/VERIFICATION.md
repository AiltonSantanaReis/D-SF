# D-SF — Registro Canônico de Verificação

## 1. Finalidade

Este documento contém evidência reproduzível e fingerprints promovidos do D-SF. Resultados antigos não são removidos quando novas fases avançam; são preservados e recebem escopo explícito.

Classes usadas:

- `REFERENCE RESULT` — CPU/reference semantics ou medições de laboratório;
- `HARDWARE RESULT` — executado/observado em hardware alvo identificado;
- `PARTIAL` — evidência válida, porém incompleta para uma conclusão mais forte;
- `FALSIFIED` — caminho rejeitado no escopo testado;
- `NOT MEASURED` — nenhuma afirmação de custo permitida.

Todo número deve ser lido como “observado neste workload/ambiente”.

---

# PARTE I — BASELINE R0–R2.1 PRESERVADO

## 2. Regra de interpretação do baseline

Particularmente:

- `position += velocity * dt` não equivale a NPC completo;
- throughput de scheduler compute-only não equivale a gameplay autoritativo;
- CPU reference result não equivale a GPU result;
- x86-64 Linux não prova Windows/ARM;
- duas versões de compilador não provam determinismo universal.

## 3. Ambiente R0–R2.1

Os artefatos finais R2/R2.1 registraram:

- Linux x86-64;
- 5 CPUs/cores disponíveis no ambiente compartilhado;
- host model exposto em uma execução: AMD EPYC 9V74;
- GCC 14.2;
- Clang 17;
- CMake 3.31.6;
- nenhuma GPU de produção/Vulkan usada nas medições.

Uma etapa anterior expôs Intel Xeon Platinum 8573C. Como a sandbox é virtualizada/compartilhada e a identidade de host variou, modelo de CPU não é tratado como propriedade estável.

---

## 4. R0 — Minimal Authoritative World

Invariantes testados:

1. Entity ID `0` reservado para mutações de world scope.
2. IDs estáveis/sequenciais sem skip silencioso por `CreateEntity`.
3. Transaction IDs monotônicos.
4. Toda transaction validada antes da primeira mutação autoritativa.
5. Vec3 não finito rejeitado.
6. `AdvanceReference` exige `dt` finito/não negativo.
7. Renderer/física não fazem parte do World Kernel de referência.

Correção descoberta no caminho para R1: `reserve_entity_id()` avançava identidade fora da transaction. `CreateEntity` passou a alocar dentro da própria transaction e `next_entity_id()` tornou-se read-only.

### Baselines iniciais

| Entidades | Tempo CPU reportado |
|---:|---:|
| 100.000 | ~0.140 ms/frame |
| 1.000.000 | ~1.357 ms/frame |
| 3.000.000 | ~3.917 ms/frame |

Execução intermediária também citou ~1.22 ms para 1M; diferença preservada como jitter/execução distinta.

Após integração transacional:

- 1.000.000 entidades / 120 frames;
- spawn transaction: 3.000.000 mutations;
- spawn commit: ~39.873 ms;
- simulation: ~1.229 ms/frame;
- ~813.5M minimal position updates/s.

Workload:

```text
position += velocity * dt
```

---

## 5. R1 — Change Journal / SHA-256 / Replay / Rollback

Canonical SHA-256 cobre serialização little-endian de schema version, identity cursor, transaction cursor, entity IDs/alive/health e raw IEEE-754 bits de position/velocity.

Cross-check C++ vs Python `hashlib.sha256()`:

```text
9052d221ad22d52fb0a43dbec4410a9546d7bbb968642614ee0deb55758e7c33
```

### Replay determinístico

Workload:

- 256 entidades iniciais;
- 5.000 frames;
- 5.001 transactions incluindo spawn;
- health changes, destruction, later create;
- `AdvanceReference(1/60)` transacional.

Hash final:

```text
9e6b6a3bac5a0564e2f3100bcf7eed9d0e48ef44615382f27931d5dcb9960c57
```

Reproduzido por original World, replay pristine, save/load e GCC/Clang x86-64 Linux.

### Rollback

- rollback para checkpoint reproduziu hash original;
- reapply tail reproduziu hash final;
- full rollback do spawn journal-owned restaurou pristine hash + identity cursor.

### Divergence guard

Rollback é rejeitado se World atual não corresponde ao hash tail esperado.

### Performance R1

- journal commit total ~272.054 ms;
- average ~0.054 ms/frame;
- journal file 224.236 bytes;
- save ~0.812 ms;
- load ~0.720 ms;
- replay ~1.504 ms;
- rollback all ~275.413 ms.

Limitações: full-state hash por commit e undo que captura todos os touched states não escalam como implementação final.

---

## 6. R2 — Dependency Execution Graph

Correctness test hash 4.096 entities / 120 frames:

```text
057e4f9d9e4921cb93da1a0c5b1245fafbadb49ce8755f572ebff34b558e53ca
```

Cross-worker workload 8.192 entities / 60 frames / 4 systems / 2 waves, workers 1/2/4/5:

```text
71ccbd8aaaed14974c7c70ab4879f099f42195dd1ed6d312d93fa8642cf4218c
```

### Sanitizers

- ASan pass;
- UBSan pass;
- TSan concurrent R2 test sem race reportada.

### Compute-only microbenchmark

32 independent systems × 750.000 deterministic integer iterations:

| Workers | Mediana | Speedup |
|---:|---:|---:|
| 1 | 80.508 ms | 1.000× |
| 2 | 41.275 ms | 1.951× |
| 4 | 24.551 ms | 3.279× |
| 5 | 23.186 ms | 3.472× |

Checksum:

```text
3462961269496396242
```

### Authoritative World benchmark

8.192 entities / 60 frames / 4 systems / 2 waves / per-entity patches:

| Workers | Mediana | Speedup |
|---:|---:|---:|
| 1 | 49.192 ms | 1.000× |
| 2 | 41.956 ms | 1.172× |
| 4 | 46.826 ms | 1.051× |
| 5 | 45.700 ms | 1.076× |

Conclusão: correctness `VERIFIED`; performance `PARTIAL`; patch materialization/merge/commit limitava escala.

---

## 7. R2.1 — Hybrid Transaction Patches

Candidatos: per-entity oracle, contiguous ranges, fixed 256 pages, page clone/COW-style, disjoint parallel publication e persistent publisher.

### Patch representation equivalence

4.096 entities / 120 frames:

```text
a073236582885e8cd53f22aa4825ed539a00c74c7c026e61d9e1db9940ada47e
```

Replay/rollback/save/load/overlap rejection/non-finite rejection passaram.

### Execution equivalence

4.096 entities / 120 frames, scalar serial oracle vs range worker-pool + PatchJournal:

```text
657f7bd1092e03c74acf7a38b7a70243f3a8decef268fcce0c552b4195f34a94
```

Journal: 240 wave transactions; replay = same hash.

### Integrated benchmark — 8.192 entities / 60 frames

| Candidate | Median | Speedup vs scalar serial |
|---|---:|---:|
| R2 scalar serial | 44.803 ms | 1.000× |
| R2 scalar / 4 workers | 61.923 ms | 0.724× |
| R2.1 ranges serial | 32.612 ms | 1.374× |
| ranges / 4 workers | 35.361 ms | 1.267× |
| ranges / 4 workers + persistent commit | 36.034 ms | 1.243× |

### 100.000 entities / 20 frames

Hash:

```text
e6803f6411816d3e2261f091e7eb82718262ee9969b33dce9135467c9072c2c4
```

| Candidate | Median | Speedup |
|---|---:|---:|
| scalar serial | 192.925 ms | 1.000× |
| scalar / 4 workers | 151.304 ms | 1.275× |
| ranges serial | 138.596 ms | 1.392× |
| ranges / 4 workers | 102.150 ms | 1.889× |
| ranges / 4 workers + persistent commit | 102.841 ms | 1.876× |

### 1.000.000 entities / 3 frames

Hash:

```text
61d624a0af70729626dafebd3b3bea4cb5a074e625ec7f17ac981f6eef5a2c60
```

| Candidate | Median | Speedup |
|---|---:|---:|
| scalar serial | 740.970 ms | 1.000× |
| scalar / 4 workers | 517.654 ms | 1.431× |
| ranges serial | 228.456 ms | 3.243× |
| ranges / 4 workers | 166.262 ms | 4.457× |
| ranges / 4 workers + persistent commit | 150.455 ms | 4.925× |

Essa tabela **não** é comparação contra engines externas.

### Dense payload

1M entities / 3 components / 1 frame:

- scalar: 3.000.000 records / ~96.000.000 B vector capacity;
- ranges: 3 records / ~28.000.120 B;
- pages256: 11.721 records / ~28.468.840 B.

### Sparse falsification — 100k, 200 frames, 1% Position

Clustered 1%:

| Candidate | Total | Records/frame | Payload/frame |
|---|---:|---:|---:|
| scalar | 16.087 ms | 1.000 | 32.512 B |
| exact ranges | 17.213 ms | 10 | 12.400 B |
| pages256 | 18.931 ms | 10 | 31.120 B |
| full range | 249.793 ms | 1 | 1.200.040 B |

Scattered 1%:

| Candidate | Total | Records/frame | Payload/frame |
|---|---:|---:|---:|
| scalar | 12.036 ms | 1.000 | 32.512 B |
| one-value ranges | 17.018 ms | 1.000 | 52.000 B |
| pages256 | 285.824 ms | 391 | 1.215.640 B |
| full range | 249.986 ms | 1 | 1.200.040 B |

Falsificado: fixed page 256 como solução universal.

### Clean release R2.1

- GCC 14.2 Release: 4/4 PASS, zero warnings no configured set;
- Clang 17 Release: 4/4 PASS, zero warnings no configured set;
- ZIP clean extraction rebuilt and tested;
- ZIP SHA-256:

```text
a4d0bcdef114e84456758c6c0067df50b1aaf599bda7686945d433aa9137cd63
```

---

# PARTE II — R3 SPATIAL KERNEL

## 8. Shared Spatial Snapshot

Architecture:

```text
Authoritative World
→ R2.1 Hybrid Transaction
→ Shared Spatial Snapshot
→ derived BVH views
```

Snapshot SoA center/half, dense/sparse identity, ~24 bytes/object dense no modelo estrutural.

R2.1 propagation usa `SpatialChangeSet {dirty_slots, dirty_ranges}`. Create/destroy causa structural rebuild. Version + structure_revision rejeitam stale/skipped deltas.

Views:

- WideBvh8View: 8-wide, 16-bin SAH/refit;
- MortonBvh8View: deterministic 30-bit radix, 3×10-bit.

Correctness: query equivalence contra oracle antes/depois; GCC/Clang/ASan+UBSan passaram no closeout R3.

### 1M medians — referência CPU

```text
snapshot        40.986 ms
SAH build     1782.978 ms
Morton radix    55.231 ms
publish 2%       0.924 ms
SAH 2%          13.960 ms
Morton 2%       51.575 ms
publish 50%      2.516 ms
SAH 50%       1664.696 ms
Morton 50%      54.426 ms
```

Shared memory structural model ~66.0 MB vs duplicated ~106.0 MB, ~37.73% reduction. Isto é modelo estrutural, não RSS.

### R3.1 Cost-Aware Fabric

Compact catchup:

```text
snapshot copy median 3.508 ms
background SAH       2969.335 ms
compact catchup         9.041 ms
```

Background interference elevou active Morton update ~43.5%; unrestricted background work não foi promovido.

Cost policy:

```text
predicted = update_ms + sampled_query_ms/sample_count * expected_queries
```

No family de AABB query testada, policy acertou winner subsequente 23/23 scenarios. Escopo limitado à família testada.

### R3.2 Budgeted Scheduler

Resumable/cooperative SAH state machine; manutenção recebe apenas slack.

- 100k SAH promoveu ~92 frames com ~1 ms maintenance slices;
- 250k ~292 frames;
- 1M com Morton critical ~54.8 ms/frame → 80/80 frames sem slack, maintenance 0;
- continuous dirty-refit queue substituiu batch catchup que podia starvation;
- extreme 1M, 50% dirty/frame ×30: peak ~10.83M pending; após parar produção, backlog drenou em 181 slices nominal ~0.5 ms, final backlog 0 e query equivalence PASS.

R3 `VERIFIED — CPU/reference`. Limite explícito: 1M mandatory Morton rebuild/frame permanece >16.67 ms na sandbox.

---

# PARTE III — R4 GEOMETRY KERNEL

## 9. R4A Representation Contract

Providers capability-oriented:

- Bounds;
- RaySurface;
- SignedDistance.

TriangleReference: Bounds/Ray. AnalyticSdf: todas.

Cube: 12 triangles vs analytic box SDF, 882 ray hits equivalentes incluindo inside rays/non-normalized directions.

200k rays reference timing: triangle brute-force ~17.5 ms, analytic SDF ~7.57 ms. Não interpretar como comparação de production triangle renderer.

## 10. R4B Sparse Implicit

Sparse narrow-band hierarchy com root sparse map → regions → upper/lower hierarchy → bricks; int16 quantized samples e error certificate.

Caminhos rejeitados:

- capped sphere tracing dependente de resolução;
- naive hierarchical child testing pior.

Caminho aceito: brick-level 3D DDA + local sphere tracing.

100k rays ~138/135/145 ms em resoluções 1/32, 1/64, 1/128 no set testado.

Sphere sparse vs dense-int16 storage:

- 1/32: 96.9%;
- 1/64: 55.3%;
- 1/128: 31.7%.

Box:

- 1/32: 191.5%;
- 1/64: 133.1%;
- 1/128: 80.0%.

Conclusão: sparse não é universalmente menor.

## 11. R4C Clustered Triangle

Adjacency-aware clusters, uint8 local indices, contiguous payload, BVH8.

Per-cluster quantization abriu cracks no torus e foi rejeitada. Shared resource quantization frame foi promovido; BVH quantized bounds usam conservative one-unit outward padding.

Cluster sweep 32/64/96/128/192/255. CPU experimental default 64 vertices / 124 triangles.

~131k triangles, 5k rays: build ~71 ms, rays ~6.2 ms, quant storage ~1.46 MB.

~524k triangles: source ~9.45 MB, quant ~6.11 MB, build ~369 ms, 5k rays ~12.5 ms.

~2.10M triangles: source ~37.77 MB, quant ~23.31 MB, build ~1.28 s, 2k rays ~5.74 ms.

## 12. R4D Selection

Sem weighted magic score. Hard constraints + explicit objective + Pareto frontier.

Sphere example:

```text
Sparse error   ~0.0135324
Sparse storage ~2,842,910 B
Cluster error  ~0.0192412
Cluster storage ~46,288 B
```

Sparse ganhou ray latency nos workloads CPU testados; Clustered ganhou memória. Pareto contém ambos.

Telemetry calibration separada do runtime; wrong revision/workload e duplicate ambiguity são rejeitados.

## 13. R4E Online Telemetry

Execution Kernel `observe_batch()` mede um batch real uma vez; Geometry interpreta por handle/revision/workload/device/batch-size class.

Recent robust window usa median, MAD, P90 e cost. Outlier sample `1,1,1,1,1,1,50` preservou median=1 e P90=50.

Always-on per-call microbatch timing foi falsificado por overhead; sampling por work volume/gap foi introduzido.

## 14. R4F Safe Exploration

Inactive provider improvement é impossível de detectar sem executar alternativa/shadow work ou aceitar ignorância.

Exploration é opt-in, sem shadow duplicate work; batch real só pode ser roteado a alternativa que satisfaz hard constraints.

Refresh-age sweep mostrou que age 1/2 com min_observed=3 pode entrar em ciclo quase permanente de exploration. Invariante: freshness horizon não deve ser menor que confidence sample requirement.

Não existe refresh universal ótimo.

R4A–F: `VERIFIED — CPU/reference` no escopo documentado.

---

# PARTE IV — R5 DEVICE REFERENCE ARCHITECTURE

## 15. R5A Residency

Reference backend semantics:

- explicit budget;
- LRU entre unpinned;
- pinning;
- immutable key/content;
- generational handles;
- key→slot hash lookup;
- free-list reuse;
- rollback/restoration após failed upload.

Nenhuma alegação de VRAM real em R5A.

## 16. R5B Geometry Device Packages

Canonical `RepresentationArchive` little-endian, sem struct ABI/padding memcpy.

Current small test package fingerprints:

```text
sparse bytes  44,422
cluster bytes 356
sparse fingerprint  14748051446487735809
cluster fingerprint 2642998132373811531
```

Atomic `ensure_group`: validate → eviction plan → stage → publish; adversarial failure no terceiro upload depois de dois sucessos deixa 0/3 package resources residentes e restaura unrelated state/stats.

## 17. R5C Device Work

Reference planner digest:

```text
9229187388161744994
```

Representative planner scaling independent/write-chain, CPU reference:

```text
100       ~0.039 / 0.029 ms
500       ~0.171 / 0.121 ms
1,000     ~0.351 / 0.275 ms
5,000     ~5.182 / 3.500 ms
10,000    ~14.228 / 10.981 ms
50,000    ~275.5 / 244.3 ms
100,000   ~1.104 s / 923.7 ms
```

Conclusão: `DeviceWorkPacket` é coarse node; 100k host packets não é granularidade de produção aceitável.

## 18. R5D Backend Translation

Reference fingerprints:

```text
semantic digest                    2894648114337488996
direct backend digest             1822164511422550589
generated-sequence backend digest 14902490109153104665
work-graph backend digest         9850775312450346422
```

Reference CPU translation medians, 7 runs:

| packets | direct | indirect | generated-sequence | work-graph |
|---:|---:|---:|---:|---:|
| 100 | 0.088183 ms | 0.033841 | 0.031347 | 0.031297 |
| 1.000 | 0.383327 | 0.356207 | 0.347003 | 0.316406 |
| 5.000 | 1.953790 | 2.044830 | 1.890270 | 1.966190 |

Essas colunas fazem quase o mesmo trabalho estrutural CPU/reference e **não** são comparação de Vulkan/D3D/GPU speed.

Final R5D gate: GCC 17/17 PASS, Clang 17/17 PASS, no configured warnings; ASan+UBSan dedicated R5A-D tests passed.

---

# PARTE V — R5E HARDWARE RESULTS

## 19. Hardware configuration

```text
GPU                 NVIDIA GeForce RTX 3070 Ti
Vendor/Device       10DE:2482
VRAM physical       8192 MiB
NVIDIA driver       610.47
Vulkan loader       1.4.357
Vulkan device API   1.4.341
Conformance         1.4.3.3
Queue family used   2 (compute + transfer)
```

## 20. HW01 — Hardware Fingerprint

Status: `VERIFIED — HARDWARE RESULT`.

Feature bits reais confirmados incluem:

- `bufferDeviceAddress`;
- `timelineSemaphore`;
- `descriptorIndexing`;
- `synchronization2`;
- `dynamicRendering`;
- `descriptorBuffer`;
- `descriptorHeap`;
- `descriptorHeapCaptureReplay`;
- `deviceGeneratedCommands`;
- `dynamicGeneratedPipelineLayout`;
- mesh/task shader;
- acceleration structure / ray tracing pipeline.

Capability presence não é performance claim.

## 21. HW02 — Real Memory Roundtrip

```text
payload: 16,777,216 bytes
input FNV-1a64:  0xc0dd6ba4a0e044c2
output FNV-1a64: 0xc0dd6ba4a0e044c2
memcmp: PASS
validation errors: 0
validation warnings: 0
```

Real `VkDevice`, host-visible buffers, device-local buffer, synchronization2 e readback foram exercitados.

Memory budget foi capturado como telemetria dinâmica; não tratado como contabilidade exata das próprias alocações.

## 22. HW03 — Direct Compute

```text
elements: 1,048,576 uint32
local_size_x: 256
workgroups_x: 4096
operation: out[i] = in[i] * 3u + 7u
input FNV-1a64: 0xac41f8629b52fce0
CPU oracle:       0x8e2eef1faffc414f
GPU output:       0x8e2eef1faffc414f
full comparison: PASS
validation errors: 0
validation warnings: 0
```

Status: `VERIFIED — HARDWARE RESULT`.

## 23. HW04 — Indirect Compute

Evidence ZIP SHA-256:

```text
da327a30d50ebfcc90431d06cd14be585efb0d6b29cb16dadbe308a3eb1faa31
```

```text
launch: vkCmdDispatchIndirect
control source: resident device buffer
VkDispatchIndirectCommand: {4096,1,1}
control bytes: 12
control FNV-1a64: 0x3891ae3c62606753
CPU oracle:       0x8e2eef1faffc414f
GPU output:       0x8e2eef1faffc414f
full comparison: PASS
validation errors: 0
validation warnings: 0
```

Status: `VERIFIED — HARDWARE RESULT`.

HW03 == HW04 == CPU oracle no workload testado. Nenhuma conclusão de performance Direct vs Indirect foi promovida.

---

# PARTE VI — REGRESSION GATE CONSOLIDADO

## 24. CPU/reference regression antes do snapshot HW04

Ambiente: Linux x86-64 sandbox, GCC 14.2, Release.

```text
ctest: 17/17 PASS
failed: 0
real test time: ~2.86 s
```

Tests:

1. `kernel_tests`
2. `r2_execution_tests`
3. `r21_patch_tests`
4. `r21_execution_patch_tests`
5. `r3_snapshot_tests`
6. `r3_fabric_tests`
7. `r32_budgeted_spatial_tests`
8. `r4_geometry_contract_tests`
9. `r4_sparse_sdf_tests`
10. `r4_clustered_triangle_tests`
11. `aion_r4_geometry_fabric_tests`
12. `aion_r4_online_geometry_telemetry_tests`
13. `aion_r4_online_geometry_routing_tests`
14. `aion_r5_device_tests`
15. `aion_r5_geometry_device_tests`
16. `aion_r5_device_work_tests`
17. `aion_r5_backend_translation_tests`

Compiler/sanitizer claims adicionais permanecem vinculados aos closeouts específicos; o gate acima não deve ampliar o escopo deles.

---

## 25. Non-claims atuais

Ainda não verificado/medido até HW04:

- Direct vs Indirect GPU execution performance;
- CPU preparation/submit comparison;
- DGC runtime/performance;
- descriptor buffer/descriptor heap runtime comparison;
- PCIe bandwidth characterization;
- production residency/eviction behavior;
- final synchronization/barrier policy;
- integrated World → Spatial → Geometry → Device demonstrator;
- universal cross-platform deterministic equivalence.
