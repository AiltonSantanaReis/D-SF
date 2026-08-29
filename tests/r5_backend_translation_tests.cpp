#include "aion/kernel/device_backend.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace aion;

[[noreturn]] void fail(const std::string& message) { std::cerr << "R5D FAIL: " << message << '\n'; std::exit(1); }
void require(bool condition, const std::string& message) { if (!condition) fail(message); }

DeviceResourceUpload upload(std::uint64_t id, DeviceResourceClass cls, std::size_t bytes = 256U) {
    DeviceResourceUpload value;
    value.key = work_resource_key(id, 1U, cls, 0U);
    value.bytes.resize(bytes, static_cast<std::byte>(id & 0xFFU));
    return value;
}

void ensure(DeviceResidencyManager& residency, const DeviceResourceUpload& value, std::string& error) {
    DeviceResourceHandle handle{};
    require(residency.ensure(value, handle, error), error);
    require(handle.valid() && residency.resident(handle), "resource did not become resident");
}

DeviceWorkPacket make_direct(std::uint32_t id, const DeviceResourceKey& input, const DeviceResourceKey& output) {
    DeviceWorkPacket packet;
    packet.id = id;
    packet.domain = DeviceWorkDomain::Compute;
    packet.program_key = 0xD500000000000000ULL + id;
    packet.launch.mode = DeviceWorkLaunchMode::Direct;
    packet.launch.x = 17U;
    packet.launch.y = 2U;
    packet.launch.z = 1U;
    packet.resources = {{input, DeviceResourceAccess::Read}, {output, DeviceResourceAccess::Write}};
    packet.parameters = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
    return packet;
}

DeviceWorkPacket make_indirect(std::uint32_t id, const DeviceResourceKey& input, const DeviceResourceKey& output, const DeviceResourceKey& control) {
    auto packet = make_direct(id, input, output);
    packet.launch.mode = DeviceWorkLaunchMode::Indirect;
    packet.launch.x = 1U;
    packet.launch.y = 1U;
    packet.launch.z = 1U;
    packet.launch.control_resource = control;
    packet.launch.control_offset = 16U;
    packet.launch.max_sequences = 4096U;
    packet.resources.push_back({control, DeviceResourceAccess::Read});
    return packet;
}

DeviceWorkPacket make_generated(std::uint32_t id, const DeviceResourceKey& input, const DeviceResourceKey& output, const DeviceResourceKey& control) {
    auto packet = make_indirect(id, input, output, control);
    packet.launch.mode = DeviceWorkLaunchMode::DeviceGenerated;
    return packet;
}

} // namespace

int main() {
    using namespace aion;
    ReferenceDeviceBackend backend;
    DeviceResidencyManager residency(backend, 1U << 20U);
    std::string error;
    const auto input = upload(100U, DeviceResourceClass::WorkInput);
    const auto output_a = upload(101U, DeviceResourceClass::WorkOutput);
    const auto output_b = upload(102U, DeviceResourceClass::WorkOutput);
    const auto control = upload(103U, DeviceResourceClass::WorkInput, 64U);
    ensure(residency, input, error);
    ensure(residency, output_a, error);
    ensure(residency, output_b, error);
    ensure(residency, control, error);

    DeviceWorkPlanner planner;
    BackendWorkTranslator translator;
    DeviceWorkCapabilities planner_caps;
    planner_caps.device_generated_work = true;

    // One static packet can be represented by different backend mechanisms without changing semantic work.
    const auto direct_packet = make_direct(10U, input.key, output_a.key);
    const std::array<DeviceWorkPacket,1> direct_packets{direct_packet};
    DeviceWorkPlan direct_plan;
    require(planner.build(direct_packets, residency, planner_caps, direct_plan, error), error);

    BackendTranslatedStream direct_stream, indirect_promoted, sequence_promoted, graph_promoted;
    BackendTranslationPolicy preserve{};
    BackendTranslationPolicy prefer_indirect{BackendLaunchPreference::PreferIndirect, true};
    BackendTranslationPolicy prefer_generated{BackendLaunchPreference::PreferDeviceGenerated, true};
    require(translator.translate(direct_packets, direct_plan, residency, reference_direct_backend(), preserve, direct_stream, error), error);
    require(translator.translate(direct_packets, direct_plan, residency, reference_indirect_backend(), prefer_indirect, indirect_promoted, error), error);
    require(translator.translate(direct_packets, direct_plan, residency, reference_generated_sequence_backend(), prefer_generated, sequence_promoted, error), error);
    require(translator.translate(direct_packets, direct_plan, residency, reference_work_graph_backend(), prefer_generated, graph_promoted, error), error);
    require(direct_stream.commands[0].launch_kind == BackendLaunchKind::Direct, "direct backend did not preserve direct launch");
    require(direct_stream.commands[0].control_source == BackendLaunchControlSource::InlineStatic, "direct launch control source is wrong");
    require(indirect_promoted.commands[0].launch_kind == BackendLaunchKind::Indirect, "static work was not promotable to indirect lowering");
    require(indirect_promoted.commands[0].control_source == BackendLaunchControlSource::BackendStaticRecord, "static promotion did not expose backend-owned launch record");
    require(sequence_promoted.commands[0].launch_kind == BackendLaunchKind::GeneratedSequence, "generated-sequence lowering missing");
    require(graph_promoted.commands[0].launch_kind == BackendLaunchKind::WorkGraph, "work-graph lowering missing");
    require(direct_stream.semantic_digest == indirect_promoted.semantic_digest
        && direct_stream.semantic_digest == sequence_promoted.semantic_digest
        && direct_stream.semantic_digest == graph_promoted.semantic_digest,
        "backend lowering changed semantic digest");
    require(direct_stream.backend_digest != indirect_promoted.backend_digest
        && sequence_promoted.backend_digest != graph_promoted.backend_digest,
        "materially different backend lowerings collapsed to one backend digest");

    // Dynamic indirect work cannot be demoted to direct without a readback/synchronization semantic change.
    const auto indirect_packet = make_indirect(20U, input.key, output_a.key, control.key);
    const std::array<DeviceWorkPacket,1> indirect_packets{indirect_packet};
    DeviceWorkPlan indirect_plan;
    require(planner.build(indirect_packets, residency, planner_caps, indirect_plan, error), error);
    BackendTranslatedStream translated;
    require(!translator.translate(indirect_packets, indirect_plan, residency, reference_direct_backend(), preserve, translated, error),
        "dynamic indirect work was silently demoted to direct");
    require(translator.translate(indirect_packets, indirect_plan, residency, reference_indirect_backend(), preserve, translated, error), error);
    require(translated.commands[0].launch_kind == BackendLaunchKind::Indirect, "indirect lowering changed launch kind");
    require(translated.commands[0].control_source == BackendLaunchControlSource::ResidentResource, "dynamic indirect work lost resident control source");
    require(translator.translate(indirect_packets, indirect_plan, residency, reference_generated_sequence_backend(), prefer_generated, translated, error), error);
    require(translated.commands[0].launch_kind == BackendLaunchKind::GeneratedSequence, "indirect work was not promotable to generated sequence");

    // Device-generated work cannot be lowered to a backend that lacks a generated-work model.
    const auto generated_packet = make_generated(30U, input.key, output_a.key, control.key);
    const std::array<DeviceWorkPacket,1> generated_packets{generated_packet};
    DeviceWorkPlan generated_plan;
    require(planner.build(generated_packets, residency, planner_caps, generated_plan, error), error);
    require(!translator.translate(generated_packets, generated_plan, residency, reference_indirect_backend(), preserve, translated, error),
        "device-generated packet was silently demoted");
    require(translator.translate(generated_packets, generated_plan, residency, reference_generated_sequence_backend(), preserve, translated, error), error);
    require(translated.commands[0].launch_kind == BackendLaunchKind::GeneratedSequence, "generated packet did not lower to generated sequence");
    BackendTranslatedStream generated_graph_stream;
    require(translator.translate(generated_packets, generated_plan, residency, reference_work_graph_backend(), preserve, generated_graph_stream, error), error);
    require(generated_graph_stream.commands[0].launch_kind == BackendLaunchKind::WorkGraph, "generated packet did not lower to work graph");
    BackendTranslatedStream indirect_semantic_stream;
    require(translator.translate(indirect_packets, indirect_plan, residency, reference_indirect_backend(), preserve, indirect_semantic_stream, error), error);
    require(generated_graph_stream.semantic_digest != indirect_semantic_stream.semantic_digest,
        "semantic digest collapsed indirect and device-generated launch contracts");

    // Launch-control resources are semantically visible dependencies, not hidden backend state.
    auto malformed = indirect_packet;
    malformed.resources.erase(std::remove_if(malformed.resources.begin(), malformed.resources.end(), [&](const DeviceWorkResourceRef& ref) {
        return ref.key == control.key;
    }), malformed.resources.end());
    const std::array<DeviceWorkPacket,1> malformed_packets{malformed};
    DeviceWorkPlan malformed_plan;
    require(!planner.build(malformed_packets, residency, planner_caps, malformed_plan, error),
        "planner accepted hidden launch-control dependency");
    require(!translator.translate(malformed_packets, indirect_plan, residency, reference_indirect_backend(), preserve, translated, error),
        "translator accepted hidden launch-control dependency");

    // Hazard waves must become explicit backend barriers, while read/read remains barrier-free.
    auto writer_a = make_direct(40U, input.key, output_a.key);
    auto writer_b = make_direct(50U, input.key, output_a.key);
    const std::array<DeviceWorkPacket,2> hazard_packets{writer_b, writer_a};
    DeviceWorkPlan hazard_plan;
    require(planner.build(hazard_packets, residency, planner_caps, hazard_plan, error), error);
    require(hazard_plan.waves.size() == 2U, "write hazard did not create two waves");
    BackendTranslatedStream hazard_stream;
    require(translator.translate(hazard_packets, hazard_plan, residency, reference_direct_backend(), preserve, hazard_stream, error), error);
    require(hazard_stream.barriers.size() == 1U, "write/write hazard did not become exactly one resource barrier");
    require(hazard_stream.barriers[0].key == output_a.key
        && hazard_stream.barriers[0].before == DeviceResourceAccess::Write
        && hazard_stream.barriers[0].after == DeviceResourceAccess::Write,
        "backend barrier does not describe the resource hazard");
    DeviceWorkPlan invalid_same_wave;
    invalid_same_wave.waves = {{40U, 50U}};
    require(!translator.translate(hazard_packets, invalid_same_wave, residency, reference_direct_backend(), preserve, translated, error),
        "translator accepted a same-wave write hazard from an invalid plan");

    auto reader_a = make_direct(60U, input.key, output_a.key);
    auto reader_b = make_direct(70U, input.key, output_b.key);
    for (auto& ref : reader_a.resources) if (ref.key == output_a.key) ref.access = DeviceResourceAccess::Read;
    for (auto& ref : reader_b.resources) if (ref.key == output_b.key) ref.access = DeviceResourceAccess::Read;
    reader_a.resources = {{input.key, DeviceResourceAccess::Read}};
    reader_b.resources = {{input.key, DeviceResourceAccess::Read}};
    const std::array<DeviceWorkPacket,2> read_packets{reader_b, reader_a};
    DeviceWorkPlan read_plan;
    require(planner.build(read_packets, residency, planner_caps, read_plan, error), error);
    BackendTranslatedStream read_stream;
    require(translator.translate(read_packets, read_plan, residency, reference_direct_backend(), preserve, read_stream, error), error);
    require(read_plan.waves.size() == 1U && read_stream.barriers.empty(), "read/read work created a false backend barrier");

    // Canonical translation must ignore registration and resource ordering.
    auto canonical_a = make_direct(80U, input.key, output_a.key);
    auto canonical_b = make_direct(90U, input.key, output_b.key);
    const std::array<DeviceWorkPacket,2> ordered{canonical_a, canonical_b};
    DeviceWorkPlan ordered_plan;
    require(planner.build(ordered, residency, planner_caps, ordered_plan, error), error);
    BackendTranslatedStream canonical_stream;
    require(translator.translate(ordered, ordered_plan, residency, reference_generated_sequence_backend(), prefer_generated, canonical_stream, error), error);
    std::reverse(canonical_a.resources.begin(), canonical_a.resources.end());
    std::reverse(canonical_b.resources.begin(), canonical_b.resources.end());
    const std::array<DeviceWorkPacket,2> shuffled{canonical_b, canonical_a};
    DeviceWorkPlan shuffled_plan;
    require(planner.build(shuffled, residency, planner_caps, shuffled_plan, error), error);
    BackendTranslatedStream shuffled_stream;
    require(translator.translate(shuffled, shuffled_plan, residency, reference_generated_sequence_backend(), prefer_generated, shuffled_stream, error), error);
    require(canonical_stream.semantic_digest == shuffled_stream.semantic_digest
        && canonical_stream.backend_digest == shuffled_stream.backend_digest,
        "backend translation changed under semantically irrelevant ordering");
    require(canonical_stream.descriptors.size() == 3U, "descriptor table did not deduplicate shared resources");
    require(canonical_stream.uses.size() == 4U, "resource-use stream lost per-packet access declarations");

    std::cout << "R5D PASS semantic_digest=" << direct_stream.semantic_digest
              << " direct_digest=" << direct_stream.backend_digest
              << " generated_digest=" << sequence_promoted.backend_digest
              << " work_graph_digest=" << graph_promoted.backend_digest
              << " barriers=" << hazard_stream.barriers.size() << '\n';
    return 0;
}
