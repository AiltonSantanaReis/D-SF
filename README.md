# D-SF — Experimental World / Geometry / Device Architecture

> **Research-first engine architecture where authoritative world state is independent from visual geometry, physics representation and device/backend execution.**

D-SF is an experimental engine research program. Its purpose is not to reproduce Unreal, Unity or another existing engine, and it does not assume that meshes, voxels, SDFs, GPU execution or any current API are inherently the correct foundation.

The project follows a stricter rule:

```text
Theory / Hypothesis
        ↓
Reference / Oracle
        ↓
Controlled Experiment
        ↓
Evidence
        ↓
Conclusion
        ↓
Specification / Promotion / Rejection
```

A mechanism is promoted only after the evidence supports it. Attractive ideas that fail are retained as research results rather than hidden.

---

## 1. Core Thesis

The central architectural hypothesis is:

> **Authoritative world state must not be defined by visual geometry, renderer, physics backend or processor architecture. Those systems should consume derived representations of a more fundamental semantic/spatial state.**

The current separation is:

```text
World != Geometry != Device Package != Device Work != Backend Command Model
```

And another permanent rule is:

```text
VisualGeometry != PhysicalGeometry
```

The renderer is not the world. Physics is not the world. A triangle mesh is not the identity of an object. Vulkan is not the engine contract.

---

## 2. Research Principles

D-SF is developed under the following rules:

1. **Reference before optimization.** Optimized paths must be compared against a simpler authoritative oracle whenever semantic equivalence is expected.
2. **Correctness before performance.** A faster incorrect path has failed.
3. **No favorite technology.** Techniques are selected by evidence for a workload, not by popularity, novelty or familiarity.
4. **AI proposes; Kernel validates.** AI can generate hypotheses, designs and implementation candidates, but deterministic validation decides promotion.
5. **Hardware claims require hardware evidence.** CPU/reference measurements are never reported as GPU performance.
6. **A benchmark must describe the work it actually performs.** One million lightweight state records are not called one million complete NPCs.
7. **Failures are evidence.** Falsified approaches remain part of the research history.
8. **Contracts may stabilize; implementations remain replaceable.** A winning implementation is not automatically a permanent architectural dependency.
9. **System-level economics matter.** Local wins that increase memory, bandwidth, latency or integration complexity elsewhere may be rejected.

A useful conceptual objective is:

```text
(capacity + quality + scale)
----------------------------
(time + memory + bandwidth + complexity)
```

This is a design lens, not a single benchmark score.

---

## 3. Evidence Classes

Every relevant result must use an explicit evidence class.

| Class | Meaning |
|---|---|
| **REFERENCE RESULT** | Semantics or measurements validated in the CPU/reference research environment. |
| **HARDWARE RESULT** | Actually executed and verified/measured on identified target hardware. |
| **PARTIAL** | Useful evidence exists, but important scope remains unresolved. |
| **FALSIFIED** | A tested hypothesis/path failed in the stated scope and is not promoted. |
| **NOT MEASURED** | No performance or cost claim is permitted. |

`VERIFIED` always has a scope. As of R5E-HW05, **no D-SF contract is classified as `FOUNDATIONAL`**.

---

## 4. Architecture

```text
Authoritative World
    |
    +--> Hybrid Transactions / Change Journal
    |
    +--> Execution Kernel
    |      +--> declared reads / writes / dependencies
    |      +--> deterministic waves
    |      +--> private worker patches
    |      +--> atomic authoritative commit
    |
    +--> Shared Spatial Snapshot
    |      +--> Wide BVH8
    |      +--> Morton BVH8
    |      +--> cost-aware spatial policy
    |      +--> budgeted maintenance
    |
    +--> GeometrySet
    |      +--> Clustered Triangle
    |      +--> Analytic / Sparse SDF
    |      +--> capabilities
    |      +--> revision
    |      +--> geometric error
    |      +--> telemetry / routing
    |
    +--> Device Geometry Packages
           |
           +--> Residency Contract
           |
           +--> DeviceWorkPacket DAG
                  |
                  +--> Backend-neutral Translation
                         |
                         +--> Direct
                         +--> Indirect
                         +--> Device Generated candidates
                                |
                                +--> Real Vulkan Hardware Gates
```

The active architecture is documented in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## 5. Verified Research State

| Stage | Status | Scope / Result |
|---|---|---|
| R0 | **VERIFIED** | Minimal authoritative world reference |
| R1 | **VERIFIED** | Journal, canonical hash, replay and rollback |
| R2 | **VERIFIED / PARTIAL PERF** | Dependency execution graph correctness |
| R2.1 | **VERIFIED** | Hybrid scalar/range transaction patches |
| R3 / R3.1 / R3.2 | **VERIFIED** | Shared spatial snapshot, cost-aware fabric and budgeted maintenance — CPU/reference |
| R4A–R4F | **VERIFIED** | Geometry contract, Sparse SDF, clustered triangles, selection, telemetry and safe exploration — CPU/reference |
| R5A | **VERIFIED** | Device residency contract — reference backend |
| R5B | **VERIFIED** | Geometry device packages + atomic residency — reference backend |
| R5C | **VERIFIED** | Device work contract/planner — CPU/reference |
| R5D | **VERIFIED** | Backend capability + translation semantics — CPU/reference |
| R5E-HW01 | **VERIFIED** | Real Vulkan hardware/capability fingerprint |
| R5E-HW02 | **VERIFIED** | Real `VkDevice` + `VkDeviceMemory` roundtrip |
| R5E-HW03 | **VERIFIED** | Real Direct compute vs exact CPU oracle |
| R5E-HW04 | **VERIFIED** | Real Indirect compute vs exact CPU oracle |
| R5E-HW05 | **VERIFIED** | Direct vs Indirect GPU timestamps and CPU characterization; no universal winner |

The project roadmap and promotion rules are maintained in [`docs/PROJECT.md`](docs/PROJECT.md).

---

## 6. Current Hardware Evidence

Current hardware target used by R5E:

```text
GPU                 NVIDIA GeForce RTX 3070 Ti
Vendor / Device     10DE:2482
Physical VRAM       8192 MiB
NVIDIA Driver       610.47
Vulkan Loader       1.4.357
Vulkan Device API   1.4.341
Compute Queue       family 2
```

### HW01 — Capability Fingerprint

Real Vulkan feature discovery confirmed, among other capabilities:

- `bufferDeviceAddress`;
- `synchronization2`;
- timeline semaphore;
- descriptor indexing;
- descriptor buffer;
- `VK_EXT_descriptor_heap` feature bits;
- `VK_EXT_device_generated_commands` feature bits;
- mesh shader;
- ray tracing capabilities.

Capability presence is **not** a performance result.

### HW02 — Real Memory Roundtrip

```text
16 MiB HOST
      ↓
DEVICE_LOCAL
      ↓
16 MiB HOST READBACK
```

Result:

```text
Input  FNV-1a64: 0xc0dd6ba4a0e044c2
Output FNV-1a64: 0xc0dd6ba4a0e044c2
Full byte comparison: PASS
Validation errors:   0
Validation warnings: 0
```

### HW03 — Direct Compute

Workload:

```text
1,048,576 uint32
local_size_x = 256
workgroups_x = 4096
out[i] = in[i] * 3u + 7u
```

```text
CPU oracle: 0x8e2eef1faffc414f
GPU output: 0x8e2eef1faffc414f
Full element comparison: PASS
Validation errors:   0
Validation warnings: 0
```

### HW04 — Indirect Compute

The same workload was executed with a device-resident indirect launch-control resource:

```text
VkDispatchIndirectCommand {4096, 1, 1}
control bytes: 12
launch: vkCmdDispatchIndirect
```

```text
CPU oracle: 0x8e2eef1faffc414f
GPU output: 0x8e2eef1faffc414f
Full element comparison: PASS
Validation errors:   0
Validation warnings: 0
```

This demonstrates **functional equivalence in the tested workload** between the Direct and Indirect paths. It does **not** establish performance equivalence or superiority.

---

## 7. How Development Must Proceed

D-SF development is intentionally split into two environments:

```text
LOCAL / SANDBOX
    research
    hypotheses
    implementations
    experiments
    falsifications
    reference tests
           ↓
VERIFIED MILESTONE
           ↓
GITHUB
    consolidated source snapshot
    canonical documentation
    closeout
    evidence summaries / fingerprints
```

GitHub is the **official record of consolidated verified milestones**, not the live experimental laboratory.

A future stage must not be advanced only because the previous executable returned `0`. The required sequence is:

```text
1. State the hypothesis
2. Define the oracle / expected semantics
3. Define acceptance and rejection conditions
4. Freeze the variables that must remain constant
5. Change one architectural variable when possible
6. Execute the gate
7. Generate an evidence package
8. Validate evidence integrity
9. Analyze correctness before performance
10. Record limitations and non-claims
11. Update canonical state
12. Promote to GitHub only after the milestone is coherent
```

When comparing two mechanisms, D-SF tries to preserve the same workload, data, shader/program, binding model and oracle so that the changed variable is identifiable.

Example:

```text
HW03 Direct
vs
HW04 Indirect

same input
same shader
same binding baseline
same element count
same CPU oracle

only launch mechanism changes
```

---

## 8. Hardware Gate Delivery Protocol

Hardware experiments are distributed as self-contained gates rather than asking the operator to manually reproduce dozens of commands.

A typical package follows this structure:

```text
R5E_HWxx_GATE/
├── README.md
├── HWxx_ACCEPTANCE.md
├── RUN_R5E_HWxx.cmd
├── scripts/
│   └── Run-R5E-HWxx.ps1
├── src/
│   └── probe_or_test.cpp
├── shaders/
│   └── workload.comp          # when applicable
└── baseline / expected state
```

The gate must be designed so that both **PASS and FAIL are useful results**. A failure package should contain enough diagnostics to determine whether the failure occurred in:

```text
prerequisite discovery
→ toolchain
→ shader compilation
→ native compilation
→ native link
→ Vulkan instance/device
→ allocation/upload
→ command recording
→ synchronization
→ execution
→ readback
→ oracle comparison
→ validation
```

Runner/toolchain failures are not mislabeled as negative GPU results.

---

## 9. How Evidence Is Generated

Each hardware gate creates a timestamped evidence directory, for example:

```text
evidence/
└── R5E_HW04_YYYYMMDD-HHMMSS/
```

Depending on the gate, the directory may contain:

```text
run_manifest.json
hardware / driver snapshot
Vulkan capability snapshot
compiler discovery
shader compile stdout / stderr
C++ compile stdout / stderr
link stdout / stderr
probe stdout / stderr
validation log
input / expected / output fingerprints
launch-control data
memory telemetry
source / shader hashes
command exit codes
acceptance result
```

The runner then generates a ZIP and an external SHA-256:

```text
R5E_HW04_EVIDENCE_YYYYMMDD-HHMMSS.zip
R5E_HW04_EVIDENCE_YYYYMMDD-HHMMSS.zip.sha256
```

The operator returns **both files** for analysis.

### Evidence integrity validation

Before interpreting the result, the evidence is checked in this order:

```text
1. Compute SHA-256 of received ZIP
2. Compare with external .sha256
3. Extract into a clean directory
4. Verify internal file hashes / manifest
5. Confirm expected source/shader identity
6. Inspect command exit codes
7. Inspect validation output
8. Inspect oracle comparison
9. Only then classify PASS / FAIL / PARTIAL
```

A corrupted or incomplete evidence package cannot promote a stage.

---

## 10. Acceptance Rules

A correctness gate normally requires all relevant conditions to pass, for example:

```text
source identity          PASS
shader build             PASS
C++ compilation          PASS
native link              PASS
VkDevice creation        PASS
required feature enable  PASS
command execution        PASS
readback                 PASS
CPU oracle comparison    PASS
validation errors        0
validation warnings      0
```

A performance gate adds stronger requirements:

- correctness gate must already be green;
- warm-up behavior must be defined;
- GPU timestamps must be used for GPU execution claims;
- CPU preparation, submit and synchronization should be measured separately where relevant;
- repeated runs must be preserved, not only the best run;
- medians and tail behavior such as P90/P95 are preferred over one-shot numbers;
- workload, hardware and driver identity must remain recorded;
- a performance winner is valid only for the measured workload/scope.

---

## 11. Evidence Promotion and Closeout

After a gate passes, the result is **not immediately promoted by description alone**.

The closeout process is:

```text
Evidence ZIP
   ↓
Integrity verification
   ↓
Technical analysis
   ↓
Result classification
   ↓
Closeout report
   ↓
Structured current state
   ↓
Regression gate
   ↓
GitHub milestone snapshot
```

Typical promoted artifacts include:

```text
docs/R5E_HWxx_CLOSEOUT.md
docs/R5E_HWxx_CLOSEOUT.json
docs/R5E_CURRENT_STATE.json
docs/RESEARCH_LEDGER.md
docs/VERIFICATION.md
docs/ARCHITECTURE.md       # if architecture changed
docs/PROJECT.md            # if roadmap/governance changed
README.md                   # summarized active state
```

Raw evidence does not need to be dumped indiscriminately into the repository. The repository must preserve enough hashes, fingerprints, environment identity and conclusions to audit the promoted result while avoiding unnecessary binary/log noise.

### Immutable historical baselines

When a canonical document is significantly restructured, its prior verified version must not disappear. Exact historical snapshots are preserved under:

```text
docs/history/<milestone>/
```

The R2.1 canonical snapshot is preserved byte-for-byte in:

```text
docs/history/R2_1/README.md
docs/history/R2_1/PROJECT.md
docs/history/R2_1/ARCHITECTURE.md
docs/history/R2_1/RESEARCH_LEDGER.md
docs/history/R2_1/VERIFICATION.md
```

This rule exists specifically to prevent a future documentation update from silently replacing detailed audit history with a summary.

---

## 12. Regression Before Promotion

Before a consolidated source milestone is published, the CPU/reference regression suite must be rerun from a clean build.

The consolidated R3–R5D reference state used before the HW04 documentation promotion produced:

```text
17 / 17 tests PASS
0 failed
```

Compiler/sanitizer claims are kept scoped to the specific closeout where they were actually executed.

A later stage is not allowed to silently invalidate a previously promoted invariant. If it does, that is a regression and must be recorded explicitly.

---

## 13. State after R5E-HW05

The next authorized hardware sequence is now:

> **GPU Timestamp & Direct/Indirect Characterization**

HW05 must keep the HW03/HW04 workload semantically equivalent while adding measurement rather than changing architecture unnecessarily.

The intended comparison is:

```text
same input
same output oracle
same compute operation
same element count
same baseline binding model
same target GPU

Direct launch
vs
Indirect launch
```

Measurements should separate at least:

```text
CPU preparation
CPU command recording / submit
GPU execution timestamp
CPU/GPU synchronization
```

The experiment should also distinguish cold/warm effects where relevant and use repeated samples rather than a single timing.

HW05 has now provided an evidence-based, scope-limited statement about the cost difference between Direct and Indirect for this workload.

### After HW05

Current controlled sequence, subject to evidence:

```text
HW05  ✓ Direct vs Indirect timestamps / characterization completed
  ↓
HW06  synchronization/barrier characterization as required
  ↓
HW07  Device Generated Commands bring-up
  ↓
HW08  DGC comparison under controlled workloads
  ↓
HW09  descriptor binding candidates
      baseline sets vs descriptor buffer / descriptor heap
  ↓
R5E closeout
  ↓
R5F device architecture conclusions, if required
  ↓
R6 integrated heterogeneous demonstrator
```

This order is not ideological. A newer API is never promoted simply because it is newer. If Direct or Indirect remains superior for a workload, the data decides.

---

## 14. What Is Not Yet Claimed

As of R5E-HW05, D-SF does **not** claim:

- neither launch mode is a universal winner for the measured workload;
- DGC is faster or preferable;
- descriptor heap/buffer is the final binding model;
- current reference barriers are the final hardware synchronization policy;
- Sparse SDF is universally superior to clustered triangles;
- GPU paths have solved the 1M-object spatial update problem;
- the architecture is production-ready;
- D-SF is competitive with general-purpose commercial engines as a complete product.

Those claims require their own evidence.

---

## 15. Repository Guide

```text
docs/PROJECT.md
    mission, governance, roadmap and closure criteria

docs/ARCHITECTURE.md
    active contracts and architecture

docs/RESEARCH_LEDGER.md
    research decisions, promoted and falsified paths

docs/VERIFICATION.md
    fingerprints, environments and verification index

docs/history/R2_1/
    immutable byte-for-byte copy of the R2.1 canonical documentation

docs/R5E_CURRENT_STATE.json
    machine-readable active R5E state

docs/R5E_HARDWARE_BRINGUP.md
    hardware bring-up summary

docs/R5E_HW04_CLOSEOUT.md
    HW04 indirect-compute closeout

docs/R5E_HW05_CLOSEOUT.md
    latest closed hardware characterization gate

include/ + src/
    promoted reference implementation snapshot

tests/
    permanent regression tests for promoted properties

bench/
    reference benchmarks / characterization programs

results/
    selected structured results suitable for repository tracking
```

---

## 16. Status Summary

```text
R0      ✓
R1      ✓
R2      ✓ correctness / partial performance
R2.1    ✓
R3      ✓ CPU/reference
R4      ✓ CPU/reference
R5A     ✓ reference
R5B     ✓ reference
R5C     ✓ CPU/reference
R5D     ✓ CPU/reference translation

R5E-HW01  ✓ real hardware fingerprint
R5E-HW02  ✓ real Vulkan memory roundtrip
R5E-HW03  ✓ real Direct compute
R5E-HW04  ✓ real Indirect compute
R5E-HW05  ✓ measured Direct vs Indirect

R6        planned: integrated demonstrator
```

The research remains active. The rule for every next step is unchanged:

> **Do not promote what has not been demonstrated. Do not hide what the data falsifies.**
