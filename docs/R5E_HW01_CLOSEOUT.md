# R5E-HW01 Hardware Fingerprint Closeout

Status: **VERIFIED — HARDWARE RESULT**

Evidence ZIP SHA-256: `f2af859183bb926598b7e90356294d8cac3f92e577ae35f180e8088397fac9d6`

R5D baseline SHA-256: `f1dc9d2b82a3d5a4133cb98cc4426274ef14d93b944de2496bc9f5705ee7131e`

## Target

- GPU: NVIDIA GeForce RTX 3070 Ti
- Vendor/device: `0x10de / 0x2482`
- Physical VRAM reported by NVIDIA-SMI: 8192 MiB
- NVIDIA driver: 610.47
- Vulkan loader: 1.4.357
- Device Vulkan API: 1.4.341
- Conformance: 1.4.3.3
- Validation layer: present

## Verified feature bits

The Vulkan Profiles JSON reports `true` for:

- `bufferDeviceAddress`
- `timelineSemaphore`
- `descriptorIndexing`
- `synchronization2`
- `dynamicRendering`
- `descriptorBuffer`
- `descriptorHeap`
- `deviceGeneratedCommands`
- `dynamicGeneratedPipelineLayout`
- `meshShader`
- `taskShader`
- `accelerationStructure`
- `rayTracingPipeline`

This is stronger evidence than extension-name presence alone. Features are still not enabled on a logical device until R5E-HW02+ explicitly requests them.

## Queue families

- 0: 16 queues — graphics + compute + transfer + sparse, 64 timestamp bits
- 1: 2 queues — transfer + sparse, 64 timestamp bits
- 2: 8 queues — compute + transfer + sparse, 64 timestamp bits
- 3: video decode, 32 timestamp bits
- 4: video encode, 32 timestamp bits
- 5: optical flow + transfer, 64 timestamp bits

The headless compute bring-up can therefore test family 2 without occupying a graphics-capable queue family, while family 0 remains available for later graphics work.

## Timing capability

`timestampComputeAndGraphics = true`, `timestampPeriod = 1 ns`. This is capability evidence only; no GPU timing measurement was performed by HW01.

## DGC properties observed

- max indirect sequence count: 4,194,303
- max command tokens: 32
- compute shader stage is supported for generated commands
- `deviceGeneratedCommands = true`
- `dynamicGeneratedPipelineLayout = true`

No DGC command was executed in HW01.

## Descriptor heap properties observed

- `descriptorHeap = true`
- max resource heap size: 33,554,432 bytes
- max sampler heap size: 131,072 bytes
- resource/sampler heap alignment: 32 bytes
- sparse descriptor heaps: supported

No descriptor heap was allocated or bound in HW01.

## Not measured by HW01

- actual VkDevice creation
- VkDeviceMemory allocation
- heap budget/usage snapshot values
- host→device upload
- device→host readback
- GPU execution time
- synchronization cost
- DGC preprocessing/execution cost
- descriptor heap runtime behavior
- eviction/thrashing
- any relative performance claim

Next gate: **R5E-HW02 — logical device + real allocation + 16 MiB upload/device/readback roundtrip with validation enabled.**
