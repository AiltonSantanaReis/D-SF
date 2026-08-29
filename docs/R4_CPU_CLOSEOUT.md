# R4 — Geometry Kernel CPU/Reference Closeout

Status: **VERIFIED — CPU/reference scope**
Date: 2026-08-28
Foundational contracts: **none**

## Scope closed

R4 now contains two scalable and heterogeneous geometry representations under one capability/revision/error contract, plus measured selection and safe online routing infrastructure:

- R4A — Representation Contract: VERIFIED
- R4B — Sparse Implicit Geometry: VERIFIED
- R4C — Clustered Triangle Surface: VERIFIED
- R4D — Heterogeneous Geometry Selection: VERIFIED
- R4E — Online Runtime Geometry Telemetry: VERIFIED
- R4F — Safe Online Exploration & Routing: VERIFIED **as a mechanism**, not as a universal exploration policy

The persisted R4B checkpoint was the last physical checkpoint available when this session resumed. R4C–R4F were reconstructed from that exact R4B source baseline and the already established conversation contracts, then fully revalidated. This reconstructed, revalidated state supersedes any transient prior workspace.

## R4C — Clustered Triangle Surface

Reference layout:

```text
triangle source
   -> bounded clusters (default experimental CPU: 64 vertices / 124 triangles)
   -> uint8 local indices
   -> shared resource quantization frame for quantized positions
   -> cluster bounds
   -> Morton-ordered two-level BVH8
   -> conservative quantized BVH child bounds
```

Important falsifications/corrections retained:

1. Per-cluster independent position quantization is rejected because duplicated shared vertices may decode differently and open seams.
2. Quantized BVH bounds must be conservative; child bounds are expanded outward by one quantization unit.
3. Larger clusters are not universally better; fewer clusters trade against more local triangle tests.

Reconstructed R4C regression workload:

```text
heightfield triangles: 4418
clusters: 47
quantized storage: 46076 bytes
raw source vertices+uint32 indices: 80664 bytes
max declared quantization error: 4.32196e-05
max measured heightfield ray-t error: 8.46386e-06
torus max measured ray-t error: 4.57764e-05
```

The torus is a closed curved adversary used to catch seam and conservative-bound failures.

## R4D — Heterogeneous Geometry Selection

Selection never checks provider names or formats. It operates on:

```text
hard constraints
  - capabilities
  - source revision
  - maximum geometric error
  - maximum storage

explicit objective
  - MinStorage
  - MinGeometricError
  - MinQueryLatency
```

The selector also exposes the Pareto frontier. No weighted magic score is used.

In the reconstructed sphere workload:

```text
Sparse SDF storage:       2,842,910 bytes
Clustered Triangle:           2,880 bytes
```

Synthetic workload telemetry was intentionally used in the selector regression so the expected winner is known exactly and not affected by shared-host timing noise.

## R4E — Online Runtime Geometry Telemetry

The Execution Kernel owns only generic batch timing:

```text
ExecutionDevice + workload_id + work_units + one real task
```

It has no GeometryHandle knowledge.

Geometry telemetry is keyed by:

```text
GeometryHandle
+ source_revision
+ workload_id
+ device
+ batch-size class
```

Recent windows report median, MAD, P90, median thread CPU cost and relative age. Epoch reset discards stale historical samples after a forced refresh. Sampling is volume/gap based so timing is not forced on every micro-batch.

Verified properties include:

- one observed batch executes the real task exactly once;
- recent window forgets old regimes;
- workload/device/batch-size contexts do not contaminate one another;
- MAD exposes unstable telemetry;
- staleness is relative to observations in the same context;
- epoch reset starts a fresh confidence window.

## R4F — Safe Online Exploration & Routing

The router never performs shadow work. Exploration means the **next real batch** is routed to another already-eligible representation and that one execution becomes telemetry.

Hard constraints are applied before exploration. Exploration can be disabled; insufficient or stale knowledge then returns an explicit refusal.

### Proven policy invariant

If `min_observed_batches = M`, a fixed freshness horizon smaller than `M` can create a refresh livelock: while rebuilding confidence for one arm, the competing arm becomes stale before exploitation is possible.

The reference router therefore rejects:

```text
refresh_age < min_observed_batches
```

### Silent inactive-provider improvement experiment

Controlled ground truth:

```text
active provider:   1.0 cost units throughout
inactive provider: 2.0 before drift -> 0.5 after drift
```

The inactive improvement is not observable from active-provider telemetry. It can only be discovered by exploration.

128 different drift phases were tested for each fixed refresh age. See `results/R4F_INACTIVE_PROVIDER_DRIFT_SWEEP.csv`.

Summary:

| refresh age | pre-drift regret mean | post-drift regret mean | exploit delay p90 | exploit delay max |
|---:|---:|---:|---:|---:|
| 3  | 81.63 | 41.28 | 8  | 8  |
| 4  | 71.63 | 36.31 | 9  | 9  |
| 8  | 48.23 | 25.09 | 12 | 13 |
| 16 | 29.58 | 17.47 | 19 | 21 |
| 32 | 16.99 | 14.39 | 33 | 37 |
| 64 | 9.80  | 19.57 | 63 | 69 |

This falsifies the idea that there is a universal optimal fixed refresh interval.

The mechanism exposes freshness as a workload policy/SLA. A workload that requires faster discovery must explicitly accept more exploration cost. A workload that prioritizes stable throughput may accept older knowledge. The engine cannot infer an unobserved improvement for free.

## Validation

Final reconstructed checkpoint validation:

```text
GCC 14.2:  13/13 CTest PASS
Clang 17:  13/13 CTest PASS
strict kernel warnings: 0
ASan R4A-R4F: PASS
UBSan R4A-R4F: PASS
```

## Explicitly not promoted

- current CPU timings as GPU predictions;
- Sparse SDF or Clustered Triangle as universal geometry;
- 64/124 cluster size as a device-independent optimum;
- current R4C BVH as a production RTAS replacement;
- fixed `refresh_age` as an engine constant;
- autonomous bandit policy that claims to discover inactive improvement without exploration;
- exact equivalence of approximate representations inside their geometric ambiguity band.

## Next authorized stage

**R5 — GPU Execution / Representation Residency.**

R5 must preserve the World/Geometry separation and test whether derived representations can become device-resident without CPU/GPU synchronization, transfer cost or VRAM duplication erasing the benefits seen in the CPU/reference laboratory.
