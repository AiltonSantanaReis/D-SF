#include "aion/kernel/clustered_triangle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aion {
namespace {
constexpr float kEpsilon = 1.0e-7F;

[[nodiscard]] bool finite(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
[[nodiscard]] Vec3 add(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
[[nodiscard]] Vec3 sub(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
[[nodiscard]] Vec3 mul(Vec3 a, float s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
[[nodiscard]] float dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] float length(Vec3 a) noexcept { return std::sqrt(dot(a, a)); }
[[nodiscard]] Vec3 normalize(Vec3 a) noexcept {
    const float len = length(a);
    return len > kEpsilon ? mul(a, 1.0F / len) : Vec3{};
}
[[nodiscard]] Aabb empty_bounds() noexcept {
    const float inf = std::numeric_limits<float>::infinity();
    return {{inf, inf, inf}, {-inf, -inf, -inf}};
}
void expand(Aabb& b, Vec3 p) noexcept {
    b.min.x = std::min(b.min.x, p.x); b.min.y = std::min(b.min.y, p.y); b.min.z = std::min(b.min.z, p.z);
    b.max.x = std::max(b.max.x, p.x); b.max.y = std::max(b.max.y, p.y); b.max.z = std::max(b.max.z, p.z);
}
void expand(Aabb& b, const Aabb& x) noexcept { expand(b, x.min); expand(b, x.max); }
[[nodiscard]] Vec3 center(const Aabb& b) noexcept { return mul(add(b.min, b.max), 0.5F); }

[[nodiscard]] bool ray_aabb(const Ray& ray, const Aabb& box, float max_t, float& enter) noexcept {
    float tmin = ray.t_min;
    float tmax = std::min(ray.t_max, max_t);
    const float origins[3]{ray.origin.x, ray.origin.y, ray.origin.z};
    const float dirs[3]{ray.direction.x, ray.direction.y, ray.direction.z};
    const float mins[3]{box.min.x, box.min.y, box.min.z};
    const float maxs[3]{box.max.x, box.max.y, box.max.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(dirs[axis]) < kEpsilon) {
            if (origins[axis] < mins[axis] || origins[axis] > maxs[axis]) return false;
            continue;
        }
        const float inv = 1.0F / dirs[axis];
        float a = (mins[axis] - origins[axis]) * inv;
        float b = (maxs[axis] - origins[axis]) * inv;
        if (a > b) std::swap(a, b);
        tmin = std::max(tmin, a);
        tmax = std::min(tmax, b);
        if (tmax < tmin) return false;
    }
    enter = tmin;
    return true;
}

[[nodiscard]] bool ray_triangle(const Ray& ray, Vec3 a, Vec3 b, Vec3 c, float max_t, float& t, Vec3& normal) noexcept {
    const Vec3 e1 = sub(b, a);
    const Vec3 e2 = sub(c, a);
    const Vec3 p = cross(ray.direction, e2);
    const float det = dot(e1, p);
    if (std::fabs(det) < kEpsilon) return false;
    const float inv_det = 1.0F / det;
    const Vec3 s = sub(ray.origin, a);
    const float u = dot(s, p) * inv_det;
    if (u < -kEpsilon || u > 1.0F + kEpsilon) return false;
    const Vec3 q = cross(s, e1);
    const float v = dot(ray.direction, q) * inv_det;
    if (v < -kEpsilon || u + v > 1.0F + kEpsilon) return false;
    const float candidate = dot(e2, q) * inv_det;
    if (candidate < ray.t_min || candidate > std::min(ray.t_max, max_t)) return false;
    t = candidate;
    normal = normalize(cross(e1, e2));
    return true;
}

[[nodiscard]] std::uint32_t expand_bits(std::uint32_t x) noexcept {
    x &= 0x000003ffU;
    x = (x | (x << 16U)) & 0x030000FFU;
    x = (x | (x << 8U)) & 0x0300F00FU;
    x = (x | (x << 4U)) & 0x030C30C3U;
    x = (x | (x << 2U)) & 0x09249249U;
    return x;
}
[[nodiscard]] std::uint32_t morton3(float x, float y, float z) noexcept {
    const auto q = [](float v) {
        const float clamped = std::clamp(v, 0.0F, 0.999999F);
        return static_cast<std::uint32_t>(clamped * 1024.0F);
    };
    return expand_bits(q(x)) | (expand_bits(q(y)) << 1U) | (expand_bits(q(z)) << 2U);
}

struct QuantizedAabb {
    std::array<std::uint16_t, 3> min{};
    std::array<std::uint16_t, 3> max{};
};

struct QuantFrame {
    Vec3 origin{};
    Vec3 step{1.0F, 1.0F, 1.0F};
};

[[nodiscard]] QuantFrame make_frame(const Aabb& bounds) noexcept {
    const Vec3 extent = sub(bounds.max, bounds.min);
    QuantFrame f;
    f.origin = bounds.min;
    f.step = {
        extent.x > 0.0F ? extent.x / 65535.0F : 1.0F,
        extent.y > 0.0F ? extent.y / 65535.0F : 1.0F,
        extent.z > 0.0F ? extent.z / 65535.0F : 1.0F};
    return f;
}
[[nodiscard]] std::uint16_t quant_component(float v, float origin, float step) noexcept {
    const double q = std::round((static_cast<double>(v) - static_cast<double>(origin)) / static_cast<double>(step));
    const double c = std::clamp(q, 0.0, 65535.0);
    return static_cast<std::uint16_t>(c);
}
[[nodiscard]] Vec3 decode_vertex(const std::array<std::uint16_t, 3>& q, const QuantFrame& f) noexcept {
    return {f.origin.x + f.step.x * static_cast<float>(q[0]),
            f.origin.y + f.step.y * static_cast<float>(q[1]),
            f.origin.z + f.step.z * static_cast<float>(q[2])};
}
[[nodiscard]] QuantizedAabb quantize_bounds_conservative(const Aabb& b, const QuantFrame& f) noexcept {
    QuantizedAabb q{};
    const float mins[3]{b.min.x, b.min.y, b.min.z};
    const float maxs[3]{b.max.x, b.max.y, b.max.z};
    const float origins[3]{f.origin.x, f.origin.y, f.origin.z};
    const float steps[3]{f.step.x, f.step.y, f.step.z};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double qmin = std::floor((static_cast<double>(mins[axis]) - origins[axis]) / steps[axis]) - 1.0;
        const double qmax = std::ceil((static_cast<double>(maxs[axis]) - origins[axis]) / steps[axis]) + 1.0;
        q.min[axis] = static_cast<std::uint16_t>(std::clamp(qmin, 0.0, 65535.0));
        q.max[axis] = static_cast<std::uint16_t>(std::clamp(qmax, 0.0, 65535.0));
    }
    return q;
}
[[nodiscard]] Aabb decode_bounds(const QuantizedAabb& q, const QuantFrame& f) noexcept {
    return {{f.origin.x + f.step.x * static_cast<float>(q.min[0]),
             f.origin.y + f.step.y * static_cast<float>(q.min[1]),
             f.origin.z + f.step.z * static_cast<float>(q.min[2])},
            {f.origin.x + f.step.x * static_cast<float>(q.max[0]),
             f.origin.y + f.step.y * static_cast<float>(q.max[1]),
             f.origin.z + f.step.z * static_cast<float>(q.max[2])}};
}

} // namespace

struct ClusteredTriangleProvider::State {
    struct Cluster {
        std::uint32_t vertex_offset{};
        std::uint32_t index_offset{};
        std::uint16_t vertex_count{};
        std::uint16_t triangle_count{};
        Aabb bounds{};
    };
    struct BvhChild {
        QuantizedAabb bounds{};
        std::uint32_t index{}; // node index if count==0, otherwise cluster-order begin
        std::uint32_t count{}; // 0 internal, >0 leaf cluster count
    };
    struct BvhNode {
        std::array<BvhChild, 8> children{};
        std::uint8_t child_count{};
    };
    struct Resource {
        std::uint16_t generation{1};
        ClusteredTriangleEncoding encoding{ClusteredTriangleEncoding::Float32};
        Aabb bounds{};
        QuantFrame frame{};
        float max_error{};
        std::vector<Cluster> clusters;
        std::vector<Vec3> float_vertices;
        std::vector<std::array<std::uint16_t, 3>> quant_vertices;
        std::vector<std::uint8_t> local_indices;
        std::vector<std::uint32_t> cluster_order;
        std::vector<BvhNode> bvh;
        ClusteredTriangleStats stats{};
    };
    std::vector<Resource> resources;
};

namespace {
using Resource = ClusteredTriangleProvider::State::Resource;
using Cluster = ClusteredTriangleProvider::State::Cluster;
using BvhNode = ClusteredTriangleProvider::State::BvhNode;
using BvhChild = ClusteredTriangleProvider::State::BvhChild;

[[nodiscard]] Vec3 resource_vertex(const Resource& r, const Cluster& c, std::uint8_t local) noexcept {
    const std::size_t idx = static_cast<std::size_t>(c.vertex_offset) + local;
    return r.encoding == ClusteredTriangleEncoding::Float32
        ? r.float_vertices[idx]
        : decode_vertex(r.quant_vertices[idx], r.frame);
}

[[nodiscard]] Aabb cluster_bounds_decoded(const Resource& r, std::size_t cluster_index) noexcept {
    const auto& c = r.clusters[cluster_index];
    Aabb b = empty_bounds();
    for (std::uint16_t i = 0; i < c.vertex_count; ++i) expand(b, resource_vertex(r, c, static_cast<std::uint8_t>(i)));
    return b;
}

[[nodiscard]] Aabb range_bounds(const Resource& r, std::size_t begin, std::size_t count) noexcept {
    Aabb b = empty_bounds();
    for (std::size_t i = 0; i < count; ++i) expand(b, cluster_bounds_decoded(r, r.cluster_order[begin + i]));
    return b;
}

std::uint32_t build_bvh_recursive(Resource& r, std::size_t begin, std::size_t count) {
    const std::uint32_t node_index = static_cast<std::uint32_t>(r.bvh.size());
    r.bvh.emplace_back();
    if (count <= 8) {
        r.bvh[node_index].child_count = static_cast<std::uint8_t>(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto ci = r.cluster_order[begin + i];
            const Aabb b = cluster_bounds_decoded(r, ci);
            r.bvh[node_index].children[i] = {
                quantize_bounds_conservative(b, r.frame),
                static_cast<std::uint32_t>(begin + i),
                1U};
        }
        return node_index;
    }
    const std::size_t groups = std::min<std::size_t>(8, count);
    r.bvh[node_index].child_count = static_cast<std::uint8_t>(groups);
    const std::size_t base = count / groups;
    const std::size_t rem = count % groups;
    std::size_t cursor = begin;
    for (std::size_t g = 0; g < groups; ++g) {
        const std::size_t n = base + (g < rem ? 1U : 0U);
        const Aabb b = range_bounds(r, cursor, n);
        const std::uint32_t child_node = build_bvh_recursive(r, cursor, n);
        r.bvh[node_index].children[g] = {quantize_bounds_conservative(b, r.frame), child_node, 0U};
        cursor += n;
    }
    return node_index;
}

} // namespace

ClusteredTriangleProvider::ClusteredTriangleProvider(GeometryProviderId id)
    : id_(id), state_(std::make_shared<State>()) {}

bool ClusteredTriangleProvider::register_with(GeometryKernel& kernel, std::string& error) {
    GeometryProviderDescriptor desc{.id = id_, .name = "clustered-triangle", .capabilities = GeometryCapabilityMask::of({GeometryCapability::Bounds, GeometryCapability::RaySurface})};
    GeometryProviderOps ops{.context = state_, .valid = &valid_cb, .bounds = &bounds_cb, .raycast = &raycast_cb, .signed_distance = &distance_cb, .storage_bytes = &storage_cb};
    return kernel.register_provider(std::move(desc), ops, error);
}

ClusteredTriangleBuildResult ClusteredTriangleProvider::add_mesh(
    std::span<const Vec3> vertices,
    std::span<const std::uint32_t> indices,
    const ClusteredTriangleOptions& options) {
    ClusteredTriangleBuildResult out;
    if (vertices.empty() || indices.empty() || indices.size() % 3U != 0U) { out.error = "clustered mesh requires non-empty triangle-indexed geometry"; return out; }
    if (options.max_vertices_per_cluster < 3U || options.max_vertices_per_cluster > 255U || options.max_triangles_per_cluster == 0U) {
        out.error = "cluster limits must fit uint8 local indices and contain triangles"; return out;
    }
    for (const auto& v : vertices) if (!finite(v)) { out.error = "clustered mesh contains non-finite vertex"; return out; }
    for (const auto index : indices) if (index >= vertices.size()) { out.error = "clustered mesh index out of range"; return out; }

    Resource r;
    r.encoding = options.encoding;
    r.bounds = empty_bounds();
    for (const auto& v : vertices) expand(r.bounds, v);
    r.frame = make_frame(r.bounds);
    r.max_error = options.encoding == ClusteredTriangleEncoding::QuantizedU16
        ? 0.5F * std::sqrt(r.frame.step.x * r.frame.step.x + r.frame.step.y * r.frame.step.y + r.frame.step.z * r.frame.step.z)
        : 0.0F;

    const std::size_t tri_count = indices.size() / 3U;
    std::vector<std::vector<std::uint32_t>> vertex_to_tri(vertices.size());
    for (std::size_t t = 0; t < tri_count; ++t) {
        for (std::size_t k = 0; k < 3; ++k) vertex_to_tri[indices[t * 3U + k]].push_back(static_cast<std::uint32_t>(t));
    }
    std::vector<std::uint8_t> assigned(tri_count, 0U);
    std::vector<std::uint32_t> queued_epoch(tri_count, 0U);
    std::uint32_t current_epoch = 0U;
    std::size_t assigned_count = 0;

    while (assigned_count < tri_count) {
        ++current_epoch;
        if (current_epoch == 0U) {
            std::fill(queued_epoch.begin(), queued_epoch.end(), 0U);
            current_epoch = 1U;
        }
        std::uint32_t seed = 0;
        while (seed < tri_count && assigned[seed] != 0U) ++seed;
        if (seed >= tri_count) break;

        Cluster cluster;
        cluster.vertex_offset = static_cast<std::uint32_t>(r.encoding == ClusteredTriangleEncoding::Float32 ? r.float_vertices.size() : r.quant_vertices.size());
        cluster.index_offset = static_cast<std::uint32_t>(r.local_indices.size());
        cluster.bounds = empty_bounds();
        std::unordered_map<std::uint32_t, std::uint8_t> remap;
        remap.reserve(options.max_vertices_per_cluster);
        std::queue<std::uint32_t> frontier;
        frontier.push(seed);
        queued_epoch[seed] = current_epoch;

        auto can_add = [&](std::uint32_t tri) {
            if (assigned[tri] != 0U || cluster.triangle_count >= options.max_triangles_per_cluster) return false;
            std::size_t new_vertices = 0;
            for (std::size_t k = 0; k < 3; ++k) if (!remap.contains(indices[static_cast<std::size_t>(tri) * 3U + k])) ++new_vertices;
            return static_cast<std::size_t>(cluster.vertex_count) + new_vertices <= options.max_vertices_per_cluster;
        };
        auto add_tri = [&](std::uint32_t tri) {
            std::array<std::uint32_t, 3> globals{};
            for (std::size_t k = 0; k < 3; ++k) globals[k] = indices[static_cast<std::size_t>(tri) * 3U + k];
            for (const auto gv : globals) {
                auto it = remap.find(gv);
                if (it == remap.end()) {
                    const auto local = static_cast<std::uint8_t>(cluster.vertex_count);
                    remap.emplace(gv, local);
                    ++cluster.vertex_count;
                    const Vec3 v = vertices[gv];
                    expand(cluster.bounds, v);
                    if (r.encoding == ClusteredTriangleEncoding::Float32) r.float_vertices.push_back(v);
                    else r.quant_vertices.push_back({quant_component(v.x, r.frame.origin.x, r.frame.step.x), quant_component(v.y, r.frame.origin.y, r.frame.step.y), quant_component(v.z, r.frame.origin.z, r.frame.step.z)});
                    it = remap.find(gv);
                }
                r.local_indices.push_back(it->second);
            }
            ++cluster.triangle_count;
            assigned[tri] = 1U;
            ++assigned_count;
            for (const auto gv : globals) {
                for (const auto neighbor : vertex_to_tri[gv]) {
                    if (assigned[neighbor] == 0U && queued_epoch[neighbor] != current_epoch) {
                        queued_epoch[neighbor] = current_epoch;
                        frontier.push(neighbor);
                    }
                }
            }
        };

        while (!frontier.empty() && cluster.triangle_count < options.max_triangles_per_cluster) {
            const auto tri = frontier.front(); frontier.pop();
            if (can_add(tri)) add_tri(tri);
        }
        if (cluster.triangle_count == 0U) add_tri(seed);
        r.clusters.push_back(cluster);
    }

    r.cluster_order.resize(r.clusters.size());
    std::iota(r.cluster_order.begin(), r.cluster_order.end(), 0U);
    const Vec3 ext = sub(r.bounds.max, r.bounds.min);
    auto norm_axis = [](float v, float lo, float extent) { return extent > 0.0F ? (v - lo) / extent : 0.5F; };
    std::stable_sort(r.cluster_order.begin(), r.cluster_order.end(), [&](std::uint32_t a, std::uint32_t b) {
        const Vec3 ca = center(r.clusters[a].bounds);
        const Vec3 cb = center(r.clusters[b].bounds);
        return morton3(norm_axis(ca.x, r.bounds.min.x, ext.x), norm_axis(ca.y, r.bounds.min.y, ext.y), norm_axis(ca.z, r.bounds.min.z, ext.z))
             < morton3(norm_axis(cb.x, r.bounds.min.x, ext.x), norm_axis(cb.y, r.bounds.min.y, ext.y), norm_axis(cb.z, r.bounds.min.z, ext.z));
    });
    if (!r.clusters.empty()) (void)build_bvh_recursive(r, 0, r.clusters.size());

    r.stats.source_vertices = vertices.size();
    r.stats.source_triangles = tri_count;
    r.stats.clusters = r.clusters.size();
    r.stats.cluster_vertices = r.encoding == ClusteredTriangleEncoding::Float32 ? r.float_vertices.size() : r.quant_vertices.size();
    r.stats.bvh_nodes = r.bvh.size();
    r.stats.payload_bytes = r.float_vertices.capacity() * sizeof(Vec3) + r.quant_vertices.capacity() * sizeof(std::array<std::uint16_t, 3>) + r.local_indices.capacity();
    r.stats.topology_bytes = r.clusters.capacity() * sizeof(Cluster) + r.cluster_order.capacity() * sizeof(std::uint32_t) + r.bvh.capacity() * sizeof(BvhNode);
    r.stats.storage_bytes = r.stats.payload_bytes + r.stats.topology_bytes;
    r.stats.source_raw_bytes = vertices.size() * sizeof(Vec3) + indices.size() * sizeof(std::uint32_t);
    r.stats.max_geometric_error = r.max_error;

    const auto id = static_cast<GeometryResourceId>(state_->resources.size());
    state_->resources.push_back(std::move(r));
    out.handle = {id_, state_->resources.back().generation, id};
    out.max_geometric_error = state_->resources.back().max_error;
    out.stats = state_->resources.back().stats;
    return out;
}

ClusteredTriangleStats ClusteredTriangleProvider::stats(GeometryHandle handle) const noexcept {
    if (handle.provider != id_ || !valid_cb(state_.get(), handle.resource, handle.generation)) return {};
    return state_->resources[handle.resource].stats;
}

bool ClusteredTriangleProvider::export_archive(GeometryHandle handle, RepresentationArchive& archive, std::string& error) const {
    archive = {};
    if (handle.provider != id_ || !valid_cb(state_.get(), handle.resource, handle.generation)) {
        error = "invalid clustered triangle handle for archive export";
        return false;
    }
    const auto& r = state_->resources[handle.resource];
    CanonicalByteWriter w(archive.topology);
    w.u32(0x43545231U); // CTR1
    w.u16(r.generation);
    w.u8(static_cast<std::uint8_t>(r.encoding));
    w.f32(r.bounds.min.x); w.f32(r.bounds.min.y); w.f32(r.bounds.min.z);
    w.f32(r.bounds.max.x); w.f32(r.bounds.max.y); w.f32(r.bounds.max.z);
    w.f32(r.frame.origin.x); w.f32(r.frame.origin.y); w.f32(r.frame.origin.z);
    w.f32(r.frame.step.x); w.f32(r.frame.step.y); w.f32(r.frame.step.z);
    w.f32(r.max_error);
    w.u32(static_cast<std::uint32_t>(r.clusters.size()));
    w.u32(static_cast<std::uint32_t>(r.cluster_order.size()));
    w.u32(static_cast<std::uint32_t>(r.bvh.size()));
    for (const auto& c : r.clusters) {
        w.u32(c.vertex_offset); w.u32(c.index_offset); w.u16(c.vertex_count); w.u16(c.triangle_count);
        w.f32(c.bounds.min.x); w.f32(c.bounds.min.y); w.f32(c.bounds.min.z);
        w.f32(c.bounds.max.x); w.f32(c.bounds.max.y); w.f32(c.bounds.max.z);
    }
    for (const auto value : r.cluster_order) w.u32(value);
    for (const auto& node : r.bvh) {
        w.u8(node.child_count);
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            const auto& child = node.children[i];
            for (const auto value : child.bounds.min) w.u16(value);
            for (const auto value : child.bounds.max) w.u16(value);
            w.u32(child.index); w.u32(child.count);
        }
    }
    archive.payloads.resize(2);
    CanonicalByteWriter positions(archive.payloads[0]);
    if (r.encoding == ClusteredTriangleEncoding::Float32) {
        for (const auto& v : r.float_vertices) { positions.f32(v.x); positions.f32(v.y); positions.f32(v.z); }
    } else {
        for (const auto& v : r.quant_vertices) { positions.u16(v[0]); positions.u16(v[1]); positions.u16(v[2]); }
    }
    auto& indices = archive.payloads[1];
    indices.reserve(r.local_indices.size());
    for (const auto value : r.local_indices) indices.push_back(static_cast<std::byte>(value));
    error.clear();
    return true;
}

bool ClusteredTriangleProvider::valid_cb(const void* ctx, GeometryResourceId id, std::uint16_t generation) noexcept {
    const auto& s = *static_cast<const State*>(ctx);
    return id < s.resources.size() && s.resources[id].generation == generation;
}
GeometryBoundsResult ClusteredTriangleProvider::bounds_cb(const void* ctx, GeometryResourceId id, std::uint16_t generation) noexcept {
    if (!valid_cb(ctx, id, generation)) return {GeometryQueryStatus::InvalidHandle, {}};
    return {GeometryQueryStatus::Ok, static_cast<const State*>(ctx)->resources[id].bounds};
}
GeometryDistanceSample ClusteredTriangleProvider::distance_cb(const void*, GeometryResourceId, std::uint16_t, Vec3) noexcept {
    return {GeometryQueryStatus::UnsupportedCapability};
}
std::size_t ClusteredTriangleProvider::storage_cb(const void* ctx, GeometryResourceId id, std::uint16_t generation) noexcept {
    if (!valid_cb(ctx, id, generation)) return 0;
    return static_cast<const State*>(ctx)->resources[id].stats.storage_bytes;
}
GeometryRayHit ClusteredTriangleProvider::raycast_cb(const void* ctx, GeometryResourceId id, std::uint16_t generation, const Ray& ray) noexcept {
    if (!valid_cb(ctx, id, generation)) return {GeometryQueryStatus::InvalidHandle};
    if (!finite(ray.origin) || !finite(ray.direction) || !std::isfinite(ray.t_min) || std::isnan(ray.t_max) || ray.t_max < ray.t_min || length(ray.direction) <= kEpsilon) return {GeometryQueryStatus::NumericalFailure};
    const auto& r = static_cast<const State*>(ctx)->resources[id];
    if (r.bvh.empty()) return {GeometryQueryStatus::Miss};
    float best_t = ray.t_max;
    Vec3 best_n{};
    bool hit = false;
    struct StackItem { std::uint32_t node{}; float t{}; };
    std::vector<StackItem> stack;
    stack.push_back({0U, ray.t_min});
    while (!stack.empty()) {
        const auto item = stack.back(); stack.pop_back();
        if (item.t > best_t) continue;
        const auto& node = r.bvh[item.node];
        struct Candidate { BvhChild child{}; float t{}; };
        std::array<Candidate, 8> candidates{};
        std::size_t candidate_count = 0;
        for (std::size_t i = 0; i < node.child_count; ++i) {
            float enter{};
            const Aabb b = decode_bounds(node.children[i].bounds, r.frame);
            if (ray_aabb(ray, b, best_t, enter)) candidates[candidate_count++] = {node.children[i], enter};
        }
        for (std::size_t i = 1; i < candidate_count; ++i) {
            Candidate key = candidates[i];
            std::size_t j = i;
            while (j > 0U && key.t < candidates[j - 1U].t) {
                candidates[j] = candidates[j - 1U];
                --j;
            }
            candidates[j] = key;
        }
        for (std::size_t rev = candidate_count; rev > 0; --rev) {
            const auto& cand = candidates[rev - 1U];
            if (cand.child.count == 0U) {
                stack.push_back({cand.child.index, cand.t});
                continue;
            }
            for (std::uint32_t j = 0; j < cand.child.count; ++j) {
                const auto ci = r.cluster_order[static_cast<std::size_t>(cand.child.index) + j];
                const auto& c = r.clusters[ci];
                float cluster_enter{};
                if (!ray_aabb(ray, cluster_bounds_decoded(r, ci), best_t, cluster_enter)) continue;
                for (std::uint16_t tri = 0; tri < c.triangle_count; ++tri) {
                    const std::size_t base = static_cast<std::size_t>(c.index_offset) + static_cast<std::size_t>(tri) * 3U;
                    const Vec3 a = resource_vertex(r, c, r.local_indices[base]);
                    const Vec3 b = resource_vertex(r, c, r.local_indices[base + 1U]);
                    const Vec3 d = resource_vertex(r, c, r.local_indices[base + 2U]);
                    float t{}; Vec3 n{};
                    if (ray_triangle(ray, a, b, d, best_t, t, n) && t < best_t) { best_t = t; best_n = n; hit = true; }
                }
            }
        }
    }
    if (!hit) return {GeometryQueryStatus::Miss};
    return {GeometryQueryStatus::Ok, best_t, add(ray.origin, mul(ray.direction, best_t)), best_n};
}

} // namespace aion
