#pragma once

#include "aion/kernel/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace aion {

enum class DeviceResourceClass : std::uint8_t {
    GeometryTopology = 1,
    GeometryPayload = 2,
    WorkInput = 3,
    WorkOutput = 4,
    Scratch = 5,
};

enum class DeviceResourceNamespace : std::uint8_t {
    Geometry = 1,
    Work = 2,
    Global = 3,
};

struct DeviceResourceOwner {
    DeviceResourceNamespace name_space{DeviceResourceNamespace::Global};
    std::uint64_t object_id{};
    std::uint64_t revision{};
    friend bool operator==(const DeviceResourceOwner&, const DeviceResourceOwner&) = default;
};

struct DeviceResourceKey {
    DeviceResourceOwner owner{};
    DeviceResourceClass resource_class{DeviceResourceClass::GeometryPayload};
    std::uint16_t subresource{};
    friend bool operator==(const DeviceResourceKey&, const DeviceResourceKey&) = default;
};

struct DeviceResourceKeyHash {
    [[nodiscard]] std::size_t operator()(const DeviceResourceKey& key) const noexcept {
        constexpr std::uint64_t offset = 1469598103934665603ULL;
        constexpr std::uint64_t prime = 1099511628211ULL;
        std::uint64_t hash = offset;
        const auto mix = [&](std::uint64_t value, std::uint64_t& state) {
            for (unsigned shift = 0; shift < 64; shift += 8) {
                state ^= static_cast<std::uint8_t>((value >> shift) & 0xFFU);
                state *= prime;
            }
        };
        mix(static_cast<std::uint8_t>(key.owner.name_space), hash);
        mix(key.owner.object_id, hash);
        mix(key.owner.revision, hash);
        mix(static_cast<std::uint8_t>(key.resource_class), hash);
        mix(key.subresource, hash);
        return static_cast<std::size_t>(hash);
    }
};

[[nodiscard]] constexpr std::uint64_t pack_geometry_handle(GeometryHandle handle) noexcept {
    return (static_cast<std::uint64_t>(handle.provider) << 48U)
        | (static_cast<std::uint64_t>(handle.generation) << 32U)
        | static_cast<std::uint64_t>(handle.resource);
}

[[nodiscard]] constexpr DeviceResourceKey geometry_resource_key(
    GeometryHandle handle,
    std::uint64_t source_revision,
    DeviceResourceClass resource_class,
    std::uint16_t subresource = 0U) noexcept {
    return {{DeviceResourceNamespace::Geometry, pack_geometry_handle(handle), source_revision}, resource_class, subresource};
}

[[nodiscard]] constexpr DeviceResourceKey work_resource_key(
    std::uint64_t object_id,
    std::uint64_t revision,
    DeviceResourceClass resource_class,
    std::uint16_t subresource = 0U) noexcept {
    return {{DeviceResourceNamespace::Work, object_id, revision}, resource_class, subresource};
}

struct DeviceResourceHandle {
    std::uint32_t slot{};
    std::uint32_t generation{};
    [[nodiscard]] bool valid() const noexcept { return generation != 0U; }
    friend bool operator==(const DeviceResourceHandle&, const DeviceResourceHandle&) = default;
};

struct DeviceResourceUpload {
    DeviceResourceKey key{};
    std::vector<std::byte> bytes;
    bool pinned{false};
};

class ReferenceDeviceBackend final {
public:
    using Token = std::uint64_t;

    [[nodiscard]] bool upload(std::span<const std::byte> bytes, Token& token, std::string& error);
    [[nodiscard]] bool restore(std::span<const std::byte> bytes, Token& token, std::string& error);
    void destroy(Token token) noexcept;
    [[nodiscard]] bool alive(Token token) const noexcept;
    [[nodiscard]] std::size_t allocated_bytes() const noexcept;

    void fail_after_successful_uploads(std::size_t successes_before_failure) noexcept;
    void clear_failure_injection() noexcept;

private:
    struct Allocation { Token token{}; std::vector<std::byte> bytes; };
    [[nodiscard]] bool upload_impl(std::span<const std::byte> bytes, Token& token, std::string& error, bool honor_failure);

    std::vector<Allocation> allocations_;
    Token next_token_{1};
    bool failure_enabled_{false};
    std::size_t successes_before_failure_{};
    std::size_t successes_since_injection_{};
};

struct DeviceResidencyStats {
    std::size_t budget_bytes{};
    std::size_t resident_bytes{};
    std::size_t resident_resources{};
};

class DeviceResidencyManager final {
public:
    explicit DeviceResidencyManager(ReferenceDeviceBackend& backend, std::size_t budget_bytes)
        : backend_(backend), budget_bytes_(budget_bytes) {}

    [[nodiscard]] bool ensure(const DeviceResourceUpload& upload, DeviceResourceHandle& handle, std::string& error);
    [[nodiscard]] bool ensure_group(std::span<const DeviceResourceUpload> uploads, std::vector<DeviceResourceHandle>& handles, std::string& error);
    [[nodiscard]] bool evict(const DeviceResourceKey& key, std::string& error);

    [[nodiscard]] bool resident(DeviceResourceHandle handle) const noexcept;
    [[nodiscard]] bool resident(const DeviceResourceKey& key) const noexcept;
    [[nodiscard]] DeviceResourceHandle handle_for(const DeviceResourceKey& key) const noexcept;
    [[nodiscard]] std::span<const std::byte> host_bytes(DeviceResourceHandle handle) const noexcept;
    [[nodiscard]] DeviceResidencyStats stats() const noexcept;

private:
    struct Entry {
        bool occupied{false};
        std::uint32_t generation{1};
        DeviceResourceKey key{};
        std::vector<std::byte> host;
        ReferenceDeviceBackend::Token backend_token{};
        bool pinned{false};
        std::uint64_t last_use{};
    };

    [[nodiscard]] std::size_t find_key(const DeviceResourceKey& key) const noexcept;
    [[nodiscard]] std::size_t allocate_slot();
    void touch(std::size_t slot) noexcept;

    ReferenceDeviceBackend& backend_;
    std::size_t budget_bytes_{};
    std::size_t resident_bytes_{};
    std::uint64_t clock_{};
    std::vector<Entry> entries_;
    std::unordered_map<DeviceResourceKey, std::size_t, DeviceResourceKeyHash> key_to_slot_;
    std::vector<std::size_t> free_slots_;
};

} // namespace aion
