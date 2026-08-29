# R4A — Geometry Representation Contract

Status: **VERIFIED — CPU/reference contract scope**

## Hypothesis

A logical geometry object can own multiple independently implemented representations without making any one representation the definition of geometry. Operations are admitted by explicit capabilities; representations are revisioned derived data and may declare conservative error bounds.

## Contract

- `GeometryHandle`: compact 8-byte provider/resource/generation handle.
- `GeometryProviderOps`: provider-level function table; no virtual object per primitive.
- Provider state is owned by the registry through shared lifetime, so handles do not dangle when authoring wrappers are destroyed.
- `GeometryCapabilityMask` gates operations such as Bounds, RaySurface and SignedDistance.
- Unsupported operations fail explicitly instead of being synthesized with fake semantics.
- `GeometrySet` owns a semantic source revision. Representations from older source revisions become ineligible.
- `GeometrySet::eligible()` filters by provider validity, capability, source revision and declared geometric-error budget. It deliberately does not invent a cost policy yet.
- Math primitives (`Vec3`, `Aabb`, `Ray`) were extracted to a neutral layer so World, Spatial and Geometry remain siblings rather than depending on one another for basic types.

## Providers used to falsify the contract

### TriangleReferenceProvider

- Indexed triangle surface.
- Capabilities: Bounds, RaySurface.
- Direct triangle intersection is an oracle/reference implementation only, not a production triangle backend.

### AnalyticSdfProvider

- Analytic sphere and box signed-distance resources.
- Capabilities: Bounds, RaySurface, SignedDistance.
- RaySurface uses conservative SDF sphere tracing inside the resource bounds.

## Equivalence experiment

The same unit cube was represented as:

- 8 vertices / 12 triangles;
- analytic box SDF.

Results:

- exact equal AABB bounds;
- 882 surface hits compared across axis-oriented ray grids;
- triangle and SDF hit/miss classification matched;
- matching hits agreed in ray parameter within the declared test tolerance;
- rays starting inside the object matched;
- non-normalized ray directions preserved the same `t` semantics;
- SignedDistance was explicitly rejected for the triangle provider and correctly evaluated for the SDF provider;
- invalid handle generations were rejected;
- stale geometry-set revisions were filtered;
- error-budget filtering rejected a representation whose declared error exceeded the request.

## Reference-cost observation

200,000 identical rays, three Release runs in the current sandbox:

| Provider | Run 1 | Run 2 | Run 3 | Median |
|---|---:|---:|---:|---:|
| Triangle reference | 17.509 ms | 17.906 ms | 17.330 ms | 17.509 ms |
| Analytic SDF | 7.412 ms | 7.674 ms | 7.568 ms | 7.568 ms |

Both produced exactly 88,840 hits in every run.

This is **not** evidence that SDF is universally faster than triangles. The triangle provider is intentionally brute-force over 12 triangles, while the SDF is analytic. The benchmark only proves that the representation-neutral dispatch does not prevent radically different providers from implementing the same query contract.

Reported resource storage in this small case:

- triangle reference resource: 240 bytes of vector capacity payload;
- analytic SDF box resource: 52 bytes.

## Verification

After R4A integration:

- GCC 14.2: 8/8 tests PASS, zero strict-kernel warnings.
- Clang 17: 8/8 tests PASS, zero strict-kernel warnings.
- ASan + UBSan: R3.2 and R4A tests PASS.

## Non-promotions

R4A does not promote:

- triangle brute force as a production provider;
- analytic SDF as a complete implicit-geometry solution;
- one geometry representation as universally preferred;
- a rendering API inside GeometryKernel;
- a fixed provider cost model;
- GPU compatibility as measured fact.

## Next R4 hypothesis

R4B should test a scalable provider, not another toy primitive. The strongest next candidate is a sparse, static-topology signed-distance representation with explicit approximation error, derived from a source provider and designed so storage follows occupied/narrow-band information rather than a dense volume. The alternative production path — clustered triangle geometry — remains necessary and should be compared later under the same provider contract.
