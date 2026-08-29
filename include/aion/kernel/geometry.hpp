#pragma once

#include "aion/kernel/math.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aion {

using GeometryProviderId = std::uint16_t;
using GeometryResourceId = std::uint32_t;

struct GeometryHandle {
    GeometryProviderId provider{};
    std::uint16_t generation{};
    GeometryResourceId resource{};

    [[nodiscard]] bool valid() const noexcept { return provider != 0 && generation != 0; }
    friend bool operator==(const GeometryHandle&, const GeometryHandle&) = default;
};

static_assert(sizeof(GeometryHandle) == 8, "GeometryHandle must remain compact enough for dense representation tables");

enum class GeometryCapability : std::uint32_t {
    Bounds = 1U << 0U,
    RaySurface = 1U << 1U,
    SignedDistance = 1U << 2U,
    TruncatedSignedDistance = 1U << 3U,
};

class GeometryCapabilityMask final {
public:
    constexpr GeometryCapabilityMask() = default;
    constexpr GeometryCapabilityMask(GeometryCapability capability) : bits_(static_cast<std::uint32_t>(capability)) {}

    [[nodiscard]] static constexpr GeometryCapabilityMask of(std::initializer_list<GeometryCapability> values) noexcept {
        GeometryCapabilityMask mask;
        for (const auto value : values) mask.bits_ |= static_cast<std::uint32_t>(value);
        return mask;
    }

    [[nodiscard]] constexpr bool contains(GeometryCapability capability) const noexcept {
        return (bits_ & static_cast<std::uint32_t>(capability)) != 0U;
    }
    [[nodiscard]] constexpr bool contains_all(GeometryCapabilityMask other) const noexcept {
        return (bits_ & other.bits_) == other.bits_;
    }
    [[nodiscard]] constexpr std::uint32_t bits() const noexcept { return bits_; }

    friend constexpr GeometryCapabilityMask operator|(GeometryCapabilityMask a, GeometryCapabilityMask b) noexcept {
        GeometryCapabilityMask out;
        out.bits_ = a.bits_ | b.bits_;
        return out;
    }

private:
    std::uint32_t bits_{};
};

enum class GeometryQueryStatus : std::uint8_t {
    Ok = 0,
    Miss = 1,
    InvalidHandle = 2,
    UnsupportedCapability = 3,
    NumericalFailure = 4,
};

struct GeometryBoundsResult {
    GeometryQueryStatus status{GeometryQueryStatus::InvalidHandle};
    Aabb bounds{};
};

struct GeometryRayHit {
    GeometryQueryStatus status{GeometryQueryStatus::Miss};
    float t{std::numeric_limits<float>::infinity()};
    Vec3 position{};
    Vec3 normal{};
};

struct GeometryDistanceSample {
    GeometryQueryStatus status{GeometryQueryStatus::UnsupportedCapability};
    float signed_distance{};
    Vec3 gradient{};
};

struct GeometryProviderDescriptor {
    GeometryProviderId id{};
    std::string name;
    GeometryCapabilityMask capabilities;
};

// Provider dispatch is per resource/provider, not per primitive. The function-table shape is
// intentionally friendly to future ABI/GPU provider metadata and avoids per-object virtual objects.
struct GeometryProviderOps {
    std::shared_ptr<const void> context;
    bool (*valid)(const void*, GeometryResourceId, std::uint16_t) noexcept{};
    GeometryBoundsResult (*bounds)(const void*, GeometryResourceId, std::uint16_t) noexcept{};
    GeometryRayHit (*raycast)(const void*, GeometryResourceId, std::uint16_t, const Ray&) noexcept{};
    GeometryDistanceSample (*signed_distance)(const void*, GeometryResourceId, std::uint16_t, Vec3) noexcept{};
    std::size_t (*storage_bytes)(const void*, GeometryResourceId, std::uint16_t) noexcept{};
};

class GeometryKernel final {
public:
    [[nodiscard]] bool register_provider(GeometryProviderDescriptor descriptor, GeometryProviderOps ops, std::string& error);

    [[nodiscard]] bool valid(GeometryHandle handle) const noexcept;
    [[nodiscard]] GeometryCapabilityMask capabilities(GeometryHandle handle) const noexcept;
    [[nodiscard]] std::string_view provider_name(GeometryProviderId id) const noexcept;
    [[nodiscard]] GeometryBoundsResult bounds(GeometryHandle handle) const noexcept;
    [[nodiscard]] GeometryRayHit raycast(GeometryHandle handle, const Ray& ray) const noexcept;
    [[nodiscard]] GeometryDistanceSample signed_distance(GeometryHandle handle, Vec3 point) const noexcept;
    [[nodiscard]] GeometryDistanceSample truncated_signed_distance(GeometryHandle handle, Vec3 point) const noexcept;
    [[nodiscard]] std::size_t storage_bytes(GeometryHandle handle) const noexcept;

private:
    struct ProviderEntry {
        bool registered{false};
        GeometryProviderDescriptor descriptor;
        GeometryProviderOps ops;
    };
    std::vector<ProviderEntry> providers_;
};

struct GeometryRepresentation {
    GeometryHandle handle{};
    std::uint64_t source_revision{};
    float max_geometric_error{}; // conservative world/local-space bound declared by the producer
};

class GeometrySet final {
public:
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    void source_changed() noexcept;
    [[nodiscard]] bool add_representation(GeometryHandle handle, float max_geometric_error, std::string& error);
    [[nodiscard]] std::vector<GeometryRepresentation> eligible(
        const GeometryKernel& kernel,
        GeometryCapabilityMask required,
        float max_geometric_error) const;

private:
    std::uint64_t revision_{1};
    std::vector<GeometryRepresentation> representations_;
};

// Correctness/reference provider for triangle surfaces. It deliberately uses direct triangle
// intersection; optimized clustered/BVH-backed triangle providers are later R4 experiments.
class TriangleReferenceProvider final {
public:
    explicit TriangleReferenceProvider(GeometryProviderId id = 1) noexcept : id_(id) {}

    [[nodiscard]] bool register_with(GeometryKernel& kernel, std::string& error);
    [[nodiscard]] GeometryHandle add_mesh(std::span<const Vec3> vertices, std::span<const std::uint32_t> indices, std::string& error);

private:
    struct Resource {
        std::uint16_t generation{1};
        std::vector<Vec3> vertices;
        std::vector<std::uint32_t> indices;
        Aabb bounds{};
    };

    [[nodiscard]] static bool valid_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;
    [[nodiscard]] static GeometryBoundsResult bounds_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;
    [[nodiscard]] static GeometryRayHit raycast_cb(const void*, GeometryResourceId, std::uint16_t, const Ray&) noexcept;
    [[nodiscard]] static GeometryDistanceSample distance_cb(const void*, GeometryResourceId, std::uint16_t, Vec3) noexcept;
    [[nodiscard]] static std::size_t storage_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;

    struct State { std::vector<Resource> resources; };
    GeometryProviderId id_{1};
    std::shared_ptr<State> state_{std::make_shared<State>()};
};

class AnalyticSdfProvider final {
public:
    explicit AnalyticSdfProvider(GeometryProviderId id = 2) noexcept : id_(id) {}

    [[nodiscard]] bool register_with(GeometryKernel& kernel, std::string& error);
    [[nodiscard]] GeometryHandle add_sphere(Vec3 center, float radius, std::string& error);
    [[nodiscard]] GeometryHandle add_box(Vec3 center, Vec3 half_extent, std::string& error);

private:
    enum class Shape : std::uint8_t { Sphere = 1, Box = 2 };
    struct Resource {
        std::uint16_t generation{1};
        Shape shape{Shape::Sphere};
        Vec3 center{};
        Vec3 extent{}; // sphere: x=radius; box: xyz half extent
        Aabb bounds{};
    };

    [[nodiscard]] static bool valid_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;
    [[nodiscard]] static GeometryBoundsResult bounds_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;
    [[nodiscard]] static GeometryRayHit raycast_cb(const void*, GeometryResourceId, std::uint16_t, const Ray&) noexcept;
    [[nodiscard]] static GeometryDistanceSample distance_cb(const void*, GeometryResourceId, std::uint16_t, Vec3) noexcept;
    [[nodiscard]] static std::size_t storage_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;
    [[nodiscard]] static float distance_value(const Resource&, Vec3) noexcept;
    [[nodiscard]] static Vec3 gradient_value(const Resource&, Vec3) noexcept;

    struct State { std::vector<Resource> resources; };
    GeometryProviderId id_{2};
    std::shared_ptr<State> state_{std::make_shared<State>()};
};

} // namespace aion
