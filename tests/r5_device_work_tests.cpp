#include "aion/kernel/device_work.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace aion;

[[noreturn]] void fail(const std::string& message) { std::cerr << "R5C FAIL: " << message << '\n'; std::exit(1); }
void require(bool condition, const std::string& message) { if (!condition) fail(message); }

void cube_mesh(std::vector<Vec3>& vertices, std::vector<std::uint32_t>& indices) {
    vertices = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    const std::uint32_t raw[] = {
        0,2,1,0,3,2, 4,5,6,4,6,7,
        0,1,5,0,5,4, 1,2,6,1,6,5,
        2,3,7,2,7,6, 3,0,4,3,4,7};
    indices.assign(std::begin(raw), std::end(raw));
}

DeviceResourceUpload work_upload(std::uint64_t id, DeviceResourceClass cls, std::size_t bytes) {
    DeviceResourceUpload upload;
    upload.key = work_resource_key(id, 1U, cls, 0U);
    upload.bytes.resize(bytes, static_cast<std::byte>(id & 0xFFU));
    return upload;
}

struct Fixture {
    GeometryKernel kernel;
    AnalyticSdfProvider analytic{2U};
    SparseSdfProvider sparse{3U};
    ClusteredTriangleProvider clustered{4U};
    GeometryDeviceCompilerRegistry package_registry;
    GeometryDeviceWorkCompilerRegistry work_registry;
    DeviceGeometryPackage sparse_package;
    DeviceGeometryPackage cluster_package;
    std::string error;

    Fixture() {
        require(analytic.register_with(kernel, error), error);
        require(sparse.register_with(kernel, error), error);
        require(clustered.register_with(kernel, error), error);
        const auto sphere = analytic.add_sphere({0,0,0}, 1.0F, error);
        require(sphere.valid(), error);
        SparseSdfCompileOptions options;
        options.voxel_size = 0.125F;
        const auto sparse_build = sparse.compile_from(kernel, sphere, options);
        require(sparse_build.ok(), sparse_build.error);
        std::vector<Vec3> vertices;
        std::vector<std::uint32_t> indices;
        cube_mesh(vertices, indices);
        const auto cluster_build = clustered.add_mesh(vertices, indices, {});
        require(cluster_build.ok(), cluster_build.error);
        require(register_sparse_sdf_device_compiler(package_registry, sparse, 3U, error), error);
        require(register_clustered_triangle_device_compiler(package_registry, clustered, 4U, error), error);
        require(package_registry.compile(sparse_build.handle, 17U, sparse_package, error), error);
        require(package_registry.compile(cluster_build.handle, 17U, cluster_package, error), error);
        require(register_sparse_sdf_device_work_compiler(work_registry, 3U, error), error);
        require(register_clustered_triangle_device_work_compiler(work_registry, 4U, error), error);
    }
};
}

int main() {
    using namespace aion;
    Fixture f;
    ReferenceDeviceBackend backend;
    const std::size_t budget = f.sparse_package.storage_bytes() + f.cluster_package.storage_bytes() + 65536U;
    DeviceResidencyManager residency(backend, budget);
    std::string error;
    std::vector<DeviceResourceHandle> handles;
    auto sparse_uploads = f.sparse_package.residency_uploads();
    auto cluster_uploads = f.cluster_package.residency_uploads();
    require(residency.ensure_group(sparse_uploads, handles, error), error);
    require(residency.ensure_group(cluster_uploads, handles, error), error);

    const auto input = work_upload(100U, DeviceResourceClass::WorkInput, 4096U);
    const auto sparse_output = work_upload(101U, DeviceResourceClass::WorkOutput, 4096U);
    const auto cluster_output = work_upload(102U, DeviceResourceClass::WorkOutput, 4096U);
    const auto control = work_upload(103U, DeviceResourceClass::WorkInput, 64U);
    DeviceResourceHandle tmp{};
    require(residency.ensure(input, tmp, error), error);
    require(residency.ensure(sparse_output, tmp, error), error);
    require(residency.ensure(cluster_output, tmp, error), error);
    require(residency.ensure(control, tmp, error), error);

    DeviceWorkPacket sparse_packet, cluster_packet;
    GeometryDeviceWorkRequest sparse_request{
        .packet_id = 10U,
        .operation = GeometryDeviceOperation::RaySurfaceBatch,
        .work_items = 4096U,
        .launch_mode = DeviceWorkLaunchMode::Direct,
        .input = input.key,
        .output = sparse_output.key};
    GeometryDeviceWorkRequest cluster_request = sparse_request;
    cluster_request.packet_id = 20U;
    cluster_request.output = cluster_output.key;
    require(f.work_registry.compile(f.sparse_package, sparse_request, sparse_packet, error), error);
    require(f.work_registry.compile(f.cluster_package, cluster_request, cluster_packet, error), error);
    require(sparse_packet.resources.size() == 4U, "sparse work packet resource contract is wrong");
    require(cluster_packet.resources.size() == 5U, "cluster work packet resource contract is wrong");

    DeviceWorkPlanner planner;
    DeviceWorkCapabilities caps;
    DeviceWorkPlan parallel_plan;
    const std::array<DeviceWorkPacket,2> independent{sparse_packet, cluster_packet};
    require(planner.build(independent, residency, caps, parallel_plan, error), error);
    require(parallel_plan.waves.size() == 1U && parallel_plan.waves[0].size() == 2U, "independent read/read geometry packets should share a wave");

    // Shared output introduces a write hazard and must serialize by stable packet id.
    DeviceWorkPacket conflicting = cluster_packet;
    for (auto& resource : conflicting.resources) if (resource.key == cluster_output.key) resource.key = sparse_output.key;
    const std::array<DeviceWorkPacket,2> hazard_packets{sparse_packet, conflicting};
    DeviceWorkPlan hazard_plan;
    require(planner.build(hazard_packets, residency, caps, hazard_plan, error), error);
    require(hazard_plan.waves.size() == 2U && hazard_plan.waves[0] == std::vector<std::uint32_t>{10U} && hazard_plan.waves[1] == std::vector<std::uint32_t>{20U}, "write hazard did not serialize canonically");

    // Explicit reverse dependency contradicts the canonical data hazard and must reject as a cycle.
    DeviceWorkPacket reverse = sparse_packet;
    reverse.after = {20U};
    const std::array<DeviceWorkPacket,2> cycle_packets{reverse, conflicting};
    DeviceWorkPlan rejected;
    require(!planner.build(cycle_packets, residency, caps, rejected, error), "contradictory work dependency was not rejected");

    // Canonical command stream must not depend on packet registration order.
    ReferenceDeviceWorkCompiler reference_compiler;
    ReferenceDeviceCommandStream stream_a, stream_b;
    require(reference_compiler.compile(independent, parallel_plan, stream_a, error), error);
    const std::array<DeviceWorkPacket,2> shuffled{cluster_packet, sparse_packet};
    DeviceWorkPlan shuffled_plan;
    require(planner.build(shuffled, residency, caps, shuffled_plan, error), error);
    require(reference_compiler.compile(shuffled, shuffled_plan, stream_b, error), error);
    require(stream_a.digest == stream_b.digest && stream_a.commands.size() == stream_b.commands.size(), "command stream changed with packet registration order");
    DeviceWorkPacket reordered_sparse = sparse_packet;
    DeviceWorkPacket reordered_cluster = cluster_packet;
    std::reverse(reordered_sparse.resources.begin(), reordered_sparse.resources.end());
    std::reverse(reordered_cluster.resources.begin(), reordered_cluster.resources.end());
    const std::array<DeviceWorkPacket,2> reordered_packets{reordered_cluster, reordered_sparse};
    DeviceWorkPlan reordered_plan;
    ReferenceDeviceCommandStream reordered_stream;
    require(planner.build(reordered_packets, residency, caps, reordered_plan, error), error);
    require(reference_compiler.compile(reordered_packets, reordered_plan, reordered_stream, error), error);
    require(reordered_stream.digest == stream_a.digest, "command stream changed with semantically irrelevant resource ordering");

    // Evict one geometry payload: the packet may no longer be considered executable.
    const auto missing_key = cluster_uploads.back().key;
    require(residency.evict(missing_key, error), error);
    require(!planner.build(independent, residency, caps, rejected, error), "work plan accepted incomplete geometry residency");
    require(residency.ensure_group(cluster_uploads, handles, error), error);

    // A package from a new source revision is a distinct set of resources and must not reuse old residency silently.
    DeviceGeometryPackage stale_package;
    require(f.package_registry.compile(f.cluster_package.geometry, 18U, stale_package, error), error);
    DeviceWorkPacket stale_packet;
    require(f.work_registry.compile(stale_package, cluster_request, stale_packet, error), error);
    const std::array<DeviceWorkPacket,1> stale_array{stale_packet};
    require(!planner.build(stale_array, residency, caps, rejected, error), "new source revision reused old device residency");

    // Indirect and device-generated launch modes are capability-gated, not backend/API names in the packet.
    GeometryDeviceWorkRequest indirect_request = sparse_request;
    indirect_request.packet_id = 30U;
    indirect_request.launch_mode = DeviceWorkLaunchMode::Indirect;
    indirect_request.launch_control = control.key;
    indirect_request.max_sequences = 4096U;
    DeviceWorkPacket indirect_packet;
    require(f.work_registry.compile(f.sparse_package, indirect_request, indirect_packet, error), error);
    DeviceWorkCapabilities no_indirect = caps;
    no_indirect.indirect_launch = false;
    const std::array<DeviceWorkPacket,1> indirect_array{indirect_packet};
    require(!planner.build(indirect_array, residency, no_indirect, rejected, error), "indirect launch ignored backend capability");
    require(planner.build(indirect_array, residency, caps, rejected, error), error);

    GeometryDeviceWorkRequest generated_request = sparse_request;
    generated_request.packet_id = 40U;
    generated_request.launch_mode = DeviceWorkLaunchMode::DeviceGenerated;
    generated_request.launch_control = control.key;
    generated_request.max_sequences = 4096U;
    DeviceWorkPacket generated_packet;
    require(f.work_registry.compile(f.sparse_package, generated_request, generated_packet, error), error);
    const std::array<DeviceWorkPacket,1> generated_array{generated_packet};
    require(!planner.build(generated_array, residency, caps, rejected, error), "device-generated launch ignored missing capability");
    DeviceWorkCapabilities generated_caps = caps;
    generated_caps.device_generated_work = true;
    require(planner.build(generated_array, residency, generated_caps, rejected, error), error);

    // Clustered triangle does not advertise/compile truncated-distance work.
    GeometryDeviceWorkRequest unsupported = cluster_request;
    unsupported.packet_id = 50U;
    unsupported.operation = GeometryDeviceOperation::TruncatedDistanceBatch;
    DeviceWorkPacket unsupported_packet;
    require(!f.work_registry.compile(f.cluster_package, unsupported, unsupported_packet, error), "clustered triangle accepted unsupported distance work");

    std::cout << "R5C PASS direct_digest=" << stream_a.digest
              << " waves_parallel=" << parallel_plan.waves.size()
              << " waves_hazard=" << hazard_plan.waves.size() << '\n';
    return 0;
}
