#include "aion/kernel/journal.hpp"
#include "aion/kernel/world.hpp"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at line " << __LINE__ << '\n'; return EXIT_FAILURE; } } while(false)

namespace {

aion::Mutation create(aion::EntityId id) {
    return {aion::MutationKind::CreateEntity, id, {}, 0};
}

aion::Mutation position(aion::EntityId id, aion::Vec3 v) {
    return {aion::MutationKind::SetPosition, id, v, 0};
}

aion::Mutation velocity(aion::EntityId id, aion::Vec3 v) {
    return {aion::MutationKind::SetVelocity, id, v, 0};
}

aion::Mutation health(aion::EntityId id, std::uint32_t hp) {
    return {aion::MutationKind::SetHealth, id, {}, hp};
}

aion::Mutation advance(float dt) {
    return {aion::MutationKind::AdvanceReference, 0, {dt, 0.0F, 0.0F}, 0};
}

} // namespace

int main() {
    // R0: identity allocation is itself transaction-derived. No hidden reserve side effect.
    {
        aion::World world;
        CHECK(world.next_entity_id() == 1);

        const aion::Transaction spawn{
            .id = 1,
            .mutations = {
                create(1),
                position(1, {1.0F, 2.0F, 3.0F}),
                velocity(1, {2.0F, 0.0F, 0.0F}),
                health(1, 75),
            }
        };

        CHECK(world.commit(spawn).committed);
        CHECK(world.next_entity_id() == 2);
        CHECK(world.alive(1));
        CHECK(world.living_entities() == 1);
        CHECK(world.health(1).value() == 75);

        const aion::Transaction step{.id = 2, .mutations = {advance(0.5F)}};
        CHECK(world.commit(step).committed);
        CHECK(std::fabs(world.position(1)->x - 2.0F) < 0.0001F);

        // Atomicity: a later invalid mutation rejects the whole transaction.
        const auto before_hash = world.state_hash();
        const aion::Transaction invalid_atomic{
            .id = 3,
            .mutations = {
                position(1, {99.0F, 99.0F, 99.0F}),
                health(9999, 1),
            }
        };
        CHECK(!world.commit(invalid_atomic).committed);
        CHECK(world.state_hash() == before_hash);

        // IDs cannot be skipped or reused: identity is stable and dense in R1.
        const aion::Transaction skipped_id{.id = 3, .mutations = {create(3)}};
        CHECK(!world.commit(skipped_id).committed);
        CHECK(world.next_entity_id() == 2);

        // Non-finite authoritative values are rejected before mutation.
        const float nan = std::bit_cast<float>(0x7fc00000U);
        const aion::Transaction invalid_float{.id = 3, .mutations = {position(1, {nan, 0.0F, 0.0F})}};
        CHECK(!world.commit(invalid_float).committed);
        CHECK(world.state_hash() == before_hash);
    }

    // Canonical SHA-256 vector cross-checked against Python hashlib for a create-only world.
    {
        aion::World hash_world;
        CHECK(hash_world.commit(aion::Transaction{.id = 1, .mutations = {create(1)}}).committed);
        CHECK(hash_world.state_hash().hex() ==
              "9052d221ad22d52fb0a43dbec4410a9546d7bbb968642614ee0deb55758e7c33");
    }

    // R1: journal, deterministic replay, serialization and exact rollback.
    constexpr std::size_t entity_count = 256;
    constexpr std::size_t frames = 5000;
    constexpr float dt = 1.0F / 60.0F;

    aion::World original(entity_count + 16);
    aion::ChangeJournal journal;

    aion::Transaction spawn{.id = 1};
    spawn.mutations.reserve(entity_count * 4);
    for (std::size_t i = 0; i < entity_count; ++i) {
        const auto id = static_cast<aion::EntityId>(i + 1);
        spawn.mutations.push_back(create(id));
        spawn.mutations.push_back(position(id, {static_cast<float>(i), static_cast<float>(i % 7), 0.0F}));
        spawn.mutations.push_back(velocity(id, {0.01F * static_cast<float>((i % 5) + 1), 0.02F, -0.005F}));
        spawn.mutations.push_back(health(id, static_cast<std::uint32_t>(50 + (i % 51))));
    }
    CHECK(journal.commit(original, spawn).committed);

    aion::StateHash checkpoint_hash{};
    aion::TransactionId checkpoint_tx = 0;

    for (std::size_t frame = 0; frame < frames; ++frame) {
        aion::Transaction tx{.id = static_cast<aion::TransactionId>(frame + 2)};
        tx.mutations.push_back(advance(dt));

        if (frame % 97 == 0) {
            const auto id = static_cast<aion::EntityId>((frame % entity_count) + 1);
            tx.mutations.push_back(health(id, static_cast<std::uint32_t>(60 + (frame % 40))));
        }
        if (frame == 999) {
            tx.mutations.push_back({aion::MutationKind::DestroyEntity, 5, {}, 0});
        }
        if (frame == 1000) {
            const auto id = original.next_entity_id();
            tx.mutations.push_back(create(id));
            tx.mutations.push_back(position(id, {100.0F, 200.0F, 300.0F}));
            tx.mutations.push_back(velocity(id, {-0.2F, 0.1F, 0.0F}));
        }

        CHECK(journal.commit(original, tx).committed);

        if (frame == 2499) {
            checkpoint_tx = tx.id;
            checkpoint_hash = original.state_hash();
        }
    }

    CHECK(journal.size() == frames + 1);
    const auto final_hash = original.state_hash();
    const auto all_transactions = journal.transactions();
    CHECK(all_transactions.size() == frames + 1);

    // Replay from a pristine World using only forward transactions.
    aion::World replayed(entity_count + 16);
    const auto replay_result = aion::ChangeJournal::replay(replayed, all_transactions);
    CHECK(replay_result.ok);
    CHECK(replay_result.transactions_applied == all_transactions.size());
    CHECK(replayed.state_hash() == final_hash);

    // Persist ONLY forward transactions, reload, and reproduce the same final state.
    const auto journal_path = std::filesystem::temp_directory_path() / "aion_r1_deterministic.jnl";
    CHECK(journal.save_transactions(journal_path).ok);

    std::vector<aion::Transaction> loaded;
    CHECK(aion::ChangeJournal::load_transactions(journal_path, loaded).ok);
    CHECK(loaded.size() == all_transactions.size());

    aion::World disk_replay(entity_count + 16);
    const auto disk_replay_result = aion::ChangeJournal::replay(disk_replay, loaded);
    CHECK(disk_replay_result.ok);
    CHECK(disk_replay.state_hash() == final_hash);

    // Roll back thousands of transactions to an exact historical hash.
    CHECK(checkpoint_tx != 0);
    CHECK(journal.rollback_to(original, checkpoint_tx));
    CHECK(original.last_transaction_id() == checkpoint_tx);
    CHECK(original.state_hash() == checkpoint_hash);

    // Re-apply the removed tail and arrive at the exact same final hash again.
    std::vector<aion::Transaction> tail;
    for (const auto& tx : all_transactions) {
        if (tx.id > checkpoint_tx) tail.push_back(tx);
    }
    const auto tail_replay = aion::ChangeJournal::replay(original, tail);
    CHECK(tail_replay.ok);
    CHECK(original.state_hash() == final_hash);

    // Full rollback restores the pristine world exactly when all commits are journal-owned.
    aion::World rollback_world;
    aion::ChangeJournal rollback_journal;
    const auto pristine_hash = rollback_world.state_hash();
    CHECK(rollback_journal.commit(rollback_world, all_transactions.front()).committed);
    CHECK(rollback_journal.rollback_to(rollback_world, 0));
    CHECK(rollback_world.state_hash() == pristine_hash);
    CHECK(rollback_world.next_entity_id() == 1);

    // Safety: an undo is rejected if the world diverged outside its journal.
    aion::World diverged;
    aion::ChangeJournal diverged_journal;
    CHECK(diverged_journal.commit(diverged, aion::Transaction{.id = 1, .mutations = {create(1)}}).committed);
    CHECK(diverged.commit(aion::Transaction{.id = 2, .mutations = {health(1, 12)}}).committed);
    CHECK(!diverged_journal.rollback_last(diverged));

    std::error_code ec;
    std::filesystem::remove(journal_path, ec);

    std::cout << "kernel_tests: PASS\n"
              << "transactions=" << all_transactions.size() << '\n'
              << "frames=" << frames << '\n'
              << "final_sha256=" << final_hash.hex() << '\n';
    return EXIT_SUCCESS;
}
