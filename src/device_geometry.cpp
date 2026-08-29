#include "aion/kernel/device_geometry.hpp"

#include <algorithm>
#include <utility>

namespace aion {
namespace {
[[nodiscard]] std::uint64_t mix_u64(std::uint64_t hash, std::uint64_t value) noexcept {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash ^= static_cast<std::uint8_t>((value >> shift) & 0xFFU);
        hash *= prime;
    }
    return hash;
}

[[nodiscard]] std::uint64_t package_fingerprint(GeometryHandle handle, std::uint64_t revision, const RepresentationArchive& archive) noexcept {
    std::uint64_t hash = archive_fingerprint(archive);
    hash = mix_u64(hash, handle.provider);
    hash = mix_u64(hash, handle.generation);
    hash = mix_u64(hash, handle.resource);
    return mix_u64(hash, revision);
}
}

std::size_t DeviceGeometryPackage::storage_bytes() const noexcept {
    std::size_t total = archive.topology.size();
    for (const auto& payload : archive.payloads) total += payload.size();
    return total;
}

std::vector<DeviceResourceUpload> DeviceGeometryPackage::residency_uploads(bool pinned) const {
    std::vector<DeviceResourceUpload> uploads;
    uploads.reserve(1U + archive.payloads.size());
    uploads.push_back({
        .key = geometry_resource_key(geometry, source_revision, DeviceResourceClass::GeometryTopology, 0U),
        .bytes = archive.topology,
        .pinned = pinned});
    for (std::size_t i = 0; i < archive.payloads.size(); ++i) {
        uploads.push_back({
            .key = geometry_resource_key(geometry, source_revision, DeviceResourceClass::GeometryPayload, static_cast<std::uint16_t>(i)),
            .bytes = archive.payloads[i],
            .pinned = pinned});
    }
    return uploads;
}

bool GeometryDeviceCompilerRegistry::register_compiler(GeometryProviderId provider, Compiler compiler, std::string& error) {
    if (provider == 0 || !compiler) {
        error = "device geometry compiler requires a valid provider and callback";
        return false;
    }
    if (std::any_of(entries_.begin(), entries_.end(), [provider](const Entry& entry) { return entry.provider == provider; })) {
        error = "device geometry compiler already registered for provider";
        return false;
    }
    entries_.push_back({provider, std::move(compiler)});
    error.clear();
    return true;
}

bool GeometryDeviceCompilerRegistry::compile(GeometryHandle handle, std::uint64_t source_revision, DeviceGeometryPackage& package, std::string& error) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [handle](const Entry& entry) { return entry.provider == handle.provider; });
    if (it == entries_.end()) {
        error = "no device geometry compiler registered for provider";
        return false;
    }
    package = {};
    return it->compiler(handle, source_revision, package, error);
}

bool register_sparse_sdf_device_compiler(
    GeometryDeviceCompilerRegistry& registry,
    const SparseSdfProvider& provider,
    GeometryProviderId provider_id,
    std::string& error) {
    return registry.register_compiler(provider_id, [&provider](GeometryHandle handle, std::uint64_t revision, DeviceGeometryPackage& package, std::string& compile_error) {
        RepresentationArchive archive;
        if (!provider.export_archive(handle, archive, compile_error)) return false;
        package.geometry = handle;
        package.source_revision = revision;
        package.archive = std::move(archive);
        package.fingerprint = package_fingerprint(handle, revision, package.archive);
        compile_error.clear();
        return true;
    }, error);
}

bool register_clustered_triangle_device_compiler(
    GeometryDeviceCompilerRegistry& registry,
    const ClusteredTriangleProvider& provider,
    GeometryProviderId provider_id,
    std::string& error) {
    return registry.register_compiler(provider_id, [&provider](GeometryHandle handle, std::uint64_t revision, DeviceGeometryPackage& package, std::string& compile_error) {
        RepresentationArchive archive;
        if (!provider.export_archive(handle, archive, compile_error)) return false;
        package.geometry = handle;
        package.source_revision = revision;
        package.archive = std::move(archive);
        package.fingerprint = package_fingerprint(handle, revision, package.archive);
        compile_error.clear();
        return true;
    }, error);
}

} // namespace aion
