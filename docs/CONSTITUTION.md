# Aion Research Lab — Architecture Constitution v0.4

## Mission
Discover, by reproducible experiments, an engine architecture that maximizes world complexity and perceptual quality per unit of compute without binding the world to one geometric representation or processor class.

## Verified contracts (not yet FOUNDATIONAL)
1. **World state is authoritative.** Rendering, physics and neural representations are derived views.
2. **Stable identity.** Entity identity does not depend on memory address, scene graph position, renderer or processor. In the R1 reference model, identity allocation occurs inside `CreateEntity` transactions.
3. **Transactional authoritative mutation.** Reference authoritative state changes, including simulation advancement, enter through validated transactions.
4. **Reference before optimization.** Every optimized kernel must remain comparable against a simple reference implementation.
5. **Evidence before promotion.** A hypothesis becomes VERIFIED only with reproducible correctness and benchmark evidence.
6. **No fake hardware evidence.** Simulated or analytical GPU estimates are never labeled measured GPU performance.
7. **Workers do not own authority.** Parallel systems may compute private patches concurrently, but the reference World changes only at a validated deterministic transaction boundary.
8. **Declared access is executable metadata.** A reference system may read/write only resources declared in its access contract; scheduling decisions are derived from that contract.
9. **Authority granularity is adaptive, not semantic.** Scalar mutations and typed contiguous ranges may represent the same class of authoritative component change; no fixed chunk/page size is part of World truth.
10. **No silent patch precedence.** Same-component scalar/range overlap is rejected in the R2.1 reference path rather than resolved by hidden ordering.

## Active hypotheses
1. **Representation independence.** Mesh, SDF, voxel, splat and neural forms may coexist; none should be the definition of an object.
2. **Execution independence.** A task should declare data dependencies and constraints; CPU/GPU placement should remain an implementation decision where possible.
3. **Transaction-derived history.** Replay, rollback, networking, editor undo and incremental persistence may be able to share a common change model.

## Promotion states
- IDEA: untested proposal.
- HYPOTHESIS: falsifiable claim with planned experiment.
- EXPERIMENTAL: implementation may change freely.
- VERIFIED: passed the currently defined correctness, scale and adversarial experiments in an explicitly stated scope.
- FOUNDATIONAL: stable versioned contract that survived cross-platform and architectural stress. Implementation remains replaceable.

## Immutability rule
Only contracts/invariants may eventually become FOUNDATIONAL. Algorithms and implementations are never frozen merely because they once won a benchmark.

## Anti-goals
- Replacing triangles merely for novelty.
- Moving all work to GPU regardless of workload.
- Optimizing a benchmark while degrading the whole architecture.
- Freezing implementation because it worked once.
- Calling same-machine determinism proof of universal determinism.
