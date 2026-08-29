#include "aion/kernel/journal.hpp"
#include "aion/kernel/world.hpp"

#include <iostream>

int main() {
    aion::World world(1024);
    aion::ChangeJournal journal;

    aion::Transaction spawn{
        .id = 1,
        .mutations = {
            {aion::MutationKind::CreateEntity, 1, {}, 0},
            {aion::MutationKind::SetPosition, 1, {0.0F, 0.0F, 0.0F}, 0},
            {aion::MutationKind::SetVelocity, 1, {1.0F, 0.0F, 0.0F}, 0},
        }
    };

    if (const auto result = journal.commit(world, spawn); !result.committed) {
        std::cerr << "Commit failed: " << result.error << '\n';
        return 1;
    }

    const aion::Transaction step{
        .id = 2,
        .mutations = {{aion::MutationKind::AdvanceReference, 0, {1.0F / 60.0F, 0.0F, 0.0F}, 0}}
    };
    if (!journal.commit(world, step).committed) return 2;

    const auto p = world.position(1);
    std::cout << "Aion LAB R1\n"
              << "entities=" << world.living_entities() << '\n'
              << "player.x=" << (p ? p->x : -1.0F) << '\n'
              << "journal_entries=" << journal.size() << '\n'
              << "state_sha256=" << world.state_hash().hex() << '\n';
}
