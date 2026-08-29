# R5E-HW02 VkDevice + Real Memory Roundtrip Closeout

Status: **VERIFIED — HARDWARE RESULT**

Evidence ZIP SHA-256: `d0c5549cc7c4aac5ddb7a395197cf0b319ae0005bfb2baf403958d58aa34acaf`

HW01 evidence SHA-256 carried into the manifest: `f2af859183bb926598b7e90356294d8cac3f92e577ae35f180e8088397fac9d6`

R5D baseline SHA-256: `f1dc9d2b82a3d5a4133cb98cc4426274ef14d93b944de2496bc9f5705ee7131e`

## Build / execution gate

- MSVC: 14.51.36231 from Visual Studio 18 Build Tools
- Windows SDK: 10.0.26100.0
- Vulkan SDK: 1.4.357.0
- C++ compile exit: 0
- native link exit: 0
- Vulkan probe exit: 0
- gate duration: 3.705 s
- evidence files hashed: 18
- internal SHA-256 mismatches: 0

The accepted build model is deliberately two-stage: direct `cl.exe /c` compilation followed by direct `link.exe`. This avoids dependence on `vcvars*.bat`, inherited `INCLUDE/LIB`, CMake, and `cl.exe` forwarding of `/LIBPATH` arguments.

## Target / logical device

- Physical device: NVIDIA GeForce RTX 3070 Ti
- Vendor/device IDs: `0x10de / 0x2482`
- Device Vulkan API: 1.4.341
- selected queue family: 2 (compute + transfer, non-graphics on the HW01 fingerprint)
- `synchronization2`: explicitly queried, supported, and enabled on `VkDevice`
- `VK_EXT_memory_budget`: supported and queried
- validation layer: enabled
- validation errors: 0
- validation warnings: 0

## Real memory roundtrip

Payload: **16 MiB / 16,777,216 bytes**.

Path executed on the physical Vulkan device:

`HOST upload -> vkCmdCopyBuffer -> DEVICE_LOCAL buffer -> synchronization2 barrier -> vkCmdCopyBuffer -> HOST readback -> HOST_READ barrier -> fence -> CPU compare`

The upload and readback allocations required host-visible memory. The middle allocation required device-local memory.

Observed allocation requirements:

- upload: 16,777,216 bytes, alignment 16, memory type 4
- device: 16,777,216 bytes, alignment 16, memory type 1
- readback: 16,777,216 bytes, alignment 16, memory type 4

Authoritative payload verification:

- input FNV-1a64: `0xc0dd6ba4a0e044c2`
- output FNV-1a64: `0xc0dd6ba4a0e044c2`
- full byte-for-byte `memcmp`: PASS

Therefore the tested Vulkan path preserved all 16 MiB exactly.

## Synchronization verified in this gate

The command buffer used explicit synchronization2 dependencies:

1. transfer write to the device-local buffer -> transfer read before the second copy;
2. transfer write to the readback buffer -> host read before CPU visibility;
3. fence wait before CPU mapping/verification;
4. non-coherent flush/invalidate paths are present and conditionally used when required by the selected memory type.

Validation reported no warnings or errors for the executed path.

## Memory-budget observations

`VK_EXT_memory_budget` returned live heap-budget/usage snapshots before allocation, while allocations were resident, and after free.

Approximate observations from this run:

- heap 0: size 8017 MiB, budget 7249 MiB; usage 0 -> 18.98 -> 2.98 MiB
- heap 1: size 16345.40 MiB, budget 15577.40 MiB; usage 0 -> 43.80 -> 13.80 MiB
- heap 2: size 214 MiB, budget 200.55 MiB; usage remained 13.45 MiB

These values are **driver/WDDM budget telemetry**, not exact per-allocation accounting. They can include driver/runtime allocations, reporting granularity, delayed reclamation, and concurrent system activity. They must not be used as a precise claim that the three 16 MiB logical allocations consumed those exact deltas.

## What HW02 proves

- a real `VkInstance` and validated real `VkDevice` can be created on the target GPU;
- the intended compute+transfer queue family can be used;
- real host-visible and device-local `VkDeviceMemory` allocations work;
- explicit synchronization2 is accepted by the driver/validation path;
- a real 16 MiB host->device->host transfer roundtrip is exact;
- memory-budget telemetry can be queried on the target system.

## What HW02 does NOT prove

- compute shader execution;
- descriptor-set / descriptor-buffer / descriptor-heap execution;
- Direct/Indirect/DGC equivalence;
- GPU execution time or bandwidth;
- PCIe throughput;
- residency eviction policy correctness;
- R5A LRU/pinning behavior mapped onto real Vulkan allocations;
- DGC or descriptor-heap runtime behavior;
- any relative Vulkan performance claim.

Next gate: **R5E-HW03 — baseline resource binding + first deterministic Direct compute dispatch with CPU oracle and full GPU readback verification.**
