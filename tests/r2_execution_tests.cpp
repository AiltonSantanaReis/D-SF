#include "aion/kernel/execution.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)

namespace {

using aion::AccessMask;
using aion::ResourceKey;
using aion::SystemContext;
using aion::SystemSpec;

[[nodiscard]] aion::World make_world(std::size_t entities) {
    aion::World world(entities + 1);
    aion::Transaction spawn{.id = 1};
    spawn.mutations.reserve(entities * 4);
    for (std::size_t i = 0; i < entities; ++i) {
        const auto id = static_cast<aion::EntityId>(i + 1);
        spawn.mutations.push_back({aion::MutationKind::CreateEntity, id, {}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetPosition, id,
            {static_cast<float>(i % 97), static_cast<float>((i * 3) % 71), static_cast<float>(i % 11)}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetVelocity, id,
            {0.01F * static_cast<float>((i % 7) + 1), -0.02F, 0.005F * static_cast<float>(i % 5)}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetHealth, id, {}, static_cast<std::uint32_t>(50 + (i % 51))});
    }
    const auto result = world.commit(spawn);
    if (!result.committed) std::abort();
    return world;
}

[[nodiscard]] std::vector<SystemSpec> world_systems(float dt) {
    const auto iteration_reads = AccessMask::of({ResourceKey::Identity, ResourceKey::EntityState});

    SystemSpec integrate{
        .id = 10,
        .name = "IntegratePosition",
        .reads = iteration_reads | AccessMask::of({ResourceKey::Position, ResourceKey::Velocity}),
        .writes = AccessMask::of({ResourceKey::Position}),
        .run = [dt](SystemContext& ctx) {
            const auto capacity = ctx.entity_capacity();
            for (std::size_t i = 1; i < capacity; ++i) {
                const auto id = static_cast<aion::EntityId>(i);
                if (!ctx.alive(id)) continue;
                const auto p = ctx.position(id);
                const auto v = ctx.velocity(id);
                if (!p || !v) continue;
                ctx.set_position(id, {
                    p->x + v->x * dt,
                    p->y + v->y * dt,
                    p->z + v->z * dt,
                });
            }
        }
    };

    SystemSpec health_tick{
        .id = 20,
        .name = "HealthTick",
        .reads = iteration_reads | AccessMask::of({ResourceKey::Health}),
        .writes = AccessMask::of({ResourceKey::Health}),
        .run = [](SystemContext& ctx) {
            const auto capacity = ctx.entity_capacity();
            for (std::size_t i = 1; i < capacity; ++i) {
                const auto id = static_cast<aion::EntityId>(i);
                if (!ctx.alive(id)) continue;
                const auto hp = ctx.health(id);
                if (!hp) continue;
                const auto next = (*hp > 1U) ? (*hp - 1U) : *hp;
                ctx.set_health(id, next);
            }
        }
    };

    SystemSpec damp_velocity{
        .id = 30,
        .name = "DampVelocity",
        .reads = iteration_reads | AccessMask::of({ResourceKey::Velocity}),
        .writes = AccessMask::of({ResourceKey::Velocity}),
        .run = [](SystemContext& ctx) {
            const auto capacity = ctx.entity_capacity();
            for (std::size_t i = 1; i < capacity; ++i) {
                const auto id = static_cast<aion::EntityId>(i);
                if (!ctx.alive(id)) continue;
                const auto v = ctx.velocity(id);
                if (!v) continue;
                ctx.set_velocity(id, {v->x * 0.999F, v->y * 0.999F, v->z * 0.999F});
            }
        }
    };

    SystemSpec clamp_position{
        .id = 40,
        .name = "ClampPosition",
        .reads = iteration_reads | AccessMask::of({ResourceKey::Position}),
        .writes = AccessMask::of({ResourceKey::Position}),
        .run = [](SystemContext& ctx) {
            const auto capacity = ctx.entity_capacity();
            for (std::size_t i = 1; i < capacity; ++i) {
                const auto id = static_cast<aion::EntityId>(i);
                if (!ctx.alive(id)) continue;
                const auto p = ctx.position(id);
                if (!p) continue;
                auto clamped = *p;
                if (clamped.x > 1000.0F) clamped.x = 1000.0F;
                if (clamped.x < -1000.0F) clamped.x = -1000.0F;
                ctx.set_position(id, clamped);
            }
        }
    };

    return {std::move(integrate), std::move(health_tick), std::move(damp_velocity), std::move(clamp_position)};
}

} // namespace

int main() {
    // Read/read systems are allowed in the same wave.
    {
        aion::ExecutionPlan plan;
        std::vector<SystemSpec> systems;
        for (aion::SystemId id : {10U, 20U}) {
            systems.push_back({
                .id = id,
                .name = "reader",
                .reads = AccessMask::of({ResourceKey::Position}),
                .writes = {},
                .run = [](SystemContext& ctx) { ctx.set_auxiliary_result(7); }
            });
        }
        const auto built = aion::ExecutionPlan::build(std::move(systems), plan);
        CHECK(built.ok);
        CHECK(plan.waves().size() == 1);
        CHECK(plan.waves().front().system_indices.size() == 2);
        CHECK(plan.edge_count() == 0);
    }

    // Read/write and write/write hazards serialize by canonical SystemId order.
    {
        aion::ExecutionPlan plan;
        std::vector<SystemSpec> systems{
            {.id = 30, .name = "write", .reads = {}, .writes = AccessMask::of({ResourceKey::Position}), .run = [](SystemContext&) {}},
            {.id = 10, .name = "read", .reads = AccessMask::of({ResourceKey::Position}), .writes = {}, .run = [](SystemContext&) {}},
            {.id = 20, .name = "independent", .reads = AccessMask::of({ResourceKey::Health}), .writes = {}, .run = [](SystemContext&) {}},
        };
        const auto built = aion::ExecutionPlan::build(std::move(systems), plan);
        CHECK(built.ok);
        CHECK(plan.edge_count() == 1);
        CHECK(plan.waves().size() == 2);
        CHECK(plan.waves()[0].system_indices.size() == 2);
        CHECK(plan.waves()[1].system_indices.size() == 1);
    }

    // Explicit dependency cycles must be rejected.
    {
        aion::ExecutionPlan plan;
        std::vector<SystemSpec> systems{
            {.id = 10, .name = "A", .after = {20}, .run = [](SystemContext&) {}},
            {.id = 20, .name = "B", .after = {10}, .run = [](SystemContext&) {}},
        };
        const auto built = aion::ExecutionPlan::build(std::move(systems), plan);
        CHECK(!built.ok);
    }

    // Access declarations are enforceable: undeclared writes fail before World commit.
    {
        auto world = make_world(1);
        const auto before = world.state_hash();
        aion::ExecutionPlan plan;
        std::vector<SystemSpec> systems{
            {
                .id = 10,
                .name = "illegal-writer",
                .reads = {},
                .writes = AccessMask::of({ResourceKey::Health}),
                .run = [](SystemContext& ctx) { ctx.set_position(1, {1.0F, 2.0F, 3.0F}); }
            }
        };
        CHECK(aion::ExecutionPlan::build(std::move(systems), plan).ok);
        const auto run = aion::ExecutionKernel::execute(plan, world);
        CHECK(!run.ok);
        CHECK(run.failed_system == 10);
        CHECK(world.state_hash() == before);
    }

    // R2 core proof: serial oracle and worker-pool executor converge bit-exactly.
    constexpr std::size_t entities = 4096;
    constexpr std::size_t frames = 120;
    constexpr float dt = 1.0F / 60.0F;

    aion::ExecutionPlan plan;
    CHECK(aion::ExecutionPlan::build(world_systems(dt), plan).ok);
    CHECK(plan.waves().size() == 2);

    auto serial_world = make_world(entities);
    auto parallel_world = make_world(entities);

    std::size_t serial_transactions = 0;
    std::size_t parallel_transactions = 0;
    aion::ExecutionRuntime serial_runtime({.kind = aion::ExecutorKind::SerialReference, .workers = 1});
    aion::ExecutionRuntime parallel_runtime({.kind = aion::ExecutorKind::WorkerPool, .workers = 4});
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto serial = serial_runtime.execute(plan, serial_world);
        CHECK(serial.ok);
        serial_transactions += serial.transactions_committed;

        const auto parallel = parallel_runtime.execute(plan, parallel_world);
        CHECK(parallel.ok);
        parallel_transactions += parallel.transactions_committed;
    }

    CHECK(serial_transactions == parallel_transactions);
    CHECK(serial_world.last_transaction_id() == parallel_world.last_transaction_id());
    CHECK(serial_world.state_hash() == parallel_world.state_hash());

    std::cout << "r2_execution_tests: PASS\n"
              << "systems=" << plan.systems().size() << '\n'
              << "waves=" << plan.waves().size() << '\n'
              << "frames=" << frames << '\n'
              << "entities=" << entities << '\n'
              << "final_sha256=" << serial_world.state_hash().hex() << '\n';
    return EXIT_SUCCESS;
}
