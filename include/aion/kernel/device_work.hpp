#pragma once

#include "aion/kernel/device_geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace aion {

enum class DeviceWorkDomain : std::uint8_t {
    Compute = 1,
    Graphics = 2,
    Ray = 3,
};

enum class DeviceWorkLaunchMode : std::uint8_t {
    Direct = 1,
    Indirect = 2,
    DeviceGenerated = 3,
};

enum class DeviceResourceAccess : std::uint8_t {
    Read = 1,
    Write = 2,
    ReadWrite = 3,
};

struct DeviceWorkResourceRef {
    DeviceResourceKey key{};
    DeviceResourceAccess access{DeviceResourceAccess::Read};
    friend bool operator==(const DeviceWorkResourceRef&, const DeviceWorkResourceRef&) = default;
};

struct DeviceWorkLaunch {
    DeviceWorkLaunchMode mode{DeviceWorkLaunchMode::Direct};
    std::uint32_t x{1};
    std::uint32_t y{1};
    std::uint32_t z{1};
    DeviceResourceKey control_resource{};
    std::uint64_t control_offset{};
    std::uint32_t max_sequences{};
};

struct DeviceWorkPacket {
    std::uint32_t id{};
    DeviceWorkDomain domain{DeviceWorkDomain::Compute};
    std::uint64_t program_key{}; // opaque backend-resolved program identity
    DeviceWorkLaunch launch{};
    std::vector<DeviceWorkResourceRef> resources;
    std::vector<std::uint32_t> after;
    std::vector<std::byte> parameters;
};

struct DeviceWorkCapabilities {
    bool compute{true};
    bool graphics{true};
    bool ray{false};
    bool indirect_launch{true};
    bool device_generated_work{false};
};

struct DeviceWorkPlan {
    std::vector<std::vector<std::uint32_t>> waves; // packet ids; stable ascending order per wave
};

class DeviceWorkPlanner final {
public:
    [[nodiscard]] bool build(
        std::span<const DeviceWorkPacket> packets,
        const DeviceResidencyManager& residency,
        const DeviceWorkCapabilities& capabilities,
        DeviceWorkPlan& plan,
        std::string& error) const;
};

struct ReferenceDeviceCommand {
    std::uint32_t packet_id{};
    std::uint32_t wave{};
    DeviceWorkDomain domain{DeviceWorkDomain::Compute};
    DeviceWorkLaunchMode launch_mode{DeviceWorkLaunchMode::Direct};
    std::uint64_t program_key{};
    std::uint32_t resource_count{};
};

struct ReferenceDeviceCommandStream {
    std::vector<ReferenceDeviceCommand> commands;
    std::uint64_t digest{};
};

class ReferenceDeviceWorkCompiler final {
public:
    [[nodiscard]] bool compile(
        std::span<const DeviceWorkPacket> packets,
        const DeviceWorkPlan& plan,
        ReferenceDeviceCommandStream& stream,
        std::string& error) const;
};

enum class GeometryDeviceOperation : std::uint8_t {
    RaySurfaceBatch = 1,
    TruncatedDistanceBatch = 2,
};

struct GeometryDeviceWorkRequest {
    std::uint32_t packet_id{};
    GeometryDeviceOperation operation{GeometryDeviceOperation::RaySurfaceBatch};
    std::uint32_t work_items{};
    DeviceWorkLaunchMode launch_mode{DeviceWorkLaunchMode::Direct};
    DeviceResourceKey input{};
    DeviceResourceKey output{};
    DeviceResourceKey launch_control{}; // used by indirect/device-generated modes
    std::uint32_t max_sequences{};
};

class GeometryDeviceWorkCompilerRegistry final {
public:
    using Compiler = std::function<bool(const DeviceGeometryPackage&, const GeometryDeviceWorkRequest&, DeviceWorkPacket&, std::string&)>;

    [[nodiscard]] bool register_compiler(GeometryProviderId provider, Compiler compiler, std::string& error);
    [[nodiscard]] bool compile(
        const DeviceGeometryPackage& package,
        const GeometryDeviceWorkRequest& request,
        DeviceWorkPacket& packet,
        std::string& error) const;

private:
    struct Entry { GeometryProviderId provider{}; Compiler compiler; };
    std::vector<Entry> entries_;
};

[[nodiscard]] bool register_sparse_sdf_device_work_compiler(
    GeometryDeviceWorkCompilerRegistry& registry,
    GeometryProviderId provider,
    std::string& error);

[[nodiscard]] bool register_clustered_triangle_device_work_compiler(
    GeometryDeviceWorkCompilerRegistry& registry,
    GeometryProviderId provider,
    std::string& error);

} // namespace aion
