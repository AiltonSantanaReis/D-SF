#pragma once

#include "aion/kernel/world.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace aion {

struct JournalResult {
    bool ok{false};
    std::string error;
};

struct ReplayResult {
    bool ok{false};
    std::size_t transactions_applied{};
    TransactionId failed_transaction{};
    std::string error;
};

class ChangeJournal final {
public:
    [[nodiscard]] CommitResult commit(World& world, const Transaction& transaction);

    [[nodiscard]] bool rollback_last(World& world);
    [[nodiscard]] bool rollback_to(World& world, TransactionId transaction_id);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::vector<Transaction> transactions() const;
    [[nodiscard]] std::optional<StateHash> hash_after(TransactionId transaction_id) const;

    [[nodiscard]] JournalResult save_transactions(const std::filesystem::path& path) const;
    [[nodiscard]] static JournalResult load_transactions(
        const std::filesystem::path& path,
        std::vector<Transaction>& out_transactions);

    [[nodiscard]] static ReplayResult replay(
        World& world,
        std::span<const Transaction> transactions,
        ChangeJournal* capture = nullptr);

private:
    struct Entry {
        Transaction forward;
        World::UndoState undo;
        StateHash post_hash;
    };

    std::vector<Entry> entries_;
};

} // namespace aion
