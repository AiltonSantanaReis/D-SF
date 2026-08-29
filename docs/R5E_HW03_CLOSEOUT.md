# R5E-HW03 Closeout — Baseline Binding + Direct Compute

Status: **VERIFIED — HARDWARE RESULT**

Evidence ZIP SHA-256:

`1c4b6c2fb05d04a06d48506c114f2755d630049df24ea2386fed0a47b131d06b`

## Integrity

- external `.zip.sha256`: PASS
- internal files verified from `SHA256SUMS.txt`: 22
- internal hash mismatches: 0
- shader compilation: exit 0
- C++ compilation: exit 0
- native link: exit 0
- Vulkan probe: exit 0
- gate duration: 3.357 s

## Hardware execution

- GPU: NVIDIA GeForce RTX 3070 Ti (`10DE:2482`)
- Vulkan device API: 1.4.341
- queue family: 2
- launch mode: Direct
- binding model: traditional descriptor set baseline
- synchronization2: enabled
- elements: 1,048,576 × `uint32`
- local size X: 256
- workgroups X: 4096
- operation: `out[i] = in[i] * 3u + 7u`

Hashes:

- input FNV-1a64: `0xac41f8629b52fce0`
- CPU oracle FNV-1a64: `0x8e2eef1faffc414f`
- GPU output FNV-1a64: `0x8e2eef1faffc414f`
- full CPU oracle comparison: PASS

Validation:

- errors: 0
- warnings: 0

## Memory-budget telemetry

`VK_EXT_memory_budget` was available. Reported heap usage is dynamic driver/WDDM telemetry, not exact ownership accounting.

Device-local heap 0:

- heap size: 8017 MiB
- budget: 7249 MiB
- usage before: 0 MiB
- usage while allocated: 10.98046875 MiB
- usage after free: 2.98046875 MiB

Host heap 1:

- heap size: 16345.40234375 MiB
- budget: 15577.40234375 MiB
- usage before: 0 MiB
- usage while allocated: 19.80078125 MiB
- usage after free: 15.80078125 MiB

## What HW03 proves

HW03 proves, on the tested RTX 3070 Ti + driver/runtime configuration, that the D-SF hardware bring-up can:

1. compile and load a real SPIR-V compute shader;
2. create a real compute pipeline;
3. bind real device-local storage buffers;
4. execute a Direct Vulkan compute dispatch;
5. synchronize transfer → compute → transfer → host correctly under synchronization2;
6. reproduce the CPU oracle exactly for every output element;
7. complete with zero Vulkan validation errors or warnings.

## Explicit non-claims

HW03 does not establish:

- GPU execution time;
- CPU submission cost;
- Direct vs Indirect performance;
- DGC behavior;
- descriptor-buffer or descriptor-heap performance;
- production residency/eviction policy;
- final hardware barrier policy.

The traditional descriptor-set path is a compatibility/reference baseline, not a D-SF architectural commitment.
