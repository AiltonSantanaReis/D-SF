#include "aion/kernel/spatial.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while(false)

namespace {

std::vector<aion::SpatialRecord> make_records(std::size_t count) {
    std::mt19937 rng(0xD5F30001U);
    std::uniform_real_distribution<float> pos(-5000.0F, 5000.0F);
    std::uniform_real_distribution<float> ext(0.1F, 3.0F);
    std::vector<aion::SpatialRecord> records;
    records.reserve(count);
    for (std::size_t i=0; i<count; ++i) {
        records.push_back({
            .entity = static_cast<aion::EntityId>(i+1),
            .center = {pos(rng), pos(rng), pos(rng)},
            .half_extent = {ext(rng), ext(rng), ext(rng)}
        });
    }
    return records;
}

bool same_ray(const aion::SpatialRayResult& a, const aion::SpatialRayResult& b) {
    if (!a.ok || !b.ok) return false;
    if (a.entity == 0 || b.entity == 0) return a.entity == b.entity;
    return a.entity == b.entity && std::fabs(a.t-b.t) < 1.0e-4F;
}

} // namespace

int main() {
    constexpr std::size_t count = 12000;
    auto records = make_records(count);
    aion::SpatialSnapshot snapshot;
    std::string error;
    CHECK(snapshot.build(records, error));
    CHECK(snapshot.size() == count);
    CHECK(snapshot.version() == 1);
    CHECK(snapshot.slot_of(1) == 0);
    CHECK(snapshot.slot_of(count) == count-1);

    // Sparse/high EntityIds must scale with record count, not with the largest identity.
    {
        const std::vector<aion::SpatialRecord> sparse_ids{
            {10ULL,{0,0,0},{1,1,1}},
            {1000000000000ULL,{10,0,0},{1,1,1}},
            {9000000000000000000ULL,{20,0,0},{1,1,1}},
        };
        aion::SpatialSnapshot sparse_snapshot;
        CHECK(sparse_snapshot.build(sparse_ids,error));
        CHECK(sparse_snapshot.size()==3);
        CHECK(sparse_snapshot.slot_of(10ULL)==0);
        CHECK(sparse_snapshot.slot_of(1000000000000ULL)==1);
        CHECK(sparse_snapshot.slot_of(9000000000000000000ULL)==2);
        CHECK(sparse_snapshot.slot_of(11ULL)==aion::SpatialSnapshot::kInvalidSlot);
        CHECK(sparse_snapshot.storage_bytes() < 1024);
    }

    aion::WideBvh8View sah;
    aion::MortonBvh8View morton;
    CHECK(sah.build(snapshot, error));
    CHECK(morton.build(snapshot, error));
    CHECK(sah.snapshot_version() == snapshot.version());
    CHECK(morton.snapshot_version() == snapshot.version());

    // Correctness against brute-force oracle for AABB queries.
    std::mt19937 rng(0xD5F30002U);
    std::uniform_real_distribution<float> pos(-5000.0F, 5000.0F);
    std::uniform_real_distribution<float> size(5.0F, 400.0F);
    for (int i=0; i<300; ++i) {
        const aion::Vec3 c{pos(rng),pos(rng),pos(rng)};
        const float h=size(rng);
        const aion::Aabb q{{c.x-h,c.y-h,c.z-h},{c.x+h,c.y+h,c.z+h}};
        const auto oracle=aion::SpatialOracle::query_aabb(snapshot,q);
        const auto a=sah.query_aabb(snapshot,q);
        const auto b=morton.query_aabb(snapshot,q);
        CHECK(a.ok && b.ok);
        CHECK(a.entities == oracle);
        CHECK(b.entities == oracle);
    }

    // Ray correctness with deterministic rays.
    for (int i=0; i<200; ++i) {
        const aion::Ray ray{
            .origin={-6000.0F, pos(rng), pos(rng)},
            .direction={1.0F, 0.001F*static_cast<float>(i%7), 0.001F*static_cast<float>(i%5)},
            .t_min=0.0F,
            .t_max=15000.0F
        };
        const auto oracle=aion::SpatialOracle::raycast(snapshot,ray);
        const auto a=sah.raycast(snapshot,ray);
        const auto b=morton.raycast(snapshot,ray);
        CHECK(same_ray(a,oracle));
        CHECK(same_ray(b,oracle));
    }

    // R2.1 dense position range drives the shared snapshot directly.
    aion::PatchTransaction dense{.id=2};
    aion::Vec3RangePatch range{.component=aion::PatchComponent::Position,.first=1};
    range.values.reserve(4096);
    for (std::size_t i=0; i<4096; ++i) {
        const auto c=snapshot.center(static_cast<std::uint32_t>(i));
        range.values.push_back({c.x+10.0F,c.y-3.0F,c.z+1.5F});
    }
    dense.vec3_patches.push_back(std::move(range));
    const auto applied=snapshot.apply_patch_transaction(dense);
    CHECK(applied.ok);
    CHECK(applied.changes.changed);
    CHECK(!applied.changes.requires_rebuild);
    CHECK(applied.changes.dirty_slots.empty());
    CHECK(applied.changes.dirty_ranges.size()==1);
    CHECK(applied.changes.dirty_ranges[0].first==0);
    CHECK(applied.changes.dirty_ranges[0].count==4096);
    CHECK(snapshot.version()==2);

    // Stale views must fail explicitly rather than return previous-frame answers.
    const aion::Aabb probe{{-50,-50,-50},{50,50,50}};
    CHECK(!sah.query_aabb(snapshot,probe).ok);
    CHECK(!morton.query_aabb(snapshot,probe).ok);

    CHECK(sah.sync(snapshot,applied.changes,error));
    CHECK(morton.sync(snapshot,applied.changes,error));
    CHECK(sah.query_aabb(snapshot,probe).entities == aion::SpatialOracle::query_aabb(snapshot,probe));
    CHECK(morton.query_aabb(snapshot,probe).entities == aion::SpatialOracle::query_aabb(snapshot,probe));

    // Sparse scalar writes also use the same transaction lane.
    aion::PatchTransaction sparse{.id=3};
    for (aion::EntityId id : {7ULL, 97ULL, 997ULL, 9997ULL}) {
        const auto slot=snapshot.slot_of(id);
        const auto c=snapshot.center(slot);
        sparse.scalar_mutations.push_back({aion::MutationKind::SetPosition,id,{c.x+1.0F,c.y+2.0F,c.z+3.0F},0});
    }
    const auto sparse_applied=snapshot.apply_patch_transaction(sparse);
    CHECK(sparse_applied.ok);
    CHECK(sparse_applied.changes.dirty_slots.size()==4);
    CHECK(sah.sync(snapshot,sparse_applied.changes,error));
    CHECK(morton.sync(snapshot,sparse_applied.changes,error));

    // Invalid spatial write is atomic: no version or center change.
    const auto version_before=snapshot.version();
    const auto center_before=snapshot.center(0);
    aion::PatchTransaction invalid{.id=4};
    invalid.scalar_mutations.push_back({aion::MutationKind::SetPosition,1,{99,99,99},0});
    invalid.scalar_mutations.push_back({aion::MutationKind::SetPosition,9999999,{1,2,3},0});
    const auto rejected=snapshot.apply_patch_transaction(invalid);
    CHECK(!rejected.ok);
    CHECK(snapshot.version()==version_before);
    CHECK(snapshot.center(0)==center_before);

    // Structural mutations are not partially projected; they request a snapshot rebuild.
    aion::PatchTransaction structural{.id=5};
    structural.scalar_mutations.push_back({aion::MutationKind::DestroyEntity,3,{},0});
    const auto structure_result=snapshot.apply_patch_transaction(structural);
    CHECK(structure_result.ok);
    CHECK(structure_result.changes.requires_rebuild);
    CHECK(snapshot.version()==version_before);

    // Missing an intermediate publication is a hard synchronization error.
    {
        auto chain_records = make_records(64);
        aion::SpatialSnapshot chain_snapshot;
        CHECK(chain_snapshot.build(chain_records,error));
        aion::WideBvh8View lagging;
        CHECK(lagging.build(chain_snapshot,error));

        aion::PatchTransaction tx_a{.id=10};
        tx_a.scalar_mutations.push_back({aion::MutationKind::SetPosition,1,{1,2,3},0});
        const auto a_result=chain_snapshot.apply_patch_transaction(tx_a);
        CHECK(a_result.ok);
        aion::PatchTransaction tx_b{.id=11};
        tx_b.scalar_mutations.push_back({aion::MutationKind::SetPosition,2,{4,5,6},0});
        const auto b_result=chain_snapshot.apply_patch_transaction(tx_b);
        CHECK(b_result.ok);
        CHECK(!lagging.sync(chain_snapshot,b_result.changes,error));
        CHECK(lagging.build(chain_snapshot,error));
        CHECK(lagging.snapshot_version()==chain_snapshot.version());
    }

    // Views contain topology/references, not a second object AABB array.
    CHECK(sah.primitive_reference_count()==snapshot.size());
    CHECK(snapshot.object_payload_bytes() > 0);

    std::cout << "r3_snapshot_tests: PASS\n"
              << "entities=" << snapshot.size() << '\n'
              << "snapshot_bytes=" << snapshot.storage_bytes() << '\n'
              << "sah_view_bytes=" << sah.storage_bytes() << '\n'
              << "morton_view_bytes=" << morton.storage_bytes() << '\n'
              << "snapshot_version=" << snapshot.version() << '\n';
    return EXIT_SUCCESS;
}
