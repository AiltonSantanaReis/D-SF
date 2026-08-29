#include "aion/kernel/execution.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using aion::AccessMask;
using aion::ResourceKey;
using aion::SystemContext;
using aion::SystemSpec;

[[nodiscard]] double ms(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

[[nodiscard]] aion::World make_world(std::size_t entities) {
    aion::World world(entities + 1);
    aion::Transaction spawn{.id = 1};
    spawn.mutations.reserve(entities * 4);
    for (std::size_t i = 0; i < entities; ++i) {
        const auto id = static_cast<aion::EntityId>(i + 1);
        spawn.mutations.push_back({aion::MutationKind::CreateEntity, id, {}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetPosition, id, {static_cast<float>(i), 0.0F, 0.0F}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetVelocity, id, {0.1F, 0.02F, -0.03F}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetHealth, id, {}, static_cast<std::uint32_t>(100 - (i % 25))});
    }
    if (!world.commit(spawn).committed) std::abort();
    return world;
}

[[nodiscard]] std::vector<SystemSpec> correctness_systems(float dt) {
    const auto entity_reads = AccessMask::of({ResourceKey::Identity, ResourceKey::EntityState});
    return {
        {
            .id = 10, .name = "Integrate",
            .reads = entity_reads | AccessMask::of({ResourceKey::Position, ResourceKey::Velocity}),
            .writes = AccessMask::of({ResourceKey::Position}),
            .run = [dt](SystemContext& ctx) {
                const auto n = ctx.entity_capacity();
                for (std::size_t i = 1; i < n; ++i) {
                    const auto id = static_cast<aion::EntityId>(i);
                    if (!ctx.alive(id)) continue;
                    const auto p = ctx.position(id); const auto v = ctx.velocity(id);
                    if (p && v) ctx.set_position(id, {p->x + v->x * dt, p->y + v->y * dt, p->z + v->z * dt});
                }
            }
        },
        {
            .id = 20, .name = "Health",
            .reads = entity_reads | AccessMask::of({ResourceKey::Health}),
            .writes = AccessMask::of({ResourceKey::Health}),
            .run = [](SystemContext& ctx) {
                const auto n = ctx.entity_capacity();
                for (std::size_t i = 1; i < n; ++i) {
                    const auto id = static_cast<aion::EntityId>(i);
                    if (!ctx.alive(id)) continue;
                    const auto hp = ctx.health(id);
                    if (hp) ctx.set_health(id, *hp > 0 ? *hp - 1U : 0U);
                }
            }
        },
        {
            .id = 30, .name = "Velocity",
            .reads = entity_reads | AccessMask::of({ResourceKey::Velocity}),
            .writes = AccessMask::of({ResourceKey::Velocity}),
            .run = [](SystemContext& ctx) {
                const auto n = ctx.entity_capacity();
                for (std::size_t i = 1; i < n; ++i) {
                    const auto id = static_cast<aion::EntityId>(i);
                    if (!ctx.alive(id)) continue;
                    const auto v = ctx.velocity(id);
                    if (v) ctx.set_velocity(id, {v->x * 0.9995F, v->y * 0.9995F, v->z * 0.9995F});
                }
            }
        },
        {
            .id = 40, .name = "PositionClamp",
            .reads = entity_reads | AccessMask::of({ResourceKey::Position}),
            .writes = AccessMask::of({ResourceKey::Position}),
            .run = [](SystemContext& ctx) {
                const auto n = ctx.entity_capacity();
                for (std::size_t i = 1; i < n; ++i) {
                    const auto id = static_cast<aion::EntityId>(i);
                    if (!ctx.alive(id)) continue;
                    auto p = ctx.position(id);
                    if (!p) continue;
                    if (p->x > 100000.0F) p->x = 100000.0F;
                    ctx.set_position(id, *p);
                }
            }
        }
    };
}

[[nodiscard]] aion::StateHash correctness_run(aion::ExecutorKind kind, std::size_t workers, double& elapsed_ms) {
    constexpr std::size_t entities = 8192;
    constexpr std::size_t frames = 60;
    auto world = make_world(entities);
    aion::ExecutionPlan plan;
    const auto built = aion::ExecutionPlan::build(correctness_systems(1.0F / 60.0F), plan);
    if (!built.ok) std::abort();

    aion::ExecutionRuntime runtime({.kind = kind, .workers = workers});
    const auto start = Clock::now();
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto result = runtime.execute(plan, world);
        if (!result.ok) std::abort();
    }
    elapsed_ms = ms(Clock::now() - start);
    return world.state_hash();
}

[[nodiscard]] aion::ExecutionPlan compute_plan(std::size_t task_count, std::uint64_t iterations) {
    std::vector<SystemSpec> systems;
    systems.reserve(task_count);
    for (std::size_t t = 0; t < task_count; ++t) {
        const auto id = static_cast<aion::SystemId>(t + 1);
        systems.push_back({
            .id = id,
            .name = "ComputeTask",
            .reads = {},
            .writes = {},
            .run = [iterations, seed = static_cast<std::uint64_t>(t + 1)](SystemContext& ctx) {
                std::uint64_t x = seed * 0x9e3779b97f4a7c15ULL + 0x243f6a8885a308d3ULL;
                for (std::uint64_t i = 0; i < iterations; ++i) {
                    x ^= x >> 12U;
                    x ^= x << 25U;
                    x ^= x >> 27U;
                    x *= 0x2545F4914F6CDD1DULL;
                    x += i ^ (x >> 17U);
                }
                ctx.set_auxiliary_result(x);
            }
        });
    }
    aion::ExecutionPlan plan;
    if (!aion::ExecutionPlan::build(std::move(systems), plan).ok) std::abort();
    return plan;
}

[[nodiscard]] double compute_run(const aion::ExecutionPlan& plan, std::size_t workers, std::uint64_t& checksum) {
    aion::World world;
    aion::ExecutionRuntime runtime({
        .kind = workers == 1 ? aion::ExecutorKind::SerialReference : aion::ExecutorKind::WorkerPool,
        .workers = workers});
    const auto start = Clock::now();
    const auto result = runtime.execute(plan, world);
    const auto elapsed = ms(Clock::now() - start);
    if (!result.ok) std::abort();
    checksum = result.auxiliary_checksum;
    return elapsed;
}

} // namespace

int main(int argc, char** argv) {
    const bool correctness_only = argc > 1 && std::string_view(argv[1]) == "--correctness-only";

    aion::StateHash reference_hash{};
    bool have_reference_hash = false;
    double serial_median_ms = 0.0;

    for (std::size_t workers : {1U, 2U, 4U, 5U}) {
        std::vector<double> samples;
        samples.reserve(7);
        for (int repeat = 0; repeat < 7; ++repeat) {
            double elapsed = 0.0;
            const auto hash = correctness_run(
                workers == 1 ? aion::ExecutorKind::SerialReference : aion::ExecutorKind::WorkerPool,
                workers,
                elapsed);
            if (!have_reference_hash) {
                reference_hash = hash;
                have_reference_hash = true;
            } else if (!(hash == reference_hash)) {
                std::cerr << "R2 correctness failure: worker-count hash differs\n";
                return EXIT_FAILURE;
            }
            samples.push_back(elapsed);
        }
        std::sort(samples.begin(), samples.end());
        const auto median = samples[samples.size() / 2];
        if (workers == 1) serial_median_ms = median;
        std::cout << "world_workers=" << workers
                  << " median_ms=" << std::fixed << std::setprecision(3) << median
                  << " speedup=" << (serial_median_ms / median) << "x\n";
    }

    std::cout << "correctness_sha256=" << reference_hash.hex() << '\n';
    if (correctness_only) return EXIT_SUCCESS;

    constexpr std::size_t tasks = 32;
    constexpr std::uint64_t iterations = 750000;
    const auto plan = compute_plan(tasks, iterations);
    std::cout << "compute_tasks=" << tasks << '\n'
              << "compute_iterations_per_task=" << iterations << '\n';

    std::uint64_t reference_checksum = 0;
    double reference_ms = 0.0;
    for (std::size_t workers : {1U, 2U, 4U, 5U}) {
        std::vector<double> samples;
        samples.reserve(7);
        std::uint64_t checksum = 0;
        for (int repeat = 0; repeat < 7; ++repeat) {
            std::uint64_t current = 0;
            samples.push_back(compute_run(plan, workers, current));
            if (repeat == 0) checksum = current;
            if (current != checksum) {
                std::cerr << "auxiliary checksum instability\n";
                return EXIT_FAILURE;
            }
        }
        std::sort(samples.begin(), samples.end());
        const auto median = samples[samples.size() / 2];
        if (workers == 1) {
            reference_ms = median;
            reference_checksum = checksum;
        } else if (checksum != reference_checksum) {
            std::cerr << "parallel auxiliary result differs from serial result\n";
            return EXIT_FAILURE;
        }
        std::cout << "compute_workers=" << workers
                  << " median_ms=" << median
                  << " speedup=" << (reference_ms / median) << "x"
                  << " checksum=" << checksum << '\n';
    }

    return EXIT_SUCCESS;
}
