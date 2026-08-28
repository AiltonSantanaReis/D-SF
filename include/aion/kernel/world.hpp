#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aion {

using EntityId = std::uint64_t;
using TransactionId = std::uint64_t;

struct Vec3 {
    float x{};
    float y{};
    float z{};

    friend bool operator==(const Vec3&, const Vec3&) = default;
};

struct EntityState {
    bool alive{false};
    std::uint32_t health{100};

    friend bool operator==(const EntityState&, const EntityState&) = default;
};

struct StateHash {
    std::array<std::uint8_t, 32> bytes{};

    [[nodiscard]] std::string hex() const;
    friend bool operator==(const StateHash&, const StateHash&) = default;
};

enum class MutationKind : std::uint8_t {
    CreateEntity = 1,
    DestroyEntity = 2,
    SetPosition = 3,
    SetVelocity = 4,
    SetHealth = 5,
    AdvanceReference = 6,
};

struct Mutation {
    MutationKind kind{};
    EntityId entity{};
    Vec3 vec{};
    std::uint32_t value{};
};

struct Transaction {
    TransactionId id{};
    std::vector<Mutation> mutations;
};

struct CommitResult {
    bool committed{false};
    std::size_t applied_mutations{};
    std::string_view error{};
};

class ChangeJournal;
class PatchJournal;
class PatchAccess;
struct PatchTransaction;
enum class PatchApplyMode : std::uint8_t;
struct PatchCommitResult;

class World final {
public:
    explicit World(std::size_t reserve_entities = 0);

    // Entity IDs are allocated by CreateEntity itself. This accessor is read-only
    // and allows transaction builders to request the next legal identity.
    [[nodiscard]] EntityId next_entity_id() const noexcept { return next_entity_id_; }

    [[nodiscard]] CommitResult commit(const Transaction& transaction);

    [[nodiscard]] bool alive(EntityId id) const noexcept;
    [[nodiscard]] std::optional<Vec3> position(EntityId id) const noexcept;
    [[nodiscard]] std::optional<Vec3> velocity(EntityId id) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> health(EntityId id) const noexcept;

    [[nodiscard]] std::size_t entity_capacity() const noexcept { return states_.size(); }
    [[nodiscard]] std::size_t living_entities() const noexcept { return living_entities_; }
    [[nodiscard]] TransactionId last_transaction_id() const noexcept { return last_transaction_id_; }

    // Canonical SHA-256 over authoritative state and identity metadata.
    [[nodiscard]] StateHash state_hash() const;

private:
    friend class ChangeJournal;
    friend class PatchJournal;
    friend class PatchAccess;
    friend PatchCommitResult commit_patch_transaction(World&, const PatchTransaction&, PatchApplyMode, std::size_t);

    struct UndoEntity {
        EntityId id{};
        EntityState state{};
        Vec3 position{};
        Vec3 velocity{};
    };

    struct UndoState {
        EntityId next_entity_id{};
        TransactionId last_transaction_id{};
        std::size_t living_entities{};
        std::size_t storage_size{};
        std::vector<UndoEntity> entities;
    };

    void ensure(EntityId id);
    [[nodiscard]] bool validate(const Transaction& transaction, std::string_view& error) const noexcept;
    void advance_reference(float dt) noexcept;

    [[nodiscard]] UndoState capture_undo(const Transaction& transaction) const;
    void restore_undo(const UndoState& undo) noexcept;

    EntityId next_entity_id_{1};
    TransactionId last_transaction_id_{0};
    std::size_t living_entities_{0};

    // Structure of Arrays (SoA): layout chosen to make later CPU SIMD/GPU backends possible.
    std::vector<EntityState> states_;
    std::vector<Vec3> positions_;
    std::vector<Vec3> velocities_;
};

} // namespace aion
