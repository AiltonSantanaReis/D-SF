# Aion Research Lab — R2.1 Hybrid Transaction Patches Verification Report

## Status
- R0 Minimal Authoritative World: **VERIFIED (reference scope)**
- R1 Change Journal / Replay / Rollback: **VERIFIED (same-architecture reference scope)**
- R2 Dependency Execution Graph: **VERIFIED (reference correctness scope)**
- R2.1 Hybrid scalar/range transaction representation: **VERIFIED (reference correctness and tested-performance scope)**
- Fixed page size / COW page policy / universal parallel commit: **NOT PROMOTED**
- FOUNDATIONAL contracts: **none yet**

R2.1 was created because R2 showed that dependency-derived parallelism worked, but millions of per-entity `Mutation` records consumed enough allocation, memory bandwidth, merge and validation work to suppress whole-world speedup.

R2.1 does not replace transactions. It changes their granularity while preserving the authority boundary.

## Hypothesis

> A single authoritative transaction can combine scalar structural/sparse writes with typed contiguous component ranges, preserving R0/R1/R2 semantics while materially lowering representation and commit cost for dense workloads.

The experiment deliberately did **not** assume that one fixed page size should win all workloads.

## Resulting transaction model

`PatchTransaction` format v2 contains one `TransactionId` and three write lanes:

1. `scalar_mutations` — structural events and sparse writes using the R0/R1 `Mutation` model;
2. `vec3_patches` — ordered, non-overlapping Position/Velocity ranges;
3. `u32_patches` — ordered, non-overlapping Health ranges.

All lanes are validated before authoritative publication.

Same-component scalar/range overlap is rejected. `AdvanceReference` cannot be mixed with dense component ranges in the same hybrid transaction because it implicitly writes Position for the full living set. Dense ranges currently target entities that existed before the transaction; newly-created identities remain scalar until a later transaction.

This deliberately keeps semantics explicit rather than inventing hidden precedence rules.

## R2 integration

`SystemContext` now supports both granularities:

- `set_position`, `set_velocity`, `set_health`, `destroy`;
- `set_position_range`, `set_velocity_range`, `set_health_range`.

The original R2 `execute()` path rejects range output instead of silently dropping it.

The R2.1 `execute_patched()` path:

1. executes systems using the same R2 dependency DAG and immutable pre-wave World;
2. collects scalar and range outputs privately per system;
3. sorts system output by stable `SystemId`;
4. merges all outputs into one hybrid `PatchTransaction` per wave;
5. validates the full transaction;
6. publishes through `PatchJournal`, serial patch commit, or persistent disjoint patch publisher.

Workers still do not own World authority.

## Correctness proof in the tested scope

### Patch representation equivalence
A 4,096-entity / 120-frame test compared:

- per-entity oracle;
- contiguous ranges;
- fixed 256-entity pages;
- page clone / COW-style forward patches;
- disjoint parallel page publication;
- persistent parallel publisher.

All reproduced:

`a073236582885e8cd53f22aa4825ed539a00c74c7c026e61d9e1db9940ada47e`

The same test also verified:

- PatchJournal forward replay from an independently reconstructed base World;
- rollback to the exact frame-60 hash;
- replay of the rolled-back tail;
- binary PatchJournal save/load;
- rejection of overlapping ranges without authoritative state change;
- rejection of non-finite Vec3 payloads without authoritative state change.

### Hybrid scalar + range transaction
A dedicated transaction combined:

- scalar Health write;
- scalar DestroyEntity;
- dense Position range.

It committed, serialized, loaded, replayed and rolled back to the exact base hash. A scalar Position write overlapping a Position range was rejected atomically.

### Execution Kernel equivalence
A 4,096-entity / 120-frame R2 workload compared the original scalar serial oracle with R2.1 range-emitting systems executing through the worker pool and PatchJournal.

Both reproduced:

`657f7bd1092e03c74acf7a38b7a70243f3a8decef268fcce0c552b4195f34a94`

The R2.1 journal contained 240 wave transactions and replayed to the exact final hash.

The legacy `execute()` path was explicitly tested to reject range-producing systems.

## Cross-compiler and sanitizer evidence

The R2.1 correctness suites reproduced the exact hashes above under:

- GCC 14.2, x86-64 Linux;
- Clang 17, x86-64 Linux.

ASan/UBSan completed the dedicated R2.1 patch and execution tests without a reported memory or undefined-behavior error.

ThreadSanitizer completed the R2.1 Execution Kernel test without a reported race. A smaller persistent parallel-publisher workload also completed under ThreadSanitizer without a reported race. The full patch test under TSan is intentionally not claimed because its repeated reference thread-spawning path exceeded the sandbox invocation limit.

These checks increase confidence but do not prove universal race freedom or cross-platform determinism.

## Reference environment

Measured in the current shared/virtualized sandbox:

- Linux x86-64;
- 5 available CPU cores;
- AMD EPYC 9V74 host model exposed by the environment;
- GCC 14.2;
- Clang 17;
- CMake 3.31.6;
- no exposed production GPU/Vulkan device used for these measurements.

Timings are evidence only for the named workloads and environment.

## Integrated R2 vs R2.1 benchmark

Every candidate executed the same four-system / two-wave authoritative workload and was sampled three times; the median is reported. Every candidate reproduced the exact scenario hash.

### 8,192 entities / 60 frames

| Candidate | Median | Speedup vs R2 scalar serial |
|---|---:|---:|
| R2 scalar serial | 44.803 ms | 1.000x |
| R2 scalar, 4 workers | 61.923 ms | 0.724x |
| R2.1 ranges serial | 32.612 ms | 1.374x |
| R2.1 ranges, 4 workers | 35.361 ms | 1.267x |
| R2.1 ranges, 4 workers + persistent parallel commit | 36.034 ms | 1.243x |

Hash:

`71ccbd8aaaed14974c7c70ab4879f099f42195dd1ed6d312d93fa8642cf4218c`

At this scale, worker overhead can still exceed the saved work. Range representation itself remains beneficial.

### 100,000 entities / 20 frames

| Candidate | Median | Speedup vs R2 scalar serial |
|---|---:|---:|
| R2 scalar serial | 192.925 ms | 1.000x |
| R2 scalar, 4 workers | 151.304 ms | 1.275x |
| R2.1 ranges serial | 138.596 ms | 1.392x |
| R2.1 ranges, 4 workers | 102.150 ms | 1.889x |
| R2.1 ranges, 4 workers + persistent parallel commit | 102.841 ms | 1.876x |

Hash:

`e6803f6411816d3e2261f091e7eb82718262ee9969b33dce9135467c9072c2c4`

Here dependency-derived worker parallelism begins to combine effectively with lower patch granularity.

### 1,000,000 entities / 3 frames

| Candidate | Median | Speedup vs R2 scalar serial |
|---|---:|---:|
| R2 scalar serial | 740.970 ms | 1.000x |
| R2 scalar, 4 workers | 517.654 ms | 1.431x |
| R2.1 ranges serial | 228.456 ms | 3.243x |
| R2.1 ranges, 4 workers | 166.262 ms | 4.457x |
| R2.1 ranges, 4 workers + persistent parallel commit | 150.455 ms | 4.925x |

Hash:

`61d624a0af70729626dafebd3b3bea4cb5a074e625ec7f17ac981f6eef5a2c60`

This is the key R2.1 performance result. The original four systems can materialize roughly four million scalar mutation records per frame. The range path represents those same dense component writes with four logical ranges per frame.

The exact speedup is not claimed as a universal engine result. The architectural conclusion is that scalar write-object overhead grows fast enough to dominate the reference workload, while typed contiguous ranges remove most of that overhead without changing the final state.

## Dense representation payload evidence

In the isolated three-component dense patch benchmark at one million entities, one frame required approximately:

- per-entity oracle: 3,000,000 logical records / 96,000,000 bytes of mutation-vector capacity;
- contiguous range: 3 logical records / ~28,000,120 bytes;
- 256 fixed pages: 11,721 logical records / ~28,468,840 bytes.

The range representation removes repeated entity/kind/header fields while retaining the actual component values.

## Sparse workload falsification

A separate 100,000-entity / 200-frame workload changed only 1% of Position values.

### Clustered 1% — 100-entity runs

| Candidate | Total reference-run time | Records/frame | Payload/frame |
|---|---:|---:|---:|
| Per-entity scalar | 16.087 ms | 1,000 | 32,512 B |
| Exact ranges | 17.213 ms | 10 | 12,400 B |
| 256-page clone | 18.931 ms | 10 | 31,120 B |
| Full component range | 249.793 ms | 1 | 1,200,040 B |

Exact ranges reduced representation payload substantially, but scalar writes were still marginally faster in this small sparse workload.

### Scattered 1% — isolated entities

| Candidate | Total reference-run time | Records/frame | Payload/frame |
|---|---:|---:|---:|
| Per-entity scalar | 12.036 ms | 1,000 | 32,512 B |
| Exact one-value ranges | 17.018 ms | 1,000 | 52,000 B |
| 256-page clone | 285.824 ms | 391 | 1,215,640 B |
| Full component range | 249.986 ms | 1 | 1,200,040 B |

Hash equality held for every candidate within each pattern.

This falsifies the proposition that a fixed page/chunk representation should replace scalar mutations globally. A 256-value page can move ~100x more Position data than necessary when one changed entity causes a mostly-clean page to be cloned.

## R2.1 conclusion

The winning architectural primitive is **not a fixed page size**. It is a hybrid authority transaction capable of selecting granularity according to the write topology:

- structural events: scalar;
- sparse scattered writes: scalar;
- clustered writes: exact contiguous ranges when beneficial;
- dense component updates: large contiguous ranges;
- fixed pages: optional implementation policy, not semantic truth;
- copy-on-write pages: not promoted by current evidence;
- parallel range publication: optional optimization that becomes useful only when patch size is large enough to amortize synchronization and memory-bandwidth costs.

The strongest verified R2.1 property is:

> The same dependency-derived execution graph can publish either scalar or contiguous component-range writes through one canonical transaction model, preserve exact replay/rollback/hash semantics, and materially reduce authoritative update cost at large dense scales.

## What remains experimental

1. Automatic scalar-vs-range coalescing policy. Current systems choose the lane explicitly.
2. Sparse masked-page representation for cases between isolated scalars and dense ranges.
3. Incremental/Merkle state hash; full World SHA-256 remains O(World size).
4. Bounded/compressed rollback storage; dense undo still copies old component values.
5. Crash-safe patch journal checksums and recovery.
6. Windows/ARM determinism.
7. NUMA/cache-affinity policies.
8. GPU-resident patch production and publication.

None of these limitations invalidates the R2.1 correctness result, but they prevent FOUNDATIONAL promotion.
