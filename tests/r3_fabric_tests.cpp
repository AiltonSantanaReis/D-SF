#include "aion/kernel/spatial_fabric.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while(false)

namespace {
std::vector<aion::SpatialRecord> records(std::size_t count) {
    std::mt19937 rng(0xD5F34001U);
    std::uniform_real_distribution<float> pos(-10000.0F, 10000.0F);
    std::uniform_real_distribution<float> ext(0.1F, 4.0F);
    std::vector<aion::SpatialRecord> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        out.push_back({static_cast<aion::EntityId>(i + 1), {pos(rng),pos(rng),pos(rng)}, {ext(rng),ext(rng),ext(rng)}});
    return out;
}

aion::Aabb query_box(int i) {
    const float x = -8000.0F + static_cast<float>((i * 977) % 16000);
    const float y = -7000.0F + static_cast<float>((i * 619) % 14000);
    const float z = -6000.0F + static_cast<float>((i * 431) % 12000);
    return {{x-300.0F,y-300.0F,z-300.0F},{x+300.0F,y+300.0F,z+300.0F}};
}

aion::PatchTransaction move_patch(const aion::SpatialSnapshot& snapshot, std::uint64_t id, std::size_t count, float delta) {
    aion::PatchTransaction tx{.id=id};
    count = std::min(count, snapshot.size());
    tx.scalar_mutations.reserve(count);
    for (std::size_t i=0; i<count; ++i) {
        const auto slot=static_cast<std::uint32_t>((i*3571U)%snapshot.size());
        auto c=snapshot.center(slot);
        c.x += delta; c.y -= delta*0.5F; c.z += delta*0.25F;
        tx.scalar_mutations.push_back({aion::MutationKind::SetPosition,snapshot.entity(slot),c,0});
    }
    return tx;
}
}

int main() {
    std::string error;

    // Cost policy has no hidden churn/debt threshold: it compares measured projected frame cost.
    {
        const aion::SpatialCostObservation sah{.update_ms=0.5,.sampled_query_ms=0.2,.sampled_queries=100};
        const aion::SpatialCostObservation morton{.update_ms=2.0,.sampled_query_ms=0.1,.sampled_queries=100};
        const auto low_q=aion::choose_spatial_backend(sah,morton,50);
        CHECK(low_q.valid);
        CHECK(low_q.backend==aion::SpatialBackend::WideSah);
        const aion::SpatialCostObservation degraded_sah{.update_ms=0.5,.sampled_query_ms=4.0,.sampled_queries=100};
        const auto high_q=aion::choose_spatial_backend(degraded_sah,morton,500);
        CHECK(high_q.valid);
        CHECK(high_q.backend==aion::SpatialBackend::Morton);
    }

    // Full catch-up skips an arbitrary number of intermediate position publications but remains exact
    // because structure/slot identity has not changed.
    {
        aion::SpatialSnapshot snapshot;
        CHECK(snapshot.build(records(12000),error));
        aion::WideBvh8View view;
        CHECK(view.build(snapshot,error));
        for (std::uint64_t frame=0; frame<5; ++frame) {
            const auto applied=snapshot.apply_patch_transaction(move_patch(snapshot,frame+2,300,2.0F+static_cast<float>(frame)));
            CHECK(applied.ok);
        }
        CHECK(view.snapshot_version()!=snapshot.version());
        CHECK(view.full_refit_to(snapshot,error));
        CHECK(view.snapshot_version()==snapshot.version());
        for(int i=0;i<100;++i) CHECK(view.query_aabb(snapshot,query_box(i)).entities==aion::SpatialOracle::query_aabb(snapshot,query_box(i)));
    }

    // Build SAH in background while Morton continues to serve evolving snapshot versions.
    {
        aion::SpatialSnapshot snapshot;
        CHECK(snapshot.build(records(50000),error));
        aion::MortonBvh8View active;
        CHECK(active.build(snapshot,error));
        aion::AsyncSahBuilder builder;
        CHECK(builder.start(snapshot,error));

        std::uint64_t txid=2;
        int frames=0;
        while(builder.busy() && frames<20) {
            const auto applied=snapshot.apply_patch_transaction(move_patch(snapshot,txid++,500,0.25F));
            CHECK(applied.ok);
            CHECK(builder.observe_changes(applied.changes,error));
            CHECK(active.sync(snapshot,applied.changes,error));
            for(int q=0;q<8;++q) CHECK(active.query_aabb(snapshot,query_box(q+frames)).entities==aion::SpatialOracle::query_aabb(snapshot,query_box(q+frames)));
            ++frames;
        }

        aion::WideBvh8View candidate;
        const auto promoted=builder.wait_promote(snapshot,candidate);
        CHECK(promoted.state==aion::AsyncSahPromoteState::Promoted);
        CHECK(promoted.stats.source_records==snapshot.size());
        CHECK(candidate.snapshot_version()==snapshot.version());
        for(int i=0;i<100;++i) CHECK(candidate.query_aabb(snapshot,query_box(i)).entities==aion::SpatialOracle::query_aabb(snapshot,query_box(i)));
    }

    // Structural generation changes invalidate a candidate, even if record count happens to remain equal.
    {
        auto rec=records(20000);
        aion::SpatialSnapshot snapshot;
        CHECK(snapshot.build(rec,error));
        aion::AsyncSahBuilder builder;
        CHECK(builder.start(snapshot,error));
        std::swap(rec[0].entity,rec[1].entity); // build() sorts; force a structural generation change anyway.
        CHECK(snapshot.build(rec,error));
        aion::WideBvh8View candidate;
        const auto result=builder.wait_promote(snapshot,candidate);
        CHECK(result.state==aion::AsyncSahPromoteState::Discarded);
    }

    std::cout << "r3_fabric_tests: PASS\n";
    return EXIT_SUCCESS;
}
