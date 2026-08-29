#include "aion/kernel/journal.hpp"
#include "aion/kernel/world.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    std::size_t entities = 256;
    std::size_t frames = 5000;
    if (argc > 1) entities = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    if (argc > 2) frames = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));

    aion::World world(entities + 1);
    aion::ChangeJournal journal;

    aion::Transaction spawn{.id = 1};
    spawn.mutations.reserve(entities * 3);
    for (std::size_t i = 0; i < entities; ++i) {
        const auto id = static_cast<aion::EntityId>(i + 1);
        spawn.mutations.push_back({aion::MutationKind::CreateEntity, id, {}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetPosition, id, {static_cast<float>(i), 0.0F, 0.0F}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetVelocity, id, {0.1F, 0.2F, 0.3F}, 0});
    }
    if (!journal.commit(world, spawn).committed) return 2;

    const auto run0 = std::chrono::steady_clock::now();
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const aion::Transaction step{
            .id = static_cast<aion::TransactionId>(frame + 2),
            .mutations = {{aion::MutationKind::AdvanceReference, 0, {1.0F / 60.0F, 0.0F, 0.0F}, 0}}
        };
        if (!journal.commit(world, step).committed) return 3;
    }
    const auto run1 = std::chrono::steady_clock::now();
    const auto original_hash = world.state_hash();

    const auto path = std::filesystem::temp_directory_path() / "aion_r1_benchmark.jnl";
    const auto save0 = std::chrono::steady_clock::now();
    if (!journal.save_transactions(path).ok) return 4;
    const auto save1 = std::chrono::steady_clock::now();
    const auto file_bytes = std::filesystem::file_size(path);

    std::vector<aion::Transaction> loaded;
    const auto load0 = std::chrono::steady_clock::now();
    if (!aion::ChangeJournal::load_transactions(path, loaded).ok) return 5;
    const auto load1 = std::chrono::steady_clock::now();

    aion::World replay(entities + 1);
    const auto replay0 = std::chrono::steady_clock::now();
    const auto replay_result = aion::ChangeJournal::replay(replay, loaded);
    const auto replay1 = std::chrono::steady_clock::now();
    if (!replay_result.ok || replay.state_hash() != original_hash) return 6;

    const auto rollback0 = std::chrono::steady_clock::now();
    if (!journal.rollback_to(world, 0)) return 7;
    const auto rollback1 = std::chrono::steady_clock::now();

    const auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    std::cout << std::fixed << std::setprecision(3)
              << "entities=" << entities << '\n'
              << "frames=" << frames << '\n'
              << "transactions=" << loaded.size() << '\n'
              << "journal_commit_total_ms=" << ms(run0, run1) << '\n'
              << "journal_commit_ms_per_frame=" << (ms(run0, run1) / static_cast<double>(frames)) << '\n'
              << "save_ms=" << ms(save0, save1) << '\n'
              << "load_ms=" << ms(load0, load1) << '\n'
              << "replay_ms=" << ms(replay0, replay1) << '\n'
              << "rollback_all_ms=" << ms(rollback0, rollback1) << '\n'
              << "journal_file_bytes=" << file_bytes << '\n'
              << "final_sha256=" << original_hash.hex() << '\n';

    std::error_code ec;
    std::filesystem::remove(path, ec);
    return 0;
}
