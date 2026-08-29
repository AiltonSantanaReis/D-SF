# R5D — Backend Capability & Translation Prototype

Status: **VERIFIED — CPU/reference translation scope**

Predecessor: R5C Device Work Contract — VERIFIED (CPU/reference planner scope)

## 1. Hypothesis

A planned `DeviceWorkPacket` graph can be translated into materially different backend execution models without making Vulkan, D3D12, SDF, clustered triangles, or API command objects part of the D-SF core contract.

The translation must preserve semantic identity, residency/version requirements, resource hazards, and explicit launch autonomy requirements.

This stage does **not** claim GPU performance, driver behavior, VRAM behavior, or synchronization performance on hardware.

## 2. Translation models

The reference translator currently models four lowering outcomes:

- `Direct`
- `Indirect`
- `GeneratedSequence`
- `WorkGraph`

They correspond to different capability regimes, not to API names embedded in `DeviceWorkPacket`.

Current public API research motivating the capability split includes Vulkan device-generated commands and descriptor heaps, and D3D12 ExecuteIndirect / Work Graphs. The D-SF contract does not include any Vulkan or D3D12 headers.

## 3. Capability lattice and no silent demotion

A direct/static packet may be promoted to indirect or device-generated execution because its launch dimensions are already known. The translated command records this as `BackendStaticRecord`, making the backend-owned launch metadata explicit.

A dynamic `Indirect` packet cannot be lowered to `Direct` without CPU readback/synchronization, so reference translation rejects that demotion.

A `DeviceGenerated` packet cannot be lowered to a backend without device-generated work while preserving its autonomy semantics, so the translator rejects the lowering.

This is intentionally asymmetric: stronger backend mechanisms may represent weaker/static work, but weaker mechanisms may not silently erase dynamic/autonomous semantics.

## 4. Launch-control dependency closure

R5D exposed a missing R5C invariant: non-direct launch-control resources were checked for residency but could theoretically be omitted from `packet.resources`, hiding them from hazard tracking.

The R5C planner now requires the launch-control resource to be declared as a readable packet resource. R5D independently enforces the same invariant at translation.

## 5. Descriptor-memory model

The first R5D prototype emitted one backend binding record per packet use. This duplicated the same descriptor thousands of times and did not represent descriptor-memory/heap-oriented execution well.

The accepted layout separates:

```text
Backend descriptor table (unique resources)
             |
             +--> descriptor_index
                    |
                    +--> per-command ResourceUse { index, READ/WRITE }
```

A 5,000-packet benchmark where every packet reads the same resource therefore emits:

```text
1 descriptor
5,000 resource uses
```

rather than 5,000 duplicate descriptors.

## 6. Barrier lowering

The R5C planner produces deterministic dependency waves. R5D translates cross-wave resource hazards into explicit backend barriers.

Rules in the reference lowering:

- READ -> READ: no barrier
- WRITE -> READ: barrier
- READ -> WRITE: barrier
- WRITE -> WRITE: barrier

A same-wave write hazard is rejected as an invalid plan instead of being repaired silently by the translator.

## 7. Canonical identity

Two digests are kept separate:

- `semantic_digest`: canonical work semantics, independent of backend lowering;
- `backend_digest`: canonical translated stream including launch model, descriptor handles, uses, barriers, and backend launch metadata.

The semantic digest includes packet ID, wave, domain, program, original launch mode, launch parameters, canonical resource accesses, parameters, and explicit dependencies.

The same direct/static packet translated through Direct, Indirect, GeneratedSequence, and WorkGraph profiles preserved the same semantic digest while producing distinct backend digests.

Reference test fingerprint:

```text
semantic_digest = 2894648114337488996
direct_digest = 1822164511422550589
generated_sequence_digest = 14902490109153104665
work_graph_digest = 9850775312450346422
```

These are reference fingerprints, not stable public ABI identifiers.

## 8. Correctness and adversarial gates

The R5D test verifies:

- direct preservation;
- static direct -> indirect promotion;
- static direct -> generated-sequence promotion;
- static direct -> work-graph promotion;
- semantic digest equality across those lowerings;
- backend digest distinction;
- indirect -> direct demotion rejection;
- device-generated -> indirect/direct demotion rejection;
- indirect -> generated promotion where supported;
- launch-control residency and explicit resource declaration;
- write/write hazard -> explicit barrier;
- read/read -> no false barrier;
- invalid same-wave write hazard rejection;
- packet-registration-order independence;
- packet-resource-order independence;
- descriptor-table deduplication;
- semantic distinction between Indirect and DeviceGenerated source contracts.

## 9. Reference translation scaling

Seven-run medians on the sandbox CPU/reference backend:

| packets | direct ms | indirect ms | generated-sequence ms | work-graph ms |
|---:|---:|---:|---:|---:|
| 100 | 0.088 | 0.034 | 0.031 | 0.031 |
| 1,000 | 0.383 | 0.356 | 0.347 | 0.316 |
| 5,000 | 1.954 | 2.045 | 1.890 | 1.966 |

These values measure only CPU-side reference translation. Differences between columns are noise/implementation effects and **must not** be interpreted as relative Vulkan/D3D12/GPU performance.

## 10. Verification

Final gate:

```text
GCC 14.2: 17/17 tests PASS
Clang 17: 17/17 tests PASS
kernel warnings: 0
ASan: PASS on R5A/R5B/R5C/R5D
UBSan: PASS on R5A/R5B/R5C/R5D
```

## 11. Promoted within R5D reference scope

- backend capabilities are explicit inputs to translation;
- launch autonomy forms a capability lattice, not a universal fallback chain;
- dynamic/autonomous work is never silently demoted;
- launch-control buffers are explicit resources;
- unique descriptor table is separate from per-command resource use;
- dependency waves lower to explicit resource barriers;
- semantic and backend fingerprints are separate;
- translated streams are canonical under irrelevant input ordering;
- Direct/Indirect/GeneratedSequence/WorkGraph are backend execution models, not World or Geometry concepts.

## 12. Explicitly not promoted

- Vulkan DGC is faster than Direct or Indirect;
- D3D12 Work Graphs are faster than ExecuteIndirect;
- descriptor heaps are universally optimal on all hardware;
- the current reference descriptor table is a final hardware layout;
- CPU translation medians predict GPU/driver performance;
- current barrier records are final Vulkan/D3D12 barriers;
- device-generated execution is universally preferable;
- Direct work should normally be promoted to generated work merely because the backend supports it.

## 13. Conclusion

R5D validates the architectural boundary:

```text
World
  != Geometry
  != Device Package
  != Device Work
  != Backend Command Model
```

The same semantic work can cross materially different backend lowering models where semantics permit it, while capability-incompatible demotions are rejected explicitly.

**R5D status: VERIFIED — CPU/reference translation scope.**

The next unresolved R5 question should involve real backend execution/synchronization semantics. Performance claims require real GPU hardware and are outside the sandbox evidence.
