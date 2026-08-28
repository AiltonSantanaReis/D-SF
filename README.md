# D-SF — Experimental World / Geometry / Device Architecture

D-SF is an experimental engine research program built around a strict rule: **authoritative world state is not visual geometry**. Representations, spatial structures, physics views and device work are derived from authoritative semantic/spatial state.

The project is developed experimentally. A mechanism is promoted only after a reference/oracle, controlled experiment and recorded evidence support it.

## Evidence classes

- **REFERENCE RESULT** — validated CPU/reference semantics or measurements in the research environment.
- **HARDWARE RESULT** — executed and measured/verified on real target hardware.
- **PARTIAL** — useful evidence with an explicit unresolved scope.
- **FALSIFIED** — tested path that failed its hypothesis and is retained as evidence.
- **NOT MEASURED** — no claim is permitted.

## Verified research state

| Stage | Status | Scope |
|---|---|---|
| R0 | VERIFIED | Authoritative world reference |
| R1 | VERIFIED | Journal / hash / replay / rollback reference |
| R2 | VERIFIED | Dependency execution graph correctness; performance partial |
| R2.1 | VERIFIED | Hybrid scalar/range transaction patches |
| R3 / R3.1 / R3.2 | VERIFIED | Shared spatial snapshot, cost-aware fabric, budgeted maintenance — CPU/reference |
| R4A–R4F | VERIFIED | Heterogeneous geometry contract, Sparse SDF, clustered triangles, selection, telemetry and safe exploration — CPU/reference |
| R5A | VERIFIED | Device residency contract — reference backend |
| R5B | VERIFIED | Geometry device packages + atomic residency — reference backend |
| R5C | VERIFIED | Device work contract/planner — CPU/reference |
| R5D | VERIFIED | Backend capability + translation model — CPU/reference |
| R5E-HW01 | VERIFIED | Real GPU/Vulkan capability fingerprint |
| R5E-HW02 | VERIFIED | Real `VkDevice` + device memory roundtrip |
| R5E-HW03 | VERIFIED | Real Direct compute vs exact CPU oracle |
| R5E-HW04 | VERIFIED | Real Indirect compute vs exact CPU oracle |

**No stage is labeled foundational.** Contracts remain subject to stronger evidence.

## Architecture

```text
Authoritative World
    |
    +--> Hybrid Transactions / Change Journal
    |
    +--> Shared Spatial Snapshot
    |      +--> Wide BVH8
    |      +--> Morton BVH8
    |      +--> cost-aware / budgeted maintenance
    |
    +--> GeometrySet
    |      +--> Triangle / Clustered Triangle
    |      +--> Analytic SDF / Sparse SDF
    |      +--> capability + revision + error-budget selection
    |
    +--> Device Geometry Packages
           |
           +--> Residency contract
           +--> DeviceWorkPacket DAG
           +--> backend-neutral translation
                    |
                    +--> Vulkan hardware bring-up (R5E)
```

Core separation:

```text
World != Geometry != Device Package != Device Work != Backend Command Model
```

## Current hardware result

On the tested NVIDIA GeForce RTX 3070 Ti / Vulkan 1.4.341 configuration:

- HW03 Direct compute produced `0x8e2eef1faffc414f` for 1,048,576 `uint32` outputs.
- HW04 `vkCmdDispatchIndirect` used a resident 12-byte `{4096,1,1}` control buffer and produced the **same exact output hash**.
- Both gates matched the independent CPU oracle for every element and completed with **0 validation errors / 0 validation warnings**.

This establishes functional equivalence for that workload. It does **not** establish Direct/Indirect performance equivalence or superiority. GPU timestamp characterization is next.

See:

- `docs/RESEARCH_STATUS_R3_R5E.md`
- `docs/R5E_HARDWARE_BRINGUP.md`
- `docs/R5E_HW04_CLOSEOUT.md`
- `docs/R5E_CURRENT_STATE.json`

## Build — repository snapshot

The repository is a **research record**. Experimental implementation evolves locally and is promoted to GitHub at verified milestones rather than continuously. The tracked portable tree therefore must not be interpreted as the authority for unverified work.

## Research rules

1. Theory / hypothesis → oracle → controlled experiment → data → conclusion → specification.
2. AI may propose; the kernel validates.
3. Visual geometry and physical geometry are derived views, not world truth.
4. No GPU performance claim is inferred from CPU/reference behavior.
5. A newer API or technique is never selected merely because it is newer.
6. Failed hypotheses are evidence, not erased history.
7. Performance claims are workload- and hardware-specific.
