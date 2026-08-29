# D-SF Research Status — R3 through R5E-HW05

This document records verified research milestones that were developed and tested locally after the repository's earlier R2.1 snapshot. GitHub is a **research record**, not the authority for unverified experiments.

## Method

D-SF uses the following progression:

`hypothesis → reference/oracle → controlled experiment → data → conclusion → specification`

Result classes are explicit:

- **REFERENCE RESULT** — CPU/reference semantics or measurements.
- **HARDWARE RESULT** — executed on real target hardware.
- **PARTIAL** — evidence is useful but intentionally scoped.
- **FALSIFIED** — the tested hypothesis failed and remains recorded.
- **NOT MEASURED** — no claim is permitted.

No mechanism below is labeled foundational.

## R3 — Spatial Kernel

**Status: VERIFIED — CPU/reference scope.**

Promoted architecture:

- shared authoritative spatial snapshot rather than duplicated bounds per derived index;
- Wide BVH8 SAH/refit for coherent/low-churn cases;
- deterministic Morton BVH8 rebuild path for high-change cases;
- cost-aware selection based on update + query + rebuild/switch cost;
- cooperative budgeted SAH maintenance with explicit backlog accounting.

Important falsifications/limits:

- fixed region partition was not accepted as a universal strategy;
- unrestricted background rebuild was not treated as free;
- a mandatory 1M-object Morton rebuild did not meet a 16.67 ms CPU frame budget in the sandbox.

## R4 — Geometry Kernel

**Status: VERIFIED through R4F — CPU/reference scope.**

Promoted architecture:

- capability-oriented `GeometryProvider` contract;
- representation-independent `GeometrySet` with source revision and geometric-error constraints;
- Sparse narrow-band implicit geometry with explicit error certificate;
- clustered triangle surface with adjacency-aware clusters, BVH8 and conservative shared quantization;
- heterogeneous selection through hard constraints + explicit objective + Pareto frontier;
- online telemetry with median/MAD/P90 windows and staleness rules;
- opt-in safe exploration for otherwise unobservable inactive-provider improvement.

Important falsifications/limits:

- sparse implicit geometry is not universally smaller or faster;
- per-cluster independent quantization was rejected because it can crack shared boundaries;
- always-on timing and unrestricted exploration were rejected as universal policies;
- no GPU claim was derived from R4 CPU measurements.

## R5A–R5D — Device Architecture

**Status: VERIFIED — reference/CPU translation scope.**

Promoted separation:

`World != Geometry != Device Package != Device Work != Backend Command Model`

Verified mechanisms include:

- explicit device residency budget, pinning, LRU eviction and generational handles;
- canonical geometry device packages and atomic multi-resource residency;
- coarse `DeviceWorkPacket` DAG with resource-centric hazards;
- backend-neutral capability profile and translation model;
- asymmetric launch semantics: dynamic Indirect is not silently demoted to Direct, and DeviceGenerated is not silently demoted to CPU-controlled launch;
- canonical semantic digest separated from backend digest;
- deduplicated descriptor table and explicit per-command resource uses;
- structural backend barrier lowering in the reference model.

These results did **not** claim Vulkan/D3D GPU performance.

## R5E — Real Vulkan hardware bring-up

Target configuration used for the verified gates:

- NVIDIA GeForce RTX 3070 Ti (`10DE:2482`)
- NVIDIA driver 610.47
- Vulkan device API 1.4.341
- queue family 2 for compute/transfer tests

### HW01 — Hardware fingerprint

**VERIFIED — HARDWARE RESULT**

The real driver/runtime exposed the capabilities needed to test the later architecture, including `bufferDeviceAddress`, `synchronization2`, timeline semaphores, descriptor buffer, descriptor heap and device-generated commands feature bits.

Evidence SHA-256: `f2af859183bb926598b7e90356294d8cac3f92e577ae35f180e8088397fac9d6`

### HW02 — Real device memory roundtrip

**VERIFIED — HARDWARE RESULT**

A real `VkDevice` and `VkDeviceMemory` path completed a 16 MiB HOST → DEVICE_LOCAL → HOST roundtrip with exact byte equality and clean validation.

Evidence SHA-256: `d0c5549cc7c4aac5ddb7a395197cf0b319ae0005bfb2baf403958d58aa34acaf`

### HW03 — Direct compute

**VERIFIED — HARDWARE RESULT**

For 1,048,576 `uint32` elements, `out[i] = in[i] * 3u + 7u` produced the exact CPU oracle hash `0x8e2eef1faffc414f` with zero validation errors/warnings.

Evidence SHA-256: `1c4b6c2fb05d04a06d48506c114f2755d630049df24ea2386fed0a47b131d06b`

### HW04 — Indirect compute

**VERIFIED — HARDWARE RESULT**

The same workload was launched with `vkCmdDispatchIndirect` using a device-resident 12-byte `VkDispatchIndirectCommand` containing `{4096,1,1}`. The GPU output again matched the exact CPU oracle `0x8e2eef1faffc414f` with zero validation errors/warnings.

Evidence SHA-256: `da327a30d50ebfcc90431d06cd14be585efb0d6b29cb16dadbe308a3eb1faa31`

Controlled conclusion:

`Direct result == Indirect result == CPU oracle`

for this workload and tested configuration. This is **functional equivalence**, not performance equivalence.

## HW05 and current next question

HW05 measured Direct vs Indirect using GPU timestamps and CPU-side timing while holding the workload constant. GPU timestamp median favored Indirect in scope; total host-observed median marginally favored Direct. No universal winner is promoted. DGC and descriptor-heap execution remain later gates; their availability is not treated as evidence that they are superior.

## Publication gate

Before this status update was published, the consolidated portable CPU/reference tree was rebuilt with GCC 14.2 and its full CTest suite passed:

- **17/17 tests PASS**
- total CTest time: **2.82 s**
- no compiler warnings observed in the release build log

This verification is separate from the R5E hardware evidence above.
