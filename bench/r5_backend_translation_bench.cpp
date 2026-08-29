#include "aion/kernel/device_backend.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace aion;
using Clock = std::chrono::steady_clock;

DeviceResourceUpload upload(std::uint64_t id, DeviceResourceClass cls) {
    DeviceResourceUpload value;
    value.key = work_resource_key(id, 1U, cls, 0U);
    value.bytes.resize(256U, static_cast<std::byte>(id & 0xFFU));
    return value;
}

void require(bool condition, const std::string& error) {
    if (!condition) { std::cerr << "R5D BENCH FAIL: " << error << '\n'; std::exit(1); }
}

void run(std::size_t count, BackendCapabilityProfile profile, BackendTranslationPolicy policy, const char* label) {
    ReferenceDeviceBackend backend;
    DeviceResidencyManager residency(backend, 1U << 20U);
    std::string error;
    const auto input = upload(1U, DeviceResourceClass::WorkInput);
    const auto output = upload(2U, DeviceResourceClass::WorkOutput);
    DeviceResourceHandle handle{};
    require(residency.ensure(input, handle, error), error);
    require(residency.ensure(output, handle, error), error);

    std::vector<DeviceWorkPacket> packets;
    packets.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        DeviceWorkPacket packet;
        packet.id = static_cast<std::uint32_t>(i + 1U);
        packet.domain = DeviceWorkDomain::Compute;
        packet.program_key = 0xD500000000000000ULL + static_cast<std::uint64_t>(i % 7U);
        packet.launch.mode = DeviceWorkLaunchMode::Direct;
        packet.launch.x = 64U;
        packet.resources = {{input.key, DeviceResourceAccess::Read}};
        packet.parameters = {static_cast<std::byte>(i & 0xFFU)};
        packets.push_back(std::move(packet));
    }

    DeviceWorkPlanner planner;
    DeviceWorkCapabilities planner_caps;
    planner_caps.device_generated_work = true;
    DeviceWorkPlan plan;
    require(planner.build(packets, residency, planner_caps, plan, error), error);
    BackendWorkTranslator translator;
    BackendTranslatedStream stream;
    const auto begin = Clock::now();
    require(translator.translate(packets, plan, residency, profile, policy, stream, error), error);
    const auto end = Clock::now();
    const auto ms = std::chrono::duration<double, std::milli>(end - begin).count();
    std::cout << "count=" << count << " mode=" << label << " ms=" << ms
              << " commands=" << stream.commands.size() << " descriptors=" << stream.descriptors.size()
              << " uses=" << stream.uses.size()
              << " digest=" << stream.backend_digest << '\n';
}
}

int main() {
    using namespace aion;
    const std::vector<std::size_t> counts{100U, 1000U, 5000U};
    for (const auto count : counts) {
        run(count, reference_direct_backend(), {}, "direct");
        run(count, reference_indirect_backend(), {BackendLaunchPreference::PreferIndirect, true}, "indirect");
        run(count, reference_generated_sequence_backend(), {BackendLaunchPreference::PreferDeviceGenerated, true}, "generated_sequence");
        run(count, reference_work_graph_backend(), {BackendLaunchPreference::PreferDeviceGenerated, true}, "work_graph");
    }
    return 0;
}
