#include "aion/kernel/patch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

constexpr float kDt = 1.0F / 60.0F;

struct Metrics {
    double build_ms{};
    double commit_ms{};
    std::size_t logical_records{};
    std::size_t payload_bytes{};
    aion::StateHash hash{};
};

enum class Candidate { Oracle, Range, Page, CowPage, ParallelPage };

[[nodiscard]] double ms(Clock::duration duration) { return std::chrono::duration<double, std::milli>(duration).count(); }

[[nodiscard]] aion::World make_world(std::size_t entities) {
    aion::World world(entities + 1);
    aion::Transaction spawn{.id = 1};
    spawn.mutations.reserve(entities * 4);
    for (std::size_t i = 0; i < entities; ++i) {
        const auto id = static_cast<aion::EntityId>(i + 1);
        spawn.mutations.push_back({aion::MutationKind::CreateEntity, id, {}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetPosition, id,
            {static_cast<float>(i) * 0.125F, static_cast<float>(i % 23) * 0.25F, -static_cast<float>(i % 43)}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetVelocity, id,
            {0.1F + static_cast<float>(i % 7) * 0.005F, 0.02F, -0.03F}, 0});
        spawn.mutations.push_back({aion::MutationKind::SetHealth, id, {}, static_cast<std::uint32_t>(100 - (i % 31))});
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
        const auto p = *world.position(entity); const auto v = *world.velocity(entity); const auto h = *world.health(entity);
        tx.mutations.push_back({aion::MutationKind::SetPosition, entity, next_position(p, v), 0});
        tx.mutations.push_back({aion::MutationKind::SetVelocity, entity, next_velocity(v), 0});
        tx.mutations.push_back({aion::MutationKind::SetHealth, entity, {}, next_health(h)});
    }
    return tx;
}

[[nodiscard]] aion::PatchTransaction build_range(const aion::World& world, aion::TransactionId id) {
    const auto count = world.entity_capacity() - 1;
    aion::PatchTransaction tx{.id = id};
    aion::Vec3RangePatch p{.component = aion::PatchComponent::Position, .first = 1};
    aion::Vec3RangePatch v{.component = aion::PatchComponent::Velocity, .first = 1};
    aion::U32RangePatch h{.component = aion::PatchComponent::Health, .first = 1};
    p.values.reserve(count); v.values.reserve(count); h.values.reserve(count);
    for (std::size_t i = 1; i < world.entity_capacity(); ++i) {
        const auto entity = static_cast<aion::EntityId>(i);
        const auto old_v = *world.velocity(entity);
        p.values.push_back(next_position(*world.position(entity), old_v));
        v.values.push_back(next_velocity(old_v));
        h.values.push_back(next_health(*world.health(entity)));
    }
    tx.vec3_patches.push_back(std::move(p)); tx.vec3_patches.push_back(std::move(v)); tx.u32_patches.push_back(std::move(h));
    return tx;
}

[[nodiscard]] aion::PatchTransaction build_pages(const aion::World& world, aion::TransactionId id, std::size_t page_size, bool cow) {
    aion::PatchTransaction tx{.id = id};
    const auto capacity = world.entity_capacity();
    for (const auto component : {aion::PatchComponent::Position, aion::PatchComponent::Velocity}) {
        for (std::size_t begin = 1; begin < capacity; begin += page_size) {
            const auto count = std::min(page_size, capacity - begin);
            aion::Vec3RangePatch patch{.component = component, .first = static_cast<aion::EntityId>(begin)};
            patch.values.resize(count);
            if (cow) {
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
        for (std::size_t j = 0; j < count; ++j) patch.values[j] = next_health(*world.health(static_cast<aion::EntityId>(begin + j)));
        tx.u32_patches.push_back(std::move(patch));
    }
    return tx;
}

[[nodiscard]] std::size_t patch_payload(const aion::PatchTransaction& tx) {
    std::size_t bytes = 0;
    for (const auto& p : tx.vec3_patches) bytes += p.values.capacity() * sizeof(aion::Vec3) + sizeof(p);
    for (const auto& p : tx.u32_patches) bytes += p.values.capacity() * sizeof(std::uint32_t) + sizeof(p);
    return bytes;
}

[[nodiscard]] Metrics run(Candidate candidate, std::size_t entities, std::size_t frames, std::size_t page_size, std::size_t workers) {
    auto world = make_world(entities);
    Metrics result{};
    std::optional<aion::PatchCommitRuntime> persistent;
    if (candidate == Candidate::ParallelPage) persistent.emplace(workers);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto id = static_cast<aion::TransactionId>(frame + 2);
        if (candidate == Candidate::Oracle) {
            const auto build_start = Clock::now();
            auto tx = build_oracle(world, id);
            result.build_ms += ms(Clock::now() - build_start);
            result.logical_records = tx.mutations.size();
            result.payload_bytes = tx.mutations.capacity() * sizeof(aion::Mutation);
            const auto commit_start = Clock::now();
            if (!world.commit(tx).committed) std::abort();
            result.commit_ms += ms(Clock::now() - commit_start);
        } else {
            const auto build_start = Clock::now();
            auto tx = candidate == Candidate::Range ? build_range(world, id)
                : build_pages(world, id, page_size, candidate == Candidate::CowPage);
            result.build_ms += ms(Clock::now() - build_start);
            result.logical_records = tx.vec3_patches.size() + tx.u32_patches.size();
            result.payload_bytes = patch_payload(tx);
            const auto commit_start = Clock::now();
            const auto committed = candidate == Candidate::ParallelPage
                ? persistent->commit(world, tx)
                : aion::commit_patch_transaction(world, tx, aion::PatchApplyMode::Serial, 1);
            if (!committed.committed) std::abort();
            result.commit_ms += ms(Clock::now() - commit_start);
        }
    }
    result.hash = world.state_hash();
    return result;
}

[[nodiscard]] const char* name(Candidate c) {
    switch (c) {
        case Candidate::Oracle: return "per_entity";
        case Candidate::Range: return "contiguous_range";
        case Candidate::Page: return "fixed_page";
        case Candidate::CowPage: return "cow_page_clone";
        case Candidate::ParallelPage: return "parallel_page";
    }
    return "unknown";
}

void scenario(std::size_t entities, std::size_t frames, std::size_t page_size, std::size_t workers) {
    std::cout << "scenario entities=" << entities << " frames=" << frames << " page_size=" << page_size << " workers=" << workers << '\n';
    aion::StateHash reference{};
    bool have_reference = false;
    double reference_total = 0.0;
    for (const auto candidate : {Candidate::Oracle, Candidate::Range, Candidate::Page, Candidate::CowPage, Candidate::ParallelPage}) {
        const auto m = run(candidate, entities, frames, page_size, workers);
        if (!have_reference) { reference = m.hash; have_reference = true; reference_total = m.build_ms + m.commit_ms; }
        else if (!(m.hash == reference)) {
            std::cerr << "R2.1 hash mismatch candidate=" << name(candidate) << '\n';
            std::exit(EXIT_FAILURE);
        }
        const auto total = m.build_ms + m.commit_ms;
        std::cout << "candidate=" << name(candidate)
                  << " build_ms=" << std::fixed << std::setprecision(3) << m.build_ms
                  << " commit_ms=" << m.commit_ms
                  << " total_ms=" << total
                  << " speedup=" << (reference_total / total)
                  << " records_per_frame=" << m.logical_records
                  << " payload_bytes_per_frame=" << m.payload_bytes
                  << " sha256=" << m.hash.hex() << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 5) {
        scenario(static_cast<std::size_t>(std::stoull(argv[1])), static_cast<std::size_t>(std::stoull(argv[2])),
                 static_cast<std::size_t>(std::stoull(argv[3])), static_cast<std::size_t>(std::stoull(argv[4])));
        return EXIT_SUCCESS;
    }

    scenario(8192, 60, 256, 4);
    scenario(100000, 20, 256, 4);
    scenario(1000000, 3, 256, 4);
    return EXIT_SUCCESS;
}
