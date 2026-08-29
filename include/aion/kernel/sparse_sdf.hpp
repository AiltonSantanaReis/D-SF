#pragma once

#include "aion/kernel/geometry.hpp"
#include "aion/kernel/representation_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace aion {

struct SparseSdfSourceCertificate {
    float max_distance_error{0.0F};
    float lipschitz_bound{1.0F};
};

struct SparseSdfCompileOptions {
    float voxel_size{0.03125F};
    float half_band_voxels{3.0F};
    SparseSdfSourceCertificate source{};
};

struct SparseSdfStats {
    std::size_t root_entries{};
    std::size_t negative_root_tiles{};
    std::size_t upper_nodes{};
    std::size_t lower_nodes{};
    std::size_t negative_upper_tiles{};
    std::size_t negative_lower_tiles{};
    std::size_t leaf_bricks{};
    std::size_t quantized_samples{};
    std::size_t source_samples{};
    std::size_t storage_bytes{};
    std::size_t topology_bytes{};
    std::size_t sample_payload_bytes{};
    std::size_t dense_equivalent_samples{};
    std::size_t dense_equivalent_bytes{}; // float32 baseline
    std::size_t dense_equivalent_quantized_bytes{}; // int16 baseline, same value precision class
    float voxel_size{};
    float band_distance{};
    float max_geometric_error{};
};

struct SparseSdfBuildResult {
    GeometryHandle handle{};
    float max_geometric_error{};
    SparseSdfStats stats{};
    std::string error;
    [[nodiscard]] bool ok() const noexcept { return handle.valid() && error.empty(); }
};

// Static-topology sparse narrow-band SDF. Internally it uses a high-branching hierarchy,
// bitmasked uniform tiles, contiguous child ranges, and quantized narrow-band bricks.
class SparseSdfProvider final {
public:
    struct State; // implementation-only state; definition remains in sparse_sdf.cpp

    explicit SparseSdfProvider(GeometryProviderId id = 3);

    [[nodiscard]] bool register_with(GeometryKernel& kernel, std::string& error);
    [[nodiscard]] SparseSdfBuildResult compile_from(
        const GeometryKernel& kernel,
        GeometryHandle source,
        const SparseSdfCompileOptions& options);
    [[nodiscard]] SparseSdfStats stats(GeometryHandle handle) const noexcept;
    [[nodiscard]] bool export_archive(GeometryHandle handle, RepresentationArchive& archive, std::string& error) const;

private:
    [[nodiscard]] static bool valid_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;
    [[nodiscard]] static GeometryBoundsResult bounds_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;
    [[nodiscard]] static GeometryRayHit raycast_cb(const void*, GeometryResourceId, std::uint16_t, const Ray&) noexcept;
    [[nodiscard]] static GeometryDistanceSample distance_cb(const void*, GeometryResourceId, std::uint16_t, Vec3) noexcept;
    [[nodiscard]] static std::size_t storage_cb(const void*, GeometryResourceId, std::uint16_t) noexcept;

    GeometryProviderId id_{3};
    std::shared_ptr<State> state_;
};

} // namespace aion
