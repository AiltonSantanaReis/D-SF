#include "aion/kernel/patch.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)

namespace {

constexpr float kDt = 1.0F / 60.0F;
constexpr std::size_t kPage = 256;

[[nodiscard]] aion::World make_world(std::size_t entities) {
    aion::World world(entities + 1);
    aion::Transaction spawn{.id = 1};
    spawn.mutations.reserve(entities * 4);
    for (std::size_t i = 0; i < entities; ++i) {
        const auto id = static_cast<aion::EntityId>(i + 1);
        spawn.mutations.push_back({aion::MutationKind::CreateEntity, id, {}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetPosition, id,
            {static_cast<float>(i) * 0.25F, static_cast<float>(i % 17) * 0.5F, -static_cast<float>(i % 31)}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetVelocity, id,
            {0.15F + static_cast<float>(i % 5) * 0.01F, 0.02F, -0.03F}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetHealth, id, {}, static_cast<std::uint32_t>(100 - (i % 37))});
    }
    if (!world.commit(spawn).committed) std::abort();
    return world;
}

[[nodiscard]] aion::Vec3 next_position(aion::Vec3 p, aion::Vec3 v) noexcept {
    return {p.x + v.x * kDt, p.y + v.y * kDt, p.z + v.z * kDt};
}

[[nodiscard]] aion::Vec3 next_velocity(aion::Vec3 v) noexcept {
    return {v.x * 0.99975F, v.y * 0.99975F, v.z * 0.99975F};
}

[[nodiscard]] std::uint32_t next_health(std::uint32_t h) noexcept { return h > 0 ? h - 1U : 0U; }

[[nodiscard]] aion::Transaction build_oracle(const aion::World& world, aion::TransactionId id) {
    aion::Transaction tx{.id = id};
    tx.mutations.reserve((world.entity_capacity() - 1) * 3);
    for (std::size_t i = 1; i < world.entity_capacity(); ++i) {
        const auto entity = static_cast<aion::EntityId>(i);
        if (!world.alive(entity)) continue;
        const auto p = *world.position(entity);
        const auto v = *world.velocity(entity);
        const auto h = *world.health(entity);
        tx.mutations.push_back({aion::MutationKind::SetPosition, entity, next_position(p, v), 0});
        tx.mutations.push_back({aion::MutationKind::SetVelocity, entity, next_velocity(v), 0});
        tx.mutations.push_back({aion::MutationKind::SetHealth, entity, {}, next_health(h)});
    }
    return tx;
}

[[nodiscard]] aion::PatchTransaction build_ranges(const aion::World& world, aion::TransactionId id) {
    const auto count = world.entity_capacity() - 1;
    aion::PatchTransaction tx{.id = id};
    aion::Vec3RangePatch positions{.component = aion::PatchComponent::Position, .first = 1};
    aion::Vec3RangePatch velocities{.component = aion::PatchComponent::Velocity, .first = 1};
    aion::U32RangePatch health{.component = aion::PatchComponent::Health, .first = 1};
    positions.values.reserve(count); velocities.values.reserve(count); health.values.reserve(count);
    for (std::size_t i = 1; i < world.entity_capacity(); ++i) {
        const auto entity = static_cast<aion::EntityId>(i);
        const auto p = *world.position(entity); const auto v = *world.velocity(entity); const auto h = *world.health(entity);
        positions.values.push_back(next_position(p, v));
        velocities.values.push_back(next_velocity(v));
        health.values.push_back(next_health(h));
    }
    tx.vec3_patches.push_back(std::move(positions));
    tx.vec3_patches.push_back(std::move(velocities));
    tx.u32_patches.push_back(std::move(health));
    return tx;
}

[[nodiscard]] aion::PatchTransaction build_pages(const aion::World& world, aion::TransactionId id, std::size_t page_size, bool cow_style) {
    aion::PatchTransaction tx{.id = id};
    const auto capacity = world.entity_capacity();
    for (aion::PatchComponent component : {aion::PatchComponent::Position, aion::PatchComponent::Velocity}) {
        for (std::size_t begin = 1; begin < capacity; begin += page_size) {
            const auto count = std::min(page_size, capacity - begin);
            aion::Vec3RangePatch patch{.component = component, .first = static_cast<aion::EntityId>(begin)};
            patch.values.resize(count);
            if (cow_style) {
                for (std::size_t j = 0; j < count; ++j) {
                    const auto entity = static_cast<aion::EntityId>(begin + j);
                    patch.values[j] = component == aion::PatchComponent::Position ? *world.position(entity) : *world.velocity(entity);
                }
                for (std::size_t j = 0; j < count; ++j) {
                    const auto entity = static_cast<aion::EntityId>(begin + j);
                    if (component == aion::PatchComponent::Position) patch.values[j] = next_position(patch.values[j], *world.velocity(entity));
                    else patch.values[j] = next_velocity(patch.values[j]);
                }
            } else {
                for (std::size_t j = 0; j < count; ++j) {
                    const auto entity = static_cast<aion::EntityId>(begin + j);
                    if (component == aion::PatchComponent::Position) patch.values[j] = next_position(*world.position(entity), *world.velocity(entity));
                    else patch.values[j] = next_velocity(*world.velocity(entity));
                }
            }
            tx.vec3_patches.push_back(std::move(patch));
        }
    }
    for (std::size_t begin = 1; begin < capacity; begin += page_size) {
        const auto count = std::min(page_size, capacity - begin);
        aion::U32RangePatch patch{.component = aion::PatchComponent::Health, .first = static_cast<aion::EntityId>(begin)};
        patch.values.resize(count);
        for (std::size_t j = 0; j < count; ++j) {
            patch.values[j] = next_health(*world.health(static_cast<aion::EntityId>(begin + j)));
        }
        tx.u32_patches.push_back(std::move(patch));
    }
    return tx;
}

} // namespace

int main() {
    constexpr std::size_t entities = 4096;
    constexpr std::size_t frames = 120;

    auto oracle = make_world(entities);
    auto ranges = make_world(entities);
    auto pages = make_world(entities);
    auto cow = make_world(entities);
    auto parallel = make_world(entities);

    aion::PatchJournal range_journal;
    aion::PatchJournal page_journal;
    aion::PatchJournal cow_journal;
    aion::PatchJournal parallel_journal;

    aion::StateHash checkpoint{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto id = static_cast<aion::TransactionId>(frame + 2);
        CHECK(oracle.commit(build_oracle(oracle, id)).committed);
        CHECK(range_journal.commit(ranges, build_ranges(ranges, id)).committed);
        CHECK(page_journal.commit(pages, build_pages(pages, id, kPage, false)).committed);
        CHECK(cow_journal.commit(cow, build_pages(cow, id, kPage, true)).committed);
        CHECK(parallel_journal.commit(parallel, build_pages(parallel, id, kPage, false), aion::PatchApplyMode::ParallelDisjoint, 4).committed);
        CHECK(oracle.state_hash() == ranges.state_hash());
        CHECK(oracle.state_hash() == pages.state_hash());
        CHECK(oracle.state_hash() == cow.state_hash());
        CHECK(oracle.state_hash() == parallel.state_hash());
        if (frame == 59) checkpoint = ranges.state_hash();
    }

    const auto final_hash = oracle.state_hash();


    // Persistent parallel publisher also converges with the oracle.
    auto persistent_world = make_world(entities);
    aion::PatchCommitRuntime persistent_runtime(4);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto id = static_cast<aion::TransactionId>(frame + 2);
        CHECK(persistent_runtime.commit(persistent_world, build_pages(persistent_world, id, kPage, false)).committed);
    }
    CHECK(persistent_world.state_hash() == final_hash);

    // Replay from an independently reconstructed base World.
    auto replay_world = make_world(entities);
    const auto all = range_journal.transactions();
    aion::PatchJournal replay_capture;
    const auto replay = aion::PatchJournal::replay(replay_world, all, &replay_capture);
    CHECK(replay.ok);
    CHECK(replay.transactions_applied == frames);
    CHECK(replay_world.state_hash() == final_hash);

    // Roll back to frame 60 and reproduce the tail.
    const auto checkpoint_tx = static_cast<aion::TransactionId>(61);
    CHECK(range_journal.rollback_to(ranges, checkpoint_tx));
    CHECK(ranges.state_hash() == checkpoint);
    const auto tail = std::span<const aion::PatchTransaction>(all).subspan(60);
    const auto tail_replay = aion::PatchJournal::replay(ranges, tail, &range_journal);
    CHECK(tail_replay.ok);
    CHECK(ranges.state_hash() == final_hash);

    // Persistence round-trip.
    const auto journal_path = std::filesystem::temp_directory_path() / "aion_r21_patch_test.bin";
    CHECK(page_journal.save_transactions(journal_path).ok);
    std::vector<aion::PatchTransaction> loaded;
    CHECK(aion::PatchJournal::load_transactions(journal_path, loaded).ok);
    auto disk_replay = make_world(entities);
    CHECK(aion::PatchJournal::replay(disk_replay, loaded).ok);
    CHECK(disk_replay.state_hash() == final_hash);
    std::filesystem::remove(journal_path);


    // Hybrid scalar + range transaction: sparse/structural lane and dense lane commit atomically.
    auto hybrid_world = make_world(64);
    auto hybrid_replay_base = make_world(64);
    aion::PatchTransaction hybrid{.id = 2};
    hybrid.scalar_mutations.push_back({aion::MutationKind::SetHealth, 1, {}, 77});
    hybrid.scalar_mutations.push_back({aion::MutationKind::DestroyEntity, 64, {}, 0});
    aion::Vec3RangePatch hybrid_positions{.component = aion::PatchComponent::Position, .first = 2};
    hybrid_positions.values.reserve(31);
    for (aion::EntityId id = 2; id <= 32; ++id) {
        auto p = *hybrid_world.position(id); p.x += 5.0F; hybrid_positions.values.push_back(p);
    }
    hybrid.vec3_patches.push_back(std::move(hybrid_positions));
    aion::PatchJournal hybrid_journal;
    CHECK(hybrid_journal.commit(hybrid_world, hybrid).committed);
    CHECK(*hybrid_world.health(1) == 77U);
    CHECK(!hybrid_world.alive(64));
    const auto hybrid_hash = hybrid_world.state_hash();
    const auto hybrid_path = std::filesystem::temp_directory_path() / "aion_r21_hybrid_test.bin";
    CHECK(hybrid_journal.save_transactions(hybrid_path).ok);
    std::vector<aion::PatchTransaction> hybrid_loaded;
    CHECK(aion::PatchJournal::load_transactions(hybrid_path, hybrid_loaded).ok);
    CHECK(aion::PatchJournal::replay(hybrid_replay_base, hybrid_loaded).ok);
    CHECK(hybrid_replay_base.state_hash() == hybrid_hash);
    CHECK(hybrid_journal.rollback_last(hybrid_world));
    CHECK(hybrid_world.state_hash() == make_world(64).state_hash());
    std::filesystem::remove(hybrid_path);

    // Same-component scalar/range overlap is rejected before any authoritative change.
    auto hybrid_invalid = make_world(64);
    const auto hybrid_invalid_before = hybrid_invalid.state_hash();
    aion::PatchTransaction conflicting{.id = 2};
    conflicting.scalar_mutations.push_back({aion::MutationKind::SetPosition, 5, {1.0F, 2.0F, 3.0F}, 0});
    conflicting.vec3_patches.push_back({aion::PatchComponent::Position, 4, std::vector<aion::Vec3>(4)});
    CHECK(!aion::commit_patch_transaction(hybrid_invalid, conflicting).committed);
    CHECK(hybrid_invalid.state_hash() == hybrid_invalid_before);

    // Overlap is rejected atomically.
    auto invalid_world = make_world(32);
    const auto before_invalid = invalid_world.state_hash();
    aion::PatchTransaction overlap{.id = 2};
    overlap.vec3_patches.push_back({aion::PatchComponent::Position, 1, std::vector<aion::Vec3>(8)});
    overlap.vec3_patches.push_back({aion::PatchComponent::Position, 4, std::vector<aion::Vec3>(8)});
    CHECK(!aion::commit_patch_transaction(invalid_world, overlap).committed);
    CHECK(invalid_world.state_hash() == before_invalid);

    // Non-finite payload is rejected atomically.
    aion::PatchTransaction nonfinite{.id = 2};
    nonfinite.vec3_patches.push_back({aion::PatchComponent::Position, 1, {{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}}});
    CHECK(!aion::commit_patch_transaction(invalid_world, nonfinite).committed);
    CHECK(invalid_world.state_hash() == before_invalid);

    std::cout << "r21_patch_tests: PASS\n"
              << "entities=" << entities << '\n'
              << "frames=" << frames << '\n'
              << "page_size=" << kPage << '\n'
              << "final_sha256=" << final_hash.hex() << '\n';
    return EXIT_SUCCESS;
}
