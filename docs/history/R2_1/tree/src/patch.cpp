#include "aion/kernel/patch.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <fstream>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <limits>
#include <thread>
#include <type_traits>

namespace aion {

class PatchAccess final {
public:
    static TransactionId last_transaction_id(const World& world) noexcept { return world.last_transaction_id_; }
    static EntityId next_entity_id(const World& world) noexcept { return world.next_entity_id_; }
    static std::vector<Vec3>& positions(World& world) noexcept { return world.positions_; }
    static const std::vector<Vec3>& positions(const World& world) noexcept { return world.positions_; }
    static std::vector<Vec3>& velocities(World& world) noexcept { return world.velocities_; }
    static const std::vector<Vec3>& velocities(const World& world) noexcept { return world.velocities_; }
    static std::vector<EntityState>& states(World& world) noexcept { return world.states_; }
    static const std::vector<EntityState>& states(const World& world) noexcept { return world.states_; }
    static void set_last_transaction_id(World& world, TransactionId id) noexcept { world.last_transaction_id_ = id; }
    static bool validate_scalar(const World& world, const Transaction& tx, std::string_view& error) noexcept { return world.validate(tx, error); }
    static World::UndoState capture_scalar_undo(const World& world, const Transaction& tx) { return world.capture_undo(tx); }
    static void restore_scalar_undo(World& world, const World::UndoState& undo) noexcept { world.restore_undo(undo); }
};
namespace {

constexpr std::array<char, 8> kMagic{'A','I','O','N','P','C','H','1'};
constexpr std::uint32_t kFormatVersion = 2;
constexpr std::uint64_t kMaxTransactions = 100'000'000ULL;
constexpr std::uint64_t kMaxPatchesPerTransaction = 100'000'000ULL;
constexpr std::uint64_t kMaxValuesPerPatch = 1'000'000'000ULL;

[[nodiscard]] bool finite_vec(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool valid_vec_component(PatchComponent component) noexcept {
    return component == PatchComponent::Position || component == PatchComponent::Velocity;
}

[[nodiscard]] bool valid_u32_component(PatchComponent component) noexcept {
    return component == PatchComponent::Health;
}

[[nodiscard]] bool range_fits(EntityId first, std::size_t count, EntityId next_entity_id) noexcept {
    if (first == 0 || count == 0 || first >= next_entity_id) return false;
    const auto remaining = next_entity_id - first;
    return static_cast<std::uint64_t>(count) <= remaining;
}

[[nodiscard]] bool validate_patch_transaction(const World& world, const PatchTransaction& transaction, std::string& error) {
    Transaction scalar{.id = transaction.id, .mutations = transaction.scalar_mutations};
    std::string_view scalar_error{};
    if (!PatchAccess::validate_scalar(world, scalar, scalar_error)) {
        error.assign(scalar_error);
        return false;
    }

    // Dense ranges intentionally target identities that already existed before this transaction.
    // Newly-created entities remain on the scalar lane until a later transaction.
    const auto range_next = PatchAccess::next_entity_id(world);
    bool has_advance = false;
    for (const auto& mutation : transaction.scalar_mutations) {
        if (mutation.kind == MutationKind::AdvanceReference) has_advance = true;
    }
    if (has_advance && (!transaction.vec3_patches.empty() || !transaction.u32_patches.empty())) {
        error = "AdvanceReference cannot be mixed with component ranges in one hybrid transaction";
        return false;
    }

    EntityId pos_end = 0;
    EntityId vel_end = 0;
    for (const auto& patch : transaction.vec3_patches) {
        if (!valid_vec_component(patch.component)) {
            error = "vec3 patch uses an invalid component";
            return false;
        }
        if (!range_fits(patch.first, patch.values.size(), range_next)) {
            error = "vec3 patch range is empty or outside created entity identities";
            return false;
        }
        for (const auto value : patch.values) {
            if (!finite_vec(value)) {
                error = "vec3 patch contains a non-finite value";
                return false;
            }
        }
        const auto end = patch.first + static_cast<EntityId>(patch.values.size());
        auto& previous_end = patch.component == PatchComponent::Position ? pos_end : vel_end;
        if (previous_end != 0 && patch.first < previous_end) {
            error = "vec3 patches must be canonically ordered and non-overlapping per component";
            return false;
        }
        previous_end = end;
    }

    EntityId health_end = 0;
    for (const auto& patch : transaction.u32_patches) {
        if (!valid_u32_component(patch.component)) {
            error = "u32 patch uses an invalid component";
            return false;
        }
        if (!range_fits(patch.first, patch.values.size(), range_next)) {
            error = "u32 patch range is empty or outside created entity identities";
            return false;
        }
        const auto end = patch.first + static_cast<EntityId>(patch.values.size());
        if (health_end != 0 && patch.first < health_end) {
            error = "u32 patches must be canonically ordered and non-overlapping per component";
            return false;
        }
        health_end = end;
    }

    auto inside_vec_range = [&](PatchComponent component, EntityId entity) {
        for (const auto& patch : transaction.vec3_patches) {
            if (patch.component != component) continue;
            const auto end = patch.first + static_cast<EntityId>(patch.values.size());
            if (entity >= patch.first && entity < end) return true;
        }
        return false;
    };
    auto inside_u32_range = [&](EntityId entity) {
        for (const auto& patch : transaction.u32_patches) {
            const auto end = patch.first + static_cast<EntityId>(patch.values.size());
            if (entity >= patch.first && entity < end) return true;
        }
        return false;
    };
    for (const auto& mutation : transaction.scalar_mutations) {
        if (mutation.kind == MutationKind::SetPosition && inside_vec_range(PatchComponent::Position, mutation.entity)) {
            error = "scalar Position write overlaps a Position range"; return false;
        }
        if (mutation.kind == MutationKind::SetVelocity && inside_vec_range(PatchComponent::Velocity, mutation.entity)) {
            error = "scalar Velocity write overlaps a Velocity range"; return false;
        }
        if (mutation.kind == MutationKind::SetHealth && inside_u32_range(mutation.entity)) {
            error = "scalar Health write overlaps a Health range"; return false;
        }
    }
    return true;
}

void apply_vec_patch(World& world, const Vec3RangePatch& patch) noexcept {
    const auto first = static_cast<std::size_t>(patch.first);
    auto& target = patch.component == PatchComponent::Position ? PatchAccess::positions(world) : PatchAccess::velocities(world);
    std::copy(patch.values.begin(), patch.values.end(), target.begin() + static_cast<std::ptrdiff_t>(first));
}

void apply_u32_patch(World& world, const U32RangePatch& patch) noexcept {
    const auto first = static_cast<std::size_t>(patch.first);
    for (std::size_t i = 0; i < patch.values.size(); ++i) {
        PatchAccess::states(world)[first + i].health = patch.values[i];
    }
}

void apply_serial(World& world, const PatchTransaction& transaction) noexcept {
    for (const auto& patch : transaction.vec3_patches) apply_vec_patch(world, patch);
    for (const auto& patch : transaction.u32_patches) apply_u32_patch(world, patch);
}

class PatchWorkerPool final {
public:
    explicit PatchWorkerPool(std::size_t workers) {
        threads_.reserve(workers);
        for (std::size_t i = 0; i < workers; ++i) {
            threads_.emplace_back([this] {
                while (true) {
                    std::function<void()> job;
                    {
                        std::unique_lock lock(mutex_);
                        cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                        if (stopping_ && jobs_.empty()) return;
                        job = std::move(jobs_.front());
                        jobs_.pop();
                    }
                    job();
                }
            });
        }
    }

    ~PatchWorkerPool() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& thread : threads_) thread.join();
    }

    std::future<void> submit(std::function<void()> fn) {
        auto task = std::make_shared<std::packaged_task<void()>>(std::move(fn));
        auto future = task->get_future();
        {
            std::lock_guard lock(mutex_);
            jobs_.push([task] { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> jobs_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_{false};
};

void apply_parallel_persistent(World& world, const PatchTransaction& transaction, PatchWorkerPool& pool, std::size_t workers) {
    const auto task_count = transaction.vec3_patches.size() + transaction.u32_patches.size();
    if (workers <= 1 || task_count <= 1) { apply_serial(world, transaction); return; }
    const auto active = std::min(workers, task_count);
    std::atomic_size_t next{0};
    std::vector<std::future<void>> futures;
    futures.reserve(active);
    for (std::size_t worker = 0; worker < active; ++worker) {
        futures.push_back(pool.submit([&] {
            while (true) {
                const auto task = next.fetch_add(1, std::memory_order_relaxed);
                if (task >= task_count) break;
                if (task < transaction.vec3_patches.size()) apply_vec_patch(world, transaction.vec3_patches[task]);
                else apply_u32_patch(world, transaction.u32_patches[task - transaction.vec3_patches.size()]);
            }
        }));
    }
    for (auto& future : futures) future.get();
}

void apply_parallel(World& world, const PatchTransaction& transaction, std::size_t workers) {
    const auto task_count = transaction.vec3_patches.size() + transaction.u32_patches.size();
    if (workers <= 1 || task_count <= 1) {
        apply_serial(world, transaction);
        return;
    }

    workers = std::min(workers, task_count);
    std::atomic_size_t next{0};
    std::vector<std::thread> threads;
    threads.reserve(workers);
    for (std::size_t worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&] {
            while (true) {
                const auto task = next.fetch_add(1, std::memory_order_relaxed);
                if (task >= task_count) break;
                if (task < transaction.vec3_patches.size()) {
                    apply_vec_patch(world, transaction.vec3_patches[task]);
                } else {
                    apply_u32_patch(world, transaction.u32_patches[task - transaction.vec3_patches.size()]);
                }
            }
        });
    }
    for (auto& thread : threads) thread.join();
}

template <typename T>
bool write_le(std::ostream& out, T value) {
    static_assert(std::is_unsigned_v<T>);
    std::array<char, sizeof(T)> bytes{};
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[i] = static_cast<char>((value >> (i * 8U)) & static_cast<T>(0xffU));
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

template <typename T>
bool read_le(std::istream& in, T& value) {
    static_assert(std::is_unsigned_v<T> && sizeof(T) <= sizeof(std::uint64_t));
    std::array<unsigned char, sizeof(T)> bytes{};
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in) return false;
    std::uint64_t wide = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) wide |= static_cast<std::uint64_t>(bytes[i]) << (i * 8U);
    value = static_cast<T>(wide);
    return true;
}

[[nodiscard]] bool valid_component(std::uint8_t raw) noexcept {
    return raw >= static_cast<std::uint8_t>(PatchComponent::Position) &&
           raw <= static_cast<std::uint8_t>(PatchComponent::Health);
}

} // namespace

struct PatchCommitRuntime::Impl final {
    explicit Impl(std::size_t requested) : workers(std::max<std::size_t>(requested, 1)), pool(workers) {}
    std::size_t workers;
    PatchWorkerPool pool;
};

PatchCommitRuntime::PatchCommitRuntime(std::size_t workers) : impl_(std::make_unique<Impl>(workers)) {}
PatchCommitRuntime::~PatchCommitRuntime() = default;
PatchCommitRuntime::PatchCommitRuntime(PatchCommitRuntime&&) noexcept = default;
PatchCommitRuntime& PatchCommitRuntime::operator=(PatchCommitRuntime&&) noexcept = default;

PatchCommitResult PatchCommitRuntime::commit(World& world, const PatchTransaction& transaction) {
    std::string error;
    if (!validate_patch_transaction(world, transaction, error)) return {false, 0, 0, std::move(error)};
    Transaction scalar{.id = transaction.id, .mutations = transaction.scalar_mutations};
    const auto scalar_result = world.commit(scalar);
    if (!scalar_result.committed) return {false, 0, 0, std::string(scalar_result.error)};
    apply_parallel_persistent(world, transaction, impl_->pool, impl_->workers);
    std::size_t values = transaction.scalar_mutations.size();
    for (const auto& patch : transaction.vec3_patches) values += patch.values.size();
    for (const auto& patch : transaction.u32_patches) values += patch.values.size();
    return {true, transaction.scalar_mutations.size() + transaction.vec3_patches.size() + transaction.u32_patches.size(), values, {}};
}

PatchCommitResult commit_patch_transaction(
    World& world,
    const PatchTransaction& transaction,
    PatchApplyMode mode,
    std::size_t workers) {

    std::string error;
    if (!validate_patch_transaction(world, transaction, error)) return {false, 0, 0, std::move(error)};

    Transaction scalar{.id = transaction.id, .mutations = transaction.scalar_mutations};
    const auto scalar_result = world.commit(scalar);
    if (!scalar_result.committed) return {false, 0, 0, std::string(scalar_result.error)};
    if (mode == PatchApplyMode::ParallelDisjoint) apply_parallel(world, transaction, std::max<std::size_t>(workers, 1));
    else apply_serial(world, transaction);

    std::size_t values = transaction.scalar_mutations.size();
    for (const auto& patch : transaction.vec3_patches) values += patch.values.size();
    for (const auto& patch : transaction.u32_patches) values += patch.values.size();
    return {true, transaction.scalar_mutations.size() + transaction.vec3_patches.size() + transaction.u32_patches.size(), values, {}};
}

PatchCommitResult PatchJournal::commit(
    World& world,
    const PatchTransaction& transaction,
    PatchApplyMode mode,
    std::size_t workers) {

    std::string error;
    if (!validate_patch_transaction(world, transaction, error)) return {false, 0, 0, std::move(error)};

    const Transaction scalar{.id = transaction.id, .mutations = transaction.scalar_mutations};
    UndoState undo{.scalar_undo = PatchAccess::capture_scalar_undo(world, scalar), .vec3_ranges = {}, .u32_ranges = {}};
    undo.vec3_ranges.reserve(transaction.vec3_patches.size());
    for (const auto& patch : transaction.vec3_patches) {
        UndoVec3Range old{.component = patch.component, .first = patch.first, .values = {}};
        const auto first = static_cast<std::size_t>(patch.first);
        const auto& source = patch.component == PatchComponent::Position ? PatchAccess::positions(world) : PatchAccess::velocities(world);
        old.values.assign(source.begin() + static_cast<std::ptrdiff_t>(first),
                          source.begin() + static_cast<std::ptrdiff_t>(first + patch.values.size()));
        undo.vec3_ranges.push_back(std::move(old));
    }
    undo.u32_ranges.reserve(transaction.u32_patches.size());
    for (const auto& patch : transaction.u32_patches) {
        UndoU32Range old{.component = patch.component, .first = patch.first, .values = {}};
        const auto first = static_cast<std::size_t>(patch.first);
        old.values.reserve(patch.values.size());
        for (std::size_t i = 0; i < patch.values.size(); ++i) old.values.push_back(PatchAccess::states(world)[first + i].health);
        undo.u32_ranges.push_back(std::move(old));
    }

    const auto result = commit_patch_transaction(world, transaction, mode, workers);
    if (!result.committed) return result;
    entries_.push_back({.forward = transaction, .undo = std::move(undo), .post_hash = world.state_hash()});
    return result;
}

bool PatchJournal::rollback_last(World& world) {
    if (entries_.empty()) return false;
    const auto& entry = entries_.back();
    if (world.state_hash() != entry.post_hash) return false;

    for (const auto& patch : entry.undo.vec3_ranges) {
        const auto first = static_cast<std::size_t>(patch.first);
        auto& target = patch.component == PatchComponent::Position ? PatchAccess::positions(world) : PatchAccess::velocities(world);
        std::copy(patch.values.begin(), patch.values.end(), target.begin() + static_cast<std::ptrdiff_t>(first));
    }
    for (const auto& patch : entry.undo.u32_ranges) {
        const auto first = static_cast<std::size_t>(patch.first);
        for (std::size_t i = 0; i < patch.values.size(); ++i) PatchAccess::states(world)[first + i].health = patch.values[i];
    }
    PatchAccess::restore_scalar_undo(world, entry.undo.scalar_undo);
    entries_.pop_back();
    return true;
}

bool PatchJournal::rollback_to(World& world, TransactionId transaction_id) {
    if (transaction_id > PatchAccess::last_transaction_id(world)) return false;
    if (transaction_id != 0 && transaction_id != PatchAccess::last_transaction_id(world)) {
        bool found = false;
        for (const auto& entry : entries_) {
            if (entry.forward.id == transaction_id) { found = true; break; }
        }
        if (!found) return false;
    }
    while (!entries_.empty() && entries_.back().forward.id > transaction_id) {
        if (!rollback_last(world)) return false;
    }
    return PatchAccess::last_transaction_id(world) == transaction_id;
}

std::vector<PatchTransaction> PatchJournal::transactions() const {
    std::vector<PatchTransaction> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) result.push_back(entry.forward);
    return result;
}

std::optional<StateHash> PatchJournal::hash_after(TransactionId transaction_id) const {
    for (const auto& entry : entries_) if (entry.forward.id == transaction_id) return entry.post_hash;
    return std::nullopt;
}

PatchJournalResult PatchJournal::save_transactions(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return {false, "unable to open patch journal for writing"};
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    if (!write_le(out, kFormatVersion) || !write_le(out, static_cast<std::uint64_t>(entries_.size()))) {
        return {false, "failed writing patch journal header"};
    }
    for (const auto& entry : entries_) {
        const auto& tx = entry.forward;
        if (!write_le(out, tx.id) ||
            !write_le(out, static_cast<std::uint64_t>(tx.scalar_mutations.size())) ||
            !write_le(out, static_cast<std::uint64_t>(tx.vec3_patches.size())) ||
            !write_le(out, static_cast<std::uint64_t>(tx.u32_patches.size()))) {
            return {false, "failed writing patch transaction header"};
        }
        for (const auto& mutation : tx.scalar_mutations) {
            if (!write_le(out, static_cast<std::uint8_t>(mutation.kind)) || !write_le(out, mutation.entity) ||
                !write_le(out, std::bit_cast<std::uint32_t>(mutation.vec.x)) ||
                !write_le(out, std::bit_cast<std::uint32_t>(mutation.vec.y)) ||
                !write_le(out, std::bit_cast<std::uint32_t>(mutation.vec.z)) || !write_le(out, mutation.value)) {
                return {false, "failed writing scalar mutation"};
            }
        }
        for (const auto& patch : tx.vec3_patches) {
            if (!write_le(out, static_cast<std::uint8_t>(patch.component)) || !write_le(out, patch.first) ||
                !write_le(out, static_cast<std::uint64_t>(patch.values.size()))) {
                return {false, "failed writing vec3 patch header"};
            }
            for (const auto value : patch.values) {
                if (!write_le(out, std::bit_cast<std::uint32_t>(value.x)) ||
                    !write_le(out, std::bit_cast<std::uint32_t>(value.y)) ||
                    !write_le(out, std::bit_cast<std::uint32_t>(value.z))) {
                    return {false, "failed writing vec3 patch payload"};
                }
            }
        }
        for (const auto& patch : tx.u32_patches) {
            if (!write_le(out, static_cast<std::uint8_t>(patch.component)) || !write_le(out, patch.first) ||
                !write_le(out, static_cast<std::uint64_t>(patch.values.size()))) {
                return {false, "failed writing u32 patch header"};
            }
            for (const auto value : patch.values) if (!write_le(out, value)) return {false, "failed writing u32 patch payload"};
        }
    }
    out.flush();
    return out ? PatchJournalResult{true, {}} : PatchJournalResult{false, "failed flushing patch journal"};
}

PatchJournalResult PatchJournal::load_transactions(
    const std::filesystem::path& path,
    std::vector<PatchTransaction>& out_transactions) {

    out_transactions.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return {false, "unable to open patch journal for reading"};
    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) return {false, "invalid patch journal magic"};
    std::uint32_t version{};
    std::uint64_t tx_count{};
    if (!read_le(in, version) || !read_le(in, tx_count)) return {false, "truncated patch journal header"};
    if (version != kFormatVersion) return {false, "unsupported patch journal version"};
    if (tx_count > kMaxTransactions) return {false, "patch journal transaction count exceeds safety limit"};

    TransactionId previous = 0;
    out_transactions.reserve(static_cast<std::size_t>(tx_count));
    for (std::uint64_t ti = 0; ti < tx_count; ++ti) {
        PatchTransaction tx{};
        std::uint64_t scalar_count{}, vec_count{}, u32_count{};
        if (!read_le(in, tx.id) || !read_le(in, scalar_count) || !read_le(in, vec_count) || !read_le(in, u32_count)) {
            out_transactions.clear(); return {false, "truncated patch transaction header"};
        }
        if (tx.id <= previous) { out_transactions.clear(); return {false, "patch transaction ids are not strictly monotonic"}; }
        if (scalar_count > kMaxPatchesPerTransaction || vec_count > kMaxPatchesPerTransaction || u32_count > kMaxPatchesPerTransaction) {
            out_transactions.clear(); return {false, "patch count exceeds safety limit"};
        }
        previous = tx.id;
        tx.scalar_mutations.reserve(static_cast<std::size_t>(scalar_count));
        for (std::uint64_t mi = 0; mi < scalar_count; ++mi) {
            std::uint8_t raw{}; Mutation mutation{}; std::uint32_t x{}, y{}, z{};
            if (!read_le(in, raw) || !read_le(in, mutation.entity) || !read_le(in, x) || !read_le(in, y) || !read_le(in, z) || !read_le(in, mutation.value)) {
                out_transactions.clear(); return {false, "truncated scalar mutation"};
            }
            if (raw < static_cast<std::uint8_t>(MutationKind::CreateEntity) || raw > static_cast<std::uint8_t>(MutationKind::AdvanceReference)) {
                out_transactions.clear(); return {false, "unknown scalar mutation kind"};
            }
            mutation.kind = static_cast<MutationKind>(raw);
            mutation.vec = {std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)};
            tx.scalar_mutations.push_back(mutation);
        }
        tx.vec3_patches.reserve(static_cast<std::size_t>(vec_count));
        for (std::uint64_t pi = 0; pi < vec_count; ++pi) {
            std::uint8_t raw{}; EntityId first{}; std::uint64_t count{};
            if (!read_le(in, raw) || !read_le(in, first) || !read_le(in, count)) {
                out_transactions.clear(); return {false, "truncated vec3 patch header"};
            }
            if (!valid_component(raw) || count == 0 || count > kMaxValuesPerPatch) {
                out_transactions.clear(); return {false, "invalid vec3 patch metadata"};
            }
            Vec3RangePatch patch{.component = static_cast<PatchComponent>(raw), .first = first, .values = {}};
            patch.values.reserve(static_cast<std::size_t>(count));
            for (std::uint64_t vi = 0; vi < count; ++vi) {
                std::uint32_t x{}, y{}, z{};
                if (!read_le(in, x) || !read_le(in, y) || !read_le(in, z)) {
                    out_transactions.clear(); return {false, "truncated vec3 patch payload"};
                }
                patch.values.push_back({std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)});
            }
            tx.vec3_patches.push_back(std::move(patch));
        }
        tx.u32_patches.reserve(static_cast<std::size_t>(u32_count));
        for (std::uint64_t pi = 0; pi < u32_count; ++pi) {
            std::uint8_t raw{}; EntityId first{}; std::uint64_t count{};
            if (!read_le(in, raw) || !read_le(in, first) || !read_le(in, count)) {
                out_transactions.clear(); return {false, "truncated u32 patch header"};
            }
            if (!valid_component(raw) || count == 0 || count > kMaxValuesPerPatch) {
                out_transactions.clear(); return {false, "invalid u32 patch metadata"};
            }
            U32RangePatch patch{.component = static_cast<PatchComponent>(raw), .first = first, .values = {}};
            patch.values.reserve(static_cast<std::size_t>(count));
            for (std::uint64_t vi = 0; vi < count; ++vi) {
                std::uint32_t value{};
                if (!read_le(in, value)) { out_transactions.clear(); return {false, "truncated u32 patch payload"}; }
                patch.values.push_back(value);
            }
            tx.u32_patches.push_back(std::move(patch));
        }
        out_transactions.push_back(std::move(tx));
    }
    char trailing{};
    if (in.read(&trailing, 1)) { out_transactions.clear(); return {false, "patch journal contains trailing bytes"}; }
    return {true, {}};
}

PatchReplayResult PatchJournal::replay(
    World& world,
    std::span<const PatchTransaction> transactions,
    PatchJournal* capture,
    PatchApplyMode mode,
    std::size_t workers) {

    std::size_t applied = 0;
    for (const auto& tx : transactions) {
        const auto result = capture ? capture->commit(world, tx, mode, workers)
                                    : commit_patch_transaction(world, tx, mode, workers);
        if (!result.committed) return {false, applied, tx.id, result.error};
        ++applied;
    }
    return {true, applied, 0, {}};
}

} // namespace aion
