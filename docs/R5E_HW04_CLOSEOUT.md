# R5E-HW04 Closeout — Real Vulkan Indirect Dispatch

Status: **VERIFIED — HARDWARE RESULT**

Evidence ZIP SHA-256:

`da327a30d50ebfcc90431d06cd14be585efb0d6b29cb16dadbe308a3eb1faa31`

## Integrity

- external `.zip.sha256`: PASS
- internal files verified from `SHA256SUMS.txt`: 22
- internal hash mismatches: 0
- shader compilation: exit 0
- C++ compilation: exit 0
- native link: exit 0
- Vulkan probe: exit 0
- gate duration: 3.408 s

## Hardware execution

- GPU: NVIDIA GeForce RTX 3070 Ti (`10DE:2482`)
- Vulkan device API: 1.4.341
- queue family: 2
- launch mode: **Indirect**
- binding model: traditional descriptor-set baseline
- synchronization2: enabled
- elements: 1,048,576 × `uint32`
- local size X: 256
- workgroups X: 4096
- operation: `out[i] = in[i] * 3u + 7u`

Indirect launch control:

- control source: resident device buffer
- command size: 12 bytes (`VkDispatchIndirectCommand`)
- command: `{4096, 1, 1}`
- command FNV-1a64: `0x3891ae3c62606753`
- control buffer usage includes `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`
- transfer-to-indirect visibility uses `DRAW_INDIRECT` + `INDIRECT_COMMAND_READ`
- execution uses `vkCmdDispatchIndirect(...)`

Hashes:

- input FNV-1a64: `0xac41f8629b52fce0`
- CPU oracle FNV-1a64: `0x8e2eef1faffc414f`
- GPU output FNV-1a64: `0x8e2eef1faffc414f`
- full CPU oracle comparison: PASS

Validation:

- errors: 0
- warnings: 0

## What HW04 proves

For the tested RTX 3070 Ti + driver/runtime configuration, HW04 proves that the R5C/R5D launch-control concept survives a real Vulkan indirect-dispatch path:

1. dispatch dimensions can reside in a device-resident control resource;
2. the control buffer can be uploaded and transitioned to indirect-command read visibility;
3. `vkCmdDispatchIndirect` executes the same compute semantics as the previously verified Direct gate for this workload;
4. all 1,048,576 outputs reproduce the independent CPU oracle exactly;
5. the path completes with zero Vulkan validation errors or warnings.

HW03 and HW04 therefore establish **functional equivalence for the tested workload**, not performance equivalence:

`Direct result == Indirect result == CPU oracle`

## Explicit non-claims

HW04 does not establish:

- GPU execution time;
- CPU submission cost;
- Direct vs Indirect performance superiority;
- DGC execution or performance;
- descriptor-buffer or descriptor-heap execution;
- production residency/eviction behavior;
- a final Vulkan barrier lowering policy.

The descriptor-set path remains a compatibility/reference baseline rather than an architectural commitment.
