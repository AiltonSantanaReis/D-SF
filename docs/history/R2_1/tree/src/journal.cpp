#include "aion/kernel/journal.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <limits>
#include <type_traits>

namespace aion {
namespace {

constexpr std::array<char, 8> kMagic{'A','I','O','N','J','N','L','1'};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint64_t kMaxTransactions = 100'000'000ULL;
constexpr std::uint64_t kMaxMutationsPerTransaction = 100'000'000ULL;

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
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        wide |= static_cast<std::uint64_t>(bytes[i]) << (i * 8U);
    }
    value = static_cast<T>(wide);
    return true;
}

bool valid_kind(std::uint8_t raw) noexcept {
    return raw >= static_cast<std::uint8_t>(MutationKind::CreateEntity) &&
           raw <= static_cast<std::uint8_t>(MutationKind::AdvanceReference);
}

} // namespace

CommitResult ChangeJournal::commit(World& world, const Transaction& transaction) {
    auto undo = world.capture_undo(transaction);
    const auto result = world.commit(transaction);
    if (!result.committed) return result;

    entries_.push_back(Entry{
        .forward = transaction,
        .undo = std::move(undo),
        .post_hash = world.state_hash(),
    });
    return result;
}

bool ChangeJournal::rollback_last(World& world) {
    if (entries_.empty()) return false;
    const auto& entry = entries_.back();

    // An undo record is only valid against exactly the state it was captured from.
    if (world.state_hash() != entry.post_hash) return false;

    world.restore_undo(entry.undo);
    entries_.pop_back();
    return true;
}

bool ChangeJournal::rollback_to(World& world, TransactionId transaction_id) {
    if (transaction_id > world.last_transaction_id()) return false;
    if (transaction_id != 0) {
        bool found = false;
        for (const auto& e : entries_) {
            if (e.forward.id == transaction_id) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }

    while (!entries_.empty() && entries_.back().forward.id > transaction_id) {
        if (!rollback_last(world)) return false;
    }
    return world.last_transaction_id() == transaction_id;
}

std::vector<Transaction> ChangeJournal::transactions() const {
    std::vector<Transaction> result;
    result.reserve(entries_.size());
    for (const auto& e : entries_) result.push_back(e.forward);
    return result;
}

std::optional<StateHash> ChangeJournal::hash_after(TransactionId transaction_id) const {
    for (const auto& e : entries_) {
        if (e.forward.id == transaction_id) return e.post_hash;
    }
    return std::nullopt;
}

JournalResult ChangeJournal::save_transactions(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return {false, "unable to open journal for writing"};

    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    if (!write_le(out, kFormatVersion) || !write_le(out, static_cast<std::uint64_t>(entries_.size()))) {
        return {false, "failed writing journal header"};
    }

    for (const auto& entry : entries_) {
        const auto& tx = entry.forward;
        if (!write_le(out, tx.id) || !write_le(out, static_cast<std::uint64_t>(tx.mutations.size()))) {
            return {false, "failed writing transaction header"};
        }

        for (const auto& m : tx.mutations) {
            if (!write_le(out, static_cast<std::uint8_t>(m.kind)) ||
                !write_le(out, m.entity) ||
                !write_le(out, std::bit_cast<std::uint32_t>(m.vec.x)) ||
                !write_le(out, std::bit_cast<std::uint32_t>(m.vec.y)) ||
                !write_le(out, std::bit_cast<std::uint32_t>(m.vec.z)) ||
                !write_le(out, m.value)) {
                return {false, "failed writing mutation"};
            }
        }
    }

    out.flush();
    if (!out) return {false, "failed flushing journal"};
    return {true, {}};
}

JournalResult ChangeJournal::load_transactions(
    const std::filesystem::path& path,
    std::vector<Transaction>& out_transactions) {

    out_transactions.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return {false, "unable to open journal for reading"};

    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) return {false, "invalid journal magic"};

    std::uint32_t version{};
    std::uint64_t tx_count{};
    if (!read_le(in, version) || !read_le(in, tx_count)) return {false, "truncated journal header"};
    if (version != kFormatVersion) return {false, "unsupported journal format version"};
    if (tx_count > kMaxTransactions) return {false, "journal transaction count exceeds safety limit"};

    out_transactions.reserve(static_cast<std::size_t>(tx_count));
    TransactionId previous_id = 0;

    for (std::uint64_t ti = 0; ti < tx_count; ++ti) {
        Transaction tx{};
        std::uint64_t mutation_count{};
        if (!read_le(in, tx.id) || !read_le(in, mutation_count)) {
            out_transactions.clear();
            return {false, "truncated transaction header"};
        }
        if (tx.id <= previous_id) {
            out_transactions.clear();
            return {false, "journal transaction ids are not strictly monotonic"};
        }
        if (mutation_count > kMaxMutationsPerTransaction) {
            out_transactions.clear();
            return {false, "mutation count exceeds safety limit"};
        }
        previous_id = tx.id;
        tx.mutations.reserve(static_cast<std::size_t>(mutation_count));

        for (std::uint64_t mi = 0; mi < mutation_count; ++mi) {
            std::uint8_t raw_kind{};
            Mutation m{};
            std::uint32_t x_bits{}, y_bits{}, z_bits{};
            if (!read_le(in, raw_kind) || !read_le(in, m.entity) ||
                !read_le(in, x_bits) || !read_le(in, y_bits) || !read_le(in, z_bits) ||
                !read_le(in, m.value)) {
                out_transactions.clear();
                return {false, "truncated mutation"};
            }
            if (!valid_kind(raw_kind)) {
                out_transactions.clear();
                return {false, "journal contains unknown mutation kind"};
            }
            m.kind = static_cast<MutationKind>(raw_kind);
            m.vec = {
                std::bit_cast<float>(x_bits),
                std::bit_cast<float>(y_bits),
                std::bit_cast<float>(z_bits),
            };
            tx.mutations.push_back(m);
        }
        out_transactions.push_back(std::move(tx));
    }

    // Exact format: trailing bytes are treated as corruption/version mismatch.
    char trailing{};
    if (in.read(&trailing, 1)) {
        out_transactions.clear();
        return {false, "journal contains trailing bytes"};
    }

    return {true, {}};
}

ReplayResult ChangeJournal::replay(
    World& world,
    std::span<const Transaction> transactions,
    ChangeJournal* capture) {

    std::size_t applied = 0;
    for (const auto& tx : transactions) {
        const auto result = capture ? capture->commit(world, tx) : world.commit(tx);
        if (!result.committed) {
            return {false, applied, tx.id, std::string(result.error)};
        }
        ++applied;
    }
    return {true, applied, 0, {}};
}

} // namespace aion
