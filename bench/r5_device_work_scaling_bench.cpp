#include "aion/kernel/device_work.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace aion;
using Clock = std::chrono::steady_clock;

struct ScenarioResult { std::size_t packets{}; double ms{}; std::size_t waves{}; };

ScenarioResult run(std::size_t count, bool shared_write) {
    ReferenceDeviceBackend backend;
    DeviceResidencyManager residency(backend, count + 8U);
    std::string error;
    std::vector<DeviceResourceUpload> uploads;
    uploads.reserve(shared_write ? 2U : count + 1U);
    DeviceResourceUpload shared;
    shared.key = work_resource_key(1U, 1U, DeviceResourceClass::WorkInput);
    shared.bytes = {std::byte{1}};
    uploads.push_back(shared);
    DeviceResourceUpload shared_output;
    shared_output.key = work_resource_key(2U, 1U, DeviceResourceClass::WorkOutput);
    shared_output.bytes = {std::byte{2}};
    if (shared_write) uploads.push_back(shared_output);
    if (!shared_write) {
        for (std::size_t i = 0; i < count; ++i) {
            DeviceResourceUpload output;
            output.key = work_resource_key(100U + i, 1U, DeviceResourceClass::WorkOutput);
            output.bytes = {std::byte{3}};
            uploads.push_back(std::move(output));
        }
    }
    std::vector<DeviceResourceHandle> handles;
    if (!residency.ensure_group(uploads, handles, error)) throw std::runtime_error(error);

    std::vector<DeviceWorkPacket> packets;
    packets.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        DeviceWorkPacket packet;
        packet.id = static_cast<std::uint32_t>(i + 1U);
        packet.domain = DeviceWorkDomain::Compute;
        packet.program_key = 0x1234U;
        packet.launch = {DeviceWorkLaunchMode::Direct, 1U, 1U, 1U, {}, 0U, 0U};
        packet.resources.push_back({shared.key, DeviceResourceAccess::Read});
        packet.resources.push_back({shared_write ? shared_output.key : uploads[i + 1U].key, DeviceResourceAccess::Write});
        packets.push_back(std::move(packet));
    }

    DeviceWorkPlanner planner;
    DeviceWorkPlan plan;
    const auto begin = Clock::now();
    if (!planner.build(packets, residency, {}, plan, error)) throw std::runtime_error(error);
    const auto end = Clock::now();
    return {count, std::chrono::duration<double, std::milli>(end - begin).count(), plan.waves.size()};
}
}

int main() {
    std::cout << std::fixed << std::setprecision(3);
    for (const std::size_t n : {100U, 500U, 1000U, 5000U, 10000U, 50000U, 100000U}) {
        const auto independent = run(n, false);
        std::cout << "independent packets=" << independent.packets << " plan_ms=" << independent.ms << " waves=" << independent.waves << '\n';
    }
    for (const std::size_t n : {100U, 500U, 1000U, 5000U, 10000U, 50000U, 100000U}) {
        const auto chain = run(n, true);
        std::cout << "write_chain packets=" << chain.packets << " plan_ms=" << chain.ms << " waves=" << chain.waves << '\n';
    }
}
