# R5E Hardware Bring-up — Verified State through HW04

R5E is the first D-SF stage that distinguishes **real hardware evidence** from sandbox/reference results.

## Verified gates

| Gate | Classification | Verified result |
|---|---|---|
| HW01 | HARDWARE RESULT | RTX 3070 Ti / driver 610.47 / Vulkan 1.4.341 fingerprint; DGC, descriptor heap/buffer, BDA, sync2 and timeline features reported supported |
| HW02 | HARDWARE RESULT | Real `VkDevice`, device memory, 16 MiB HOST → DEVICE_LOCAL → HOST roundtrip, exact byte comparison, validation clean |
| HW03 | HARDWARE RESULT | Real Direct compute, 1,048,576 `uint32`, exact CPU oracle, validation clean |
| HW04 | HARDWARE RESULT | Real `vkCmdDispatchIndirect` from resident 12-byte control buffer, exact same CPU oracle, validation clean |

## HW03 / HW04 controlled comparison

Both gates intentionally hold constant:

- GPU and queue family;
- shader operation: `out[i] = in[i] * 3u + 7u`;
- 1,048,576 elements;
- local size 256;
- 4096 workgroups;
- descriptor-set compatibility baseline;
- CPU oracle hash: `0x8e2eef1faffc414f`.

Only launch control changes materially:

- HW03: Direct — launch dimensions encoded by CPU command recording.
- HW04: Indirect — `{4096,1,1}` resides in a device-local `VkDispatchIndirectCommand` buffer.

Observed result:

`Direct GPU output == Indirect GPU output == CPU oracle`

This proves functional equivalence only for the tested workload. No performance conclusion is made because GPU timestamps have not yet been collected.

## Evidence digests

- HW01: `f2af859183bb926598b7e90356294d8cac3f92e577ae35f180e8088397fac9d6`
- HW02: `d0c5549cc7c4aac5ddb7a395197cf0b319ae0005bfb2baf403958d58aa34acaf`
- HW03: `1c4b6c2fb05d04a06d48506c114f2755d630049df24ea2386fed0a47b131d06b`
- HW04: `da327a30d50ebfcc90431d06cd14be585efb0d6b29cb16dadbe308a3eb1faa31`

## Next gate

HW05 should characterize **Direct vs Indirect using GPU timestamps and CPU-side timing without changing the semantic workload**. DGC remains a later gate; capability availability is not treated as evidence of superiority.
