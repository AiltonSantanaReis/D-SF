#include "aion/kernel/device_work.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>
#include <utility>

namespace aion {
namespace {
[[nodiscard]] bool access_writes(DeviceResourceAccess access) noexcept {
    return access == DeviceResourceAccess::Write || access == DeviceResourceAccess::ReadWrite;
}


[[nodiscard]] bool domain_supported(DeviceWorkDomain domain, const DeviceWorkCapabilities& caps) noexcept {
    switch (domain) {
        case DeviceWorkDomain::Compute: return caps.compute;
        case DeviceWorkDomain::Graphics: return caps.graphics;
        case DeviceWorkDomain::Ray: return caps.ray;
    }
    return false;
}

[[nodiscard]] bool resource_is_default(const DeviceResourceKey& key) noexcept {
    return key.owner.object_id == 0U && key.owner.revision == 0U;
}


void add_edge(std::vector<std::vector<std::size_t>>& edges, std::vector<std::uint32_t>& indegree, std::size_t from, std::size_t to) {
    if (from == to) return;
    if (std::find(edges[from].begin(), edges[from].end(), to) != edges[from].end()) return;
    edges[from].push_back(to);
    ++indegree[to];
}

[[nodiscard]] bool resource_key_less(const DeviceResourceKey& a, const DeviceResourceKey& b) noexcept {
    if (a.owner.name_space != b.owner.name_space) return static_cast<std::uint8_t>(a.owner.name_space) < static_cast<std::uint8_t>(b.owner.name_space);
    if (a.owner.object_id != b.owner.object_id) return a.owner.object_id < b.owner.object_id;
    if (a.owner.revision != b.owner.revision) return a.owner.revision < b.owner.revision;
    if (a.resource_class != b.resource_class) return static_cast<std::uint8_t>(a.resource_class) < static_cast<std::uint8_t>(b.resource_class);
    return a.subresource < b.subresource;
}

[[nodiscard]] std::uint64_t fnv_mix(std::uint64_t hash, std::uint64_t value) noexcept {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash ^= static_cast<std::uint8_t>((value >> shift) & 0xFFU);
        hash *= prime;
    }
    return hash;
}

[[nodiscard]] std::uint64_t packet_digest(std::uint64_t hash, const DeviceWorkPacket& packet, std::uint32_t wave) noexcept {
    hash = fnv_mix(hash, wave);
    hash = fnv_mix(hash, packet.id);
    hash = fnv_mix(hash, static_cast<std::uint8_t>(packet.domain));
    hash = fnv_mix(hash, packet.program_key);
    hash = fnv_mix(hash, static_cast<std::uint8_t>(packet.launch.mode));
    hash = fnv_mix(hash, packet.launch.x); hash = fnv_mix(hash, packet.launch.y); hash = fnv_mix(hash, packet.launch.z);
    hash = fnv_mix(hash, packet.launch.control_offset); hash = fnv_mix(hash, packet.launch.max_sequences);
    std::vector<DeviceWorkResourceRef> resources = packet.resources;
    std::stable_sort(resources.begin(), resources.end(), [](const DeviceWorkResourceRef& a, const DeviceWorkResourceRef& b) {
        if (a.key == b.key) return static_cast<std::uint8_t>(a.access) < static_cast<std::uint8_t>(b.access);
        return resource_key_less(a.key, b.key);
    });
    for (const auto& resource : resources) {
        hash = fnv_mix(hash, static_cast<std::uint8_t>(resource.key.owner.name_space));
        hash = fnv_mix(hash, resource.key.owner.object_id);
        hash = fnv_mix(hash, resource.key.owner.revision);
        hash = fnv_mix(hash, static_cast<std::uint8_t>(resource.key.resource_class));
        hash = fnv_mix(hash, resource.key.subresource);
        hash = fnv_mix(hash, static_cast<std::uint8_t>(resource.access));
    }
    std::vector<std::uint32_t> dependencies = packet.after;
    std::sort(dependencies.begin(), dependencies.end());
    for (const auto dep : dependencies) hash = fnv_mix(hash, dep);
    for (const auto byte : packet.parameters) hash = fnv_mix(hash, static_cast<std::uint8_t>(byte));
    return hash;
}

void append_geometry_resources(const DeviceGeometryPackage& package, DeviceWorkPacket& packet) {
    const auto uploads = package.residency_uploads();
    for (const auto& upload : uploads) packet.resources.push_back({upload.key, DeviceResourceAccess::Read});
}

void append_work_io(const GeometryDeviceWorkRequest& request, DeviceWorkPacket& packet) {
    packet.resources.push_back({request.input, DeviceResourceAccess::Read});
    packet.resources.push_back({request.output, DeviceResourceAccess::Write});
    if (request.launch_mode != DeviceWorkLaunchMode::Direct) packet.resources.push_back({request.launch_control, DeviceResourceAccess::Read});
}

void configure_launch(const GeometryDeviceWorkRequest& request, DeviceWorkPacket& packet) {
    packet.launch.mode = request.launch_mode;
    if (request.launch_mode == DeviceWorkLaunchMode::Direct) {
        constexpr std::uint32_t lane_group = 64U;
        packet.launch.x = (request.work_items + lane_group - 1U) / lane_group;
        packet.launch.y = 1U;
        packet.launch.z = 1U;
    } else {
        packet.launch.control_resource = request.launch_control;
        packet.launch.control_offset = 0U;
        packet.launch.max_sequences = request.max_sequences == 0U ? request.work_items : request.max_sequences;
    }
}

[[nodiscard]] bool validate_request(const DeviceGeometryPackage& package, const GeometryDeviceWorkRequest& request, std::string& error) {
    if (!package.geometry.valid() || package.source_revision == 0U) { error = "device work requires a valid versioned geometry package"; return false; }
    if (request.packet_id == 0U || request.work_items == 0U) { error = "device work requires non-zero packet id and work item count"; return false; }
    if (resource_is_default(request.input) || resource_is_default(request.output)) { error = "device work requires explicit input and output resources"; return false; }
    if (request.launch_mode != DeviceWorkLaunchMode::Direct && resource_is_default(request.launch_control)) { error = "indirect/generated work requires a launch-control resource"; return false; }
    return true;
}
}

bool DeviceWorkPlanner::build(
    std::span<const DeviceWorkPacket> packets,
    const DeviceResidencyManager& residency,
    const DeviceWorkCapabilities& capabilities,
    DeviceWorkPlan& plan,
    std::string& error) const {
    plan = {};
    if (packets.empty()) { error = "device work plan requires at least one packet"; return false; }
    std::unordered_map<std::uint32_t, std::size_t> id_to_index;
    id_to_index.reserve(packets.size());
    for (std::size_t i = 0; i < packets.size(); ++i) {
        const auto& packet = packets[i];
        if (packet.id == 0U || packet.program_key == 0U) { error = "device work packet requires non-zero id and program key"; return false; }
        if (!id_to_index.emplace(packet.id, i).second) { error = "duplicate device work packet id"; return false; }
        if (!domain_supported(packet.domain, capabilities)) { error = "device work domain unsupported by backend capabilities"; return false; }
        if (packet.launch.mode == DeviceWorkLaunchMode::Direct && (packet.launch.x == 0U || packet.launch.y == 0U || packet.launch.z == 0U)) {
            error = "direct device work requires non-zero launch dimensions"; return false;
        }
        if (packet.launch.mode == DeviceWorkLaunchMode::Indirect && !capabilities.indirect_launch) { error = "indirect launch unsupported"; return false; }
        if (packet.launch.mode == DeviceWorkLaunchMode::DeviceGenerated && !capabilities.device_generated_work) { error = "device-generated work unsupported"; return false; }
        if (packet.launch.mode != DeviceWorkLaunchMode::Direct) {
            if (!residency.resident(packet.launch.control_resource)) { error = "launch-control resource is not resident"; return false; }
            const bool declared_control = std::any_of(packet.resources.begin(), packet.resources.end(), [&](const DeviceWorkResourceRef& resource) {
                const bool reads = resource.access == DeviceResourceAccess::Read || resource.access == DeviceResourceAccess::ReadWrite;
                return resource.key == packet.launch.control_resource && reads;
            });
            if (!declared_control) { error = "launch-control resource must be declared as a readable packet resource"; return false; }
        }
        for (const auto& resource : packet.resources) {
            if (!residency.resident(resource.key)) { error = "device work references a non-resident resource"; return false; }
        }
    }

    std::vector<std::vector<std::size_t>> edges(packets.size());
    std::vector<std::uint32_t> indegree(packets.size(), 0U);
    for (std::size_t i = 0; i < packets.size(); ++i) {
        for (const auto dep : packets[i].after) {
            const auto it = id_to_index.find(dep);
            if (it == id_to_index.end()) { error = "device work dependency references unknown packet"; return false; }
            add_edge(edges, indegree, it->second, i);
        }
    }
    struct HazardState {
        std::size_t last_writer{std::numeric_limits<std::size_t>::max()};
        std::vector<std::size_t> active_readers;
    };
    std::unordered_map<DeviceResourceKey, HazardState, DeviceResourceKeyHash> hazards;
    std::vector<std::size_t> canonical_indices(packets.size());
    for (std::size_t i = 0; i < packets.size(); ++i) canonical_indices[i] = i;
    std::stable_sort(canonical_indices.begin(), canonical_indices.end(), [&](std::size_t a, std::size_t b) { return packets[a].id < packets[b].id; });
    for (const auto index : canonical_indices) {
        for (const auto& resource : packets[index].resources) {
            auto& state = hazards[resource.key];
            const bool reads = resource.access == DeviceResourceAccess::Read || resource.access == DeviceResourceAccess::ReadWrite;
            const bool writes = access_writes(resource.access);
            if (reads && !writes) {
                if (state.last_writer != std::numeric_limits<std::size_t>::max()) add_edge(edges, indegree, state.last_writer, index);
                if (std::find(state.active_readers.begin(), state.active_readers.end(), index) == state.active_readers.end()) state.active_readers.push_back(index);
                continue;
            }
            if (writes) {
                if (state.last_writer != std::numeric_limits<std::size_t>::max()) add_edge(edges, indegree, state.last_writer, index);
                for (const auto reader : state.active_readers) add_edge(edges, indegree, reader, index);
                state.active_readers.clear();
                state.last_writer = index;
            }
        }
    }

    std::vector<std::size_t> ready;
    ready.reserve(packets.size());
    for (std::size_t i = 0; i < packets.size(); ++i) if (indegree[i] == 0U) ready.push_back(i);
    std::stable_sort(ready.begin(), ready.end(), [&](std::size_t a, std::size_t b) { return packets[a].id < packets[b].id; });
    std::size_t emitted = 0;
    while (!ready.empty()) {
        std::vector<std::uint32_t> wave;
        wave.reserve(ready.size());
        std::vector<std::size_t> next_ready;
        for (const auto index : ready) {
            wave.push_back(packets[index].id);
            ++emitted;
            for (const auto to : edges[index]) {
                if (indegree[to] == 0U) continue;
                --indegree[to];
                if (indegree[to] == 0U) next_ready.push_back(to);
            }
        }
        std::stable_sort(next_ready.begin(), next_ready.end(), [&](std::size_t a, std::size_t b) { return packets[a].id < packets[b].id; });
        plan.waves.push_back(std::move(wave));
        ready = std::move(next_ready);
    }
    if (emitted != packets.size()) { error = "device work graph contains a dependency cycle"; return false; }
    error.clear();
    return true;
}

bool ReferenceDeviceWorkCompiler::compile(
    std::span<const DeviceWorkPacket> packets,
    const DeviceWorkPlan& plan,
    ReferenceDeviceCommandStream& stream,
    std::string& error) const {
    stream = {};
    constexpr std::uint64_t offset = 1469598103934665603ULL;
    std::uint64_t digest = offset;
    std::unordered_map<std::uint32_t, std::size_t> id_to_index;
    id_to_index.reserve(packets.size());
    for (std::size_t i = 0; i < packets.size(); ++i) id_to_index.emplace(packets[i].id, i);
    for (std::size_t wave_index = 0; wave_index < plan.waves.size(); ++wave_index) {
        for (const auto packet_id : plan.waves[wave_index]) {
            const auto it = id_to_index.find(packet_id);
            if (it == id_to_index.end()) { error = "device work plan references unknown packet"; return false; }
            const auto& packet = packets[it->second];
            stream.commands.push_back({packet.id, static_cast<std::uint32_t>(wave_index), packet.domain, packet.launch.mode, packet.program_key, static_cast<std::uint32_t>(packet.resources.size())});
            digest = packet_digest(digest, packet, static_cast<std::uint32_t>(wave_index));
        }
    }
    stream.digest = digest;
    error.clear();
    return true;
}

bool GeometryDeviceWorkCompilerRegistry::register_compiler(GeometryProviderId provider, Compiler compiler, std::string& error) {
    if (provider == 0U || !compiler) { error = "geometry device work compiler requires provider and callback"; return false; }
    if (std::any_of(entries_.begin(), entries_.end(), [provider](const Entry& entry) { return entry.provider == provider; })) { error = "geometry device work compiler already registered"; return false; }
    entries_.push_back({provider, std::move(compiler)});
    error.clear();
    return true;
}

bool GeometryDeviceWorkCompilerRegistry::compile(
    const DeviceGeometryPackage& package,
    const GeometryDeviceWorkRequest& request,
    DeviceWorkPacket& packet,
    std::string& error) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.provider == package.geometry.provider; });
    if (it == entries_.end()) { error = "no device work compiler registered for geometry provider"; return false; }
    packet = {};
    return it->compiler(package, request, packet, error);
}

bool register_sparse_sdf_device_work_compiler(GeometryDeviceWorkCompilerRegistry& registry, GeometryProviderId provider, std::string& error) {
    return registry.register_compiler(provider, [](const DeviceGeometryPackage& package, const GeometryDeviceWorkRequest& request, DeviceWorkPacket& packet, std::string& compile_error) {
        if (!validate_request(package, request, compile_error)) return false;
        if (request.operation != GeometryDeviceOperation::RaySurfaceBatch && request.operation != GeometryDeviceOperation::TruncatedDistanceBatch) {
            compile_error = "sparse SDF device work operation unsupported"; return false;
        }
        packet.id = request.packet_id;
        packet.domain = DeviceWorkDomain::Compute;
        packet.program_key = request.operation == GeometryDeviceOperation::RaySurfaceBatch ? 0x5350444652415901ULL : 0x5350444644495301ULL;
        append_geometry_resources(package, packet);
        append_work_io(request, packet);
        configure_launch(request, packet);
        CanonicalByteWriter parameters(packet.parameters);
        parameters.u32(request.work_items);
        parameters.u8(static_cast<std::uint8_t>(request.operation));
        compile_error.clear();
        return true;
    }, error);
}

bool register_clustered_triangle_device_work_compiler(GeometryDeviceWorkCompilerRegistry& registry, GeometryProviderId provider, std::string& error) {
    return registry.register_compiler(provider, [](const DeviceGeometryPackage& package, const GeometryDeviceWorkRequest& request, DeviceWorkPacket& packet, std::string& compile_error) {
        if (!validate_request(package, request, compile_error)) return false;
        if (request.operation != GeometryDeviceOperation::RaySurfaceBatch) { compile_error = "clustered triangle device work operation unsupported"; return false; }
        packet.id = request.packet_id;
        packet.domain = DeviceWorkDomain::Compute;
        packet.program_key = 0x434C555354524159ULL;
        append_geometry_resources(package, packet);
        append_work_io(request, packet);
        configure_launch(request, packet);
        CanonicalByteWriter parameters(packet.parameters);
        parameters.u32(request.work_items);
        parameters.u8(static_cast<std::uint8_t>(request.operation));
        compile_error.clear();
        return true;
    }, error);
}

} // namespace aion
