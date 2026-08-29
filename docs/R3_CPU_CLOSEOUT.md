# R3 CPU/Reference Closeout

Status: **VERIFIED — CPU/reference scope**

R3 is closed only for the tested CPU/reference architecture. It is not GPU evidence and does not promote a universal spatial backend.

## Contracts that survived

1. Spatial data is a derived snapshot, not authoritative World truth.
2. Multiple spatial views share one data-oriented snapshot instead of duplicating per-object AABBs.
3. Snapshot publications are versioned; stale views reject queries rather than silently returning prior-frame answers.
4. R2.1 sparse and range changes stay compact across the spatial boundary.
5. Spatial backend choice is cost-aware; churn or topology debt alone are insufficient.
6. Expensive quality maintenance can be cooperative and budgeted by ExecutionBudgetScheduler.
7. Continuous dirty refit exposes backlog explicitly and converges when service capacity exceeds incoming dirty work.
8. Structural revision changes invalidate an in-flight candidate.

## Continuous dirty-refit queue gate

The final queue tracks three distinct forms of work:

- raw dirty values not yet mapped to leaves;
- dirty leaf nodes;
- dirty internal nodes.

A regression test intentionally creates sustained sparse pressure with an undersized maintenance grant. Backlog must become non-zero, then drain completely after production stops, and the promoted view must reproduce the SpatialOracle results.

Final test suite after the queue change:

- GCC 14.2: 7/7 before R4, zero warnings in the strict kernel build.
- Clang 17: 7/7 before R4, zero warnings in the strict kernel build.
- ASan + UBSan: R3.2 budgeted spatial tests PASS.

### Dense queue observations

These are workload observations, not universal throughput claims.

| Objects | Dirty rate | Production frames | Semantic dirty values | Peak raw backlog | Drain slices (0.5 ms grant) | Maintenance CPU (production + drain) | Exact final queries |
|---:|---:|---:|---:|---:|---:|---:|---|
| 100,000 | 1% | 100 | 100,000 | 0 | 0 | ~23.7–26.3 ms across 3 runs | yes |
| 100,000 | 10% | 100 | 1,000,000 | 0–7,184 | 2 | ~51.7–52.7 ms across 3 runs | yes |
| 100,000 | 50% | 100 | 5,000,000 | 0–28,752 | 3–4 | ~52.8–53.8 ms across 3 runs | yes |
| 250,000 | 1% | 80 | 200,000 | 0 | 2 | 42.076 ms | yes |
| 250,000 | 10% | 80 | 2,000,000 | 0 | 12 | 47.059 ms | yes |
| 250,000 | 50% | 80 | 10,000,000 | 65,240 | 15 | 48.206 ms | yes |
| 1,000,000 | 1% | 40 | 400,000 | 0 | 78 | 61.205 ms | yes |
| 1,000,000 | 10% | 40 | 4,000,000 | 36,512 | 99 | 73.272 ms | yes |
| 1,000,000 | 50% | 30 | 15,000,000 | 10,829,760 | 181 | 110.740 ms | yes |

The one-million/50% case is deliberately beyond the maintenance service rate during production. Backlog grows, remains explicit, and drains after production stops. Final backlog is zero and the promoted tree matches the oracle.

### Sparse/distributed observations

100,000 objects, 50 production frames:

- 1% sparse writes/frame: final backlog zero, no drain slice required, exact queries.
- 10% sparse writes/frame: final backlog zero after 2 drain slices, exact queries.
- 50% sparse writes/frame: final backlog zero after 4 drain slices, exact queries.

This rejects the hypothesis that the queue only works because contiguous range writes collapse naturally.

## Limits deliberately retained

- The scheduler is soft real-time. User-space CPU work cannot guarantee hard deadlines against OS scheduling, page faults or virtual-machine steal time.
- A one-million-object regime requiring a global Morton rebuild every frame is still outside a 60 Hz CPU budget in this sandbox.
- GPU construction, residency and synchronization remain unmeasured.
- The current 256-primitive cooperative quantum is an experimental CPU policy, not a foundational constant.
- Wide SAH and Morton BVH8 are currently verified views, not universal winners.

## R3 closure decision

R3 CPU/reference is closed because the remaining major uncertainty is no longer the spatial contract. The next architectural risk is geometry heterogeneity. GPU-specific spatial questions are intentionally deferred to R5, where hardware measurements exist.
