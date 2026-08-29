# Aion Research Lab — R0/R1 Verification Report

## Status
- R0 Minimal Authoritative World: **VERIFIED (reference scope)**
- R1 Change Journal / Replay / Rollback: **VERIFIED (same-architecture reference scope)**
- FOUNDATIONAL contracts: **none yet**

`VERIFIED` here does not mean universally proven. It means the current hypothesis passed the defined correctness and adversarial experiments on the reference environment. Windows, ARM, GPU execution, network replication, crash recovery and cross-machine deterministic floating-point remain unverified.

## Architectural correction discovered during R1
LAB-0 used `reserve_entity_id()` to advance identity state outside a transaction. A journal containing only transactions could therefore fail to reconstruct the exact future identity cursor.

R1 removed that side effect. `CreateEntity` now allocates the next sequential ID as part of the authoritative transaction. `next_entity_id()` is read-only.

This makes identity reconstruction transaction-complete under the current model.

## R0 invariants now tested
1. Entity ID 0 is reserved for world-scope mutations.
2. Entity IDs are stable, sequential and never silently skipped by `CreateEntity`.
3. Transaction IDs are strictly monotonic.
4. A transaction is fully validated before any mutation is applied.
5. Non-finite vector state is rejected.
6. `AdvanceReference` is a world-scope transaction mutation and must use a finite, non-negative `dt`.
7. Rendering/physics are still absent from the World Kernel; authoritative state does not depend on either.

## Canonical state hash
The World Kernel computes SHA-256 over a canonical little-endian serialization of:
- schema version;
- next entity ID;
- last transaction ID;
- living entity count;
- each allocated entity ID;
- alive flag;
- health;
- raw IEEE-754 bit patterns for position and velocity.

The SHA-256 implementation was cross-checked with Python `hashlib` for a known canonical world state.

This is intentionally an exact-state hash, not a perceptual or semantic-equivalence hash: `+0.0` and `-0.0` are different bit patterns and therefore different states.

## R1 journal design
The persistent journal stores **forward transactions only**.

A compact simulation frame can therefore be recorded as one `AdvanceReference(dt)` mutation rather than recording the resulting position of every entity.

For exact rollback, `ChangeJournal` separately stores ephemeral undo data containing the pre-transaction state of affected entities. Undo data is not part of the persisted forward journal.

Rollback is guarded: the journal refuses to apply an undo record if the current world hash no longer matches the journal's expected tail hash. This prevents applying stale undo data to a world that was mutated outside that journal.

## Deterministic replay experiment
Reference workload:
- 256 initial entities;
- 5,000 simulation frames;
- 5,001 total transactions including spawn;
- periodic health changes;
- entity destruction;
- later creation of a new identity;
- transactional reference integration at 1/60 s.

Observed final hash:

`9e6b6a3bac5a0564e2f3100bcf7eed9d0e48ef44615382f27931d5dcb9960c57`

The same forward transaction sequence produced this exact hash on:
- GCC 14.2, Release, x86-64 Linux;
- Clang 17, Release, x86-64 Linux.

The result was also reproduced after binary journal save/load.

This is evidence for deterministic replay across the tested compilers on the same architecture. It is **not yet evidence of bit-identical replay across Windows/Linux, x86/ARM, different floating-point modes or heterogeneous CPU/GPU execution**.

## Rollback experiment
The 5,001-transaction history was rolled back from the final state to an earlier checkpoint thousands of transactions in the past. The restored state hash exactly matched the hash originally recorded at that checkpoint.

The removed transaction tail was then replayed and the world returned to the exact original final SHA-256.

A separate full rollback of a journal-owned spawn transaction restored the exact pristine-world hash and identity cursor.

## R1 reference performance
Scenario: 256 entities, 5,000 frames, full post-commit SHA-256 and exact undo capture enabled.

- journal commit total: ~272.054 ms
- journal commit average per frame: ~0.054 ms
- persistent journal file: 224,236 bytes
- save: ~0.812 ms
- load: ~0.720 ms
- replay: ~1.504 ms
- rollback all: ~275.413 ms

These are reference measurements from the sandbox environment and are not production targets.

The current rollback and verification implementation intentionally favors correctness over scalability. In particular:
- `AdvanceReference` undo captures every existing entity state touched by the step;
- a full SHA-256 is computed after every journal-owned commit.

Both costs are expected to become unacceptable at very large entity counts. R2+ must investigate incremental hashing, page/chunk dirty tracking, copy-on-write history and/or bounded rollback windows without weakening correctness.

## R0 scalability baseline after transactional integration
The simulation benchmark now advances the world through a one-mutation transaction per frame rather than mutating positions through a hidden side channel.

Measured reference result for 1,000,000 lightweight entities / 120 frames:
- spawn transaction: 3,000,000 mutations;
- spawn commit: ~39.873 ms;
- simulation: ~1.229 ms/frame;
- ~813.5 million minimal entity position updates/s.

This benchmark is only `position += velocity * dt`; it is not evidence for one million full gameplay actors.

## Conclusion
R1 validates the first useful property of Aion's architecture in the tested scope:

> A world can be reconstructed from a pristine state using only its ordered forward transaction history and arrive at an identical canonical state hash; journal-owned history can also be rolled back exactly.

The property is strong enough to proceed to R2, but not strong enough to promote the implementation to FOUNDATIONAL.
