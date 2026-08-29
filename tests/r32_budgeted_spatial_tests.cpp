#include "aion/kernel/spatial_fabric.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while(false)

namespace {
std::vector<aion::SpatialRecord> records(std::size_t count) {
    std::mt19937 rng(0xD5F32002U);
    std::uniform_real_distribution<float> pos(-5000.0F, 5000.0F);
    std::uniform_real_distribution<float> ext(0.1F, 3.0F);
    std::vector<aion::SpatialRecord> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        out.push_back({static_cast<aion::EntityId>(i + 1), {pos(rng), pos(rng), pos(rng)}, {ext(rng), ext(rng), ext(rng)}});
    return out;
}

aion::PatchTransaction move_patch(const aion::SpatialSnapshot& snapshot, std::uint64_t id, std::size_t count, float delta) {
    aion::PatchTransaction tx{.id = id};
    count = std::min(count, snapshot.size());
    tx.scalar_mutations.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto slot = static_cast<std::uint32_t>((i * 3571U + static_cast<std::size_t>(id) * 17U) % snapshot.size());
        auto c = snapshot.center(slot);
        c.x += delta; c.y -= delta * 0.25F; c.z += delta * 0.125F;
        tx.scalar_mutations.push_back({aion::MutationKind::SetPosition, snapshot.entity(slot), c, 0});
    }
    return tx;
}

aion::Aabb query_box(int i) {
    const float x = -4200.0F + static_cast<float>((i * 977) % 8400);
    const float y = -3900.0F + static_cast<float>((i * 619) % 7800);
    const float z = -3600.0F + static_cast<float>((i * 431) % 7200);
    return {{x - 180.0F, y - 180.0F, z - 180.0F}, {x + 180.0F, y + 180.0F, z + 180.0F}};
}
}

int main() {
    std::string error;

    // Generic Execution Kernel budget gate: no slack means no maintenance invocation.
    {
        std::size_t calls = 0;
        const aion::MaintenanceBudget blocked{
            .frame_budget_ms = 16.0,
            .critical_path_ms = 15.5,
            .safety_margin_ms = 1.0,
            .max_maintenance_slice_ms = 2.0,
        };
        const auto result = aion::ExecutionBudgetScheduler::run_maintenance(blocked, [&](double) {
            ++calls;
            return aion::CooperativeTaskProgress{};
        });
        CHECK(result.ok);
        CHECK(!result.ran);
        CHECK(result.granted_ms == 0.0);
        CHECK(calls == 0);
    }

    // Cooperative capture + build while the live snapshot changes. No monolithic snapshot copy is used.
    {
        aion::SpatialSnapshot snapshot;
        CHECK(snapshot.build(records(30000), error));
        aion::MortonBvh8View active;
        CHECK(active.build(snapshot, error));

        aion::BudgetedSahBuilder builder(8, 512);
        CHECK(builder.start(snapshot, error));
        std::uint64_t txid = 2;
        std::size_t frames = 0;
        std::size_t zero_grant_frames = 0;

        while (builder.state() != aion::BudgetedSahState::Ready && frames < 20000) {
            if ((frames % 3U) == 0U) {
                const auto applied = snapshot.apply_patch_transaction(move_patch(snapshot, txid++, 120, 0.05F));
                CHECK(applied.ok);
                CHECK(builder.observe_changes(applied.changes, error));
                CHECK(active.sync(snapshot, applied.changes, error));
            }

            const bool starve = (frames % 11U) == 0U;
            const aion::MaintenanceBudget budget{
                .frame_budget_ms = 16.666,
                .critical_path_ms = starve ? 16.0 : 13.5,
                .safety_margin_ms = 1.0,
                .max_maintenance_slice_ms = 0.35,
            };
            const auto run = aion::ExecutionBudgetScheduler::run_maintenance(budget, [&](double grant) {
                return builder.run_slice(snapshot, grant);
            });
            CHECK(run.ok);
            if (starve) { CHECK(!run.ran); ++zero_grant_frames; }
            ++frames;
        }
        CHECK(zero_grant_frames != 0);
        CHECK(builder.state() == aion::BudgetedSahState::Ready);

        aion::WideBvh8View promoted;
        const auto promotion = builder.try_promote(snapshot, promoted);
        CHECK(promotion.state == aion::BudgetedSahState::Promoted);
        CHECK(promoted.snapshot_version() == snapshot.version());
        CHECK(promotion.stats.slices != 0);
        CHECK(promotion.stats.primitive_visits != 0);

        for (int i = 0; i < 200; ++i)
            CHECK(promoted.query_aabb(snapshot, query_box(i)).entities == aion::SpatialOracle::query_aabb(snapshot, query_box(i)));
    }



    // Continuous dirty-refit queue: sustained sparse writes may outrun a tiny maintenance grant,
    // but backlog must remain explicit, drain after production stops, and preserve exact queries.
    {
        aion::SpatialSnapshot snapshot;
        CHECK(snapshot.build(records(20000), error));
        aion::BudgetedSahBuilder builder(8, 256);
        CHECK(builder.start(snapshot, error));
        while (builder.state() != aion::BudgetedSahState::Ready) {
            const auto progress = builder.run_slice(snapshot, 0.5);
            CHECK(progress.ok);
        }

        std::uint64_t txid = 2;
        std::size_t peak_backlog = 0;
        for (std::size_t frame = 0; frame < 40; ++frame) {
            const auto applied = snapshot.apply_patch_transaction(move_patch(snapshot, txid++, 2000, 0.075F));
            CHECK(applied.ok);
            CHECK(builder.observe_changes(applied.changes, error));
            const auto progress = builder.run_slice(snapshot, 0.05);
            CHECK(progress.ok);
            peak_backlog = std::max(peak_backlog, builder.backlog().total_items());
        }
        CHECK(peak_backlog != 0);

        std::size_t drain_slices = 0;
        while (builder.state() != aion::BudgetedSahState::Ready && drain_slices < 20000) {
            const auto progress = builder.run_slice(snapshot, 0.25);
            CHECK(progress.ok);
            ++drain_slices;
        }
        CHECK(builder.state() == aion::BudgetedSahState::Ready);
        CHECK(builder.backlog().total_items() == 0);
        CHECK(drain_slices != 0);

        aion::WideBvh8View promoted;
        const auto promotion = builder.try_promote(snapshot, promoted);
        CHECK(promotion.state == aion::BudgetedSahState::Promoted);
        CHECK(promotion.stats.observed_dirty_values == 80000);
        CHECK(promotion.stats.mapped_dirty_values == 80000);
        CHECK(promotion.stats.refit_leaf_children != 0);
        CHECK(promotion.stats.refit_internal_nodes != 0);
        for (int i = 0; i < 200; ++i)
            CHECK(promoted.query_aabb(snapshot, query_box(i)).entities == aion::SpatialOracle::query_aabb(snapshot, query_box(i)));
    }

        // A structural generation change invalidates a partially captured/built candidate immediately.
    {
        auto rec = records(10000);
        aion::SpatialSnapshot snapshot;
        CHECK(snapshot.build(rec, error));
        aion::BudgetedSahBuilder builder(8, 256);
        CHECK(builder.start(snapshot, error));
        CHECK(builder.run_slice(snapshot, 0.2).ok);
        std::swap(rec[0].entity, rec[1].entity);
        CHECK(snapshot.build(rec, error));
        const auto progress = builder.run_slice(snapshot, 0.2);
        CHECK(!progress.ok);
        CHECK(builder.state() == aion::BudgetedSahState::Discarded);
    }

    std::cout << "r32_budgeted_spatial_tests: PASS\n";
    return EXIT_SUCCESS;
}
