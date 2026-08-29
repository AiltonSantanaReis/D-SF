# Aion Research Lab — R2 Execution Kernel Verification Report

## Status
- R0 Minimal Authoritative World: **VERIFIED (reference scope)**
- R1 Change Journal / Replay / Rollback: **VERIFIED (same-architecture reference scope)**
- R2 Dependency Execution Graph: **VERIFIED (reference correctness scope)**
- R2 performance hypothesis: **PARTIAL**
- FOUNDATIONAL contracts: **none yet**

R2 is considered complete as a correctness experiment: declared resource access can be converted into a deterministic dependency graph, parallel-safe waves can execute concurrently, and the resulting authoritative World converges bit-exactly with the serial oracle in the tested scope.

R2 is **not** a final scheduler architecture. The current per-entity transaction patch representation limits real-world speedup and remains explicitly experimental.

## R2 hypothesis

> Systems can declare their data dependencies instead of relying on hard-coded frame phases, allowing the engine to derive safe parallel execution while retaining a deterministic authoritative commit order.

The experiment separated two responsibilities:

1. **Work execution** may happen concurrently.
2. **World authority** remains transactional and deterministic.

Workers never mutate `World` directly.

## Resource access model
R2 introduces first-class resource keys:

- `Identity`
- `EntityState`
- `Position`
- `Velocity`
- `Health`

Every system declares:

- stable `SystemId`;
- read set;
- write set;
- optional explicit `after` dependencies;
- system function.

`SystemContext` enforces declarations at runtime in the reference implementation. An undeclared read or write fails the system before its wave is committed.

Write permission does not silently imply read permission: read-modify-write systems must declare the resource in both sets.

## Hazard derivation
Two systems conflict when at least one writes a resource read or written by the other.

- read/read: no edge;
- read/write: serialized;
- write/read: serialized;
- write/write: serialized.

For otherwise ambiguous data hazards, lower stable `SystemId` currently defines canonical precedence. This is a deterministic tie-break rule, **not a promoted engine-wide frame phase model**.

Explicit dependencies are also edges. Cycles, including contradictions between explicit dependencies and canonical hazard ordering, are rejected at graph-build time.

## Deterministic waves
Kahn topological sorting produces deterministic waves.

All systems in a wave:

1. observe the same immutable pre-wave `World`;
2. execute serially or concurrently;
3. produce private mutation patches;
4. have their outputs sorted by `SystemId`;
5. are merged into one transaction;
6. commit atomically before the next wave begins.

This creates an important invariant:

> Parallel workers compute proposals; only the transaction boundary mutates authoritative state.

If any system in a wave fails, no mutation from that wave is committed.

## Persistent execution runtime
The first R2 prototype created a worker pool for every `execute()` call. Benchmarking exposed that this lifecycle polluted frame timings.

R2 therefore introduced `ExecutionRuntime`, which owns persistent worker threads across frames. `ExecutionKernel::execute()` remains only as a one-shot convenience path.

This architectural correction was discovered experimentally before any scheduler contract was promoted.

## Correctness tests
The R2 suite verifies:

1. read/read systems share one wave;
2. read/write hazards serialize automatically;
3. write/write hazards serialize automatically;
4. explicit dependency cycles are rejected;
5. undeclared access is rejected before commit;
6. serial and worker-pool execution produce identical transaction counts;
7. serial and parallel final World hashes are identical;
8. 120 frames / 4,096 entities reproduce an exact final hash.

R2 test final SHA-256:

`057e4f9d9e4921cb93da1a0c5b1245fafbadb49ce8755f572ebff34b558e53ca`

This exact hash was reproduced with:

- GCC 14.2, x86-64 Linux;
- Clang 17, x86-64 Linux;
- GCC ThreadSanitizer build.

## Cross-worker correctness benchmark
A separate 8,192-entity / 60-frame workload was executed with 1, 2, 4 and 5 workers.

All worker counts produced:

`71ccbd8aaaed14974c7c70ab4879f099f42195dd1ed6d312d93fa8642cf4218c`

GCC and Clang reproduced the same final hash.

This is evidence that worker count does not change authoritative results for the tested R2 systems.

It is not proof of universal floating-point determinism, Windows/Linux equality, x86/ARM equality or future CPU/GPU equality.

## Sanitizer evidence
Reference source was additionally tested with:

- AddressSanitizer;
- UndefinedBehaviorSanitizer;
- ThreadSanitizer on the R2 concurrent test.

ASan/UBSan completed the full current CTest suite without reported errors.

The R2 ThreadSanitizer test completed without a reported data race.

This substantially increases confidence in the current concurrency implementation, but it is not a mathematical proof of race freedom for future systems or backends.

## Reference environment
R2 measurements in this report were taken in the current sandbox environment:

- Linux x86-64;
- 5 available CPUs;
- AMD EPYC 9V74 host model exposed by the environment;
- GCC 14.2;
- Clang 17;
- CMake 3.31.6.

The environment is virtualized/shared, so timing variance is expected. Performance results use medians and are evidence only for these workloads.

## Scheduler microbenchmark
Workload:

- 32 independent systems;
- 750,000 deterministic integer iterations per system;
- no authoritative World writes;
- seven samples per worker count; median reported.

One reference GCC run produced:

| Workers | Median | Speedup |
|---:|---:|---:|
| 1 | 80.508 ms | 1.000x |
| 2 | 41.275 ms | 1.951x |
| 4 | 24.551 ms | 3.279x |
| 5 | 23.186 ms | 3.472x |

All worker counts produced the same auxiliary checksum:

`3462961269496396242`

Conclusion: the worker scheduler itself can expose substantial parallelism when tasks are sufficiently independent and compute-heavy.

## Authoritative world benchmark
Workload:

- 8,192 entities;
- 60 frames;
- four systems;
- two derived execution waves;
- per-entity transactional patches;
- seven samples per worker count; median reported.

One reference GCC run produced:

| Workers | Median | Speedup |
|---:|---:|---:|
| 1 | 49.192 ms | 1.000x |
| 2 | 41.956 ms | 1.172x |
| 4 | 46.826 ms | 1.051x |
| 5 | 45.700 ms | 1.076x |

The exact timings vary in the shared sandbox, but repeated runs show a consistent architectural result: unlike the compute-only benchmark, adding workers does **not** scale the authoritative workload proportionally.

## Why scaling is limited
R2 currently materializes each changed component as an individual mutation:

`SetPosition(entity)`

`SetHealth(entity)`

`SetVelocity(entity)`

Workers can compute patches concurrently, but each wave then pays for:

- vector growth / patch materialization;
- merging outputs;
- serial transaction validation;
- serial authoritative commit;
- repeated memory traversal.

For memory-heavy systems, multiple workers may also compete for memory bandwidth while the final commit remains serial.

Therefore the R2 performance hypothesis is only **PARTIAL**:

> Dependency-derived parallelism works, but per-entity authoritative patch granularity prevents the current design from converting available parallelism into proportional whole-world speedup.

This is a useful falsification, not a failure to hide.

## Architectural limitations discovered
Before any FOUNDATIONAL promotion, R2 still needs alternatives for:

1. **Patch granularity** — page/chunk/component-range patches rather than one mutation per entity.
2. **Commit scalability** — avoid serially re-walking every computed value where correctness permits.
3. **Graph construction** — current reference builder uses an O(S^2) hazard comparison and adjacency matrix.
4. **Spatial/resource ranges** — `Position` is currently one global resource; future systems need disjoint chunk/range access declarations.
5. **Dynamic work** — no child jobs, work stealing or runtime task spawning yet.
6. **NUMA/cache affinity** — not modeled.
7. **GPU execution** — not tested in this sandbox.
8. **Cross-platform scheduler determinism** — Windows and ARM remain open.

## R2 conclusion
R2 validates the architectural separation between **parallel computation** and **authoritative mutation** in the tested scope.

The strongest proven property is:

> A dependency graph derived from declared resource access can execute independent systems concurrently while preserving a canonical transactional commit and reproducing the serial reference World's exact SHA-256 state.

The current scheduler contract is strong enough to retain for further research, but its patch/commit implementation is not ready for FOUNDATIONAL status.

## Required next experiment: R2.1 — Chunked Transaction Patches
Before moving the architecture wholesale into the Spatial Kernel, the most rational next experiment is to attack the bottleneck R2 actually exposed.

R2.1 should compare:

- per-entity mutations (current oracle);
- contiguous component-range patches;
- fixed-size page/chunk patches;
- copy-on-write component pages;
- deterministic parallel commit for proven-disjoint ranges.

Every candidate must reproduce the current per-entity oracle's exact final hash.

Only after we know the authority boundary can scale should R3 build the spatial database on top of it.

## Post-R2 note — R2.1 result
R2.1 subsequently implemented and verified a hybrid scalar/range authority transaction. The per-entity patch bottleneck identified by this report was materially reduced in the tested dense workloads without changing final authoritative hashes. Fixed-size pages were not promoted as a universal replacement. See `R2_1_REPORT.md` for the current result.
