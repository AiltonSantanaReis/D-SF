# Research Roadmap

## R0 — Minimal authoritative world — VERIFIED (reference scope)
Question: What is the smallest state model that can survive renderer/physics/backend replacement?

Acceptance completed:
- Stable entity identity.
- Identity allocation is transaction-derived; no hidden reservation side effect.
- Transactions validated before mutation.
- Transactional deterministic reference integration.
- >= 1,000,000 lightweight entities benchmarked on CPU.
- Tests prove invalid transaction does not partially mutate state.
- Non-finite authoritative vector values rejected.

See `R0_R1_REPORT.md`.

## R1 — Change Journal / rollback — VERIFIED (same-architecture reference scope)
Implemented:
- Forward-only persistent transaction journal.
- Canonical SHA-256 world state hash.
- Binary journal save/load with version and corruption guards.
- Deterministic replay from pristine world.
- Exact rollback using ephemeral pre-transaction undo state.
- Rollback hash guard against out-of-journal divergence.
- 5,001-transaction / 5,000-frame replay test.
- Exact final hash reproduced under GCC 14.2 and Clang 17 on x86-64 Linux.

Open before any FOUNDATIONAL promotion:
- Windows x86-64 cross-machine replay.
- ARM replay.
- Explicit floating-point determinism policy.
- Crash-safe journal persistence / checksums per record.
- Scalable history storage and incremental state hashing.

## R2 — Dependency execution graph — VERIFIED (reference correctness scope)
Systems declare read/write sets and optional explicit dependencies. Independent systems execute as deterministic waves; workers produce private patches and one canonical transaction commits each wave.

Verified:
- DAG construction and cycle rejection.
- Read/read concurrency.
- Read/write and write/write hazard serialization.
- Runtime enforcement of declared accesses.
- Deterministic SystemId tie-break for ambiguous hazards.
- Persistent worker pool across frames.
- Serial vs worker-pool state-hash equivalence.
- 1, 2, 4 and 5 worker benchmark hash equivalence.
- GCC 14.2 vs Clang 17 exact hash equivalence on x86-64 Linux.
- ASan/UBSan pass; R2 concurrent test passes ThreadSanitizer without a reported race.

Performance conclusion: **PARTIAL**. Independent compute-heavy tasks scale strongly, but per-entity authoritative patches materially limit whole-world scaling.

See `R2_REPORT.md`.

## R2.1 — Hybrid transaction patches — VERIFIED (reference correctness and tested-performance scope)
Question: can the authoritative transaction boundary retain exact R0/R1/R2 semantics without materializing one mutation per changed entity?

Verified:
- Hybrid transaction contains scalar structural/sparse writes plus typed Position/Velocity/Health ranges under one TransactionId.
- Same-component scalar/range overlap rejected before publication.
- Dense ranges, fixed pages, COW-style page clones and parallel publication reproduce the per-entity oracle hash.
- PatchJournal save/load, replay and rollback reproduce exact hashes.
- R2 `SystemContext` can emit ranges; `execute_patched()` commits one hybrid transaction per deterministic wave.
- Legacy `execute()` rejects range output rather than ignoring it.
- GCC 14.2 and Clang 17 reproduce exact R2.1 hashes.
- ASan/UBSan dedicated R2.1 tests pass; R2.1 concurrent execution and a smaller persistent-publisher workload pass ThreadSanitizer without a reported race.
- Integrated 1M-entity reference workload: 740.970 ms median R2 scalar serial vs 228.456 ms range serial vs 150.455 ms range/4-worker/persistent parallel commit, exact same final hash.

Falsified:
- Fixed page size as universal transaction representation.
- Copy-on-write page cloning as a universal dense/sparse winner.
- Parallel commit as automatically beneficial at small workloads.

Retained decision:
- scalar lane for structure and sparse scattered writes;
- contiguous typed ranges for dense/clustered writes;
- page/chunk sizing remains an implementation/spatial policy, not an authoritative semantic.

Open:
- automatic coalescing policy;
- sparse masked-page candidate;
- scalable incremental hash and bounded rollback storage.

See `R2_1_REPORT.md`.

## R3 — Spatial kernel bake-off
Implement at least: flat grid, hashed grid, sparse brick hierarchy. Measure insert/update/query/streaming behavior rather than selecting a structure in advance.

## R4 — Geometry providers
Common contracts for triangle, analytic SDF and sparse sampled SDF representations. Rendering is initially CPU/reference-only for correctness.

## R5 — GPU laboratory (external GPU required)
Vulkan/DX12 compute backend, timestamp queries, GPU resident buffers, indirect execution. Results must be measured on named hardware and driver.

## R6 — Heterogeneous representation experiment
Same world region exposed as mesh/SDF/sparse volume. Select representation by operation and error budget.
