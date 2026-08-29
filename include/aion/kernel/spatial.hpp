#pragma once

#include "aion/kernel/math.hpp"
#include "aion/kernel/patch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aion {

struct SpatialRecord {
    EntityId entity{};
    Vec3 center{};
    Vec3 half_extent{0.5F, 0.5F, 0.5F};
};

struct SpatialSlotRange {
    std::uint32_t first{};
    std::uint32_t count{};
};

struct SpatialChangeSet {
    std::uint64_t from_version{};
    std::uint64_t to_version{};
    std::vector<std::uint32_t> dirty_slots;       // sparse lane
    std::vector<SpatialSlotRange> dirty_ranges;  // dense/clustered lane
    bool requires_rebuild{false};
    bool changed{false};
    float mean_displacement{};
    float max_displacement{};
    float normalized_motion{}; // mean displacement / snapshot coherence scale
    float topology_debt_delta{}; // changed fraction * normalized_motion

    [[nodiscard]] std::size_t dirty_value_count() const noexcept {
        std::size_t total = dirty_slots.size();
        for (const auto& range : dirty_ranges) total += range.count;
        return total;
    }
};

struct SpatialApplyResult {
    bool ok{false};
    std::string error;
    SpatialChangeSet changes;
};

// Shared, derived, data-oriented spatial state. It is not authoritative World state.
// All query views reference this storage instead of owning per-object AABB copies.
class SpatialSnapshot final {
    friend class BudgetedSahBuilder;
public:
    static constexpr std::uint32_t kInvalidSlot = std::numeric_limits<std::uint32_t>::max();

    [[nodiscard]] bool build(std::span<const SpatialRecord> records, std::string& error);
    [[nodiscard]] SpatialApplyResult apply_patch_transaction(const PatchTransaction& transaction);

    [[nodiscard]] std::size_t size() const noexcept { return record_count_; }
    [[nodiscard]] bool empty() const noexcept { return record_count_ == 0; }
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    [[nodiscard]] std::uint64_t structure_revision() const noexcept { return structure_revision_; }

    [[nodiscard]] EntityId entity(std::uint32_t slot) const noexcept { return dense_identity_ ? static_cast<EntityId>(slot) + 1U : entity_ids_[slot]; }
    [[nodiscard]] std::uint32_t slot_of(EntityId entity) const noexcept;
    [[nodiscard]] Vec3 center(std::uint32_t slot) const noexcept;
    [[nodiscard]] Vec3 half_extent(std::uint32_t slot) const noexcept;
    [[nodiscard]] Aabb bounds(std::uint32_t slot) const noexcept;

    [[nodiscard]] std::size_t storage_bytes() const noexcept;
    [[nodiscard]] std::size_t object_payload_bytes() const noexcept;
    [[nodiscard]] float coherence_scale() const noexcept { return coherence_scale_; }

private:
    [[nodiscard]] bool validate_position_write(EntityId entity, Vec3 value, std::string& error) const noexcept;
    void write_center(std::uint32_t slot, Vec3 value) noexcept;

    std::uint64_t version_{0};
    std::uint64_t structure_revision_{0};
    std::size_t record_count_{0};
    bool dense_identity_{false};
    float coherence_scale_{1.0F};
    std::vector<EntityId> entity_ids_;

    std::vector<float> center_x_;
    std::vector<float> center_y_;
    std::vector<float> center_z_;
    std::vector<float> half_x_;
    std::vector<float> half_y_;
    std::vector<float> half_z_;
};

struct SpatialQueryResult {
    bool ok{false};
    std::string error;
    std::vector<EntityId> entities;
};

struct SpatialRayResult {
    bool ok{false};
    std::string error;
    EntityId entity{};
    float t{std::numeric_limits<float>::infinity()};
};

// Correctness oracle only; intentionally O(N) and never a production candidate.
class SpatialOracle final {
public:
    [[nodiscard]] static std::vector<EntityId> query_aabb(const SpatialSnapshot& snapshot, const Aabb& query);
    [[nodiscard]] static SpatialRayResult raycast(const SpatialSnapshot& snapshot, const Ray& ray);
};

enum class WideBvhSyncMode : std::uint8_t {
    Automatic = 0,
    RefitOnly = 1,
    Rebuild = 2,
};

class WideBvh8View final {
    friend class BudgetedSahBuilder;
public:
    explicit WideBvh8View(std::size_t leaf_size = 8);

    [[nodiscard]] bool build(const SpatialSnapshot& snapshot, std::string& error);
    [[nodiscard]] bool sync(const SpatialSnapshot& snapshot, const SpatialChangeSet& changes, std::string& error,
                            WideBvhSyncMode mode = WideBvhSyncMode::Automatic);
    // Catch an asynchronously built topology up to a newer data version without replaying every intermediate delta.
    // Valid only while entity/slot structure is unchanged. Runs in O(primitives + nodes).
    [[nodiscard]] bool full_refit_to(const SpatialSnapshot& snapshot, std::string& error);
    [[nodiscard]] SpatialQueryResult query_aabb(const SpatialSnapshot& snapshot, const Aabb& query) const;
    [[nodiscard]] SpatialRayResult raycast(const SpatialSnapshot& snapshot, const Ray& ray) const;

    [[nodiscard]] std::uint64_t snapshot_version() const noexcept { return snapshot_version_; }
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }
    [[nodiscard]] std::size_t primitive_reference_count() const noexcept { return primitive_slots_.size(); }

private:
    struct Child {
        Aabb bounds{};
        std::uint32_t index{}; // node index for internal, primitive offset for leaf
        std::uint32_t count{}; // >0 for leaf
    };

    struct Node {
        std::array<Child, 8> children{};
        std::uint32_t parent{std::numeric_limits<std::uint32_t>::max()};
        std::uint8_t parent_child{};
        std::uint8_t child_count{};
        std::uint16_t depth{};
    };

    struct LeafLocation {
        std::uint32_t node{std::numeric_limits<std::uint32_t>::max()};
        std::uint8_t child{};
    };

    struct Range { std::size_t begin{}; std::size_t end{}; };

    [[nodiscard]] std::uint32_t build_node(const SpatialSnapshot& snapshot, Range range,
                                           std::uint32_t parent, std::uint8_t parent_child,
                                           std::uint16_t depth);
    [[nodiscard]] bool split_sah(const SpatialSnapshot& snapshot, Range range, Range& left, Range& right);
    [[nodiscard]] Aabb range_bounds(const SpatialSnapshot& snapshot, Range range) const noexcept;
    [[nodiscard]] Aabb range_centroid_bounds(const SpatialSnapshot& snapshot, Range range) const noexcept;
    [[nodiscard]] Aabb leaf_bounds(const SpatialSnapshot& snapshot, const Child& child) const noexcept;
    [[nodiscard]] Aabb node_bounds(std::uint32_t node_index) const noexcept;
    void refit(const SpatialSnapshot& snapshot, const SpatialChangeSet& changes);

    std::size_t leaf_size_{8};
    std::uint64_t snapshot_version_{0};
    std::uint64_t snapshot_structure_revision_{0};
    std::vector<std::uint32_t> working_slots_;
    std::vector<std::uint32_t> primitive_slots_;
    std::vector<Node> nodes_;
    std::vector<LeafLocation> leaf_locations_;
};

// Rebuild-oriented wide BVH generated from Morton order. Intended for high-churn regimes.
class MortonBvh8View final {
public:
    explicit MortonBvh8View(std::size_t leaf_size = 8);

    [[nodiscard]] bool build(const SpatialSnapshot& snapshot, std::string& error);
    [[nodiscard]] bool sync(const SpatialSnapshot& snapshot, const SpatialChangeSet& changes, std::string& error);
    [[nodiscard]] SpatialQueryResult query_aabb(const SpatialSnapshot& snapshot, const Aabb& query) const;
    [[nodiscard]] SpatialRayResult raycast(const SpatialSnapshot& snapshot, const Ray& ray) const;

    [[nodiscard]] std::uint64_t snapshot_version() const noexcept { return snapshot_version_; }
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }

private:
    struct Child {
        Aabb bounds{};
        std::uint32_t index{};
        std::uint32_t count{};
    };
    struct Node {
        std::array<Child, 8> children{};
        std::uint8_t child_count{};
    };

    [[nodiscard]] Aabb node_bounds(std::uint32_t node_index) const noexcept;

    std::size_t leaf_size_{8};
    std::uint64_t snapshot_version_{0};
    std::uint64_t snapshot_structure_revision_{0};
    std::vector<std::uint32_t> primitive_slots_;
    std::vector<Node> nodes_;
    std::uint32_t root_{std::numeric_limits<std::uint32_t>::max()};
};

[[nodiscard]] bool aabb_intersects(const Aabb& a, const Aabb& b) noexcept;
[[nodiscard]] float aabb_surface_area(const Aabb& a) noexcept;
[[nodiscard]] bool ray_intersects_aabb(const Ray& ray, const Aabb& box, float& t) noexcept;

} // namespace aion
