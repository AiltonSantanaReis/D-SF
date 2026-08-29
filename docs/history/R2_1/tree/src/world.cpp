#include "aion/kernel/world.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace aion {
namespace {

class Sha256 final {
public:
    Sha256() { reset(); }

    void update(const std::uint8_t* data, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            buffer_[buffer_len_++] = data[i];
            if (buffer_len_ == 64) {
                transform(buffer_.data());
                bit_len_ += 512;
                buffer_len_ = 0;
            }
        }
    }

    template <typename T>
    void little_endian(T value) {
        static_assert(std::is_unsigned_v<T>);
        std::array<std::uint8_t, sizeof(T)> bytes{};
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & static_cast<T>(0xffU));
        }
        update(bytes.data(), bytes.size());
    }

    StateHash finish() {
        const std::uint64_t total_bits = bit_len_ + static_cast<std::uint64_t>(buffer_len_) * 8ULL;

        buffer_[buffer_len_++] = 0x80U;
        if (buffer_len_ > 56) {
            while (buffer_len_ < 64) buffer_[buffer_len_++] = 0;
            transform(buffer_.data());
            buffer_len_ = 0;
        }
        while (buffer_len_ < 56) buffer_[buffer_len_++] = 0;

        for (int i = 7; i >= 0; --i) {
            buffer_[buffer_len_++] = static_cast<std::uint8_t>((total_bits >> (i * 8)) & 0xffULL);
        }
        transform(buffer_.data());

        StateHash result{};
        for (std::size_t i = 0; i < state_.size(); ++i) {
            result.bytes[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24U) & 0xffU);
            result.bytes[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16U) & 0xffU);
            result.bytes[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8U) & 0xffU);
            result.bytes[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xffU);
        }
        return result;
    }

private:
    static constexpr std::array<std::uint32_t, 64> k_{
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
    };

    static constexpr std::uint32_t rotr(std::uint32_t x, std::uint32_t n) noexcept {
        return (x >> n) | (x << (32U - n));
    }

    void reset() {
        state_ = {0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
        buffer_.fill(0);
        buffer_len_ = 0;
        bit_len_ = 0;
    }

    void transform(const std::uint8_t* block) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            const auto j = i * 4;
            w[i] = (static_cast<std::uint32_t>(block[j]) << 24U) |
                   (static_cast<std::uint32_t>(block[j + 1]) << 16U) |
                   (static_cast<std::uint32_t>(block[j + 2]) << 8U) |
                   static_cast<std::uint32_t>(block[j + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const auto s0 = rotr(w[i - 15], 7U) ^ rotr(w[i - 15], 18U) ^ (w[i - 15] >> 3U);
            const auto s1 = rotr(w[i - 2], 17U) ^ rotr(w[i - 2], 19U) ^ (w[i - 2] >> 10U);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        auto a = state_[0]; auto b = state_[1]; auto c = state_[2]; auto d = state_[3];
        auto e = state_[4]; auto f = state_[5]; auto g = state_[6]; auto h = state_[7];

        for (std::size_t i = 0; i < 64; ++i) {
            const auto s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
            const auto ch = (e & f) ^ ((~e) & g);
            const auto temp1 = h + s1 + ch + k_[i] + w[i];
            const auto s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = s0 + maj;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }

        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_len_{};
    std::uint64_t bit_len_{};
};

[[nodiscard]] bool finite_vec(const Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

std::string StateHash::hex() const {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto b : bytes) out << std::setw(2) << static_cast<unsigned>(b);
    return out.str();
}

World::World(std::size_t reserve_entities) {
    states_.reserve(reserve_entities + 1);
    positions_.reserve(reserve_entities + 1);
    velocities_.reserve(reserve_entities + 1);
    states_.resize(1);
    positions_.resize(1);
    velocities_.resize(1);
}

void World::ensure(EntityId id) {
    const auto required = static_cast<std::size_t>(id + 1);
    if (states_.size() < required) {
        states_.resize(required);
        positions_.resize(required);
        velocities_.resize(required);
    }
}

bool World::validate(const Transaction& transaction, std::string_view& error) const noexcept {
    if (transaction.id <= last_transaction_id_) {
        error = "transaction id must be strictly monotonic";
        return false;
    }

    auto simulated_next = next_entity_id_;
    for (const auto& m : transaction.mutations) {
        switch (m.kind) {
            case MutationKind::CreateEntity:
            case MutationKind::DestroyEntity:
            case MutationKind::SetPosition:
            case MutationKind::SetVelocity:
            case MutationKind::SetHealth:
            case MutationKind::AdvanceReference:
                break;
            default:
                error = "unknown mutation kind";
                return false;
        }

        if (m.kind == MutationKind::AdvanceReference) {
            if (m.entity != 0) {
                error = "AdvanceReference must target entity 0";
                return false;
            }
            if (!std::isfinite(m.vec.x) || m.vec.x < 0.0F) {
                error = "AdvanceReference dt must be finite and non-negative";
                return false;
            }
            continue;
        }

        if (m.entity == 0) {
            error = "entity id 0 is reserved for world-scope mutations";
            return false;
        }

        if ((m.kind == MutationKind::SetPosition || m.kind == MutationKind::SetVelocity) && !finite_vec(m.vec)) {
            error = "vector mutation must contain finite values";
            return false;
        }

        if (m.kind == MutationKind::CreateEntity) {
            if (simulated_next == std::numeric_limits<EntityId>::max() ||
                simulated_next >= static_cast<EntityId>(std::numeric_limits<std::size_t>::max())) {
                error = "entity id space exhausted on this platform";
                return false;
            }
            if (m.entity != simulated_next) {
                error = "CreateEntity must allocate the next sequential entity id";
                return false;
            }
            ++simulated_next;
            continue;
        }

        if (m.entity >= simulated_next) {
            error = "mutation references an entity identity that has not been created";
            return false;
        }
    }
    return true;
}

void World::advance_reference(float dt) noexcept {
    const auto n = states_.size();
    for (std::size_t i = 1; i < n; ++i) {
        if (!states_[i].alive) continue;
        positions_[i].x += velocities_[i].x * dt;
        positions_[i].y += velocities_[i].y * dt;
        positions_[i].z += velocities_[i].z * dt;
    }
}

CommitResult World::commit(const Transaction& transaction) {
    std::string_view error{};
    if (!validate(transaction, error)) {
        return {false, 0, error};
    }

    for (const auto& m : transaction.mutations) {
        if (m.kind == MutationKind::AdvanceReference) {
            advance_reference(m.vec.x);
            continue;
        }

        if (m.kind == MutationKind::CreateEntity) {
            ensure(m.entity);
            const auto index = static_cast<std::size_t>(m.entity);
            states_[index] = EntityState{.alive = true, .health = 100};
            positions_[index] = {};
            velocities_[index] = {};
            ++living_entities_;
            ++next_entity_id_;
            continue;
        }

        const auto index = static_cast<std::size_t>(m.entity);
        switch (m.kind) {
            case MutationKind::DestroyEntity:
                if (states_[index].alive) {
                    states_[index].alive = false;
                    --living_entities_;
                }
                break;
            case MutationKind::SetPosition:
                positions_[index] = m.vec;
                break;
            case MutationKind::SetVelocity:
                velocities_[index] = m.vec;
                break;
            case MutationKind::SetHealth:
                states_[index].health = m.value;
                break;
            case MutationKind::CreateEntity:
            case MutationKind::AdvanceReference:
                break;
        }
    }

    last_transaction_id_ = transaction.id;
    return {true, transaction.mutations.size(), {}};
}

bool World::alive(EntityId id) const noexcept {
    const auto i = static_cast<std::size_t>(id);
    return i < states_.size() && states_[i].alive;
}

std::optional<Vec3> World::position(EntityId id) const noexcept {
    const auto i = static_cast<std::size_t>(id);
    if (!alive(id) || i >= positions_.size()) return std::nullopt;
    return positions_[i];
}

std::optional<Vec3> World::velocity(EntityId id) const noexcept {
    const auto i = static_cast<std::size_t>(id);
    if (!alive(id) || i >= velocities_.size()) return std::nullopt;
    return velocities_[i];
}

std::optional<std::uint32_t> World::health(EntityId id) const noexcept {
    const auto i = static_cast<std::size_t>(id);
    if (!alive(id) || i >= states_.size()) return std::nullopt;
    return states_[i].health;
}

World::UndoState World::capture_undo(const Transaction& transaction) const {
    UndoState undo{
        .next_entity_id = next_entity_id_,
        .last_transaction_id = last_transaction_id_,
        .living_entities = living_entities_,
        .storage_size = states_.size(),
        .entities = {},
    };

    bool captures_all = false;
    for (const auto& m : transaction.mutations) {
        if (m.kind == MutationKind::AdvanceReference) {
            captures_all = true;
            break;
        }
    }

    std::vector<EntityId> ids;
    if (captures_all) {
        ids.reserve(static_cast<std::size_t>(next_entity_id_ - 1));
        for (EntityId id = 1; id < next_entity_id_; ++id) ids.push_back(id);
    } else {
        ids.reserve(transaction.mutations.size());
        for (const auto& m : transaction.mutations) {
            if (m.entity != 0 && m.entity < next_entity_id_) ids.push_back(m.entity);
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }

    undo.entities.reserve(ids.size());
    for (const auto id : ids) {
        const auto i = static_cast<std::size_t>(id);
        undo.entities.push_back({id, states_[i], positions_[i], velocities_[i]});
    }
    return undo;
}

void World::restore_undo(const UndoState& undo) noexcept {
    if (states_.size() > undo.storage_size) {
        states_.resize(undo.storage_size);
        positions_.resize(undo.storage_size);
        velocities_.resize(undo.storage_size);
    }

    for (const auto& e : undo.entities) {
        const auto i = static_cast<std::size_t>(e.id);
        if (i >= states_.size()) continue;
        states_[i] = e.state;
        positions_[i] = e.position;
        velocities_[i] = e.velocity;
    }

    next_entity_id_ = undo.next_entity_id;
    last_transaction_id_ = undo.last_transaction_id;
    living_entities_ = undo.living_entities;
}

StateHash World::state_hash() const {
    Sha256 h;
    constexpr std::uint64_t schema_version = 1;
    h.little_endian(schema_version);
    h.little_endian(next_entity_id_);
    h.little_endian(last_transaction_id_);
    h.little_endian(static_cast<std::uint64_t>(living_entities_));

    for (EntityId id = 1; id < next_entity_id_; ++id) {
        const auto i = static_cast<std::size_t>(id);
        h.little_endian(id);
        h.little_endian(static_cast<std::uint8_t>(states_[i].alive ? 1U : 0U));
        h.little_endian(states_[i].health);
        h.little_endian(std::bit_cast<std::uint32_t>(positions_[i].x));
        h.little_endian(std::bit_cast<std::uint32_t>(positions_[i].y));
        h.little_endian(std::bit_cast<std::uint32_t>(positions_[i].z));
        h.little_endian(std::bit_cast<std::uint32_t>(velocities_[i].x));
        h.little_endian(std::bit_cast<std::uint32_t>(velocities_[i].y));
        h.little_endian(std::bit_cast<std::uint32_t>(velocities_[i].z));
    }
    return h.finish();
}

} // namespace aion
