#include "aion/kernel/execution.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)

namespace {
using aion::AccessMask;
using aion::ResourceKey;
using aion::SystemContext;
using aion::SystemSpec;
constexpr float kDt = 1.0F / 60.0F;

[[nodiscard]] aion::World make_world(std::size_t entities) {
    aion::World world(entities + 1);
    aion::Transaction spawn{.id = 1}; spawn.mutations.reserve(entities * 4);
    for (std::size_t i = 0; i < entities; ++i) {
        const auto id = static_cast<aion::EntityId>(i + 1);
        spawn.mutations.push_back({aion::MutationKind::CreateEntity, id, {}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetPosition, id, {static_cast<float>(i), 0.0F, 0.0F}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetVelocity, id, {0.1F, 0.02F, -0.03F}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetHealth, id, {}, static_cast<std::uint32_t>(100 - (i % 25))});
    }
    if (!world.commit(spawn).committed) std::abort(); return world;
}

[[nodiscard]] std::vector<SystemSpec> systems(bool ranged) {
    const auto entity_reads = AccessMask::of({ResourceKey::Identity, ResourceKey::EntityState});
    return {
        {.id = 10, .name = "Integrate", .reads = entity_reads | AccessMask::of({ResourceKey::Position, ResourceKey::Velocity}),
         .writes = AccessMask::of({ResourceKey::Position}), .run = [ranged](SystemContext& ctx) {
            const auto n = ctx.entity_capacity();
            if (ranged) {
                std::vector<aion::Vec3> values; values.reserve(n - 1);
                for (std::size_t i = 1; i < n; ++i) { const auto p = *ctx.position(i); const auto v = *ctx.velocity(i); values.push_back({p.x + v.x*kDt,p.y + v.y*kDt,p.z + v.z*kDt}); }
                ctx.set_position_range(1, std::move(values));
            } else {
                for (std::size_t i = 1; i < n; ++i) { const auto p = *ctx.position(i); const auto v = *ctx.velocity(i); ctx.set_position(i,{p.x + v.x*kDt,p.y + v.y*kDt,p.z + v.z*kDt}); }
            }
         }},
        {.id = 20, .name = "Health", .reads = entity_reads | AccessMask::of({ResourceKey::Health}),
         .writes = AccessMask::of({ResourceKey::Health}), .run = [ranged](SystemContext& ctx) {
            const auto n = ctx.entity_capacity();
            if (ranged) { std::vector<std::uint32_t> values; values.reserve(n - 1); for (std::size_t i=1;i<n;++i) { auto h=*ctx.health(i); values.push_back(h? h-1U:0U);} ctx.set_health_range(1,std::move(values)); }
            else { for (std::size_t i=1;i<n;++i) { auto h=*ctx.health(i); ctx.set_health(i,h? h-1U:0U);} }
         }},
        {.id = 30, .name = "Velocity", .reads = entity_reads | AccessMask::of({ResourceKey::Velocity}),
         .writes = AccessMask::of({ResourceKey::Velocity}), .run = [ranged](SystemContext& ctx) {
            const auto n=ctx.entity_capacity();
            if (ranged) { std::vector<aion::Vec3> values; values.reserve(n-1); for(std::size_t i=1;i<n;++i){auto v=*ctx.velocity(i);values.push_back({v.x*0.9995F,v.y*0.9995F,v.z*0.9995F});} ctx.set_velocity_range(1,std::move(values)); }
            else { for(std::size_t i=1;i<n;++i){auto v=*ctx.velocity(i);ctx.set_velocity(i,{v.x*0.9995F,v.y*0.9995F,v.z*0.9995F});} }
         }},
        {.id = 40, .name = "Clamp", .reads = entity_reads | AccessMask::of({ResourceKey::Position}),
         .writes = AccessMask::of({ResourceKey::Position}), .run = [ranged](SystemContext& ctx) {
            const auto n=ctx.entity_capacity();
            if (ranged) { std::vector<aion::Vec3> values; values.reserve(n-1); for(std::size_t i=1;i<n;++i){auto p=*ctx.position(i); if(p.x>100000.0F)p.x=100000.0F; values.push_back(p);} ctx.set_position_range(1,std::move(values)); }
            else { for(std::size_t i=1;i<n;++i){auto p=*ctx.position(i);if(p.x>100000.0F)p.x=100000.0F;ctx.set_position(i,p);} }
         }}
    };
}
} // namespace

int main() {
    constexpr std::size_t entities = 4096, frames = 120;
    aion::ExecutionPlan scalar_plan, range_plan;
    CHECK(aion::ExecutionPlan::build(systems(false), scalar_plan).ok);
    CHECK(aion::ExecutionPlan::build(systems(true), range_plan).ok);
    CHECK(scalar_plan.waves().size() == range_plan.waves().size());

    auto scalar_world = make_world(entities);
    auto range_world = make_world(entities);
    aion::ExecutionRuntime scalar_runtime({.kind=aion::ExecutorKind::SerialReference,.workers=1});
    aion::ExecutionRuntime range_runtime({.kind=aion::ExecutorKind::WorkerPool,.workers=4});
    aion::PatchJournal journal;

    // Legacy execution refuses range outputs rather than silently dropping them.
    auto reject_world = make_world(64);
    aion::ExecutionRuntime reject_runtime;
    const auto rejected = reject_runtime.execute(range_plan, reject_world);
    CHECK(!rejected.ok);

    for (std::size_t frame=0; frame<frames; ++frame) {
        CHECK(scalar_runtime.execute(scalar_plan, scalar_world).ok);
        CHECK(range_runtime.execute_patched(range_plan, range_world, &journal).ok);
        CHECK(scalar_world.state_hash() == range_world.state_hash());
    }

    const auto final_hash = scalar_world.state_hash();
    auto replay_world = make_world(entities);
    const auto txs = journal.transactions();
    CHECK(aion::PatchJournal::replay(replay_world, txs).ok);
    CHECK(replay_world.state_hash() == final_hash);

    std::cout << "r21_execution_patch_tests: PASS\nentities=" << entities << "\nframes=" << frames
              << "\ntransactions=" << journal.size() << "\nfinal_sha256=" << final_hash.hex() << '\n';
    return EXIT_SUCCESS;
}
