# D-SF — Research Ledger

Este ledger registra decisões e resultados consolidados. Relatórios de fase preservam detalhes experimentais completos.

## R0/R1 — World + History

- World autoritativo, identidade e transações validadas demonstrados.
- Journal, canonical SHA-256, save/load, replay e rollback demonstrados.
- 1M lightweight entity integration é benchmark de estado mínimo, **não NPC completo**.
- Whole-world hashing/undo copy foram identificados como custos de escala.

## R2 — Execution Kernel

**Decisão:** promover dependency graph determinístico com access declarations e private worker patches.

**Evidência:** read/read parallel wave; hazards serializam; cycles/regras de acesso falham; worker counts preservam hash.

**Limite:** world workload não escalou como compute microbench devido a materialização/merge/commit/memory bandwidth.

## R2.1 — Hybrid Transaction Patches

**Hipótese:** uma granularidade universal de mutation não é adequada.

**Resultado:** VERIFIED. Scalar sparse lane + typed dense ranges coexistem.

**Falsificado:** fixed page 256 como solução universal; always-parallel commit como política universal.

## R3 — Spatial Kernel

**Resultado final:** Shared Spatial Snapshot + derived BVH views.

- WideBVH8 SAH/refit forte para baixa mudança.
- Morton wide BVH rebuild forte para alta mudança.
- cost policy deve considerar update + query count * query cost + rebuild/switch.
- compact catchup e budgeted/cooperative build reduzem bloqueio, mas background work não é “grátis”.
- fixed region partition e MortonClusterBVH8 não foram promovidos.

R3 fechado como VERIFIED no CPU/reference scope. 1M mandatory Morton rebuild por frame continua acima de 60 Hz no ambiente CPU testado.

## R4 — Geometry Kernel

### R4A Representation Contract
`GeometryProvider` capability-oriented; TriangleReference e AnalyticSdf comprovam contrato comum.

### R4B Sparse Implicit
Sparse narrow-band hierarchy verificada. Brick DDA + local sphere tracing substituiu abordagens que pioravam com resolução. Sparse não é universalmente menor nem mais rápido.

### R4C Clustered Triangle
Adjacency-aware clusters + BVH8. Per-cluster quantization independente causou cracks e foi rejeitada; shared resource quantization frame foi promovido.

### R4D Selection
Hard constraints + explicit objective + Pareto frontier. Sparse e Clustered permanecem no frontier por tradeoffs diferentes.

### R4E Telemetry
Online batch telemetry workload/device/batch-scoped com median/MAD/P90. Always-on microbatch timing foi rejeitado por overhead.

### R4F Exploration
Inactive provider improvement é não observável sem exploration/shadow work. Refresh horizon não possui valor universal; freshness é SLA/policy de workload.

R4 fechado como VERIFIED no CPU/reference scope.

## R5A–R5D — Device Architecture

### R5A Residency
Explicit budget/LRU/pinning/generational handles. Reference backend apenas; nenhuma alegação VRAM real nesta fase.

### R5B Device Geometry Packages
Canonical RepresentationArchive, compiler registry e atomic `ensure_group`. Bug real de staging-slot sentinel encontrado e corrigido.

### R5C Device Work
Generic `DeviceWorkPacket`, resource-centric hazard tracker e deterministic frontier Kahn. 100k host packets mostraram custo inadequado e reforçaram que packet é coarse node.

### R5D Backend Translation
Capability lattice e asymmetric launch semantics. Semantic digest separado de backend digest. Descriptor table deduplicada + resource uses. Barrier lowering explícito. Direct static promotion permitido; dynamic Indirect/DeviceGenerated não podem ser silenciosamente demovidos.

R5A-D fechados como VERIFIED em reference/CPU translation scope.

## R5E — Real Hardware Bring-up

Hardware atual: NVIDIA GeForce RTX 3070 Ti, driver 610.47, Vulkan device API 1.4.341.

### HW01 — VERIFIED
Capability fingerprint real. Feature bits confirmaram `bufferDeviceAddress`, `synchronization2`, timeline semaphore, descriptor buffer/heap, DGC, mesh shader e ray tracing capabilities no driver testado.

### HW02 — VERIFIED
Real `VkDevice` + real memory roundtrip. 16 MiB HOST -> DEVICE_LOCAL -> HOST, byte-exact, validation clean. `VK_EXT_memory_budget` capturado como telemetria dinâmica, não contabilidade exata do buffer.

### HW03 — VERIFIED
Direct compute real:

- 1,048,576 `uint32`;
- `local_size_x=256`, 4096 workgroups;
- `out[i] = in[i] * 3u + 7u`;
- CPU expected/GPU output FNV-1a64 `0x8e2eef1faffc414f`;
- full element comparison PASS;
- validation 0 errors / 0 warnings.

### HW04 — VERIFIED
Indirect compute real com resident device control buffer:

- `VkDispatchIndirectCommand {4096,1,1}`;
- 12-byte control resource com `INDIRECT_BUFFER_BIT`;
- transfer -> indirect visibility explícita;
- `vkCmdDispatchIndirect` executado;
- mesmo CPU/GPU hash `0x8e2eef1faffc414f`;
- validation 0 errors / 0 warnings.

**Conclusão HW03/HW04:** equivalência funcional no workload testado. **Performance equivalence/superiority não medida.**

### Próxima decisão
HW05 medirá timestamps GPU e custos CPU relevantes antes de qualquer escolha Direct vs Indirect. DGC/descriptor modern paths permanecem candidatos a testar, não preferências.
