# R5A–R5C — Device Residency, Geometry Packages, and Device Work Contract

Status:
- R5A Device Residency: **VERIFIED — reference backend scope**
- R5B Geometry Device Packages + atomic residency groups: **VERIFIED — reference backend scope**
- R5C Device Work Contract: **VERIFIED — CPU/reference planner scope**
- Real GPU performance/residency/synchronization: **NOT MEASURED**

Environment for the final local reference gate:
- g++ (Debian 14.2.0-19) 14.2.0
- clang version 17.0.0 (https://github.com/swiftlang/llvm-project.git 10999b6d034fe318f3d56c83bddb6572593a8bb0)
- Linux x86-64 sandbox; no production GPU result is claimed.

## R5A contract

Residency is derived, versioned state. A generic device resource identity is:

```text
namespace + object_id + revision + resource_class + subresource
```

Geometry is encoded through a helper that packs `GeometryHandle` into the generic owner identity; work/global resources do not pretend to be geometry.

Verified reference behavior:
- explicit memory budget;
- deterministic LRU victim selection among unpinned resources;
- pinned resources are not evicted;
- immutable resource key cannot be reused with different bytes;
- generation invalidates stale handles after slot reuse;
- key lookup is hash-indexed;
- free slots use a generational free-list rather than linear scans.

## R5B representation packages

Providers export a backend-independent `RepresentationArchive` with canonical little-endian serialization. No C++ struct padding or host ABI is copied into the archive.

Sparse SDF:

```text
Topology archive
+ contiguous int16 sample payload
```

Clustered Triangle:

```text
Topology/BVH archive
+ position payload
+ local-index payload
```

`GeometryDeviceCompilerRegistry` maps provider id to a package compiler; `DeviceResidencyManager` never branches on SDF/triangle/meshlet/brick format.

Reference test package sizes in the compact gate workload:
- Sparse SDF: 44,422 bytes
- Clustered Triangle: 356 bytes

Package fingerprints reproduced in the final gate:
- Sparse SDF: `14748051446487735809`
- Clustered Triangle: `2642998132373811531`

These fingerprints are for this archive format/workload only; they are not ABI promises.

## Atomic package residency

A geometry package is usable only if its complete required subresource set is resident.

Adversarial gate:
1. keep an unrelated resource resident;
2. submit a three-subresource Clustered Triangle package;
3. inject failure after two successful backend uploads;
4. require the third upload to fail;
5. destroy staged uploads and restore any planned evictions from host copies;
6. require zero package subresources resident;
7. require the unrelated resource and byte accounting to match the pre-operation state.

The first implementation exposed a planning bug in which multiple new subresources shared a mutable `entries.size()` sentinel. That implementation was rejected. The final plan carries explicit `existed_before` state and passed the injected-third-upload test.

Reference-backend rollback is **not** promoted as proof that real GPUs can always provide zero-headroom atomic replacement. Hardware backends may require staging/headroom/fence-specific publication rules.

## R5C work packet contract

A `DeviceWorkPacket` is a coarse semantic node, not a renamed draw call.

It declares:
- packet id;
- execution domain;
- opaque backend-resolved program key;
- launch mode: Direct / Indirect / DeviceGenerated;
- required resources and read/write access;
- explicit `after` dependencies;
- canonical immutable parameters.

The packet contains no Vulkan or D3D12 object.

Geometry-specific work is produced through `GeometryDeviceWorkCompilerRegistry`:
- Sparse SDF: ray-surface + truncated-distance operations;
- Clustered Triangle: ray-surface operation.

Both become generic work packets before planning.

### Data dependencies

The final planner is resource-centric rather than pairwise O(P²):

```text
resource -> last writer + active readers
```

Packets are processed in canonical packet-id order. Read-after-write, write-after-read and write-after-write edges are derived from declared access. Explicit dependencies are added independently; contradictions become cycles and are rejected.

Topological execution uses incremental frontier waves rather than rescanning every packet per wave.

### Canonical command stream

The reference command compiler canonicalizes semantically unordered resource/dependency lists before hashing. Shuffling packet registration order or resource order does not change the command-stream digest.

Final reference digest:
`9229187388161744994`

### Capability gating

The contract permits Direct, Indirect and DeviceGenerated launches. The planner rejects modes not supported by declared backend capabilities. This is a semantic reference only; no Vulkan DGC, D3D12 ExecuteIndirect or Work Graph execution occurred in the sandbox.

## Scaling characterization

The scaling benchmark deliberately includes both intended and adversarial packet counts. Representative final run is in `R5C_SCALING_RAW.txt`.

Observed reference points in that run:
- 100 independent packets: ~0.039 ms
- 1,000 independent packets: ~0.351 ms
- 5,000 independent packets: ~5.182 ms
- 10,000 independent packets: ~14.228 ms
- 100,000 independent packets: ~1.104 s

The 100k-host-packet case is **not** accepted as a production target. `DeviceWorkPacket` is intentionally coarse-grained: fine work should be represented by `work_items`, indirect sequences, or device-generated expansion. Creating one host packet per primitive/meshlet/draw would recreate the CPU submission model under a new name.

These timings are sandbox CPU reference measurements, not hardware GPU results.

## Final gate

```text
GCC:   16/16 PASS
Clang: 16/16 PASS
strict kernel warnings: 0
ASan R5A/R5B/R5C: PASS
UBSan R5A/R5B/R5C: PASS
```

## What is promoted

- Device state is derived and versioned, never World authority.
- Generic device resource identity is namespace/object/revision based.
- Geometry package publication is atomic at the residency-manager semantic boundary.
- Representation archives are canonical and backend-independent.
- Device work declares resources/access/dependencies and an opaque program identity.
- Work planning derives data hazards from resource metadata.
- Work packets are coarse launch nodes; fine work belongs on the device side.
- Indirect/device-generated work are capabilities, not the architecture itself.

## What is not promoted

- current archive bytes as final GPU-native layout;
- current LRU as universal residency policy;
- current packet-count performance as production performance;
- any GPU bandwidth/latency/occupancy claim;
- Vulkan or D3D12 as the engine contract;
- reference-backend rollback as proof of hardware atomicity;
- one host work packet per draw/meshlet/primitive.

## Next authorized experiment

**R5D — Backend Capability & Translation Prototype**

The next useful question is no longer whether the semantic packet exists. It is whether the same packet can be lowered into materially different backend capability models without contaminating World/Geometry/DeviceWork contracts.

The reference target should test at least:
- direct dispatch fallback;
- indirect launch lowering;
- device-generated-capable lowering;
- resource binding model abstraction compatible with modern descriptor-memory/heap approaches;
- capability negotiation and deterministic fallback.

Real Vulkan/D3D12 execution remains a hardware gate and cannot be truthfully validated in this sandbox.
