# R4B — Sparse Implicit Geometry Closeout

Status: **VERIFIED — CPU/reference scope**
Predecessor: R4A Representation Contract — VERIFIED
Next authorized experiment: **R4C — Clustered Triangle Surface**

## 1. Hypothesis

A geometry representation can be compiled from a certified signed-distance source into a static-topology sparse narrow-band field without allocating a dense staging volume, while preserving:

- a conservative geometric-error certificate;
- bounded truncated-distance error near the surface;
- ray-surface capability;
- storage that trends with active surface information rather than full enclosing volume as resolution/scale increases;
- representation independence inside `GeometrySet`.

The hypothesis does **not** claim that sparse storage is always smaller or faster than dense storage.

## 2. Implemented representation

`SparseSdfProvider` is a provider registered through the R4A Geometry Kernel. It exposes:

- `Bounds`;
- `RaySurface`;
- `TruncatedSignedDistance`.

It intentionally does **not** advertise exact `SignedDistance`. Far from the narrow band, values are truncated to `+band` or `-band`.

### 2.1 Static hierarchy

The current reference layout is VDB-inspired but independent of OpenVDB at runtime:

```text
Sparse root entries
      │
      ▼
128^3-cell root region
      │
      ▼
4^3 wide upper node
      │
      ▼
4^3 wide lower node
      │
      ▼
8^3-cell narrow-band brick
      │
      ▼
9^3 quantized lattice samples
```

Uniform positive regions are omitted. Uniform negative regions are encoded as bitmasked tiles. Only regions that cannot be conservatively classified outside the narrow band descend to a child.

Internal nodes store bitmasks and contiguous child ranges rather than pointer-per-child objects.

### 2.2 Topology/payload separation

The final R4B implementation stores topology and values separately:

```text
roots[]
upper_nodes[]
lower_nodes[]
leaf_bricks[]  -> sample_offset
samples[]      -> one contiguous int16 payload
```

Each leaf brick stores only coordinates plus an offset into the resource-wide contiguous sample buffer. The thousands of per-brick heap allocations present in an intermediate prototype were removed before verification.

### 2.3 No dense staging grid

Compilation recursively classifies cells using the source distance and a Lipschitz certificate. Leaf sample storage is allocated only for narrow-band candidate bricks. A full dense volume is never allocated by the sparse compiler.

The number of source evaluations can still exceed a dense grid at coarse/unfavorable regimes because adjacent bricks duplicate halo samples and classification itself consumes samples. This is explicitly measured and is not hidden.

## 3. Source certificate and conservative classification

Compilation requires an explicit source certificate:

```text
max_distance_error
lipschitz_bound
```

For a cell of half-diagonal `r`, a sampled center value `d` is classified as a uniform exterior tile only when:

```text
d > band + source_error + L*r
```

and as a uniform interior tile only when:

```text
d < -(band + source_error + L*r)
```

Otherwise the compiler descends.

For exact analytic SDF sources in the tests:

```text
source_error = 0
L = 1
```

## 4. Quantization and error certificate

Leaf values are clamped to the narrow band and quantized to signed 16-bit normalized values.

For trilinear interpolation of an `L`-Lipschitz field on a voxel of edge length `h`, the reference compiler uses the conservative interpolation term:

```text
L * (sqrt(3) / 2) * h
```

The declared representation error is:

```text
source_error
+ L * (sqrt(3) / 2) * h
+ quantization_error
```

The certificate is derived analytically; it is not fitted to benchmark output.

## 5. Ray traversal evolution

### Rejected path A — truncated-distance marching through the whole domain

The first ray implementation used `abs(truncated_distance)` as its global step size. As resolution increased, `band = 3h` decreased, so ray cost increased with resolution.

Rejected.

### Rejected path B — scan wide hierarchy children per ray

The next attempt traversed hierarchy nodes but tested too many child AABBs. It was slower than the original reference.

Rejected.

### Accepted reference path — brick DDA + local tracing

The final R4B ray path uses 3D DDA at the 8-cell brick scale. Uniform/inactive bricks are skipped. Sphere tracing is performed only while the ray is inside an active narrow-band brick, and local brick samples are accessed directly without repeating the global hierarchy lookup for each step.

This removed the pathological resolution dependence observed in the first implementation.

## 6. Correctness gates

The verified test includes:

- exact analytic sphere -> sparse SDF compilation;
- exact analytic box with edges/corners -> sparse SDF compilation;
- thousands of truncated-distance comparisons;
- conservative error-bound assertions;
- ray hit/miss agreement outside the error-budget silhouette ambiguity region;
- bounded ray `t` deviation on stable hits;
- exact-vs-truncated capability separation;
- rejection of triangle-only sources as SDF compiler inputs;
- invalid source-certificate rejection;
- multiple sparse resolutions coexisting in one `GeometrySet`;
- error-budget filtering without format-specific selection;
- provider/resource lifetime after authoring wrappers leave scope.

Final validation:

```text
GCC 14.2   9/9 tests PASS
Clang 17   9/9 tests PASS
kernel warnings: 0
ASan: PASS on R4A/R4B
UBSan: PASS on R4A/R4B
```

## 7. Error results

At sphere resolution `h = 1/32`:

```text
observed maximum truncated-field error: ~0.00458
declared conservative error:           ~0.02706
```

The box test, which includes non-smooth edges/corners, observed approximately:

```text
~0.00948
```

and remained within the same conservative certificate.

Observed error approximately halved as voxel size halved in the resolution sweep.

## 8. Resolution sweep — sphere

Medians from three benchmark runs where timing is applicable:

| Grid divisor | voxel h | leaves | Sparse bytes | Dense int16 bytes | Sparse / dense-i16 | Dense float32 bytes | Observed error | Declared error | 100k rays |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 32 | 0.03125 | 511 | 753,734 | 778,034 | 0.969x | 1,556,068 | ~0.0075 | ~0.0271 | ~86.1 ms |
| 64 | 0.015625 | 1,927 | 2,842,910 | 5,142,706 | 0.553x | 10,285,412 | ~0.0038 | ~0.0135 | ~80.9 ms |
| 128 | 0.0078125 | 7,985 | 11,779,090 | 37,219,250 | 0.317x | 74,438,500 | ~0.0019 | ~0.0068 | ~102.3 ms |

The fair memory baseline is dense-int16 because the sparse payload is also int16. Dense-float is retained only as a second reference.

## 9. Scale sweep — fixed voxel size

With `h = 1/32`, increasing sphere radius produced:

| Radius | Leaves | Sparse bytes | Dense int16 bytes | Sparse / dense-i16 |
|---:|---:|---:|---:|---:|
| 1 | 511 | 753,734 | 778,034 | 0.969x |
| 2 | 1,927 | 2,842,910 | 5,142,706 | 0.553x |
| 4 | 7,985 | 11,779,090 | 37,219,250 | 0.317x |

Doubling object scale at fixed voxel size increased active leaf count by roughly 4x in these samples, while the dense domain grew substantially faster. This is consistent with a narrow-band representation dominated by surface area rather than full volume.

## 10. Shape dependence — important falsification

The box showed that sparse is not universally memory efficient against an equally quantized dense grid:

| Shape: box | Sparse / dense-i16 |
|---|---:|
| 1/32 | 1.915x — sparse loses |
| 1/64 | 1.331x — sparse loses |
| 1/128 | 0.800x — sparse wins modestly |

Therefore the following rule is rejected:

```text
Sparse SDF => always lower memory
```

Break-even depends on resolution, narrow-band thickness, shape surface/volume relationship, brick halo overhead, and future device/layout costs.

## 11. Dense-int16 versus sparse CPU lookup trade-off

For 200k random distance samples on a sphere:

| Divisor | Dense build | Sparse build | Dense query 200k | Sparse query 200k | Sparse/Dense query cost |
|---:|---:|---:|---:|---:|---:|
| 32 | ~24.0 ms | ~14.4 ms | ~3.89 ms | ~59.0 ms | ~15.3x |
| 64 | ~106.7 ms | ~73.1 ms | ~8.03 ms | ~49.9 ms | ~6.0x |
| 128 | ~770.9 ms | ~270.9 ms | ~13.29 ms | ~52.6 ms | ~3.5x |

Sparse trades random CPU lookup speed for memory and compilation scaling. This is not considered a failure because the Geometry Kernel was designed to retain multiple representations and choose by workload; however it prohibits promoting Sparse SDF as the universal CPU distance representation.

Coherent/batched/GPU access remains a separate future experiment.

## 12. Memory composition

At `1/128` sphere resolution:

```text
topology bytes:      ~136,960
sample payload bytes: ~11,642,130
```

The payload dominates. Further major memory reduction should target sample layout/precision/brick halos, not node micro-optimization.

The current 9^3 brick samples intentionally duplicate boundary values to make local trilinear interpolation cheap. A compact 8^3 persistent payload plus derived interpolation cache is a valid future hypothesis, but is not promoted without a measured CPU/GPU trade-off.

## 13. Decisions

### Promoted within R4B reference scope

- `TruncatedSignedDistance` is distinct from exact `SignedDistance`.
- Sparse compilation requires an explicit source error/Lipschitz certificate.
- Static topology and sample payload are separated.
- Sample payload is contiguous and quantized int16.
- Narrow-band hierarchy uses wide bitmasked nodes and uniform tiles.
- Error budget is explicit and representation-level.
- Ray traversal uses brick-level spatial skipping rather than global truncated sphere marching.
- Sparse and dense/other representations must be allowed to coexist; selection cannot be format dogma.

### Explicitly not promoted

- sparse is always smaller;
- sparse is always faster;
- 9^3 halo bricks are the final storage layout;
- current CPU random-query performance is production quality;
- current hierarchy is a NanoVDB/OpenVDB replacement;
- exact cross-platform floating point/GPU behavior;
- a universal GeometrySet cost policy.

## 14. R4B conclusion

R4B validates the R4A thesis under real approximation/storage pressure: geometry can remain representation-independent when an implicit representation becomes sampled, sparse, quantized, error-bounded and scale-dependent.

The experiment also falsifies the idea that the implicit representation should automatically replace triangles or dense fields.

**R4B status: VERIFIED — CPU/reference scope.**

## 15. Next experiment — R4C Clustered Triangle Surface

The current triangle provider is brute-force and exists only as a correctness oracle. It is not a fair scalable counterpart to Sparse SDF.

R4C should construct a cluster-first triangle representation with:

- compact bounded-size triangle clusters/meshlets;
- local vertex reuse and local indices;
- contiguous cluster payload;
- explicit per-cluster bounds;
- two-level or cluster-oriented ray acceleration;
- optional position quantization with a conservative error certificate;
- future compatibility with mesh shaders, cluster acceleration structures and fine-grained streaming.

Only after R4C should the laboratory begin a serious heterogeneous geometry cost policy between scalable triangle and implicit representations.
