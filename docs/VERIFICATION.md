# D-SF — Verification Index

## Regra

Toda afirmação deve ser classificada por escopo. CPU/reference evidence não substitui hardware evidence. Hardware evidence identifica configuração e non-claims.

## Regressão consolidada antes do snapshot R5E-HW04

Environment: Linux x86-64 sandbox, GCC 14.2, Release build.

```text
ctest: 17/17 PASS
failed: 0
real test time: 2.86 s
```

Tests:

1. `kernel_tests`
2. `r2_execution_tests`
3. `r21_patch_tests`
4. `r21_execution_patch_tests`
5. `r3_snapshot_tests`
6. `r3_fabric_tests`
7. `r32_budgeted_spatial_tests`
8. `r4_geometry_contract_tests`
9. `r4_sparse_sdf_tests`
10. `r4_clustered_triangle_tests`
11. `aion_r4_geometry_fabric_tests`
12. `aion_r4_online_geometry_telemetry_tests`
13. `aion_r4_online_geometry_routing_tests`
14. `aion_r5_device_tests`
15. `aion_r5_geometry_device_tests`
16. `aion_r5_device_work_tests`
17. `aion_r5_backend_translation_tests`

Historical closeout gates also passed Clang and ASan/UBSan where recorded in their phase reports. Do not generalize those sanitizer/compiler claims beyond the corresponding snapshots.

## Reference fingerprints / reports

- R1 state hash: `9e6b6a3bac5a0564e2f3100bcf7eed9d0e48ef44615382f27931d5dcb9960c57`.
- R2 correctness hash: `057e4f9d9e4921cb93da1a0c5b1245fafbadb49ce8755f572ebff34b558e53ca`.
- R2 benchmark hash: `71ccbd8aaaed14974c7c70ab4879f099f42195dd1ed6d312d93fa8642cf4218c`.
- R2.1 functional proof hash: `a073236582885e8cd53f22aa4825ed539a00c74c7c026e61d9e1db9940ada47e`.
- R2.1 1M integrated hash: `61d624a0af70729626dafebd3b3bea4cb5a074e625ec7f17ac981f6eef5a2c60`.
- R5C planner digest: `9229187388161744994`.
- R5D semantic digest sample: `2894648114337488996`.

Detailed values and benchmarks: see `R*_REPORT.md`, `R3_CPU_CLOSEOUT.md`, `R4_CPU_CLOSEOUT.md`, `R5_DEVICE_EXECUTION_CLOSEOUT.md` and `R5D_BACKEND_TRANSLATION_REPORT.md`.

## R5E hardware configuration

```text
GPU: NVIDIA GeForce RTX 3070 Ti
Vendor/Device: 10DE:2482
VRAM physical: 8192 MiB
NVIDIA driver: 610.47
Vulkan loader: 1.4.357
Vulkan device API: 1.4.341
Queue family used by HW02-HW04: 2 (compute + transfer)
```

### HW01 — Hardware fingerprint

Status: `VERIFIED — HARDWARE RESULT`.

Confirmed by real Vulkan profile feature bits, including BDA, synchronization2, timeline semaphore, descriptor indexing/buffer/heap, DGC, mesh shader and ray tracing capabilities. Capability presence is not a performance claim.

### HW02 — Memory roundtrip

Status: `VERIFIED — HARDWARE RESULT`.

```text
payload: 16,777,216 bytes
input FNV-1a64:  0xc0dd6ba4a0e044c2
output FNV-1a64: 0xc0dd6ba4a0e044c2
memcmp: PASS
validation errors: 0
validation warnings: 0
```

### HW03 — Direct compute

Status: `VERIFIED — HARDWARE RESULT`.

```text
elements: 1,048,576 uint32
local_size_x: 256
workgroups_x: 4096
operation: out[i] = in[i] * 3u + 7u
input FNV-1a64:    0xac41f8629b52fce0
CPU oracle:         0x8e2eef1faffc414f
GPU output:         0x8e2eef1faffc414f
full comparison: PASS
validation errors: 0
validation warnings: 0
```

### HW04 — Indirect compute

Status: `VERIFIED — HARDWARE RESULT`.

Evidence ZIP SHA-256:

`da327a30d50ebfcc90431d06cd14be585efb0d6b29cb16dadbe308a3eb1faa31`

```text
launch: vkCmdDispatchIndirect
control source: resident device buffer
VkDispatchIndirectCommand: {4096,1,1}
control bytes: 12
control FNV-1a64: 0x3891ae3c62606753
CPU oracle:         0x8e2eef1faffc414f
GPU output:         0x8e2eef1faffc414f
full comparison: PASS
validation errors: 0
validation warnings: 0
```

HW03 == HW04 == CPU oracle for the tested semantics. **No GPU execution-time comparison has yet been made.**

## Current non-claims

Not yet verified/measured as of HW04:

- Direct vs Indirect GPU performance;
- CPU submit/preparation comparison;
- DGC execution/performance;
- descriptor-buffer/descriptor-heap runtime comparison;
- production VRAM residency/eviction behavior;
- PCIe bandwidth characterization;
- final synchronization/barrier policy;
- integrated World -> Spatial -> Geometry -> Device demonstrator.
