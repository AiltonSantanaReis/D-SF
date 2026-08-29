#include "aion/kernel/spatial_fabric.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <utility>

namespace aion {
namespace {
using Clock = std::chrono::steady_clock;

template <class F>
[[nodiscard]] double elapsed_ms(F&& fn) {
    const auto begin = Clock::now();
    fn();
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}
}

SpatialCostDecision choose_spatial_backend(
    const SpatialCostObservation& sah,
    const SpatialCostObservation& morton,
    std::size_t expected_queries) {
    SpatialCostDecision result;
    if (sah.sampled_queries == 0 || morton.sampled_queries == 0 ||
        !std::isfinite(sah.update_ms) || !std::isfinite(morton.update_ms) ||
        !std::isfinite(sah.sampled_query_ms) || !std::isfinite(morton.sampled_query_ms) ||
        sah.update_ms < 0.0 || morton.update_ms < 0.0 ||
        sah.sampled_query_ms < 0.0 || morton.sampled_query_ms < 0.0) {
        result.reason = "cost observation is incomplete or invalid";
        return result;
    }

    const double sah_per_query = sah.sampled_query_ms / static_cast<double>(sah.sampled_queries);
    const double morton_per_query = morton.sampled_query_ms / static_cast<double>(morton.sampled_queries);
    result.predicted_sah_ms = sah.update_ms + sah_per_query * static_cast<double>(expected_queries);
    result.predicted_morton_ms = morton.update_ms + morton_per_query * static_cast<double>(expected_queries);
    result.backend = result.predicted_sah_ms <= result.predicted_morton_ms
        ? SpatialBackend::WideSah : SpatialBackend::Morton;
    result.valid = true;
    result.reason = "measured update cost + sampled query cost extrapolation";
    return result;
}

struct AsyncSahBuilder::Impl {
    struct SharedState {
        mutable std::mutex mutex;
        bool done{false};
        bool ok{false};
        std::string error;
        std::optional<WideBvh8View> candidate;
        AsyncSahBuildStats stats;
    };

    std::shared_ptr<SharedState> state;
    std::jthread worker;
    std::uint64_t observed_version{0};
    bool observed_chain_valid{true};
    bool structural_change_seen{false};
    std::vector<std::uint32_t> observed_slots;
    std::vector<SpatialSlotRange> observed_ranges;

    [[nodiscard]] SpatialChangeSet coalesced_changes(std::uint64_t source_version, std::uint64_t target_version) {
        SpatialChangeSet out;
        out.from_version=source_version; out.to_version=target_version; out.changed=!observed_slots.empty()||!observed_ranges.empty();
        std::sort(observed_slots.begin(),observed_slots.end());
        observed_slots.erase(std::unique(observed_slots.begin(),observed_slots.end()),observed_slots.end());
        std::sort(observed_ranges.begin(),observed_ranges.end(),[](const auto&a,const auto&b){return a.first<b.first || (a.first==b.first&&a.count<b.count);});
        for(const auto&r:observed_ranges){
            if(r.count==0)continue;
            if(out.dirty_ranges.empty()){out.dirty_ranges.push_back(r);continue;}
            auto& back=out.dirty_ranges.back();
            const std::uint64_t back_end=static_cast<std::uint64_t>(back.first)+back.count;
            const std::uint64_t cur_end=static_cast<std::uint64_t>(r.first)+r.count;
            if(static_cast<std::uint64_t>(r.first)<=back_end){
                const auto merged_end=std::max(back_end,cur_end);
                back.count=static_cast<std::uint32_t>(merged_end-back.first);
            }else out.dirty_ranges.push_back(r);
        }
        // Remove sparse slots already covered by a range.
        for(auto slot:observed_slots){
            bool covered=false;
            for(const auto&r:out.dirty_ranges){if(slot<r.first)break;if(static_cast<std::uint64_t>(slot)<static_cast<std::uint64_t>(r.first)+r.count){covered=true;break;}}
            if(!covered)out.dirty_slots.push_back(slot);
        }
        return out;
    }

    [[nodiscard]] AsyncSahPromoteResult promote(const SpatialSnapshot& snapshot, WideBvh8View& out, bool wait) {
        AsyncSahPromoteResult result;
        if (!state) {
            result.state = AsyncSahPromoteState::Idle;
            return result;
        }
        if (wait && worker.joinable()) worker.join();

        std::optional<WideBvh8View> candidate;
        {
            std::scoped_lock lock(state->mutex);
            result.stats = state->stats;
            if (!state->done) {
                result.state = AsyncSahPromoteState::Building;
                return result;
            }
            if (!state->ok || !state->candidate) {
                result.state = AsyncSahPromoteState::Failed;
                result.error = state->error.empty() ? "background SAH build failed" : state->error;
                return result;
            }
            candidate.emplace(std::move(*state->candidate));
            state->candidate.reset();
        }

        if (result.stats.source_structure_revision != snapshot.structure_revision() ||
            result.stats.source_records != snapshot.size()) {
            result.state = AsyncSahPromoteState::Discarded;
            result.error = "async SAH candidate belongs to an obsolete spatial structure generation";
            return result;
        }

        std::string error;
        result.stats.catchup_refit_ms = elapsed_ms([&] {
            if (structural_change_seen || !observed_chain_valid) { candidate.reset(); error="async SAH change journal is structurally invalid or discontinuous"; return; }
            if (observed_version == snapshot.version()) {
                auto catchup=coalesced_changes(result.stats.source_version,snapshot.version());
                if (!candidate->sync(snapshot,catchup,error,WideBvhSyncMode::RefitOnly)) candidate.reset();
            } else {
                // Missing publications: correctness-first fallback remains a full O(N) catch-up.
                if (!candidate->full_refit_to(snapshot,error)) candidate.reset();
            }
        });
        if (!candidate) {
            result.state = AsyncSahPromoteState::Failed;
            result.error = error.empty() ? "async SAH catch-up failed" : error;
            return result;
        }

        out = std::move(*candidate);
        result.state = AsyncSahPromoteState::Promoted;
        return result;
    }
};

AsyncSahBuilder::AsyncSahBuilder() : impl_(std::make_unique<Impl>()) {}
AsyncSahBuilder::~AsyncSahBuilder() = default;
AsyncSahBuilder::AsyncSahBuilder(AsyncSahBuilder&&) noexcept = default;
AsyncSahBuilder& AsyncSahBuilder::operator=(AsyncSahBuilder&&) noexcept = default;

bool AsyncSahBuilder::start(const SpatialSnapshot& snapshot, std::string& error) {
    error.clear();
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (busy()) {
        error = "async SAH builder already has work in flight";
        return false;
    }
    if (impl_->worker.joinable()) impl_->worker.join();

    auto state = std::make_shared<Impl::SharedState>();
    SpatialSnapshot frozen;
    state->stats.snapshot_copy_ms = elapsed_ms([&] { frozen = snapshot; });
    state->stats.source_version = snapshot.version();
    state->stats.source_structure_revision = snapshot.structure_revision();
    state->stats.source_records = snapshot.size();
    impl_->state = state;
    impl_->observed_version = snapshot.version();
    impl_->observed_chain_valid = true;
    impl_->structural_change_seen = false;
    impl_->observed_slots.clear();
    impl_->observed_ranges.clear();

    impl_->worker = std::jthread([state, frozen = std::move(frozen)]() mutable {
        WideBvh8View candidate;
        std::string build_error;
        const double build_ms = elapsed_ms([&] { (void)candidate.build(frozen, build_error); });
        std::scoped_lock lock(state->mutex);
        state->stats.background_build_ms = build_ms;
        state->error = std::move(build_error);
        state->ok = state->error.empty();
        if (state->ok) state->candidate.emplace(std::move(candidate));
        state->done = true;
    });
    return true;
}

bool AsyncSahBuilder::busy() const {
    if (!impl_ || !impl_->state) return false;
    std::scoped_lock lock(impl_->state->mutex);
    return !impl_->state->done;
}

bool AsyncSahBuilder::ready() const {
    if (!impl_ || !impl_->state) return false;
    std::scoped_lock lock(impl_->state->mutex);
    return impl_->state->done;
}

bool AsyncSahBuilder::observe_changes(const SpatialChangeSet& changes, std::string& error) {
    error.clear();
    if (!impl_ || !impl_->state) { error="async SAH builder has no active generation"; return false; }
    if (changes.from_version != impl_->observed_version) { impl_->observed_chain_valid=false; error="async SAH observed a discontinuous spatial publication chain"; return false; }
    if (changes.requires_rebuild) impl_->structural_change_seen=true;
    impl_->observed_slots.insert(impl_->observed_slots.end(),changes.dirty_slots.begin(),changes.dirty_slots.end());
    impl_->observed_ranges.insert(impl_->observed_ranges.end(),changes.dirty_ranges.begin(),changes.dirty_ranges.end());
    impl_->observed_version=changes.to_version;
    return true;
}

AsyncSahPromoteResult AsyncSahBuilder::try_promote(const SpatialSnapshot& snapshot, WideBvh8View& out) {
    return impl_->promote(snapshot, out, false);
}

AsyncSahPromoteResult AsyncSahBuilder::wait_promote(const SpatialSnapshot& snapshot, WideBvh8View& out) {
    return impl_->promote(snapshot, out, true);
}



namespace {
[[nodiscard]] Aabb budget_empty_aabb() noexcept {
    const float inf = std::numeric_limits<float>::infinity();
    return {{inf, inf, inf}, {-inf, -inf, -inf}};
}

[[nodiscard]] Aabb budget_merge(Aabb a, const Aabb& b) noexcept {
    a.min.x = std::min(a.min.x, b.min.x); a.min.y = std::min(a.min.y, b.min.y); a.min.z = std::min(a.min.z, b.min.z);
    a.max.x = std::max(a.max.x, b.max.x); a.max.y = std::max(a.max.y, b.max.y); a.max.z = std::max(a.max.z, b.max.z);
    return a;
}

[[nodiscard]] float budget_axis(const Vec3& v, int axis) noexcept {
    if (axis == 0) return v.x;
    if (axis == 1) return v.y;
    return v.z;
}
}

struct BudgetedSahBuilder::Impl {
    static constexpr std::size_t kBins = 16;

    enum class BuildStage : std::uint8_t {
        Capture = 0,
        CaptureCatchup = 1,
        InitReserve = 2,
        InitArrays = 3,
        RootBounds = 4,
        Nodes = 5,
        CatchupMap = 6,
        CatchupLeaves = 7,
        CatchupNodes = 8,
        Ready = 9,
        Failed = 10,
        Discarded = 11,
    };

    struct Group {
        std::size_t begin{};
        std::size_t end{};
        Aabb bounds{budget_empty_aabb()};
    };

    struct Bin {
        Aabb bounds{budget_empty_aabb()};
        std::size_t count{};
    };

    struct SplitState {
        enum class Phase : std::uint8_t { Centroid = 0, Bins = 1, Scatter = 2, Copy = 3, Done = 4 };
        Phase phase{Phase::Centroid};
        std::size_t group_index{};
        Group source{};
        std::size_t cursor{};
        Aabb centroid_bounds{budget_empty_aabb()};
        std::array<std::array<Bin, kBins>, 3> bins{};
        int axis{-1};
        int split{-1};
        float threshold{};
        bool fallback{false};
        std::size_t left_count{};
        std::size_t left_write{};
        std::size_t right_write{};
        Aabb left_bounds{budget_empty_aabb()};
        Aabb right_bounds{budget_empty_aabb()};
    };

    struct NodeTask {
        enum class Phase : std::uint8_t { Choose = 0, Split = 1, Finalize = 2 };
        std::uint32_t node_index{};
        std::size_t begin{};
        std::size_t end{};
        std::uint16_t depth{};
        std::vector<Group> groups;
        Phase phase{Phase::Choose};
        SplitState split{};
    };

    explicit Impl(std::size_t leaf, std::size_t chunk)
        : leaf_size(std::max<std::size_t>(1, leaf)), chunk_primitives(std::max<std::size_t>(64, chunk)), candidate(leaf_size) {}

    std::size_t leaf_size{8};
    std::size_t chunk_primitives{2048};
    BuildStage stage{BuildStage::Capture};
    BudgetedSahState public_state{BudgetedSahState::Idle};
    BudgetedSahStats stats{};
    std::string error;

    SpatialSnapshot frozen;
    WideBvh8View candidate;
    std::vector<std::uint32_t> partition_scratch;
    std::vector<NodeTask> tasks;

    std::uint64_t initial_version{};
    std::uint64_t observed_version{};
    std::uint64_t structure_revision{};
    std::size_t record_count{};
    bool observed_chain_valid{true};
    bool structural_change_seen{false};

    std::vector<std::uint32_t> capture_dirty_slots;
    std::vector<SpatialSlotRange> capture_dirty_ranges;
    std::size_t capture_dirty_slot_cursor{};
    std::size_t capture_dirty_range_cursor{};
    std::uint32_t capture_dirty_range_offset{};

    // Continuous maintenance queue. Raw R2.1 changes are mapped cooperatively to dirty leaf
    // children; leaf work then enqueues ancestors by depth. Backlog is explicit and bounded by the
    // maintenance service rate instead of forcing whole-tree catch-up cycles.
    std::vector<std::uint32_t> build_dirty_slots;
    std::vector<SpatialSlotRange> build_dirty_ranges;
    std::size_t build_dirty_slot_cursor{};
    std::size_t build_dirty_range_cursor{};
    std::uint32_t build_dirty_range_offset{};
    std::vector<std::uint8_t> dirty_leaf_masks;
    std::vector<std::uint8_t> leaf_queued;
    std::vector<std::uint32_t> dirty_leaf_queue;
    std::vector<std::uint8_t> dirty_node_marks;
    using DirtyNodeItem = std::pair<std::uint16_t, std::uint32_t>;
    std::priority_queue<DirtyNodeItem> dirty_node_queue;

    std::size_t capture_cursor{};
    std::size_t reserve_phase{};
    std::size_t init_cursor{};
    std::size_t root_cursor{};
    Aabb root_bounds{budget_empty_aabb()};

    [[nodiscard]] bool active() const noexcept {
        return public_state == BudgetedSahState::Capturing || public_state == BudgetedSahState::Building || public_state == BudgetedSahState::Ready;
    }

    void fail(std::string message) {
        error = std::move(message);
        stage = BuildStage::Failed;
        public_state = BudgetedSahState::Failed;
    }

    void discard(std::string message) {
        error = std::move(message);
        stage = BuildStage::Discarded;
        public_state = BudgetedSahState::Discarded;
    }

    [[nodiscard]] bool structure_matches(const SpatialSnapshot& snapshot) const noexcept {
        return snapshot.structure_revision() == structure_revision && snapshot.size() == record_count;
    }

    void initialize_frozen_metadata(const SpatialSnapshot& snapshot) {
        frozen = SpatialSnapshot{};
        frozen.version_ = initial_version;
        frozen.structure_revision_ = structure_revision;
        frozen.record_count_ = record_count;
        frozen.dense_identity_ = snapshot.dense_identity_;
        frozen.coherence_scale_ = snapshot.coherence_scale_;
        frozen.entity_ids_.clear();
        if (!frozen.dense_identity_) frozen.entity_ids_.reserve(record_count);
        frozen.center_x_.clear(); frozen.center_y_.clear(); frozen.center_z_.clear();
        frozen.half_x_.clear(); frozen.half_y_.clear(); frozen.half_z_.clear();
        frozen.center_x_.reserve(record_count); frozen.center_y_.reserve(record_count); frozen.center_z_.reserve(record_count);
        frozen.half_x_.reserve(record_count); frozen.half_y_.reserve(record_count); frozen.half_z_.reserve(record_count);
    }

    [[nodiscard]] bool capture_one_chunk(const SpatialSnapshot& snapshot, std::size_t& visits) {
        const auto end = std::min(record_count, capture_cursor + chunk_primitives);
        for (std::size_t i = capture_cursor; i < end; ++i) {
            if (!frozen.dense_identity_) frozen.entity_ids_.push_back(snapshot.entity_ids_[i]);
            frozen.center_x_.push_back(snapshot.center_x_[i]); frozen.center_y_.push_back(snapshot.center_y_[i]); frozen.center_z_.push_back(snapshot.center_z_[i]);
            frozen.half_x_.push_back(snapshot.half_x_[i]); frozen.half_y_.push_back(snapshot.half_y_[i]); frozen.half_z_.push_back(snapshot.half_z_[i]);
        }
        visits += end - capture_cursor;
        capture_cursor = end;
        if (capture_cursor == record_count) stage = BuildStage::CaptureCatchup;
        return true;
    }

    [[nodiscard]] bool capture_catchup_one_chunk(const SpatialSnapshot& snapshot, std::size_t& visits) {
        std::size_t remaining = chunk_primitives;
        while (remaining != 0 && capture_dirty_slot_cursor < capture_dirty_slots.size()) {
            const auto slot = capture_dirty_slots[capture_dirty_slot_cursor++];
            if (slot >= record_count) { fail("capture catch-up contains out-of-range slot"); return false; }
            frozen.write_center(slot, snapshot.center(slot));
            ++visits; --remaining;
        }
        while (remaining != 0 && capture_dirty_range_cursor < capture_dirty_ranges.size()) {
            const auto& range = capture_dirty_ranges[capture_dirty_range_cursor];
            if (range.count == 0) { ++capture_dirty_range_cursor; capture_dirty_range_offset = 0; continue; }
            const std::uint64_t slot64 = static_cast<std::uint64_t>(range.first) + capture_dirty_range_offset;
            if (slot64 >= record_count) { fail("capture catch-up range exceeds snapshot"); return false; }
            const auto slot = static_cast<std::uint32_t>(slot64);
            frozen.write_center(slot, snapshot.center(slot));
            ++capture_dirty_range_offset; ++visits; --remaining;
            if (capture_dirty_range_offset == range.count) { ++capture_dirty_range_cursor; capture_dirty_range_offset = 0; }
        }

        if (capture_dirty_slot_cursor == capture_dirty_slots.size() &&
            capture_dirty_range_cursor == capture_dirty_ranges.size() &&
            observed_version == snapshot.version()) {
            frozen.version_ = snapshot.version();
            stats.source_version = frozen.version_;
            capture_dirty_slots.clear(); capture_dirty_ranges.clear();
            capture_dirty_slot_cursor = 0; capture_dirty_range_cursor = 0; capture_dirty_range_offset = 0;
            build_dirty_slots.clear(); build_dirty_ranges.clear();
            build_dirty_slot_cursor = 0; build_dirty_range_cursor = 0; build_dirty_range_offset = 0;
            stage = BuildStage::InitReserve;
            public_state = BudgetedSahState::Building;

            candidate = WideBvh8View(leaf_size);
            candidate.snapshot_version_ = frozen.version_;
            candidate.snapshot_structure_revision_ = frozen.structure_revision_;
            candidate.working_slots_.clear();
            candidate.primitive_slots_.clear();
            candidate.nodes_.clear();
            candidate.leaf_locations_.clear();
            partition_scratch.clear();
            reserve_phase = 0;
            init_cursor = 0;
        }
        return true;
    }

    void init_reserve_one() {
        switch (reserve_phase) {
            case 0: candidate.working_slots_.reserve(record_count); break;
            case 1: candidate.primitive_slots_.reserve(record_count); break;
            case 2: candidate.leaf_locations_.reserve(record_count); break;
            case 3: partition_scratch.reserve(record_count); break;
            case 4: {
                const auto estimate = record_count / std::max<std::size_t>(1, leaf_size * 2U) + 2048U;
                candidate.nodes_.reserve(estimate);
                break;
            }
            default: stage = BuildStage::InitArrays; return;
        }
        ++reserve_phase;
        if (reserve_phase > 4U) stage = BuildStage::InitArrays;
    }

    void init_arrays_one_chunk(std::size_t& visits) {
        const auto end = std::min(record_count, init_cursor + chunk_primitives);
        for (std::size_t i = init_cursor; i < end; ++i) {
            candidate.working_slots_.push_back(static_cast<std::uint32_t>(i));
            partition_scratch.push_back(0U);
            candidate.leaf_locations_.push_back({});
        }
        visits += end - init_cursor;
        init_cursor = end;
        if (init_cursor == record_count) {
            if (record_count == 0) {
                stage = BuildStage::CatchupMap;
            } else {
                root_cursor = 0;
                root_bounds = budget_empty_aabb();
                stage = BuildStage::RootBounds;
            }
        }
    }

    void root_bounds_one_chunk(std::size_t& visits) {
        const auto end = std::min(record_count, root_cursor + chunk_primitives);
        for (std::size_t i = root_cursor; i < end; ++i) root_bounds = budget_merge(root_bounds, frozen.bounds(static_cast<std::uint32_t>(i)));
        visits += end - root_cursor;
        root_cursor = end;
        if (root_cursor == record_count) {
            candidate.nodes_.push_back({});
            candidate.nodes_[0].parent = std::numeric_limits<std::uint32_t>::max();
            candidate.nodes_[0].parent_child = 0;
            candidate.nodes_[0].depth = 0;
            NodeTask root{};
            root.node_index = 0; root.begin = 0; root.end = record_count; root.depth = 0;
            root.groups.push_back({0, record_count, root_bounds});
            tasks.push_back(std::move(root));
            stage = BuildStage::Nodes;
        }
    }

    [[nodiscard]] std::size_t choose_group(const NodeTask& task) const noexcept {
        std::size_t chosen = task.groups.size();
        float best_cost = -1.0F;
        for (std::size_t i = 0; i < task.groups.size(); ++i) {
            const auto count = task.groups[i].end - task.groups[i].begin;
            if (count <= leaf_size) continue;
            const float cost = aabb_surface_area(task.groups[i].bounds) * static_cast<float>(count);
            if (cost > best_cost) { best_cost = cost; chosen = i; }
        }
        return chosen;
    }

    void begin_split(NodeTask& task, std::size_t group_index) {
        task.split = SplitState{};
        task.split.group_index = group_index;
        task.split.source = task.groups[group_index];
        task.split.cursor = task.split.source.begin;
        task.phase = NodeTask::Phase::Split;
    }

    void split_centroid_chunk(NodeTask& task, std::size_t& visits) {
        auto& split = task.split;
        const auto end = std::min(split.source.end, split.cursor + chunk_primitives);
        for (std::size_t i = split.cursor; i < end; ++i) {
            const auto c = frozen.center(candidate.working_slots_[i]);
            split.centroid_bounds = budget_merge(split.centroid_bounds, {c, c});
        }
        visits += end - split.cursor;
        split.cursor = end;
        if (split.cursor != split.source.end) return;

        bool any_extent = false;
        for (int axis = 0; axis < 3; ++axis)
            any_extent = any_extent || (budget_axis(split.centroid_bounds.max, axis) - budget_axis(split.centroid_bounds.min, axis) > 1.0e-9F);
        if (!any_extent) {
            split.fallback = true;
            split.left_count = (split.source.end - split.source.begin) / 2U;
            split.cursor = split.source.begin;
            split.left_write = split.source.begin;
            split.right_write = split.source.begin + split.left_count;
            split.phase = SplitState::Phase::Scatter;
            return;
        }
        split.cursor = split.source.begin;
        split.phase = SplitState::Phase::Bins;
    }

    void split_bins_chunk(NodeTask& task, std::size_t& visits) {
        auto& split = task.split;
        const auto end = std::min(split.source.end, split.cursor + chunk_primitives);
        for (std::size_t i = split.cursor; i < end; ++i) {
            const auto slot = candidate.working_slots_[i];
            const auto c = frozen.center(slot);
            const auto b = frozen.bounds(slot);
            for (int axis = 0; axis < 3; ++axis) {
                const float cmin = budget_axis(split.centroid_bounds.min, axis);
                const float cmax = budget_axis(split.centroid_bounds.max, axis);
                const float extent = cmax - cmin;
                if (extent <= 1.0e-9F) continue;
                int bi = static_cast<int>(((budget_axis(c, axis) - cmin) / extent) * static_cast<float>(kBins));
                bi = std::clamp(bi, 0, static_cast<int>(kBins) - 1);
                auto& bin = split.bins[static_cast<std::size_t>(axis)][static_cast<std::size_t>(bi)];
                ++bin.count;
                bin.bounds = budget_merge(bin.bounds, b);
            }
        }
        visits += end - split.cursor;
        split.cursor = end;
        if (split.cursor != split.source.end) return;

        float best_cost = std::numeric_limits<float>::infinity();
        std::size_t best_left_count = 0;
        int best_axis = -1, best_split = -1;
        for (int axis = 0; axis < 3; ++axis) {
            const float cmin = budget_axis(split.centroid_bounds.min, axis);
            const float cmax = budget_axis(split.centroid_bounds.max, axis);
            if (cmax - cmin <= 1.0e-9F) continue;
            std::array<Aabb, kBins> prefix_bounds{}, suffix_bounds{};
            std::array<std::size_t, kBins> prefix_count{}, suffix_count{};
            Aabb pb = budget_empty_aabb(); std::size_t pn = 0;
            for (std::size_t i = 0; i < kBins; ++i) {
                const auto& bin = split.bins[static_cast<std::size_t>(axis)][i];
                if (bin.count != 0) pb = budget_merge(pb, bin.bounds);
                pn += bin.count; prefix_bounds[i] = pb; prefix_count[i] = pn;
            }
            Aabb sb = budget_empty_aabb(); std::size_t sn = 0;
            for (std::size_t ri = kBins; ri-- > 0;) {
                const auto& bin = split.bins[static_cast<std::size_t>(axis)][ri];
                if (bin.count != 0) sb = budget_merge(sb, bin.bounds);
                sn += bin.count; suffix_bounds[ri] = sb; suffix_count[ri] = sn;
            }
            for (std::size_t si = 0; si + 1 < kBins; ++si) {
                const auto ln = prefix_count[si], rn = suffix_count[si + 1];
                if (ln == 0 || rn == 0) continue;
                const float cost = aabb_surface_area(prefix_bounds[si]) * static_cast<float>(ln) +
                                   aabb_surface_area(suffix_bounds[si + 1]) * static_cast<float>(rn);
                if (cost < best_cost) {
                    best_cost = cost; best_axis = axis; best_split = static_cast<int>(si); best_left_count = ln;
                }
            }
        }

        const auto count = split.source.end - split.source.begin;
        if (best_axis < 0 || best_left_count == 0 || best_left_count >= count) {
            split.fallback = true;
            split.left_count = count / 2U;
        } else {
            split.axis = best_axis; split.split = best_split; split.left_count = best_left_count;
            const float cmin = budget_axis(split.centroid_bounds.min, best_axis);
            const float cmax = budget_axis(split.centroid_bounds.max, best_axis);
            split.threshold = cmin + (cmax - cmin) * (static_cast<float>(best_split + 1) / static_cast<float>(kBins));
        }
        split.cursor = split.source.begin;
        split.left_write = split.source.begin;
        split.right_write = split.source.begin + split.left_count;
        split.phase = SplitState::Phase::Scatter;
    }

    void split_scatter_chunk(NodeTask& task, std::size_t& visits) {
        auto& split = task.split;
        const auto end = std::min(split.source.end, split.cursor + chunk_primitives);
        const auto fallback_mid = split.source.begin + split.left_count;
        for (std::size_t i = split.cursor; i < end; ++i) {
            const auto slot = candidate.working_slots_[i];
            const bool left = split.fallback ? (i < fallback_mid) : (budget_axis(frozen.center(slot), split.axis) < split.threshold);
            if (left) {
                split.left_bounds = budget_merge(split.left_bounds, frozen.bounds(slot));
                if (!split.fallback) partition_scratch[split.left_write++] = slot;
            } else {
                split.right_bounds = budget_merge(split.right_bounds, frozen.bounds(slot));
                if (!split.fallback) partition_scratch[split.right_write++] = slot;
            }
        }
        visits += end - split.cursor;
        split.cursor = end;
        if (split.cursor != split.source.end) return;
        if (!split.fallback && (split.left_write != split.source.begin + split.left_count || split.right_write != split.source.end)) {
            // Numerical threshold/bin disagreement: correctness-first deterministic fallback by current order.
            split.fallback = true;
            split.left_bounds = budget_empty_aabb(); split.right_bounds = budget_empty_aabb();
            split.cursor = split.source.begin;
            return;
        }
        if (split.fallback) {
            split.phase = SplitState::Phase::Done;
        } else {
            split.cursor = split.source.begin;
            split.phase = SplitState::Phase::Copy;
        }
    }

    void split_copy_chunk(NodeTask& task, std::size_t& visits) {
        auto& split = task.split;
        const auto end = std::min(split.source.end, split.cursor + chunk_primitives);
        std::copy(partition_scratch.begin() + static_cast<std::ptrdiff_t>(split.cursor),
                  partition_scratch.begin() + static_cast<std::ptrdiff_t>(end),
                  candidate.working_slots_.begin() + static_cast<std::ptrdiff_t>(split.cursor));
        visits += end - split.cursor;
        split.cursor = end;
        if (split.cursor == split.source.end) split.phase = SplitState::Phase::Done;
    }

    void finish_split(NodeTask& task) {
        auto& split = task.split;
        const auto mid = split.source.begin + split.left_count;
        if (mid <= split.source.begin || mid >= split.source.end) { fail("budgeted SAH produced invalid split"); return; }
        const Group left{split.source.begin, mid, split.left_bounds};
        const Group right{mid, split.source.end, split.right_bounds};
        task.groups[split.group_index] = left;
        task.groups.insert(task.groups.begin() + static_cast<std::ptrdiff_t>(split.group_index + 1), right);
        task.phase = NodeTask::Phase::Choose;
    }

    void finalize_node(NodeTask& task) {
        const auto node_index = task.node_index;
        auto groups = std::move(task.groups);
        std::vector<NodeTask> children;
        children.reserve(groups.size());
        for (std::size_t gi = 0; gi < groups.size(); ++gi) {
            const auto& g = groups[gi];
            WideBvh8View::Child child{};
            child.bounds = g.bounds;
            const auto count = g.end - g.begin;
            if (count <= leaf_size) {
                child.index = static_cast<std::uint32_t>(candidate.primitive_slots_.size());
                child.count = static_cast<std::uint32_t>(count);
                for (std::size_t i = g.begin; i < g.end; ++i) {
                    const auto slot = candidate.working_slots_[i];
                    candidate.primitive_slots_.push_back(slot);
                    candidate.leaf_locations_[slot] = {node_index, static_cast<std::uint8_t>(gi)};
                }
            } else {
                const auto child_index = static_cast<std::uint32_t>(candidate.nodes_.size());
                candidate.nodes_.push_back({});
                candidate.nodes_[child_index].parent = node_index;
                candidate.nodes_[child_index].parent_child = static_cast<std::uint8_t>(gi);
                candidate.nodes_[child_index].depth = static_cast<std::uint16_t>(task.depth + 1U);
                child.index = child_index;
                child.count = 0;
                NodeTask child_task{};
                child_task.node_index = child_index; child_task.begin = g.begin; child_task.end = g.end;
                child_task.depth = static_cast<std::uint16_t>(task.depth + 1U);
                child_task.groups.push_back(g);
                children.push_back(std::move(child_task));
            }
            candidate.nodes_[node_index].children[gi] = child;
        }
        candidate.nodes_[node_index].child_count = static_cast<std::uint8_t>(groups.size());
        tasks.pop_back();
        for (auto it = children.rbegin(); it != children.rend(); ++it) tasks.push_back(std::move(*it));
        if (tasks.empty()) stage = BuildStage::CatchupMap;
    }

    void nodes_one_quantum(std::size_t& visits) {
        if (tasks.empty()) { stage = BuildStage::CatchupMap; return; }
        auto& task = tasks.back();
        if (task.phase == NodeTask::Phase::Choose) {
            if (task.groups.size() >= 8U) { task.phase = NodeTask::Phase::Finalize; return; }
            const auto chosen = choose_group(task);
            if (chosen == task.groups.size()) { task.phase = NodeTask::Phase::Finalize; return; }
            begin_split(task, chosen);
            return;
        }
        if (task.phase == NodeTask::Phase::Finalize) { finalize_node(task); return; }

        switch (task.split.phase) {
            case SplitState::Phase::Centroid: split_centroid_chunk(task, visits); break;
            case SplitState::Phase::Bins: split_bins_chunk(task, visits); break;
            case SplitState::Phase::Scatter: split_scatter_chunk(task, visits); break;
            case SplitState::Phase::Copy: split_copy_chunk(task, visits); break;
            case SplitState::Phase::Done: finish_split(task); break;
        }
    }

    void ensure_catchup_storage() {
        if (dirty_leaf_masks.size() != candidate.nodes_.size()) dirty_leaf_masks.assign(candidate.nodes_.size(), 0U);
        if (leaf_queued.size() != candidate.nodes_.size()) leaf_queued.assign(candidate.nodes_.size(), 0U);
        if (dirty_node_marks.size() != candidate.nodes_.size()) dirty_node_marks.assign(candidate.nodes_.size(), 0U);
    }

    [[nodiscard]] bool raw_dirty_pending() const noexcept {
        return build_dirty_slot_cursor < build_dirty_slots.size() || build_dirty_range_cursor < build_dirty_ranges.size();
    }

    [[nodiscard]] std::size_t raw_dirty_value_count() const noexcept {
        std::size_t total = build_dirty_slots.size() - std::min(build_dirty_slot_cursor, build_dirty_slots.size());
        if (build_dirty_range_cursor < build_dirty_ranges.size()) {
            const auto& current = build_dirty_ranges[build_dirty_range_cursor];
            if (current.count > build_dirty_range_offset)
                total += static_cast<std::size_t>(current.count - build_dirty_range_offset);
            for (std::size_t i = build_dirty_range_cursor + 1; i < build_dirty_ranges.size(); ++i)
                total += build_dirty_ranges[i].count;
        }
        return total;
    }

    [[nodiscard]] BudgetedSahBacklog backlog() const noexcept {
        return {
            .raw_dirty_values = raw_dirty_value_count(),
            .dirty_leaf_nodes = dirty_leaf_queue.size(),
            .dirty_internal_nodes = dirty_node_queue.size(),
        };
    }

    [[nodiscard]] bool map_dirty_slot(std::uint32_t slot) {
        if (slot >= candidate.leaf_locations_.size()) { fail("budgeted SAH catch-up slot is out of range"); return false; }
        const auto loc = candidate.leaf_locations_[slot];
        if (loc.node == std::numeric_limits<std::uint32_t>::max() || loc.node >= dirty_leaf_masks.size() || loc.child >= 8U) {
            fail("budgeted SAH catch-up has invalid leaf location"); return false;
        }
        dirty_leaf_masks[loc.node] = static_cast<std::uint8_t>(dirty_leaf_masks[loc.node] | static_cast<std::uint8_t>(1U << loc.child));
        if (leaf_queued[loc.node] == 0U) {
            leaf_queued[loc.node] = 1U;
            dirty_leaf_queue.push_back(loc.node);
        }
        return true;
    }

    void enqueue_ancestors(std::uint32_t node) {
        std::uint32_t current = node;
        while (current != std::numeric_limits<std::uint32_t>::max()) {
            if (dirty_node_marks[current] == 0U) {
                dirty_node_marks[current] = 1U;
                dirty_node_queue.push({candidate.nodes_[current].depth, current});
            }
            current = candidate.nodes_[current].parent;
        }
    }

    void choose_catchup_stage(const SpatialSnapshot& snapshot) {
        if (raw_dirty_pending()) { stage = BuildStage::CatchupMap; return; }
        if (!dirty_leaf_queue.empty()) { stage = BuildStage::CatchupLeaves; return; }
        if (!dirty_node_queue.empty()) { stage = BuildStage::CatchupNodes; return; }
        if (observed_version != snapshot.version()) {
            fail("budgeted SAH reached an empty maintenance queue without observing the current spatial version");
            return;
        }
        candidate.snapshot_version_ = snapshot.version();
        stage = BuildStage::Ready;
        public_state = BudgetedSahState::Ready;
    }

    void catchup_map_chunk(const SpatialSnapshot& snapshot, std::size_t& visits) {
        ensure_catchup_storage();
        std::size_t remaining = chunk_primitives;
        while (remaining != 0 && build_dirty_slot_cursor < build_dirty_slots.size()) {
            if (!map_dirty_slot(build_dirty_slots[build_dirty_slot_cursor++])) return;
            ++stats.mapped_dirty_values;
            ++visits; --remaining;
        }
        while (remaining != 0 && build_dirty_range_cursor < build_dirty_ranges.size()) {
            const auto& range = build_dirty_ranges[build_dirty_range_cursor];
            if (range.count == 0) { ++build_dirty_range_cursor; build_dirty_range_offset = 0; continue; }
            const std::uint64_t slot64 = static_cast<std::uint64_t>(range.first) + build_dirty_range_offset;
            if (slot64 >= record_count || !map_dirty_slot(static_cast<std::uint32_t>(slot64))) return;
            ++stats.mapped_dirty_values;
            ++build_dirty_range_offset; ++visits; --remaining;
            if (build_dirty_range_offset == range.count) { ++build_dirty_range_cursor; build_dirty_range_offset = 0; }
        }
        if (!raw_dirty_pending()) {
            build_dirty_slots.clear(); build_dirty_ranges.clear();
            build_dirty_slot_cursor = 0; build_dirty_range_cursor = 0; build_dirty_range_offset = 0;
            choose_catchup_stage(snapshot);
        }
    }

    void catchup_leaves_chunk(const SpatialSnapshot& snapshot, std::size_t& visits) {
        if (raw_dirty_pending()) { stage = BuildStage::CatchupMap; return; }
        std::size_t processed = 0;
        const std::size_t node_budget = std::max<std::size_t>(1, chunk_primitives / 8U);
        while (processed < node_budget && !dirty_leaf_queue.empty()) {
            const auto ni = dirty_leaf_queue.back(); dirty_leaf_queue.pop_back();
            leaf_queued[ni] = 0U;
            const auto mask = dirty_leaf_masks[ni]; dirty_leaf_masks[ni] = 0U;
            if (mask == 0U) { ++processed; continue; }
            auto& node = candidate.nodes_[ni];
            for (std::size_t ci = 0; ci < node.child_count; ++ci) {
                if ((mask & static_cast<std::uint8_t>(1U << ci)) == 0U) continue;
                auto& child = node.children[ci];
                if (child.count == 0) { fail("budgeted SAH catch-up leaf mask points to internal child"); return; }
                child.bounds = candidate.leaf_bounds(snapshot, child);
                ++stats.refit_leaf_children;
                visits += child.count;
            }
            enqueue_ancestors(ni);
            ++processed;
        }
        if (dirty_leaf_queue.empty()) choose_catchup_stage(snapshot);
    }

    void catchup_nodes_chunk(const SpatialSnapshot& snapshot, std::size_t& visits) {
        if (raw_dirty_pending()) { stage = BuildStage::CatchupMap; return; }
        if (!dirty_leaf_queue.empty()) { stage = BuildStage::CatchupLeaves; return; }
        std::size_t processed = 0;
        while (processed < chunk_primitives && !dirty_node_queue.empty()) {
            const auto [depth, ni] = dirty_node_queue.top();
            (void)depth;
            dirty_node_queue.pop();
            if (dirty_node_marks[ni] == 0U) continue;
            dirty_node_marks[ni] = 0U;
            const Aabb b = candidate.node_bounds(ni);
            const auto parent = candidate.nodes_[ni].parent;
            if (parent != std::numeric_limits<std::uint32_t>::max())
                candidate.nodes_[parent].children[candidate.nodes_[ni].parent_child].bounds = b;
            ++stats.refit_internal_nodes;
            ++processed; ++visits;
        }
        if (dirty_node_queue.empty()) choose_catchup_stage(snapshot);
    }

};

BudgetedSahBuilder::BudgetedSahBuilder(std::size_t leaf_size, std::size_t chunk_primitives)
    : impl_(std::make_unique<Impl>(leaf_size, chunk_primitives)) {}
BudgetedSahBuilder::~BudgetedSahBuilder() = default;
BudgetedSahBuilder::BudgetedSahBuilder(BudgetedSahBuilder&&) noexcept = default;
BudgetedSahBuilder& BudgetedSahBuilder::operator=(BudgetedSahBuilder&&) noexcept = default;

bool BudgetedSahBuilder::start(const SpatialSnapshot& snapshot, std::string& error) {
    error.clear();
    if (!impl_) impl_ = std::make_unique<Impl>(8, 256);
    if (impl_->public_state == BudgetedSahState::Capturing || impl_->public_state == BudgetedSahState::Building) {
        error = "budgeted SAH builder already has an active generation";
        return false;
    }

    const auto leaf = impl_->leaf_size, chunk = impl_->chunk_primitives;
    impl_ = std::make_unique<Impl>(leaf, chunk);
    impl_->public_state = BudgetedSahState::Capturing;
    impl_->stage = Impl::BuildStage::Capture;
    impl_->initial_version = snapshot.version();
    impl_->observed_version = snapshot.version();
    impl_->structure_revision = snapshot.structure_revision();
    impl_->record_count = snapshot.size();
    impl_->stats.source_structure_revision = snapshot.structure_revision();
    impl_->stats.source_records = snapshot.size();
    impl_->initialize_frozen_metadata(snapshot);
    return true;
}

bool BudgetedSahBuilder::observe_changes(const SpatialChangeSet& changes, std::string& error) {
    error.clear();
    if (!impl_ || (impl_->public_state != BudgetedSahState::Capturing && impl_->public_state != BudgetedSahState::Building && impl_->public_state != BudgetedSahState::Ready)) {
        error = "budgeted SAH builder has no active generation";
        return false;
    }
    if (changes.from_version != impl_->observed_version) {
        impl_->observed_chain_valid = false;
        error = "budgeted SAH observed a discontinuous spatial publication chain";
        return false;
    }
    impl_->observed_version = changes.to_version;
    impl_->stats.observed_dirty_values += changes.dirty_value_count();
    if (changes.requires_rebuild) impl_->structural_change_seen = true;
    const bool capturing = impl_->public_state == BudgetedSahState::Capturing;
    if (impl_->public_state == BudgetedSahState::Ready) {
        impl_->public_state = BudgetedSahState::Building;
        impl_->stage = Impl::BuildStage::CatchupMap;
    }
    auto& slots = capturing ? impl_->capture_dirty_slots : impl_->build_dirty_slots;
    auto& ranges = capturing ? impl_->capture_dirty_ranges : impl_->build_dirty_ranges;
    slots.insert(slots.end(), changes.dirty_slots.begin(), changes.dirty_slots.end());
    // Absolute writes are preserved until mapped into the leaf queue. Repeated ranges may target the
    // same leaves, but leaf_queued/dirty masks deduplicate the expensive refit work while retaining the
    // fact that values changed again after an earlier maintenance slice.
    ranges.insert(ranges.end(), changes.dirty_ranges.begin(), changes.dirty_ranges.end());
    return true;
}

CooperativeTaskProgress BudgetedSahBuilder::run_slice(const SpatialSnapshot& snapshot, double budget_ms) {
    CooperativeTaskProgress progress{};
    if (!impl_) { progress.ok = false; progress.error = "budgeted SAH builder is not initialized"; return progress; }
    if (!std::isfinite(budget_ms) || budget_ms <= 0.0) { progress.ok = false; progress.error = "budget must be finite and positive"; return progress; }
    if (impl_->public_state == BudgetedSahState::Ready || impl_->public_state == BudgetedSahState::Promoted) {
        progress.complete = true; return progress;
    }
    if (impl_->public_state == BudgetedSahState::Failed || impl_->public_state == BudgetedSahState::Discarded || impl_->public_state == BudgetedSahState::Idle) {
        progress.ok = false; progress.error = impl_->error.empty() ? "budgeted SAH builder is not active" : impl_->error; return progress;
    }
    if (!impl_->observed_chain_valid) { impl_->fail("budgeted SAH change chain is discontinuous"); progress.ok = false; progress.error = impl_->error; return progress; }
    if (impl_->structural_change_seen || !impl_->structure_matches(snapshot)) { impl_->discard("budgeted SAH generation invalidated by structural change"); progress.ok = false; progress.error = impl_->error; return progress; }

    const auto begin = Clock::now();
    const auto deadline = begin + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(budget_ms));
    std::size_t visits = 0;
    const auto stage_before = impl_->stage;

    while (Clock::now() < deadline) {
        switch (impl_->stage) {
            case Impl::BuildStage::Capture:
                if (!impl_->capture_one_chunk(snapshot, visits)) break;
                break;
            case Impl::BuildStage::CaptureCatchup:
                if (!impl_->capture_catchup_one_chunk(snapshot, visits)) break;
                break;
            case Impl::BuildStage::InitReserve:
                impl_->init_reserve_one();
                break;
            case Impl::BuildStage::InitArrays:
                impl_->init_arrays_one_chunk(visits);
                break;
            case Impl::BuildStage::RootBounds:
                impl_->root_bounds_one_chunk(visits);
                break;
            case Impl::BuildStage::Nodes:
                impl_->nodes_one_quantum(visits);
                break;
            case Impl::BuildStage::CatchupMap:
                impl_->catchup_map_chunk(snapshot, visits);
                break;
            case Impl::BuildStage::CatchupLeaves:
                impl_->catchup_leaves_chunk(snapshot, visits);
                break;
            case Impl::BuildStage::CatchupNodes:
                impl_->catchup_nodes_chunk(snapshot, visits);
                break;
            case Impl::BuildStage::Ready:
                progress.complete = true;
                goto done;
            case Impl::BuildStage::Failed:
            case Impl::BuildStage::Discarded:
                progress.ok = false; progress.error = impl_->error;
                goto done;
        }
        if (impl_->public_state == BudgetedSahState::Failed || impl_->public_state == BudgetedSahState::Discarded) {
            progress.ok = false; progress.error = impl_->error; break;
        }
        if (impl_->public_state == BudgetedSahState::Ready) { progress.complete = true; break; }
    }

done:
    const auto end = Clock::now();
    const double actual = std::chrono::duration<double, std::milli>(end - begin).count();
    ++impl_->stats.slices;
    impl_->stats.primitive_visits += visits;
    impl_->stats.granted_ms += budget_ms;
    impl_->stats.actual_ms += actual;
    impl_->stats.max_slice_ms = std::max(impl_->stats.max_slice_ms, actual);
    if (stage_before == Impl::BuildStage::Capture || stage_before == Impl::BuildStage::CaptureCatchup) {
        impl_->stats.capture_ms += actual;
        impl_->stats.max_capture_slice_ms = std::max(impl_->stats.max_capture_slice_ms, actual);
    } else if (stage_before == Impl::BuildStage::CatchupMap || stage_before == Impl::BuildStage::CatchupLeaves || stage_before == Impl::BuildStage::CatchupNodes) {
        impl_->stats.catchup_ms += actual;
        impl_->stats.max_catchup_slice_ms = std::max(impl_->stats.max_catchup_slice_ms, actual);
    } else if (stage_before != Impl::BuildStage::Ready && stage_before != Impl::BuildStage::Failed && stage_before != Impl::BuildStage::Discarded) {
        impl_->stats.build_ms += actual;
        impl_->stats.max_build_slice_ms = std::max(impl_->stats.max_build_slice_ms, actual);
        if (stage_before == Impl::BuildStage::InitReserve) impl_->stats.max_reserve_slice_ms = std::max(impl_->stats.max_reserve_slice_ms, actual);
        else if (stage_before == Impl::BuildStage::InitArrays) impl_->stats.max_init_slice_ms = std::max(impl_->stats.max_init_slice_ms, actual);
        else if (stage_before == Impl::BuildStage::RootBounds) impl_->stats.max_root_slice_ms = std::max(impl_->stats.max_root_slice_ms, actual);
        else if (stage_before == Impl::BuildStage::Nodes) impl_->stats.max_node_slice_ms = std::max(impl_->stats.max_node_slice_ms, actual);
    }
    progress.work_units = visits;
    progress.complete = progress.complete || impl_->public_state == BudgetedSahState::Ready;
    return progress;
}

BudgetedSahPromoteResult BudgetedSahBuilder::try_promote(const SpatialSnapshot& snapshot, WideBvh8View& out) {
    BudgetedSahPromoteResult result{};
    if (!impl_) return result;
    result.state = impl_->public_state;
    result.stats = impl_->stats;
    if (impl_->public_state != BudgetedSahState::Ready) {
        result.error = impl_->error;
        return result;
    }
    if (!impl_->structure_matches(snapshot) || impl_->structural_change_seen || !impl_->observed_chain_valid || impl_->observed_version != snapshot.version()) {
        impl_->discard("budgeted SAH candidate cannot be promoted because its publication chain is incomplete or structurally obsolete");
        result.state = impl_->public_state; result.error = impl_->error; return result;
    }

    if (impl_->candidate.snapshot_version_ != snapshot.version() || impl_->candidate.snapshot_structure_revision_ != snapshot.structure_revision()) {
        impl_->fail("budgeted SAH candidate reached Ready with a stale version");
        result.state = impl_->public_state; result.error = impl_->error; return result;
    }
    out = std::move(impl_->candidate);
    impl_->public_state = BudgetedSahState::Promoted;
    result.state = BudgetedSahState::Promoted;
    return result;
}

BudgetedSahState BudgetedSahBuilder::state() const noexcept {
    return impl_ ? impl_->public_state : BudgetedSahState::Idle;
}

const BudgetedSahStats& BudgetedSahBuilder::stats() const noexcept {
    static const BudgetedSahStats empty{};
    return impl_ ? impl_->stats : empty;
}

BudgetedSahBacklog BudgetedSahBuilder::backlog() const noexcept {
    return impl_ ? impl_->backlog() : BudgetedSahBacklog{};
}

} // namespace aion
