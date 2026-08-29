#include "aion/kernel/device_geometry.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace aion;

[[noreturn]] void fail(const std::string& message) { std::cerr << "R5B FAIL: " << message << '\n'; std::exit(1); }
void require(bool condition, const std::string& message) { if (!condition) fail(message); }

void cube_mesh(std::vector<Vec3>& vertices, std::vector<std::uint32_t>& indices) {
    vertices = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    const std::uint32_t raw[] = {
        0,2,1,0,3,2, 4,5,6,4,6,7,
        0,1,5,0,5,4, 1,2,6,1,6,5,
        2,3,7,2,7,6, 3,0,4,3,4,7};
    indices.assign(std::begin(raw), std::end(raw));
}

DeviceResourceUpload unrelated_upload() {
    DeviceResourceUpload u;
    u.key = work_resource_key(77U, 1U, DeviceResourceClass::Scratch, 0U);
    u.bytes.resize(64U, std::byte{0x5A});
    return u;
}
}

int main() {
    using namespace aion;
    GeometryKernel kernel;
    AnalyticSdfProvider analytic(2U);
    SparseSdfProvider sparse(3U);
    ClusteredTriangleProvider clustered(4U);
    std::string error;
    require(analytic.register_with(kernel, error), error);
    require(sparse.register_with(kernel, error), error);
    require(clustered.register_with(kernel, error), error);

    const auto sphere = analytic.add_sphere({0,0,0}, 1.0F, error);
    require(sphere.valid(), error);
    SparseSdfCompileOptions sdf_options;
    sdf_options.voxel_size = 0.125F;
    const auto sdf = sparse.compile_from(kernel, sphere, sdf_options);
    require(sdf.ok(), sdf.error);

    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> indices;
    cube_mesh(vertices, indices);
    const auto mesh = clustered.add_mesh(vertices, indices, {});
    require(mesh.ok(), mesh.error);

    GeometryDeviceCompilerRegistry registry;
    require(register_sparse_sdf_device_compiler(registry, sparse, 3U, error), error);
    require(register_clustered_triangle_device_compiler(registry, clustered, 4U, error), error);

    DeviceGeometryPackage sdf_pkg_a, sdf_pkg_b, mesh_pkg_a, mesh_pkg_b, mesh_pkg_new_revision;
    require(registry.compile(sdf.handle, 17U, sdf_pkg_a, error), error);
    require(registry.compile(sdf.handle, 17U, sdf_pkg_b, error), error);
    require(registry.compile(mesh.handle, 17U, mesh_pkg_a, error), error);
    require(registry.compile(mesh.handle, 17U, mesh_pkg_b, error), error);
    require(registry.compile(mesh.handle, 18U, mesh_pkg_new_revision, error), error);

    require(sdf_pkg_a.fingerprint == sdf_pkg_b.fingerprint, "sparse package fingerprint is not deterministic");
    require(sdf_pkg_a.archive.topology == sdf_pkg_b.archive.topology && sdf_pkg_a.archive.payloads == sdf_pkg_b.archive.payloads, "sparse package bytes are not deterministic");
    require(mesh_pkg_a.fingerprint == mesh_pkg_b.fingerprint, "cluster package fingerprint is not deterministic");
    require(mesh_pkg_a.archive.topology == mesh_pkg_b.archive.topology && mesh_pkg_a.archive.payloads == mesh_pkg_b.archive.payloads, "cluster package bytes are not deterministic");
    require(mesh_pkg_a.fingerprint != mesh_pkg_new_revision.fingerprint, "source revision did not change package identity");
    require(sdf_pkg_a.archive.payloads.size() == 1U, "sparse package must expose topology + one sample payload");
    require(mesh_pkg_a.archive.payloads.size() == 2U, "cluster package must expose topology + positions + indices");

    const auto mesh_uploads = mesh_pkg_a.residency_uploads();
    require(mesh_uploads.size() == 3U, "cluster package residency shape is wrong");

    // Atomic package test: an unrelated resource is resident. Force enough budget that no eviction is needed,
    // then fail on the third package upload. No partial package may become resident.
    ReferenceDeviceBackend backend;
    const std::size_t budget = 64U + mesh_pkg_a.storage_bytes() + 64U;
    DeviceResidencyManager manager(backend, budget);
    DeviceResourceHandle unrelated{};
    require(manager.ensure(unrelated_upload(), unrelated, error), error);
    const auto before = manager.stats();

    backend.fail_after_successful_uploads(2U);
    std::vector<DeviceResourceHandle> group_handles;
    require(!manager.ensure_group(mesh_uploads, group_handles, error), "injected third-upload failure unexpectedly succeeded");
    require(manager.resident(unrelated), "unrelated resident resource was not restored after failed group");
    for (const auto& upload : mesh_uploads) require(!manager.resident(upload.key), "partial geometry package remained resident after failed group");
    const auto after_failure = manager.stats();
    require(after_failure.resident_bytes == before.resident_bytes && after_failure.resident_resources == before.resident_resources, "failed atomic group changed residency state");
    require(after_failure.resident_bytes == backend.allocated_bytes(), "backend bytes diverged after rollback");

    backend.clear_failure_injection();
    require(manager.ensure_group(mesh_uploads, group_handles, error), error);
    require(group_handles.size() == mesh_uploads.size(), "atomic group did not return every handle");
    for (std::size_t i = 0; i < mesh_uploads.size(); ++i) {
        require(manager.resident(mesh_uploads[i].key), "successful package subresource missing from residency");
        require(manager.resident(group_handles[i]), "returned package handle is not resident");
    }

    // Existing immutable resources must be reused atomically, not duplicated.
    const auto stable_stats = manager.stats();
    std::vector<DeviceResourceHandle> second_handles;
    require(manager.ensure_group(mesh_uploads, second_handles, error), error);
    require(second_handles == group_handles, "idempotent group ensure changed resource handles");
    require(manager.stats().resident_bytes == stable_stats.resident_bytes, "idempotent group ensure duplicated storage");

    std::cout << "R5B PASS sparse_bytes=" << sdf_pkg_a.storage_bytes()
              << " cluster_bytes=" << mesh_pkg_a.storage_bytes()
              << " sparse_fingerprint=" << sdf_pkg_a.fingerprint
              << " cluster_fingerprint=" << mesh_pkg_a.fingerprint << '\n';
    return 0;
}
