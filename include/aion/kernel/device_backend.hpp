#pragma once

#include "aion/kernel/device_work.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace aion {

enum class BackendBindingModel : std::uint8_t {
    DescriptorMemory = 1,
};

enum class BackendGeneratedModel : std::uint8_t {
    None = 0,
    GeneratedSequence = 1,
    WorkGraph = 2,
};

enum class BackendLaunchKind : std::uint8_t {
    Direct = 1,
    Indirect = 2,
    GeneratedSequence = 3,
    WorkGraph = 4,
};

enum class BackendLaunchPreference : std::uint8_t {
    Preserve = 1,
    PreferIndirect = 2,
    PreferDeviceGenerated = 3,
};

struct BackendCapabilityProfile {
    bool compute{true};
    bool graphics{true};
    bool ray{false};
    bool direct_launch{true};
    bool indirect_launch{false};
    bool device_generated_work{false};
    BackendGeneratedModel generated_model{BackendGeneratedModel::None};
    BackendBindingModel binding_model{BackendBindingModel::DescriptorMemory};
};

struct BackendTranslationPolicy {
    BackendLaunchPreference launch_preference{BackendLaunchPreference::Preserve};
    bool allow_static_promotion{true};
};

struct BackendDescriptor {
    DeviceResourceKey key{};
    DeviceResourceHandle handle{};
    std::uint32_t descriptor_index{};
    friend bool operator==(const BackendDescriptor&, const BackendDescriptor&) = default;
};

struct BackendResourceUse {
    std::uint32_t descriptor_index{};
    DeviceResourceAccess access{DeviceResourceAccess::Read};
    friend bool operator==(const BackendResourceUse&, const BackendResourceUse&) = default;
};

enum class BackendLaunchControlSource : std::uint8_t {
    InlineStatic = 1,
    ResidentResource = 2,
    BackendStaticRecord = 3,
};

struct BackendBarrier {
    DeviceResourceKey key{};
    DeviceResourceAccess before{DeviceResourceAccess::Read};
    DeviceResourceAccess after{DeviceResourceAccess::Read};
    std::uint32_t from_wave{};
    std::uint32_t to_wave{};
    friend bool operator==(const BackendBarrier&, const BackendBarrier&) = default;
};

struct BackendCommand {
    std::uint32_t packet_id{};
    std::uint32_t wave{};
    DeviceWorkDomain domain{DeviceWorkDomain::Compute};
    BackendLaunchKind launch_kind{BackendLaunchKind::Direct};
    std::uint64_t program_key{};
    std::uint32_t x{1};
    std::uint32_t y{1};
    std::uint32_t z{1};
    BackendLaunchControlSource control_source{BackendLaunchControlSource::InlineStatic};
    DeviceResourceHandle control_handle{};
    std::uint64_t control_offset{};
    std::uint32_t max_sequences{};
    std::uint32_t first_use{};
    std::uint32_t use_count{};
    std::uint64_t parameter_digest{};
    friend bool operator==(const BackendCommand&, const BackendCommand&) = default;
};

struct BackendTranslatedStream {
    std::vector<BackendDescriptor> descriptors;
    std::vector<BackendResourceUse> uses;
    std::vector<BackendBarrier> barriers;
    std::vector<BackendCommand> commands;
    std::uint64_t semantic_digest{};
    std::uint64_t backend_digest{};
};

class BackendWorkTranslator final {
public:
    [[nodiscard]] bool translate(
        std::span<const DeviceWorkPacket> packets,
        const DeviceWorkPlan& plan,
        const DeviceResidencyManager& residency,
        const BackendCapabilityProfile& capabilities,
        const BackendTranslationPolicy& policy,
        BackendTranslatedStream& stream,
        std::string& error) const;
};

[[nodiscard]] constexpr BackendCapabilityProfile reference_direct_backend() noexcept {
    return {true, true, false, true, false, false, BackendGeneratedModel::None, BackendBindingModel::DescriptorMemory};
}

[[nodiscard]] constexpr BackendCapabilityProfile reference_indirect_backend() noexcept {
    return {true, true, false, true, true, false, BackendGeneratedModel::None, BackendBindingModel::DescriptorMemory};
}

[[nodiscard]] constexpr BackendCapabilityProfile reference_generated_sequence_backend() noexcept {
    return {true, true, false, true, true, true, BackendGeneratedModel::GeneratedSequence, BackendBindingModel::DescriptorMemory};
}

[[nodiscard]] constexpr BackendCapabilityProfile reference_work_graph_backend() noexcept {
    return {true, true, false, true, true, true, BackendGeneratedModel::WorkGraph, BackendBindingModel::DescriptorMemory};
}

} // namespace aion
