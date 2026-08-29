# R5E-HW05 Closeout — Direct/Indirect Timestamp Characterization

Status: **VERIFIED — HARDWARE RESULT**

Evidence ZIP: `R5E_HW05_EVIDENCE_20260829-150208.zip`

Evidence ZIP SHA-256:

`528305209d0c6b9dfc39edf6ec6b4f50d6a19bdfd6aa093c21d3c95c23b77405`

## Integrity and execution

- internal evidence files verified: 23;
- internal hash mismatches: 0;
- external ZIP hash: PASS;
- shader compilation: exit 0;
- C++ compilation: exit 0;
- native link: exit 0;
- Vulkan probe: exit 0;
- gate duration: 3.392 s;
- validation errors: 0;
- validation warnings: 0.

## Controlled hardware protocol

Hardware target:

- GPU: NVIDIA GeForce RTX 3070 Ti (`10DE:2482`);
- NVIDIA driver: 610.47;
- Vulkan device API: 1.4.341;
- queue family: 2;
- timestamp period: 1 ns;
- timestamp valid bits: 64.

The HW03/HW04 semantic baseline remained fixed:

- 1,048,576 `uint32` elements;
- `local_size_x=256`, 4096 workgroups;
- operation `out[i] = in[i] * 3u + 7u`;
- descriptor-set binding baseline;
- CPU oracle and input generation unchanged;
- device-resident indirect command `{4096,1,1}` for the Indirect path.

The only launch variable was Direct versus Indirect. Each mode executed one cold sample and 31 warm samples. Warm samples alternated by mode. GPU timestamps bracketed the compute dispatch. CPU preparation, command recording, submit, fence wait, query/readback and total time were recorded separately.

## Results

Warm-sample medians, tails and GPU timestamps:

| Metric | Direct median | Direct P95 | Direct max | Indirect median | Indirect P95 | Indirect max |
|---|---:|---:|---:|---:|---:|---:|
| Preparation (ms) | 0.0103 | 0.0115 | 0.0128 | 0.0096 | 0.0113 | 0.0127 |
| Record (ms) | 0.0433 | 0.0508 | 0.0656 | 0.0436 | 0.0535 | 0.0610 |
| Submit (ms) | 0.0347 | 0.0380 | 0.0490 | 0.0349 | 0.0450 | 0.0487 |
| Wait (ms) | 0.2642 | 0.4064 | 0.4970 | 0.2571 | 0.4138 | 0.4398 |
| Query/readback (ms) | 4.2615 | 4.6458 | 4.6704 | 4.2544 | 4.6342 | 4.6558 |
| Total (ms) | 4.6354 | 5.0393 | 5.1055 | 4.6424 | 5.0865 | 5.1853 |
| GPU timestamp (ms) | 0.015392 | 0.020992 | 0.021664 | 0.014944 | 0.018144 | 0.018144 |

All 64 samples produced the exact CPU-oracle output hash `0x8e2eef1faffc414f`. Direct and Indirect therefore remain functionally equivalent for this workload. The GPU timestamp median was lower for Indirect in this run, while the host-observed total median was marginally lower for Direct. These are measured observations for this target, driver, workload and protocol, not a universal ranking.

## Decision

HW05 verifies that the controlled Direct/Indirect characterization protocol works on the target hardware and provides the first GPU execution-time evidence for the two launch modes. It does not justify freezing either mode as a universal choice. DGC and modern descriptor paths remain later controlled candidates.

## Explicit non-claims

HW05 does not establish:

- universal Direct or Indirect superiority;
- DGC execution or performance;
- descriptor-buffer or descriptor-heap runtime superiority;
- production residency or eviction behavior;
- cross-GPU or cross-driver generalization;
- final synchronization/barrier policy;
- an integrated World → Spatial → Geometry → Device demonstrator;
- production readiness or competitiveness with complete commercial engines.

R5E remains in progress. RC-1 remains open.
