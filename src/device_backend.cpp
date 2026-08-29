#include "aion/kernel/device_backend.hpp"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace aion {
namespace {

constexpr std::uint64_t fnv_offset = 1469598103934665603ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

[[nodiscard]] std::uint64_t fnv_mix(std::uint64_t hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hash ^= static_cast<std::uint8_t>((value >> shift) & 0xFFU);
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] bool access_reads(DeviceResourceAccess access) noexcept {
    return access == DeviceResourceAccess::Read || access == DeviceResourceAccess::ReadWrite;
}

[[nodiscard]] bool access_writes(DeviceResourceAccess access) noexcept {
    return access == DeviceResourceAccess::Write || access == DeviceResourceAccess::ReadWrite;
}

[[nodiscard]] bool resource_key_less(const DeviceResourceKey& a, const DeviceResourceKey& b) noexcept {
    if (a.owner.name_space != b.owner.name_space) return static_cast<std::uint8_t>(a.owner.name_space) < static_cast<std::uint8_t>(b.owner.name_space);
    if (a.owner.object_id != b.owner.object_id) return a.owner.object_id < b.owner.object_id;
    if (a.owner.revision != b.owner.revision) return a.owner.revision < b.owner.revision;
    if (a.resource_class != b.resource_class) return static_cast<std::uint8_t>(a.resource_class) < static_cast<std::uint8_t>(b.resource_class);
    return a.subresource < b.subresource;
}

[[nodiscard]] bool domain_supported(DeviceWorkDomain domain, const BackendCapabilityProfile& caps) noexcept {
    switch (domain) {
        case DeviceWorkDomain::Compute: return caps.compute;
        case DeviceWorkDomain::Graphics: return caps.graphics;
        case DeviceWorkDomain::Ray: return caps.ray;
    }
    return false;
}

[[nodiscard]] bool packet_declares_control(const DeviceWorkPacket& packet) noexcept {
    if (packet.launch.mode == DeviceWorkLaunchMode::Direct) return true;
    return std::any_of(packet.resources.begin(), packet.resources.end(), [&](const DeviceWorkResourceRef& ref) {
        return ref.key == packet.launch.control_resource && access_reads(ref.access);
    });
}

[[nodiscard]] bool choose_launch(
    const DeviceWorkPacket& packet,
    const BackendCapabilityProfile& caps,
    const BackendTranslationPolicy& policy,
    BackendLaunchKind& launch,
    std::string& error) {
    const auto generated_kind = [&]() -> BackendLaunchKind {
        return caps.generated_model == BackendGeneratedModel::WorkGraph
            ? BackendLaunchKind::WorkGraph
            : BackendLaunchKind::GeneratedSequence;
    };

    if (packet.launch.mode == DeviceWorkLaunchMode::DeviceGenerated) {
        if (!caps.device_generated_work || caps.generated_model == BackendGeneratedModel::None) {
            error = "device-generated packet cannot be demoted without changing semantics";
            return false;
        }
        launch = generated_kind();
        return true;
    }

    if (packet.launch.mode == DeviceWorkLaunchMode::Indirect) {
        if (policy.launch_preference == BackendLaunchPreference::PreferDeviceGenerated
            && caps.device_generated_work && caps.generated_model != BackendGeneratedModel::None) {
            launch = generated_kind();
            return true;
        }
        if (!caps.indirect_launch) {
            error = "indirect packet cannot be lowered to direct without CPU readback";
            return false;
        }
        launch = BackendLaunchKind::Indirect;
        return true;
    }

    if (!caps.direct_launch) {
        error = "direct launch unsupported";
        return false;
    }
    if (policy.allow_static_promotion && policy.launch_preference == BackendLaunchPreference::PreferDeviceGenerated
        && caps.device_generated_work && caps.generated_model != BackendGeneratedModel::None) {
        launch = generated_kind();
        return true;
    }
    if (policy.allow_static_promotion && policy.launch_preference == BackendLaunchPreference::PreferIndirect && caps.indirect_launch) {
        launch = BackendLaunchKind::Indirect;
        return true;
    }
    launch = BackendLaunchKind::Direct;
    return true;
}

[[nodiscard]] std::uint64_t parameter_digest(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset;
    for (const auto byte : bytes) hash = fnv_mix(hash, static_cast<std::uint8_t>(byte));
    return hash;
}

[[nodiscard]] std::uint64_t semantic_packet_digest(std::uint64_t hash, const DeviceWorkPacket& packet, std::uint32_t wave) {
    hash = fnv_mix(hash, packet.id);
    hash = fnv_mix(hash, wave);
    hash = fnv_mix(hash, static_cast<std::uint8_t>(packet.domain));
    hash = fnv_mix(hash, packet.program_key);
    hash = fnv_mix(hash, static_cast<std::uint8_t>(packet.launch.mode));
    hash = fnv_mix(hash, packet.launch.x);
    hash = fnv_mix(hash, packet.launch.y);
    hash = fnv_mix(hash, packet.launch.z);
    hash = fnv_mix(hash, packet.launch.control_offset);
    hash = fnv_mix(hash, packet.launch.max_sequences);
    hash = fnv_mix(hash, parameter_digest(packet.parameters));
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
    for (const auto dependency : dependencies) hash = fnv_mix(hash, dependency);
    return hash;
}

[[nodiscard]] std::uint64_t backend_stream_digest(const BackendTranslatedStream& stream) noexcept {
    std::uint64_t hash = fnv_offset;
    hash = fnv_mix(hash, stream.semantic_digest);
    for (const auto& descriptor : stream.descriptors) {
        hash = fnv_mix(hash, descriptor.descriptor_index);
        hash = fnv_mix(hash, static_cast<std::uint8_t>(descriptor.key.owner.name_space));
        hash = fnv_mix(hash, descriptor.key.owner.object_id);
        hash = fnv_mix(hash, descriptor.key.owner.revision);
        hash = fnv_mix(hash, static_cast<std::uint8_t>(descriptor.key.resource_class));
        hash = fnv_mix(hash, descriptor.key.subresource);
        hash = fnv_mix(hash, descriptor.handle.slot);
        hash = fnv_mix(hash, descriptor.handle.generation);
    }
    for (const auto& use : stream.uses) {
        hash = fnv_mix(hash, use.descriptor_index);
        hash = fnv_mix(hash, static_cast<std::uint8_t>(use.access));
    }
    for (const auto& barrier : stream.barriers) {
        hash = fnv_mix(hash, barrier.from_wave);
        hash = fnv_mix(hash, barrier.to_wave);
        hash = fnv_mix(hash, static_cast<std::uint8_t>(barrier.before));
        hash = fnv_mix(hash, static_cast<std::uint8_t>(barrier.after));
        hash = fnv_mix(hash, static_cast<std::uint8_t>(barrier.key.owner.name_space));
        hash = fnv_mix(hash, barrier.key.owner.object_id);
        hash = fnv_mix(hash, barrier.key.owner.revision);
        hash = fnv_mix(hash, static_cast<std::uint8_t>(barrier.key.resource_class));
        hash = fnv_mix(hash, barrier.key.subresource);
    }
    for (const auto& command : stream.commands) {
        hash = fnv_mix(hash, command.packet_id);
        hash = fnv_mix(hash, command.wave);
        hash = fnv_mix(hash, static_cast<std::uint8_t>(command.launch_kind));
        hash = fnv_mix(hash, command.program_key);
        hash = fnv_mix(hash, command.x);
        hash = fnv_mix(hash, command.y);
        hash = fnv_mix(hash, command.z);
        hash = fnv_mix(hash, static_cast<std::uint8_t>(command.control_source));
        hash = fnv_mix(hash, command.control_handle.slot);
        hash = fnv_mix(hash, command.control_handle.generation);
        hash = fnv_mix(hash, command.control_offset);
        hash = fnv_mix(hash, command.max_sequences);
        hash = fnv_mix(hash, command.first_use);
        hash = fnv_mix(hash, command.use_count);
        hash = fnv_mix(hash, command.parameter_digest);
    }
    return hash;
}

struct LastUse {
    DeviceResourceAccess access{DeviceResourceAccess::Read};
    std::uint32_t wave{};
};

} // namespace

bool BackendWorkTranslator::translate(
    std::span<const DeviceWorkPacket> packets,
    const DeviceWorkPlan& plan,
    const DeviceResidencyManager& residency,
    const BackendCapabilityProfile& capabilities,
    const BackendTranslationPolicy& policy,
    BackendTranslatedStream& stream,
    std::string& error) const {
    stream = {};
    if (capabilities.binding_model != BackendBindingModel::DescriptorMemory) {
        error = "unsupported backend binding model";
        return false;
    }
    if (packets.empty() || plan.waves.empty()) {
        error = "backend translation requires a non-empty planned workload";
        return false;
    }

    std::unordered_map<std::uint32_t, const DeviceWorkPacket*> by_id;
    by_id.reserve(packets.size());
    for (const auto& packet : packets) {
        if (!by_id.emplace(packet.id, &packet).second) {
            error = "backend translation received duplicate packet id";
            return false;
        }
        if (!domain_supported(packet.domain, capabilities)) {
            error = "backend profile does not support packet domain";
            return false;
        }
        if (!packet_declares_control(packet)) {
            error = "non-direct packet must declare its launch-control resource as readable";
            return false;
        }
    }

    std::vector<DeviceResourceKey> descriptor_keys;
    for (const auto& packet : packets) {
        for (const auto& resource : packet.resources) descriptor_keys.push_back(resource.key);
    }
    std::stable_sort(descriptor_keys.begin(), descriptor_keys.end(), resource_key_less);
    descriptor_keys.erase(std::unique(descriptor_keys.begin(), descriptor_keys.end()), descriptor_keys.end());
    std::unordered_map<DeviceResourceKey, std::uint32_t, DeviceResourceKeyHash> descriptor_index;
    descriptor_index.reserve(descriptor_keys.size());
    for (const auto& key : descriptor_keys) {
        const auto handle = residency.handle_for(key);
        if (!handle.valid() || !residency.resident(handle)) {
            error = "backend descriptor table references non-resident resource";
            return false;
        }
        const auto index = static_cast<std::uint32_t>(stream.descriptors.size());
        stream.descriptors.push_back({key, handle, index});
        descriptor_index.emplace(key, index);
    }

    std::unordered_map<DeviceResourceKey, LastUse, DeviceResourceKeyHash> last_use;
    std::uint64_t semantic = fnv_offset;
    std::unordered_set<std::uint32_t> emitted;
    emitted.reserve(packets.size());

    for (std::size_t wave_index = 0; wave_index < plan.waves.size(); ++wave_index) {
        for (const auto packet_id : plan.waves[wave_index]) {
            const auto packet_it = by_id.find(packet_id);
            if (packet_it == by_id.end()) {
                error = "backend translation plan references unknown packet";
                return false;
            }
            if (!emitted.insert(packet_id).second) {
                error = "backend translation plan references packet more than once";
                return false;
            }
            const auto& packet = *packet_it->second;
            BackendLaunchKind launch_kind{};
            if (!choose_launch(packet, capabilities, policy, launch_kind, error)) return false;

            std::vector<DeviceWorkResourceRef> resources = packet.resources;
            std::stable_sort(resources.begin(), resources.end(), [](const DeviceWorkResourceRef& a, const DeviceWorkResourceRef& b) {
                if (a.key == b.key) return static_cast<std::uint8_t>(a.access) < static_cast<std::uint8_t>(b.access);
                return resource_key_less(a.key, b.key);
            });
            const auto duplicate = std::adjacent_find(resources.begin(), resources.end(), [](const auto& a, const auto& b) { return a.key == b.key; });
            if (duplicate != resources.end()) {
                error = "backend translation requires one canonical access declaration per packet resource";
                return false;
            }

            const auto first_use = static_cast<std::uint32_t>(stream.uses.size());
            for (const auto& resource : resources) {
                const auto descriptor = descriptor_index.find(resource.key);
                if (descriptor == descriptor_index.end()) {
                    error = "backend translation resource missing from descriptor table";
                    return false;
                }
                stream.uses.push_back({descriptor->second, resource.access});

                const auto previous = last_use.find(resource.key);
                if (previous != last_use.end() && (access_writes(previous->second.access) || access_writes(resource.access))) {
                    if (previous->second.wave == static_cast<std::uint32_t>(wave_index)) {
                        error = "backend translation plan contains a same-wave resource hazard";
                        return false;
                    }
                    stream.barriers.push_back({resource.key, previous->second.access, resource.access, previous->second.wave, static_cast<std::uint32_t>(wave_index)});
                }
                last_use[resource.key] = {resource.access, static_cast<std::uint32_t>(wave_index)};
            }

            DeviceResourceHandle control{};
            BackendLaunchControlSource control_source = BackendLaunchControlSource::InlineStatic;
            if (packet.launch.mode != DeviceWorkLaunchMode::Direct) {
                control = residency.handle_for(packet.launch.control_resource);
                if (!control.valid() || !residency.resident(control)) {
                    error = "backend translation launch-control resource is not resident";
                    return false;
                }
                control_source = BackendLaunchControlSource::ResidentResource;
            } else if (launch_kind != BackendLaunchKind::Direct) {
                control_source = BackendLaunchControlSource::BackendStaticRecord;
            }

            BackendCommand command{};
            command.packet_id = packet.id;
            command.wave = static_cast<std::uint32_t>(wave_index);
            command.domain = packet.domain;
            command.launch_kind = launch_kind;
            command.program_key = packet.program_key;
            command.x = packet.launch.x;
            command.y = packet.launch.y;
            command.z = packet.launch.z;
            command.control_source = control_source;
            command.control_handle = control;
            command.control_offset = packet.launch.control_offset;
            command.max_sequences = packet.launch.max_sequences;
            command.first_use = first_use;
            command.use_count = static_cast<std::uint32_t>(resources.size());
            command.parameter_digest = parameter_digest(packet.parameters);
            stream.commands.push_back(command);
            semantic = semantic_packet_digest(semantic, packet, static_cast<std::uint32_t>(wave_index));
        }
    }
    if (emitted.size() != packets.size()) {
        error = "backend translation plan did not emit every packet";
        return false;
    }

    std::stable_sort(stream.barriers.begin(), stream.barriers.end(), [](const BackendBarrier& a, const BackendBarrier& b) {
        if (a.to_wave != b.to_wave) return a.to_wave < b.to_wave;
        if (a.from_wave != b.from_wave) return a.from_wave < b.from_wave;
        return resource_key_less(a.key, b.key);
    });
    stream.semantic_digest = semantic;
    stream.backend_digest = backend_stream_digest(stream);
    error.clear();
    return true;
}

} // namespace aion
