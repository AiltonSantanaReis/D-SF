#pragma once

#include "aion/kernel/execution.hpp"
#include "aion/kernel/spatial.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace aion {

enum class SpatialBackend : std::uint8_t {
    WideSah = 0,
    Morton = 1,
};

struct SpatialCostObservation {
    double update_ms{};
    double sampled_query_ms{};
    std::size_t sampled_queries{};
};

struct SpatialCostDecision {
    SpatialBackend backend{SpatialBackend::Morton};
    double predicted_sah_ms{};
    double predicted_morton_ms{};
    bool valid{false};
    std::string reason;
};

// Reference cost policy. It deliberately uses measured work from the current regime instead of
// hard-coded churn/debt thresholds. Query cost is extrapolated linearly from a representative sample.
[[nodiscard]] SpatialCostDecision choose_spatial_backend(
    const SpatialCostObservation& sah,
    const SpatialCostObservation& morton,
    std::size_t expected_queries);

struct AsyncSahBuildStats {
    std::uint64_t source_version{};
    std::uint64_t source_structure_revision{};
    std::size_t source_records{};
    double snapshot_copy_ms{};
    double background_build_ms{};
    double catchup_refit_ms{};
};

enum class AsyncSahPromoteState : std::uint8_t {
    Idle = 0,
    Building = 1,
    Promoted = 2,
    Discarded = 3,
    Failed = 4,
};

struct AsyncSahPromoteResult {
    AsyncSahPromoteState state{AsyncSahPromoteState::Idle};
    AsyncSahBuildStats stats{};
    std::string error;
};

// R3.1 reference implementation retained as an oracle for the R3.2 scheduler experiment.
class AsyncSahBuilder final {
public:
    AsyncSahBuilder();
    ~AsyncSahBuilder();
    AsyncSahBuilder(AsyncSahBuilder&&) noexcept;
    AsyncSahBuilder& operator=(AsyncSahBuilder&&) noexcept;
    AsyncSahBuilder(const AsyncSahBuilder&) = delete;
    AsyncSahBuilder& operator=(const AsyncSahBuilder&) = delete;

    [[nodiscard]] bool start(const SpatialSnapshot& snapshot, std::string& error);
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool observe_changes(const SpatialChangeSet& changes, std::string& error);
    [[nodiscard]] AsyncSahPromoteResult try_promote(const SpatialSnapshot& snapshot, WideBvh8View& out);
    [[nodiscard]] AsyncSahPromoteResult wait_promote(const SpatialSnapshot& snapshot, WideBvh8View& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class BudgetedSahState : std::uint8_t {
    Idle = 0,
    Capturing = 1,
    Building = 2,
    Ready = 3,
    Promoted = 4,
    Discarded = 5,
    Failed = 6,
};

struct BudgetedSahBacklog {
    std::size_t raw_dirty_values{};
    std::size_t dirty_leaf_nodes{};
    std::size_t dirty_internal_nodes{};

    [[nodiscard]] std::size_t total_items() const noexcept {
        return raw_dirty_values + dirty_leaf_nodes + dirty_internal_nodes;
    }
};

struct BudgetedSahStats {
    std::uint64_t source_version{};
    std::uint64_t source_structure_revision{};
    std::size_t source_records{};
    std::size_t slices{};
    std::size_t primitive_visits{};
    double granted_ms{};
    double actual_ms{};
    double max_slice_ms{};
    double max_capture_slice_ms{};
    double max_build_slice_ms{};
    double max_reserve_slice_ms{};
    double max_init_slice_ms{};
    double max_root_slice_ms{};
    double max_node_slice_ms{};
    double max_catchup_slice_ms{};
    double capture_ms{};
    double build_ms{};
    double catchup_ms{};
    std::size_t observed_dirty_values{};
    std::size_t mapped_dirty_values{};
    std::size_t refit_leaf_children{};
    std::size_t refit_internal_nodes{};
};

struct BudgetedSahPromoteResult {
    BudgetedSahState state{BudgetedSahState::Idle};
    BudgetedSahStats stats{};
    std::string error;
};

// R3.2 cooperative builder. Every expensive O(N) phase is resumable in small primitive chunks.
// No worker thread is required: the ExecutionBudgetScheduler grants only frame slack to this job.
class BudgetedSahBuilder final {
public:
    explicit BudgetedSahBuilder(std::size_t leaf_size = 8, std::size_t chunk_primitives = 256);
    ~BudgetedSahBuilder();
    BudgetedSahBuilder(BudgetedSahBuilder&&) noexcept;
    BudgetedSahBuilder& operator=(BudgetedSahBuilder&&) noexcept;
    BudgetedSahBuilder(const BudgetedSahBuilder&) = delete;
    BudgetedSahBuilder& operator=(const BudgetedSahBuilder&) = delete;

    // Starts a generation without copying the full spatial snapshot. Capture itself is cooperative.
    [[nodiscard]] bool start(const SpatialSnapshot& snapshot, std::string& error);

    // Must observe every publication in order while capture/build is active.
    [[nodiscard]] bool observe_changes(const SpatialChangeSet& changes, std::string& error);

    // Cooperative task adapter used by ExecutionBudgetScheduler.
    [[nodiscard]] CooperativeTaskProgress run_slice(const SpatialSnapshot& snapshot, double budget_ms);

    [[nodiscard]] BudgetedSahPromoteResult try_promote(const SpatialSnapshot& snapshot, WideBvh8View& out);
    [[nodiscard]] BudgetedSahState state() const noexcept;
    [[nodiscard]] const BudgetedSahStats& stats() const noexcept;
    [[nodiscard]] BudgetedSahBacklog backlog() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aion
