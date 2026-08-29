#pragma once

#include "aion/kernel/clustered_triangle.hpp"
#include "aion/kernel/device.hpp"
#include "aion/kernel/representation_archive.hpp"
#include "aion/kernel/sparse_sdf.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aion {

struct DeviceGeometryPackage {
    GeometryHandle geometry{};
    std::uint64_t source_revision{};
    RepresentationArchive archive;
    std::uint64_t fingerprint{};

    [[nodiscard]] std::size_t storage_bytes() const noexcept;
    [[nodiscard]] std::vector<DeviceResourceUpload> residency_uploads(bool pinned = false) const;
};

class GeometryDeviceCompilerRegistry final {
public:
    using Compiler = std::function<bool(GeometryHandle, std::uint64_t, DeviceGeometryPackage&, std::string&)>;

    [[nodiscard]] bool register_compiler(GeometryProviderId provider, Compiler compiler, std::string& error);
    [[nodiscard]] bool compile(GeometryHandle handle, std::uint64_t source_revision, DeviceGeometryPackage& package, std::string& error) const;

private:
    struct Entry { GeometryProviderId provider{}; Compiler compiler; };
    std::vector<Entry> entries_;
};

[[nodiscard]] bool register_sparse_sdf_device_compiler(
    GeometryDeviceCompilerRegistry& registry,
    const SparseSdfProvider& provider,
    GeometryProviderId provider_id,
    std::string& error);

[[nodiscard]] bool register_clustered_triangle_device_compiler(
    GeometryDeviceCompilerRegistry& registry,
    const ClusteredTriangleProvider& provider,
    GeometryProviderId provider_id,
    std::string& error);

} // namespace aion
