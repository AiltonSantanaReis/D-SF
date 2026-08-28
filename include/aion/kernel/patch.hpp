#pragma once

#include "aion/kernel/world.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aion {

enum class PatchComponent : std::uint8_t {
    Position = 1,
    Velocity = 2,
    Health = 3,
};

struct Vec3RangePatch {
    PatchComponent component{PatchComponent::Position};
    EntityId first{};
    std::vector<Vec3> values;
};

struct U32RangePatch {
    PatchComponent component{PatchComponent::Health};
    EntityId first{};
    std::vector<std::uint32_t> values;
};

struct PatchTransaction {
    TransactionId id{};
    // Scalar lane: structural events and sparse writes. Range lanes: dense/clustered component writes.
    std::vector<Mutation> scalar_mutations;
    std::vector<Vec3RangePatch> vec3_patches;
    std::vector<U32RangePatch> u32_patches;
};

enum class PatchApplyMode : std::uint8_t {
    Serial = 0,
    ParallelDisjoint = 1,
};

struct PatchCommitResult {
    bool committed{false};
    std::size_t patches_applied{};
    std::size_t values_applied{};
    std::string error;
};

struct PatchReplayResult {
    bool ok{false};
    std::size_t transactions_applied{};
    TransactionId failed_transaction{};
    std::string error;
};

struct PatchJournalResult {
    bool ok{false};
    std::string error;
};

class PatchCommitRuntime final {
public:
    explicit PatchCommitRuntime(std::size_t workers);
    ~PatchCommitRuntime();
    PatchCommitRuntime(PatchCommitRuntime&&) noexcept;
    PatchCommitRuntime& operator=(PatchCommitRuntime&&) noexcept;
    PatchCommitRuntime(const PatchCommitRuntime&) = delete;
    PatchCommitRuntime& operator=(const PatchCommitRuntime&) = delete;

    [[nodiscard]] PatchCommitResult commit(World& world, const PatchTransaction& transaction);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PatchJournal final {
public:
    [[nodiscard]] PatchCommitResult commit(
        World& world,
        const PatchTransaction& transaction,
        PatchApplyMode mode = PatchApplyMode::Serial,
        std::size_t workers = 1);

    [[nodiscard]] bool rollback_last(World& world);
    [[nodiscard]] bool rollback_to(World& world, TransactionId transaction_id);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::vector<PatchTransaction> transactions() const;
    [[nodiscard]] std::optional<StateHash> hash_after(TransactionId transaction_id) const;

    [[nodiscard]] PatchJournalResult save_transactions(const std::filesystem::path& path) const;
    [[nodiscard]] static PatchJournalResult load_transactions(
        const std::filesystem::path& path,
        std::vector<PatchTransaction>& out_transactions);

    [[nodiscard]] static PatchReplayResult replay(
        World& world,
        std::span<const PatchTransaction> transactions,
        PatchJournal* capture = nullptr,
        PatchApplyMode mode = PatchApplyMode::Serial,
        std::size_t workers = 1);

private:
    struct UndoVec3Range {
        PatchComponent component{};
        EntityId first{};
        std::vector<Vec3> values;
    };

    struct UndoU32Range {
        PatchComponent component{};
        EntityId first{};
        std::vector<std::uint32_t> values;
    };

    struct UndoState {
        World::UndoState scalar_undo;
        std::vector<UndoVec3Range> vec3_ranges;
        std::vector<UndoU32Range> u32_ranges;
    };

    struct Entry {
        PatchTransaction forward;
        UndoState undo;
        StateHash post_hash;
    };

    std::vector<Entry> entries_;
};

[[nodiscard]] PatchCommitResult commit_patch_transaction(
    World& world,
    const PatchTransaction& transaction,
    PatchApplyMode mode = PatchApplyMode::Serial,
    std::size_t workers = 1);

} // namespace aion
