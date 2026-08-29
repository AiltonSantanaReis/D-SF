#pragma once

#include "aion/kernel/execution.hpp"
#include "aion/kernel/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace aion {

enum class GeometryObjective : std::uint8_t {
    MinQueryLatency = 0,
    MinStorage = 1,
    MinGeometricError = 2,
};

struct GeometryConstraints {
    GeometryCapabilityMask required{};
    float max_geometric_error{std::numeric_limits<float>::infinity()};
    std::size_t max_storage_bytes{std::numeric_limits<std::size_t>::max()};
};

struct GeometryTelemetrySummary {
    bool available{false};
    std::uint64_t epoch{};
    std::size_t samples{};
    double median_us_per_unit{};
    double mad_us_per_unit{};
    double p90_us_per_unit{};
    double median_cpu_us_per_unit{};
    std::uint64_t age{};
};

[[nodiscard]] std::uint8_t geometry_batch_class(std::size_t work_units) noexcept;

class GeometryTelemetryStore final {
public:
    explicit GeometryTelemetryStore(std::size_t window_size = 7);

    void record(
        GeometryHandle handle,
        std::uint64_t source_revision,
        const BatchObservation& observation);

    // Test/simulation entry point. Cost is normalized microseconds per work unit and still advances
    // the same context sequence used by real observations.
    void record_normalized(
        GeometryHandle handle,
        std::uint64_t source_revision,
        std::uint64_t workload_id,
        ExecutionDevice device,
        std::size_t work_units,
        double wall_us_per_unit,
        double cpu_us_per_unit);

    void reset_epoch(
        GeometryHandle handle,
        std::uint64_t source_revision,
        std::uint64_t workload_id,
        ExecutionDevice device,
        std::size_t work_units);

    [[nodiscard]] GeometryTelemetrySummary summary(
        GeometryHandle handle,
        std::uint64_t source_revision,
        std::uint64_t workload_id,
        ExecutionDevice device,
        std::size_t work_units) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

struct GeometrySelectionRequest {
    GeometryConstraints constraints{};
    GeometryObjective objective{GeometryObjective::MinStorage};
    std::uint64_t workload_id{};
    ExecutionDevice device{ExecutionDevice::CpuReference};
    std::size_t work_units{1};
    std::size_t min_observed_batches{3};
    std::uint64_t max_observation_age{std::numeric_limits<std::uint64_t>::max()};
    double max_relative_mad{std::numeric_limits<double>::infinity()};
};

struct GeometrySelectionDecision {
    bool ok{false};
    GeometryHandle handle{};
    std::vector<GeometryHandle> pareto;
    std::string error;
};

[[nodiscard]] GeometrySelectionDecision select_geometry(
    const GeometrySet& set,
    const GeometryKernel& kernel,
    const GeometryTelemetryStore& telemetry,
    const GeometrySelectionRequest& request);

struct GeometryObservationSamplingPolicy {
    std::size_t warmup_samples{3};
    std::size_t work_units_per_sample{2048};
    std::size_t max_gap_batches{32};
};

class GeometryObservationSampler final {
public:
    [[nodiscard]] bool should_observe(
        GeometryHandle handle,
        std::uint64_t source_revision,
        std::uint64_t workload_id,
        ExecutionDevice device,
        std::size_t work_units,
        const GeometryTelemetryStore& telemetry,
        const GeometryObservationSamplingPolicy& policy);
private:
    struct State { std::size_t unobserved_work{}; std::size_t gap_batches{}; };
    std::map<std::tuple<std::uint64_t, std::uint64_t, std::uint8_t, std::uint8_t>, State> states_;
};

struct GeometryRayBatchResult {
    bool ok{false};
    bool observed{false};
    BatchObservation observation{};
    std::vector<GeometryRayHit> hits;
    std::string error;
};

[[nodiscard]] GeometryRayBatchResult execute_geometry_ray_batch(
    const GeometryKernel& kernel,
    GeometryHandle handle,
    std::span<const Ray> rays);

[[nodiscard]] GeometryRayBatchResult execute_geometry_ray_batch_observed(
    const GeometryKernel& kernel,
    GeometryHandle handle,
    std::uint64_t source_revision,
    std::uint64_t workload_id,
    ExecutionDevice device,
    std::span<const Ray> rays,
    GeometryTelemetryStore& telemetry,
    GeometryObservationSampler* sampler,
    const GeometryObservationSamplingPolicy& sampling);

struct GeometryRouteRequest {
    GeometryConstraints constraints{};
    std::uint64_t workload_id{};
    ExecutionDevice device{ExecutionDevice::CpuReference};
    std::size_t work_units{1};
    std::size_t min_observed_batches{3};
    std::uint64_t refresh_age{8};
    bool allow_exploration{true};
};

struct GeometryRouteDecision {
    bool ok{false};
    GeometryHandle handle{};
    bool exploration{false};
    bool stale_refresh{false};
    bool reset_telemetry_before_observe{false};
    std::string reason;
};

// R4F fixed-freshness reference policy. It never duplicates a batch. Bootstrap/stale refresh routes
// the next real batch to an eligible alternative; caller then observes that one execution.
[[nodiscard]] GeometryRouteDecision route_geometry_batch(
    const GeometrySet& set,
    const GeometryKernel& kernel,
    const GeometryTelemetryStore& telemetry,
    const GeometryRouteRequest& request);

} // namespace aion
