#include "aion/kernel/geometry_fabric.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>

namespace aion {
namespace {

[[nodiscard]] std::uint64_t pack_handle(GeometryHandle h) noexcept {
    return (static_cast<std::uint64_t>(h.provider) << 48U)
         | (static_cast<std::uint64_t>(h.generation) << 32U)
         | static_cast<std::uint64_t>(h.resource);
}

struct ContextKey {
    std::uint64_t workload{};
    std::uint8_t device{};
    std::uint8_t batch_class{};
    friend bool operator<(const ContextKey& a, const ContextKey& b) noexcept {
        return std::tie(a.workload, a.device, a.batch_class) < std::tie(b.workload, b.device, b.batch_class);
    }
};
struct SeriesKey {
    std::uint64_t packed_handle{};
    std::uint64_t revision{};
    ContextKey context{};
    friend bool operator<(const SeriesKey& a, const SeriesKey& b) noexcept {
        return std::tie(a.packed_handle, a.revision, a.context) < std::tie(b.packed_handle, b.revision, b.context);
    }
};

[[nodiscard]] double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2U;
    if ((values.size() & 1U) != 0U) return values[mid];
    return 0.5 * (values[mid - 1U] + values[mid]);
}
[[nodiscard]] double percentile90(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(std::ceil(0.9 * static_cast<double>(values.size()))) - 1U;
    return values[std::min(index, values.size() - 1U)];
}

struct CandidateMetrics {
    GeometryRepresentation rep{};
    std::size_t storage{};
    GeometryTelemetrySummary telemetry{};
};

[[nodiscard]] std::vector<CandidateMetrics> candidate_metrics(
    const GeometrySet& set,
    const GeometryKernel& kernel,
    const GeometryTelemetryStore& telemetry,
    const GeometrySelectionRequest& request) {
    std::vector<CandidateMetrics> out;
    for (const auto& rep : set.eligible(kernel, request.constraints.required, request.constraints.max_geometric_error)) {
        const auto storage = kernel.storage_bytes(rep.handle);
        if (storage > request.constraints.max_storage_bytes) continue;
        out.push_back({rep, storage, telemetry.summary(rep.handle, rep.source_revision, request.workload_id, request.device, request.work_units)});
    }
    return out;
}

[[nodiscard]] bool latency_usable(const GeometryTelemetrySummary& s, const GeometrySelectionRequest& request) noexcept {
    if (!s.available || s.samples < request.min_observed_batches || s.age > request.max_observation_age) return false;
    if (s.median_us_per_unit <= 0.0) return true;
    return (s.mad_us_per_unit / s.median_us_per_unit) <= request.max_relative_mad;
}

[[nodiscard]] std::vector<GeometryHandle> pareto_front(
    const std::vector<CandidateMetrics>& values,
    bool include_latency) {
    std::vector<GeometryHandle> out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        bool dominated = false;
        for (std::size_t j = 0; j < values.size() && !dominated; ++j) {
            if (i == j) continue;
            const bool error_le = values[j].rep.max_geometric_error <= values[i].rep.max_geometric_error;
            const bool storage_le = values[j].storage <= values[i].storage;
            const bool latency_le = !include_latency || values[j].telemetry.median_us_per_unit <= values[i].telemetry.median_us_per_unit;
            const bool strict = values[j].rep.max_geometric_error < values[i].rep.max_geometric_error
                || values[j].storage < values[i].storage
                || (include_latency && values[j].telemetry.median_us_per_unit < values[i].telemetry.median_us_per_unit);
            dominated = error_le && storage_le && latency_le && strict;
        }
        if (!dominated) out.push_back(values[i].rep.handle);
    }
    return out;
}

} // namespace

struct GeometryTelemetryStore::Impl {
    struct Series {
        std::uint64_t epoch{1};
        std::uint64_t last_context_sequence{};
        std::vector<double> wall;
        std::vector<double> cpu;
    };
    std::size_t window_size{7};
    std::map<ContextKey, std::uint64_t> context_sequences;
    std::map<SeriesKey, Series> series;
};

std::uint8_t geometry_batch_class(std::size_t work_units) noexcept {
    if (work_units <= 1U) return 0U;
    const unsigned int width = static_cast<unsigned int>(std::bit_width(work_units));
    return static_cast<std::uint8_t>(std::min(width - 1U, 63U));
}

GeometryTelemetryStore::GeometryTelemetryStore(std::size_t window_size)
    : impl_(std::make_shared<Impl>()) {
    impl_->window_size = std::max<std::size_t>(1U, window_size);
}

void GeometryTelemetryStore::record(
    GeometryHandle handle,
    std::uint64_t source_revision,
    const BatchObservation& observation) {
    if (observation.work_units == 0U) return;
    const double scale = 1000.0 / static_cast<double>(observation.work_units);
    record_normalized(handle, source_revision, observation.workload_id, observation.device, observation.work_units,
        observation.wall_ms * scale, observation.cpu_ms * scale);
}

void GeometryTelemetryStore::record_normalized(
    GeometryHandle handle,
    std::uint64_t source_revision,
    std::uint64_t workload_id,
    ExecutionDevice device,
    std::size_t work_units,
    double wall_us_per_unit,
    double cpu_us_per_unit) {
    if (!handle.valid() || source_revision == 0U || work_units == 0U || !std::isfinite(wall_us_per_unit) || wall_us_per_unit < 0.0) return;
    const ContextKey context{workload_id, static_cast<std::uint8_t>(device), geometry_batch_class(work_units)};
    auto& sequence = impl_->context_sequences[context];
    ++sequence;
    const SeriesKey key{pack_handle(handle), source_revision, context};
    auto& series = impl_->series[key];
    series.last_context_sequence = sequence;
    series.wall.push_back(wall_us_per_unit);
    series.cpu.push_back(std::max(0.0, cpu_us_per_unit));
    if (series.wall.size() > impl_->window_size) series.wall.erase(series.wall.begin());
    if (series.cpu.size() > impl_->window_size) series.cpu.erase(series.cpu.begin());
}

void GeometryTelemetryStore::reset_epoch(
    GeometryHandle handle,
    std::uint64_t source_revision,
    std::uint64_t workload_id,
    ExecutionDevice device,
    std::size_t work_units) {
    const ContextKey context{workload_id, static_cast<std::uint8_t>(device), geometry_batch_class(work_units)};
    const SeriesKey key{pack_handle(handle), source_revision, context};
    auto& series = impl_->series[key];
    ++series.epoch;
    series.wall.clear();
    series.cpu.clear();
    series.last_context_sequence = impl_->context_sequences[context];
}

GeometryTelemetrySummary GeometryTelemetryStore::summary(
    GeometryHandle handle,
    std::uint64_t source_revision,
    std::uint64_t workload_id,
    ExecutionDevice device,
    std::size_t work_units) const {
    GeometryTelemetrySummary out;
    const ContextKey context{workload_id, static_cast<std::uint8_t>(device), geometry_batch_class(work_units)};
    const SeriesKey key{pack_handle(handle), source_revision, context};
    const auto it = impl_->series.find(key);
    if (it == impl_->series.end()) return out;
    const auto& series = it->second;
    out.epoch = series.epoch;
    out.samples = series.wall.size();
    out.available = !series.wall.empty();
    if (!out.available) return out;
    out.median_us_per_unit = median(series.wall);
    std::vector<double> deviations;
    deviations.reserve(series.wall.size());
    for (const double value : series.wall) deviations.push_back(std::fabs(value - out.median_us_per_unit));
    out.mad_us_per_unit = median(std::move(deviations));
    out.p90_us_per_unit = percentile90(series.wall);
    out.median_cpu_us_per_unit = median(series.cpu);
    const auto seq = impl_->context_sequences.find(context);
    const std::uint64_t current = seq == impl_->context_sequences.end() ? 0U : seq->second;
    out.age = current >= series.last_context_sequence ? current - series.last_context_sequence : 0U;
    return out;
}

GeometrySelectionDecision select_geometry(
    const GeometrySet& set,
    const GeometryKernel& kernel,
    const GeometryTelemetryStore& telemetry,
    const GeometrySelectionRequest& request) {
    GeometrySelectionDecision out;
    auto values = candidate_metrics(set, kernel, telemetry, request);
    if (values.empty()) { out.error = "no geometry representation satisfies hard constraints"; return out; }
    if (request.objective == GeometryObjective::MinQueryLatency) {
        for (const auto& value : values) {
            if (!latency_usable(value.telemetry, request)) {
                out.error = "latency objective requires fresh, sufficiently stable telemetry for every eligible representation";
                return out;
            }
        }
    }
    out.pareto = pareto_front(values, request.objective == GeometryObjective::MinQueryLatency);
    auto better = [&](const CandidateMetrics& a, const CandidateMetrics& b) {
        if (request.objective == GeometryObjective::MinStorage) return a.storage < b.storage;
        if (request.objective == GeometryObjective::MinGeometricError) return a.rep.max_geometric_error < b.rep.max_geometric_error;
        return a.telemetry.median_us_per_unit < b.telemetry.median_us_per_unit;
    };
    const auto best = std::min_element(values.begin(), values.end(), [&](const auto& a, const auto& b) {
        if (better(a, b)) return true;
        if (better(b, a)) return false;
        return pack_handle(a.rep.handle) < pack_handle(b.rep.handle);
    });
    out.ok = true;
    out.handle = best->rep.handle;
    return out;
}

bool GeometryObservationSampler::should_observe(
    GeometryHandle handle,
    std::uint64_t source_revision,
    std::uint64_t workload_id,
    ExecutionDevice device,
    std::size_t work_units,
    const GeometryTelemetryStore& telemetry,
    const GeometryObservationSamplingPolicy& policy) {
    const auto summary = telemetry.summary(handle, source_revision, workload_id, device, work_units);
    const auto key = std::make_tuple(pack_handle(handle), source_revision, static_cast<std::uint8_t>(device), geometry_batch_class(work_units));
    auto& state = states_[key];
    if (summary.samples < policy.warmup_samples) { state = {}; return true; }
    state.unobserved_work += work_units;
    ++state.gap_batches;
    if (state.unobserved_work >= policy.work_units_per_sample || state.gap_batches >= policy.max_gap_batches) {
        state = {};
        return true;
    }
    return false;
}

GeometryRayBatchResult execute_geometry_ray_batch(
    const GeometryKernel& kernel,
    GeometryHandle handle,
    std::span<const Ray> rays) {
    GeometryRayBatchResult out;
    if (!kernel.valid(handle) || !kernel.capabilities(handle).contains(GeometryCapability::RaySurface)) {
        out.error = "ray batch requires a valid RaySurface representation";
        return out;
    }
    out.hits.reserve(rays.size());
    for (const auto& ray : rays) out.hits.push_back(kernel.raycast(handle, ray));
    out.ok = true;
    return out;
}

GeometryRayBatchResult execute_geometry_ray_batch_observed(
    const GeometryKernel& kernel,
    GeometryHandle handle,
    std::uint64_t source_revision,
    std::uint64_t workload_id,
    ExecutionDevice device,
    std::span<const Ray> rays,
    GeometryTelemetryStore& telemetry,
    GeometryObservationSampler* sampler,
    const GeometryObservationSamplingPolicy& sampling) {
    GeometryRayBatchResult out;
    if (!kernel.valid(handle) || !kernel.capabilities(handle).contains(GeometryCapability::RaySurface)) {
        out.error = "observed ray batch requires a valid RaySurface representation";
        return out;
    }
    const bool observe = sampler == nullptr || sampler->should_observe(handle, source_revision, workload_id, device, rays.size(), telemetry, sampling);
    out.hits.reserve(rays.size());
    if (observe) {
        out.observation = ExecutionKernel::observe_batch(device, workload_id, rays.size(), [&] {
            for (const auto& ray : rays) out.hits.push_back(kernel.raycast(handle, ray));
        });
        telemetry.record(handle, source_revision, out.observation);
        out.observed = true;
    } else {
        for (const auto& ray : rays) out.hits.push_back(kernel.raycast(handle, ray));
    }
    out.ok = true;
    return out;
}

GeometryRouteDecision route_geometry_batch(
    const GeometrySet& set,
    const GeometryKernel& kernel,
    const GeometryTelemetryStore& telemetry,
    const GeometryRouteRequest& request) {
    GeometryRouteDecision out;
    GeometrySelectionRequest base{};
    base.constraints = request.constraints;
    base.objective = GeometryObjective::MinQueryLatency;
    base.workload_id = request.workload_id;
    base.device = request.device;
    base.work_units = request.work_units;
    const auto reps = set.eligible(kernel, request.constraints.required, request.constraints.max_geometric_error);
    struct Item { GeometryRepresentation rep{}; std::size_t storage{}; GeometryTelemetrySummary summary{}; };
    std::vector<Item> eligible;
    for (const auto& rep : reps) {
        const auto storage = kernel.storage_bytes(rep.handle);
        if (storage <= request.constraints.max_storage_bytes) eligible.push_back({rep, storage, telemetry.summary(rep.handle, rep.source_revision, request.workload_id, request.device, request.work_units)});
    }
    if (eligible.empty()) { out.reason = "no representation satisfies hard constraints"; return out; }
    if (request.allow_exploration && request.refresh_age < request.min_observed_batches) {
        out.reason = "refresh_age must be at least min_observed_batches to avoid freshness livelock";
        return out;
    }

    auto deterministic_less = [](const Item& a, const Item& b) { return pack_handle(a.rep.handle) < pack_handle(b.rep.handle); };
    std::stable_sort(eligible.begin(), eligible.end(), deterministic_less);

    auto under = std::min_element(eligible.begin(), eligible.end(), [](const Item& a, const Item& b) {
        if (a.summary.samples != b.summary.samples) return a.summary.samples < b.summary.samples;
        return pack_handle(a.rep.handle) < pack_handle(b.rep.handle);
    });
    if (under != eligible.end() && under->summary.samples < request.min_observed_batches) {
        if (!request.allow_exploration) { out.reason = "insufficient telemetry and exploration disabled"; return out; }
        out.ok = true; out.handle = under->rep.handle; out.exploration = true; out.reason = "bootstrap"; return out;
    }

    auto stale = std::max_element(eligible.begin(), eligible.end(), [](const Item& a, const Item& b) {
        if (a.summary.age != b.summary.age) return a.summary.age < b.summary.age;
        return pack_handle(a.rep.handle) > pack_handle(b.rep.handle);
    });
    if (stale != eligible.end() && stale->summary.age > request.refresh_age) {
        if (!request.allow_exploration) { out.reason = "telemetry stale and exploration disabled"; return out; }
        out.ok = true; out.handle = stale->rep.handle; out.exploration = true; out.stale_refresh = true;
        out.reset_telemetry_before_observe = true; out.reason = "refresh_stale"; return out;
    }

    const auto best = std::min_element(eligible.begin(), eligible.end(), [](const Item& a, const Item& b) {
        if (a.summary.median_us_per_unit != b.summary.median_us_per_unit) return a.summary.median_us_per_unit < b.summary.median_us_per_unit;
        return pack_handle(a.rep.handle) < pack_handle(b.rep.handle);
    });
    out.ok = true; out.handle = best->rep.handle; out.reason = "exploit";
    return out;
}

} // namespace aion
