#pragma once

#include "aion/kernel/geometry.hpp"
#include "aion/kernel/representation_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace aion {

enum class ClusteredTriangleEncoding : std::uint8_t {
    Float32 = 0,
    QuantizedU16 = 1,
};

struct ClusteredTriangleOptions {
    std::uint16_t max_vertices_per_cluster{64};
    std::uint16_t max_triangles_per_cluster{124};
    ClusteredTriangleEncoding encoding{ClusteredTriangleEncoding::QuantizedU16};
};

struct ClusteredTriangleStats {
    std::size_t source_vertices{};
    std::size_t source_triangles{};
    std::size_t clusters{};
    std::size_t cluster_vertices{};
    std::size_t bvh_nodes{};
    std::size_t payload_bytes{};
    std::size_t topology_bytes{};
    std::size_t storage_bytes{};
    std::size_t source_raw_bytes{};
    float max_geometric_error{};
};

struct ClusteredTriangleBuildResult {
    GeometryHandle handle{};
    float max_geometric_error{};
    ClusteredTriangleStats stats{};
    std::string error;
    [[nodiscard]] bool ok() const noexcept { return handle.valid() && error.empty(); }
};

// R4C CPU/reference clustered triangle representation. Clusters are bounded-size units with
// local uint8 indices. Position quantization is shared across the whole resource, so a source
// vertex replicated into adjacent clusters decodes identically and cannot open a cluster seam.
class ClusteredTriangleProvider final {
public:
    struct State;

    explicit ClusteredTriangleProvider(GeometryProviderId id = 4);

    [[nodiscard]] bool register_with(GeometryKernel& kernel, std::string& error);
    [[nodiscard]] ClusteredTriangleBuildResult add_mesh(
        std::span<const Vec3> vertices,
        std::span<const std::uint32_t> indices,
        const ClusteredTriangleOptions& options);
    [[nodiscard]] ClusteredTriangleStats stats(GeometryHandle handle) const noexcept;
    [[nodiscard]] bool export_archive(GeometryHandle handle, RepresentationArchive& archive, std::string& error) const;

private:
    [[nodiscard]] static bool valid_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;
    [[nodiscard]] static GeometryBoundsResult bounds_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;
    [[nodiscard]] static GeometryRayHit raycast_cb(const void*, GeometryResourceId, std::uint16_t, const Ray&) noexcept;
    [[nodiscard]] static GeometryDistanceSample distance_cb(const void*, GeometryResourceId, std::uint16_t, Vec3) noexcept;
    [[nodiscard]] static std::size_t storage_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;

    GeometryProviderId id_{4};
    std::shared_ptr<State> state_;
};

} // namespace aion
