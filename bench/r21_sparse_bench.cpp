#include "aion/kernel/patch.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kPage = 256;

struct Metrics { double build_ms{}; double commit_ms{}; std::size_t records{}; std::size_t payload{}; aion::StateHash hash{}; };
enum class Pattern { Clustered, Scattered };
enum class Candidate { PerEntity, ExactRanges, FixedPageClone, FullRange };

[[nodiscard]] double ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

[[nodiscard]] aion::World make_world(std::size_t entities) {
    aion::World world(entities + 1);
    aion::Transaction tx{.id = 1}; tx.mutations.reserve(entities * 2);
    for (std::size_t i = 1; i <= entities; ++i) {
        const auto id = static_cast<aion::EntityId>(i);
        tx.mutations.push_back({aion::MutationKind::CreateEntity, id, {}, 0});
        tx.mutations.push_back({aion::MutationKind::SetPosition, id, {static_cast<float>(i), 0.0F, 0.0F}, 0});
    }
    if (!world.commit(tx).committed) std::abort();
    return world;
}

[[nodiscard]] bool dirty(std::size_t id, Pattern pattern) noexcept {
    if (pattern == Pattern::Clustered) return ((id - 1) % 10000) < 100; // 1% in 100-entity runs.
    return (id % 100) == 1; // 1% isolated, touches most 256-entity pages.
}

[[nodiscard]] aion::Vec3 updated(aion::Vec3 p) noexcept { p.x += 0.125F; p.y += 0.03125F; return p; }

[[nodiscard]] std::size_t payload(const aion::PatchTransaction& tx) {
    std::size_t bytes = 0;
    for (const auto& p : tx.vec3_patches) bytes += sizeof(p) + p.values.capacity() * sizeof(aion::Vec3);
    return bytes;
}

[[nodiscard]] Metrics run(Candidate candidate, Pattern pattern, std::size_t entities, std::size_t frames) {
    auto world = make_world(entities); Metrics m{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto txid = static_cast<aion::TransactionId>(frame + 2);
        if (candidate == Candidate::PerEntity) {
            const auto t0 = Clock::now();
            aion::Transaction tx{.id = txid}; tx.mutations.reserve(entities / 100 + 16);
            for (std::size_t i = 1; i <= entities; ++i) if (dirty(i, pattern)) {
                const auto id = static_cast<aion::EntityId>(i);
                tx.mutations.push_back({aion::MutationKind::SetPosition, id, updated(*world.position(id)), 0});
            }
            m.build_ms += ms(Clock::now() - t0); m.records = tx.mutations.size(); m.payload = tx.mutations.capacity() * sizeof(aion::Mutation);
            const auto t1 = Clock::now(); if (!world.commit(tx).committed) std::abort(); m.commit_ms += ms(Clock::now() - t1);
            continue;
        }

        const auto t0 = Clock::now();
        aion::PatchTransaction tx{.id = txid};
        if (candidate == Candidate::FullRange) {
            aion::Vec3RangePatch patch{.component = aion::PatchComponent::Position, .first = 1}; patch.values.resize(entities);
            for (std::size_t i = 1; i <= entities; ++i) {
                auto p = *world.position(static_cast<aion::EntityId>(i)); if (dirty(i, pattern)) p = updated(p); patch.values[i - 1] = p;
            }
            tx.vec3_patches.push_back(std::move(patch));
        } else if (candidate == Candidate::ExactRanges) {
            std::size_t i = 1;
            while (i <= entities) {
                if (!dirty(i, pattern)) { ++i; continue; }
                const auto begin = i;
                while (i <= entities && dirty(i, pattern)) ++i;
                aion::Vec3RangePatch patch{.component = aion::PatchComponent::Position, .first = static_cast<aion::EntityId>(begin)};
                patch.values.reserve(i - begin);
                for (std::size_t id = begin; id < i; ++id) patch.values.push_back(updated(*world.position(static_cast<aion::EntityId>(id))));
                tx.vec3_patches.push_back(std::move(patch));
            }
        } else {
            for (std::size_t begin = 1; begin <= entities; begin += kPage) {
                const auto count = std::min(kPage, entities - begin + 1);
                bool touched = false;
                for (std::size_t j = 0; j < count; ++j) if (dirty(begin + j, pattern)) { touched = true; break; }
                if (!touched) continue;
                aion::Vec3RangePatch patch{.component = aion::PatchComponent::Position, .first = static_cast<aion::EntityId>(begin)}; patch.values.resize(count);
                for (std::size_t j = 0; j < count; ++j) {
                    auto p = *world.position(static_cast<aion::EntityId>(begin + j)); if (dirty(begin + j, pattern)) p = updated(p); patch.values[j] = p;
                }
                tx.vec3_patches.push_back(std::move(patch));
            }
        }
        m.build_ms += ms(Clock::now() - t0); m.records = tx.vec3_patches.size(); m.payload = payload(tx);
        const auto t1 = Clock::now(); if (!aion::commit_patch_transaction(world, tx).committed) std::abort(); m.commit_ms += ms(Clock::now() - t1);
    }
    m.hash = world.state_hash(); return m;
}

[[nodiscard]] const char* pname(Pattern p) { return p == Pattern::Clustered ? "clustered_1pct" : "scattered_1pct"; }
[[nodiscard]] const char* cname(Candidate c) {
    switch (c) { case Candidate::PerEntity: return "per_entity"; case Candidate::ExactRanges: return "exact_ranges"; case Candidate::FixedPageClone: return "page_clone_256"; case Candidate::FullRange: return "full_range"; }
    return "unknown";
}

} // namespace

int main() {
    constexpr std::size_t entities = 100000; constexpr std::size_t frames = 200;
    for (const auto pattern : {Pattern::Clustered, Pattern::Scattered}) {
        std::cout << "pattern=" << pname(pattern) << " entities=" << entities << " frames=" << frames << '\n';
        aion::StateHash reference{}; bool have = false; double ref = 0.0;
        for (const auto candidate : {Candidate::PerEntity, Candidate::ExactRanges, Candidate::FixedPageClone, Candidate::FullRange}) {
            const auto m = run(candidate, pattern, entities, frames); const auto total = m.build_ms + m.commit_ms;
            if (!have) { reference = m.hash; ref = total; have = true; } else if (!(reference == m.hash)) { std::cerr << "sparse hash mismatch\n"; return EXIT_FAILURE; }
            std::cout << "candidate=" << cname(candidate) << " build_ms=" << std::fixed << std::setprecision(3) << m.build_ms
                      << " commit_ms=" << m.commit_ms << " total_ms=" << total << " speedup=" << (ref / total)
                      << " records_per_frame=" << m.records << " payload_bytes_per_frame=" << m.payload
                      << " sha256=" << m.hash.hex() << '\n';
        }
    }
    return EXIT_SUCCESS;
}
