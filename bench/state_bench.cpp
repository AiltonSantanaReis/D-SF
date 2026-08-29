#include "aion/kernel/world.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    std::size_t count = 1'000'000;
    std::size_t frames = 120;
    if (argc > 1) count = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    if (argc > 2) frames = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));

    aion::World world(count);
    aion::Transaction tx{.id = 1};
    tx.mutations.reserve(count * 3);

    for (std::size_t i = 0; i < count; ++i) {
        const auto id = static_cast<aion::EntityId>(i + 1);
        tx.mutations.push_back({aion::MutationKind::CreateEntity, id, {}, 0});
        tx.mutations.push_back({aion::MutationKind::SetPosition, id, {static_cast<float>(i % 1000), 0.0F, 0.0F}, 0});
        tx.mutations.push_back({aion::MutationKind::SetVelocity, id, {0.1F, 0.2F, 0.3F}, 0});
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto result = world.commit(tx);
    const auto t1 = std::chrono::steady_clock::now();
    if (!result.committed) {
        std::cerr << "commit failed: " << result.error << '\n';
        return 2;
    }

    const auto sim0 = std::chrono::steady_clock::now();
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const aion::Transaction step{
            .id = static_cast<aion::TransactionId>(frame + 2),
            .mutations = {{aion::MutationKind::AdvanceReference, 0, {1.0F / 60.0F, 0.0F, 0.0F}, 0}}
        };
        if (!world.commit(step).committed) return 3;
    }
    const auto sim1 = std::chrono::steady_clock::now();

    const auto commit_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const auto sim_ms = std::chrono::duration<double, std::milli>(sim1 - sim0).count();

    std::cout << std::fixed << std::setprecision(3)
              << "entities=" << count << '\n'
              << "spawn_mutations=" << tx.mutations.size() << '\n'
              << "spawn_commit_ms=" << commit_ms << '\n'
              << "frames=" << frames << '\n'
              << "simulation_total_ms=" << sim_ms << '\n'
              << "simulation_ms_per_frame=" << (sim_ms / static_cast<double>(frames)) << '\n'
              << "entity_updates_per_second=" << (static_cast<double>(count) * static_cast<double>(frames) / (sim_ms / 1000.0)) << '\n'
              << "final_sha256=" << world.state_hash().hex() << '\n';
}
