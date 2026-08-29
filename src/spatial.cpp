#include "aion/kernel/spatial.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_set>
#include <utility>

namespace aion {
namespace {

[[nodiscard]] bool finite(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] Aabb empty_aabb() noexcept {
    const float inf = std::numeric_limits<float>::infinity();
    return {{inf, inf, inf}, {-inf, -inf, -inf}};
}

[[nodiscard]] bool is_empty(const Aabb& a) noexcept {
    return a.min.x > a.max.x || a.min.y > a.max.y || a.min.z > a.max.z;
}

[[nodiscard]] Aabb merge(Aabb a, const Aabb& b) noexcept {
    if (is_empty(a)) return b;
    if (is_empty(b)) return a;
    a.min.x = std::min(a.min.x, b.min.x);
    a.min.y = std::min(a.min.y, b.min.y);
    a.min.z = std::min(a.min.z, b.min.z);
    a.max.x = std::max(a.max.x, b.max.x);
    a.max.y = std::max(a.max.y, b.max.y);
    a.max.z = std::max(a.max.z, b.max.z);
    return a;
}


[[nodiscard]] float axis_value(Vec3 v, int axis) noexcept {
    if (axis == 0) return v.x;
    if (axis == 1) return v.y;
    return v.z;
}

[[nodiscard]] std::uint32_t expand_bits(std::uint32_t v) noexcept {
    v &= 0x000003ffU;
    v = (v | (v << 16U)) & 0x030000FFU;
    v = (v | (v << 8U)) & 0x0300F00FU;
    v = (v | (v << 4U)) & 0x030C30C3U;
    v = (v | (v << 2U)) & 0x09249249U;
    return v;
}

[[nodiscard]] std::uint32_t morton3d(float x, float y, float z) noexcept {
    const auto q = [](float v) {
        const float c = std::clamp(v, 0.0F, 0.999999F);
        return static_cast<std::uint32_t>(c * 1024.0F);
    };
    return (expand_bits(q(x)) << 2U) | (expand_bits(q(y)) << 1U) | expand_bits(q(z));
}

[[nodiscard]] std::vector<EntityId> sorted_unique(std::vector<EntityId> ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace

bool aabb_intersects(const Aabb& a, const Aabb& b) noexcept {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

float aabb_surface_area(const Aabb& a) noexcept {
    if (is_empty(a)) return 0.0F;
    const float x = std::max(0.0F, a.max.x - a.min.x);
    const float y = std::max(0.0F, a.max.y - a.min.y);
    const float z = std::max(0.0F, a.max.z - a.min.z);
    return 2.0F * (x*y + y*z + z*x);
}

bool ray_intersects_aabb(const Ray& ray, const Aabb& box, float& t) noexcept {
    float lo = ray.t_min;
    float hi = ray.t_max;
    const std::array<float,3> o{ray.origin.x, ray.origin.y, ray.origin.z};
    const std::array<float,3> d{ray.direction.x, ray.direction.y, ray.direction.z};
    const std::array<float,3> mn{box.min.x, box.min.y, box.min.z};
    const std::array<float,3> mx{box.max.x, box.max.y, box.max.z};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::fabs(d[axis]) < 1.0e-20F) {
            if (o[axis] < mn[axis] || o[axis] > mx[axis]) return false;
            continue;
        }
        const float inv = 1.0F / d[axis];
        float t0 = (mn[axis] - o[axis]) * inv;
        float t1 = (mx[axis] - o[axis]) * inv;
        if (t0 > t1) std::swap(t0, t1);
        lo = std::max(lo, t0);
        hi = std::min(hi, t1);
        if (lo > hi) return false;
    }
    t = lo;
    return true;
}

bool SpatialSnapshot::build(std::span<const SpatialRecord> records, std::string& error) {
    error.clear();
    std::vector<SpatialRecord> sorted(records.begin(), records.end());
    std::sort(sorted.begin(), sorted.end(), [](const SpatialRecord& a, const SpatialRecord& b) {
        return a.entity < b.entity;
    });

    EntityId max_entity = 0;
    EntityId previous = 0;
    for (const auto& r : sorted) {
        if (r.entity == 0 || (previous != 0 && r.entity == previous)) {
            error = "SpatialSnapshot requires unique non-zero EntityId values";
            return false;
        }
        if (!finite(r.center) || !finite(r.half_extent) ||
            r.half_extent.x < 0.0F || r.half_extent.y < 0.0F || r.half_extent.z < 0.0F) {
            error = "SpatialSnapshot rejects non-finite or negative bounds";
            return false;
        }
        previous = r.entity;
        max_entity = std::max(max_entity, r.entity);
    }
    record_count_ = sorted.size();
    dense_identity_ = true;
    for (std::size_t i=0; i<sorted.size(); ++i) {
        if (sorted[i].entity != static_cast<EntityId>(i+1)) { dense_identity_ = false; break; }
    }

    entity_ids_.clear();
    center_x_.clear(); center_y_.clear(); center_z_.clear();
    half_x_.clear(); half_y_.clear(); half_z_.clear();
    if (!dense_identity_) entity_ids_.reserve(sorted.size());
    center_x_.reserve(sorted.size()); center_y_.reserve(sorted.size()); center_z_.reserve(sorted.size());
    half_x_.reserve(sorted.size()); half_y_.reserve(sorted.size()); half_z_.reserve(sorted.size());

    Aabb center_bounds = empty_aabb();
    for (std::size_t i=0; i<sorted.size(); ++i) {
        const auto& r = sorted[i];
        if (!dense_identity_) entity_ids_.push_back(r.entity);
        center_x_.push_back(r.center.x); center_y_.push_back(r.center.y); center_z_.push_back(r.center.z);
        half_x_.push_back(r.half_extent.x); half_y_.push_back(r.half_extent.y); half_z_.push_back(r.half_extent.z);
        center_bounds = merge(center_bounds, {r.center, r.center});
    }
    if (record_count_ > 1 && !is_empty(center_bounds)) {
        const float dx=center_bounds.max.x-center_bounds.min.x;
        const float dy=center_bounds.max.y-center_bounds.min.y;
        const float dz=center_bounds.max.z-center_bounds.min.z;
        const float diagonal=std::sqrt(dx*dx+dy*dy+dz*dz);
        coherence_scale_=std::max(1.0e-6F, diagonal/std::cbrt(static_cast<float>(record_count_)));
    } else {
        coherence_scale_=1.0F;
    }
    ++structure_revision_;
    ++version_;
    return true;
}

std::uint32_t SpatialSnapshot::slot_of(EntityId entity_id) const noexcept {
    if (dense_identity_) {
        if (entity_id == 0 || entity_id > record_count_) return kInvalidSlot;
        return static_cast<std::uint32_t>(entity_id - 1U);
    }
    const auto it = std::lower_bound(entity_ids_.begin(), entity_ids_.end(), entity_id);
    if (it == entity_ids_.end() || *it != entity_id) return kInvalidSlot;
    const auto distance = static_cast<std::size_t>(std::distance(entity_ids_.begin(), it));
    if (distance > std::numeric_limits<std::uint32_t>::max()) return kInvalidSlot;
    return static_cast<std::uint32_t>(distance);
}

Vec3 SpatialSnapshot::center(std::uint32_t slot) const noexcept {
    return {center_x_[slot], center_y_[slot], center_z_[slot]};
}

Vec3 SpatialSnapshot::half_extent(std::uint32_t slot) const noexcept {
    return {half_x_[slot], half_y_[slot], half_z_[slot]};
}

Aabb SpatialSnapshot::bounds(std::uint32_t slot) const noexcept {
    const Vec3 c = center(slot);
    const Vec3 h = half_extent(slot);
    return {{c.x-h.x, c.y-h.y, c.z-h.z}, {c.x+h.x, c.y+h.y, c.z+h.z}};
}

std::size_t SpatialSnapshot::storage_bytes() const noexcept {
    return entity_ids_.capacity()*sizeof(EntityId) +
           center_x_.capacity()*sizeof(float) + center_y_.capacity()*sizeof(float) + center_z_.capacity()*sizeof(float) +
           half_x_.capacity()*sizeof(float) + half_y_.capacity()*sizeof(float) + half_z_.capacity()*sizeof(float);
}

std::size_t SpatialSnapshot::object_payload_bytes() const noexcept {
    return size() * (sizeof(EntityId) + 6U*sizeof(float));
}

bool SpatialSnapshot::validate_position_write(EntityId entity_id, Vec3 value, std::string& error) const noexcept {
    if (!finite(value)) {
        error = "spatial position patch contains non-finite value";
        return false;
    }
    if (slot_of(entity_id) == kInvalidSlot) {
        error = "spatial position patch references entity absent from snapshot";
        return false;
    }
    return true;
}

void SpatialSnapshot::write_center(std::uint32_t slot, Vec3 value) noexcept {
    center_x_[slot] = value.x; center_y_[slot] = value.y; center_z_[slot] = value.z;
}

SpatialApplyResult SpatialSnapshot::apply_patch_transaction(const PatchTransaction& transaction) {
    SpatialApplyResult result;
    result.changes.from_version = version_;
    result.changes.to_version = version_;

    // Structural changes require a new snapshot build. Reject partial spatial publication.
    for (const auto& m : transaction.scalar_mutations) {
        if (m.kind == MutationKind::CreateEntity || m.kind == MutationKind::DestroyEntity) {
            result.ok = true;
            result.changes.requires_rebuild = true;
            return result;
        }
        if (m.kind == MutationKind::SetPosition && !validate_position_write(m.entity, m.vec, result.error)) return result;
    }
    for (const auto& p : transaction.vec3_patches) {
        if (p.component != PatchComponent::Position) continue;
        if (p.values.empty()) continue;
        if (p.first == 0 || p.first > std::numeric_limits<EntityId>::max() - p.values.size()) {
            result.error = "invalid spatial position range";
            return result;
        }
        for (std::size_t i=0; i<p.values.size(); ++i) {
            const EntityId id = p.first + static_cast<EntityId>(i);
            if (!validate_position_write(id, p.values[i], result.error)) return result;
        }
    }

    double displacement_sum=0.0;
    float displacement_max=0.0F;
    std::size_t displacement_count=0;
    const auto record_displacement = [&](Vec3 from, Vec3 to) {
        const float dx=to.x-from.x, dy=to.y-from.y, dz=to.z-from.z;
        const float d=std::sqrt(dx*dx+dy*dy+dz*dz);
        displacement_sum += d; displacement_max=std::max(displacement_max,d); ++displacement_count;
    };

    std::vector<std::uint32_t> dirty;
    dirty.reserve(transaction.scalar_mutations.size());
    for (const auto& m : transaction.scalar_mutations) {
        if (m.kind != MutationKind::SetPosition) continue;
        const auto slot = slot_of(m.entity);
        record_displacement(center(slot),m.vec);
        write_center(slot, m.vec);
        dirty.push_back(slot);
    }
    std::sort(dirty.begin(), dirty.end());
    dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
    result.changes.dirty_slots = std::move(dirty);

    for (const auto& p : transaction.vec3_patches) {
        if (p.component != PatchComponent::Position || p.values.empty()) continue;
        const auto first_slot = slot_of(p.first);
        bool contiguous = first_slot != kInvalidSlot;
        for (std::size_t i=0; i<p.values.size(); ++i) {
            const auto slot = slot_of(p.first + static_cast<EntityId>(i));
            record_displacement(center(slot),p.values[i]);
            write_center(slot, p.values[i]);
            if (contiguous && slot != first_slot + static_cast<std::uint32_t>(i)) contiguous = false;
        }
        if (contiguous && p.values.size() <= std::numeric_limits<std::uint32_t>::max()) {
            result.changes.dirty_ranges.push_back({first_slot, static_cast<std::uint32_t>(p.values.size())});
        } else {
            for (std::size_t i=0; i<p.values.size(); ++i)
                result.changes.dirty_slots.push_back(slot_of(p.first + static_cast<EntityId>(i)));
        }
    }
    if (!result.changes.dirty_ranges.empty() && result.changes.dirty_slots.size() > 1) {
        std::sort(result.changes.dirty_slots.begin(), result.changes.dirty_slots.end());
        result.changes.dirty_slots.erase(std::unique(result.changes.dirty_slots.begin(), result.changes.dirty_slots.end()), result.changes.dirty_slots.end());
    }
    result.ok = true;
    result.changes.changed = result.changes.dirty_value_count() != 0;
    if (displacement_count != 0) {
        result.changes.mean_displacement=static_cast<float>(displacement_sum/static_cast<double>(displacement_count));
        result.changes.max_displacement=displacement_max;
        result.changes.normalized_motion=result.changes.mean_displacement/std::max(1.0e-6F,coherence_scale_);
        const float changed_fraction=record_count_ ? static_cast<float>(result.changes.dirty_value_count())/static_cast<float>(record_count_) : 0.0F;
        result.changes.topology_debt_delta=changed_fraction*result.changes.normalized_motion;
    }
    if (result.changes.changed) ++version_;
    result.changes.to_version = version_;
    return result;
}

std::vector<EntityId> SpatialOracle::query_aabb(const SpatialSnapshot& snapshot, const Aabb& query) {
    std::vector<EntityId> result;
    for (std::uint32_t slot=0; slot<snapshot.size(); ++slot) {
        if (aabb_intersects(snapshot.bounds(slot), query)) result.push_back(snapshot.entity(slot));
    }
    return sorted_unique(std::move(result));
}

SpatialRayResult SpatialOracle::raycast(const SpatialSnapshot& snapshot, const Ray& ray) {
    SpatialRayResult result;
    result.ok=true;
    for (std::uint32_t slot=0; slot<snapshot.size(); ++slot) {
        float t = 0.0F;
        if (ray_intersects_aabb(ray, snapshot.bounds(slot), t) && t < result.t) {
            result.t = t;
            result.entity = snapshot.entity(slot);
        }
    }
    return result;
}

WideBvh8View::WideBvh8View(std::size_t leaf_size) : leaf_size_(std::max<std::size_t>(1, leaf_size)) {}

Aabb WideBvh8View::range_bounds(const SpatialSnapshot& snapshot, Range range) const noexcept {
    Aabb b = empty_aabb();
    for (std::size_t i=range.begin; i<range.end; ++i) b = merge(b, snapshot.bounds(working_slots_[i]));
    return b;
}

Aabb WideBvh8View::range_centroid_bounds(const SpatialSnapshot& snapshot, Range range) const noexcept {
    Aabb b = empty_aabb();
    for (std::size_t i=range.begin; i<range.end; ++i) {
        const Vec3 c = snapshot.center(working_slots_[i]);
        b = merge(b, {c,c});
    }
    return b;
}

bool WideBvh8View::split_sah(const SpatialSnapshot& snapshot, Range range, Range& left, Range& right) {
    constexpr int bins_count = 16;
    const std::size_t count = range.end - range.begin;
    if (count <= 1) return false;
    const Aabb centroid_bounds = range_centroid_bounds(snapshot, range);

    struct Best { float cost{std::numeric_limits<float>::infinity()}; int axis{-1}; int split{-1}; float cmin{}; float cmax{}; } best;
    for (int axis=0; axis<3; ++axis) {
        const float cmin = axis_value(centroid_bounds.min, axis);
        const float cmax = axis_value(centroid_bounds.max, axis);
        const float extent = cmax - cmin;
        if (extent <= 1.0e-9F) continue;
        struct Bin { Aabb b{empty_aabb()}; std::size_t n{}; };
        std::array<Bin,bins_count> bins{};
        for (std::size_t i=range.begin; i<range.end; ++i) {
            const auto slot = working_slots_[i];
            const float c = axis_value(snapshot.center(slot), axis);
            int bi = static_cast<int>(((c-cmin)/extent) * static_cast<float>(bins_count));
            bi = std::clamp(bi, 0, bins_count-1);
            bins[static_cast<std::size_t>(bi)].n++;
            bins[static_cast<std::size_t>(bi)].b = merge(bins[static_cast<std::size_t>(bi)].b, snapshot.bounds(slot));
        }
        std::array<Aabb,bins_count> prefix_b{}, suffix_b{};
        std::array<std::size_t,bins_count> prefix_n{}, suffix_n{};
        Aabb pb=empty_aabb(); std::size_t pn=0;
        for (int i=0;i<bins_count;++i) { pb=merge(pb,bins[static_cast<std::size_t>(i)].b); pn+=bins[static_cast<std::size_t>(i)].n; prefix_b[static_cast<std::size_t>(i)]=pb; prefix_n[static_cast<std::size_t>(i)]=pn; }
        Aabb sb=empty_aabb(); std::size_t sn=0;
        for (int i=bins_count-1;i>=0;--i) { sb=merge(sb,bins[static_cast<std::size_t>(i)].b); sn+=bins[static_cast<std::size_t>(i)].n; suffix_b[static_cast<std::size_t>(i)]=sb; suffix_n[static_cast<std::size_t>(i)]=sn; }
        for (int s=0;s<bins_count-1;++s) {
            const auto ln=prefix_n[static_cast<std::size_t>(s)];
            const auto rn=suffix_n[static_cast<std::size_t>(s+1)];
            if (ln==0 || rn==0) continue;
            const float cost=aabb_surface_area(prefix_b[static_cast<std::size_t>(s)])*static_cast<float>(ln)+
                             aabb_surface_area(suffix_b[static_cast<std::size_t>(s+1)])*static_cast<float>(rn);
            if (cost < best.cost) best={cost,axis,s,cmin,cmax};
        }
    }

    if (best.axis < 0) {
        const auto mid=range.begin+count/2;
        std::nth_element(working_slots_.begin()+static_cast<std::ptrdiff_t>(range.begin),
                         working_slots_.begin()+static_cast<std::ptrdiff_t>(mid),
                         working_slots_.begin()+static_cast<std::ptrdiff_t>(range.end),
                         [&](std::uint32_t a,std::uint32_t b){ return snapshot.center(a).x < snapshot.center(b).x; });
        left={range.begin,mid}; right={mid,range.end}; return true;
    }
    const float extent=best.cmax-best.cmin;
    const float threshold=best.cmin + extent*(static_cast<float>(best.split+1)/static_cast<float>(bins_count));
    auto first=working_slots_.begin()+static_cast<std::ptrdiff_t>(range.begin);
    auto last=working_slots_.begin()+static_cast<std::ptrdiff_t>(range.end);
    auto pivot=std::partition(first,last,[&](std::uint32_t slot){ return axis_value(snapshot.center(slot),best.axis) < threshold; });
    const std::size_t mid=static_cast<std::size_t>(std::distance(working_slots_.begin(),pivot));
    if (mid==range.begin || mid==range.end) {
        const auto fallback=range.begin+count/2;
        std::nth_element(first,working_slots_.begin()+static_cast<std::ptrdiff_t>(fallback),last,[&](std::uint32_t a,std::uint32_t b){return axis_value(snapshot.center(a),best.axis)<axis_value(snapshot.center(b),best.axis);});
        left={range.begin,fallback}; right={fallback,range.end}; return true;
    }
    left={range.begin,mid}; right={mid,range.end};
    return true;
}

std::uint32_t WideBvh8View::build_node(const SpatialSnapshot& snapshot, Range range,
                                       std::uint32_t parent, std::uint8_t parent_child,
                                       std::uint16_t depth) {
    const auto node_index=static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back({});
    nodes_[node_index].parent=parent;
    nodes_[node_index].parent_child=parent_child;
    nodes_[node_index].depth=depth;

    std::vector<Range> groups{range};
    while (groups.size()<8) {
        std::size_t candidate=groups.size();
        float candidate_cost=-1.0F;
        for (std::size_t i=0;i<groups.size();++i) {
            const auto n=groups[i].end-groups[i].begin;
            if (n<=leaf_size_) continue;
            const float cost=aabb_surface_area(range_bounds(snapshot,groups[i]))*static_cast<float>(n);
            if (cost>candidate_cost) {candidate=i;candidate_cost=cost;}
        }
        if (candidate==groups.size()) break;
        Range left{},right{};
        if (!split_sah(snapshot,groups[candidate],left,right)) break;
        groups[candidate]=left;
        groups.insert(groups.begin()+static_cast<std::ptrdiff_t>(candidate+1),right);
    }

    // nodes_ may reallocate during recursion; never retain references across recursive calls.
    for (std::size_t gi=0;gi<groups.size();++gi) {
        const auto g=groups[gi];
        Child child{};
        child.bounds=range_bounds(snapshot,g);
        const auto count=g.end-g.begin;
        if (count<=leaf_size_) {
            child.index=static_cast<std::uint32_t>(primitive_slots_.size());
            child.count=static_cast<std::uint32_t>(count);
            for (std::size_t i=g.begin;i<g.end;++i) {
                const auto slot=working_slots_[i];
                primitive_slots_.push_back(slot);
                leaf_locations_[slot]={node_index,static_cast<std::uint8_t>(gi)};
            }
        } else {
            child.count=0;
            child.index=build_node(snapshot,g,node_index,static_cast<std::uint8_t>(gi),static_cast<std::uint16_t>(depth+1));
            child.bounds=node_bounds(child.index);
        }
        nodes_[node_index].children[gi]=child;
    }
    nodes_[node_index].child_count=static_cast<std::uint8_t>(groups.size());
    return node_index;
}

Aabb WideBvh8View::node_bounds(std::uint32_t node_index) const noexcept {
    Aabb b=empty_aabb();
    const auto& n=nodes_[node_index];
    for (std::size_t i=0;i<n.child_count;++i) b=merge(b,n.children[i].bounds);
    return b;
}

Aabb WideBvh8View::leaf_bounds(const SpatialSnapshot& snapshot, const Child& child) const noexcept {
    Aabb b=empty_aabb();
    for (std::uint32_t i=0;i<child.count;++i) b=merge(b,snapshot.bounds(primitive_slots_[child.index+i]));
    return b;
}

bool WideBvh8View::build(const SpatialSnapshot& snapshot, std::string& error) {
    error.clear();
    working_slots_.resize(snapshot.size());
    std::iota(working_slots_.begin(),working_slots_.end(),0U);
    primitive_slots_.clear(); nodes_.clear();
    leaf_locations_.assign(snapshot.size(),{});
    if (!snapshot.empty()) (void)build_node(snapshot,{0,working_slots_.size()},std::numeric_limits<std::uint32_t>::max(),0,0);
    working_slots_.clear(); working_slots_.shrink_to_fit();
    snapshot_version_=snapshot.version();
    snapshot_structure_revision_=snapshot.structure_revision();
    return true;
}

void WideBvh8View::refit(const SpatialSnapshot& snapshot, const SpatialChangeSet& changes) {
    if (nodes_.empty() || changes.dirty_value_count() == 0) return;
    std::vector<std::uint64_t> leaf_keys;
    leaf_keys.reserve(std::min<std::size_t>(changes.dirty_value_count(), leaf_locations_.size()));
    const auto add_slot = [&](std::uint32_t slot) {
        if (slot>=leaf_locations_.size()) return;
        const auto loc=leaf_locations_[slot];
        if (loc.node==std::numeric_limits<std::uint32_t>::max()) return;
        leaf_keys.push_back((static_cast<std::uint64_t>(loc.node)<<8U)|loc.child);
    };
    for (auto slot : changes.dirty_slots) add_slot(slot);
    for (const auto& range : changes.dirty_ranges) {
        const auto end = static_cast<std::uint64_t>(range.first) + range.count;
        for (std::uint64_t slot=range.first; slot<end; ++slot) add_slot(static_cast<std::uint32_t>(slot));
    }
    std::sort(leaf_keys.begin(),leaf_keys.end()); leaf_keys.erase(std::unique(leaf_keys.begin(),leaf_keys.end()),leaf_keys.end());

    std::vector<std::uint32_t> dirty_nodes;
    for (const auto key:leaf_keys) {
        const auto ni=static_cast<std::uint32_t>(key>>8U);
        const auto ci=static_cast<std::uint8_t>(key&0xffU);
        nodes_[ni].children[ci].bounds=leaf_bounds(snapshot,nodes_[ni].children[ci]);
        std::uint32_t n=ni;
        while (n!=std::numeric_limits<std::uint32_t>::max()) { dirty_nodes.push_back(n); n=nodes_[n].parent; }
    }
    std::sort(dirty_nodes.begin(),dirty_nodes.end()); dirty_nodes.erase(std::unique(dirty_nodes.begin(),dirty_nodes.end()),dirty_nodes.end());
    std::sort(dirty_nodes.begin(),dirty_nodes.end(),[&](auto a,auto b){return nodes_[a].depth>nodes_[b].depth;});
    for (auto ni:dirty_nodes) {
        const Aabb b=node_bounds(ni);
        const auto parent=nodes_[ni].parent;
        if (parent!=std::numeric_limits<std::uint32_t>::max()) nodes_[parent].children[nodes_[ni].parent_child].bounds=b;
    }
}

bool WideBvh8View::full_refit_to(const SpatialSnapshot& snapshot, std::string& error) {
    error.clear();
    if (snapshot_structure_revision_ != snapshot.structure_revision() || leaf_locations_.size() != snapshot.size()) {
        error = "WideBvh8View structural generation mismatch";
        return false;
    }
    if (nodes_.empty()) {
        snapshot_version_ = snapshot.version();
        return true;
    }

    // build_node appends parents before descendants, so reverse node order guarantees
    // internal children have already been updated when their parent is recomputed.
    for (std::size_t ri = nodes_.size(); ri-- > 0;) {
        auto& node = nodes_[ri];
        for (std::size_t ci = 0; ci < node.child_count; ++ci) {
            auto& child = node.children[ci];
            if (child.count > 0) child.bounds = leaf_bounds(snapshot, child);
            else child.bounds = node_bounds(child.index);
        }
    }
    snapshot_version_ = snapshot.version();
    return true;
}

bool WideBvh8View::sync(const SpatialSnapshot& snapshot, const SpatialChangeSet& changes, std::string& error,
                        WideBvhSyncMode mode) {
    error.clear();
    if (changes.requires_rebuild) return build(snapshot,error);
    if (snapshot_structure_revision_ != snapshot.structure_revision()) {
        error="WideBvh8View structural generation mismatch"; return false;
    }
    if (snapshot_version_!=changes.from_version || snapshot.version()!=changes.to_version) {
        error="WideBvh8View version chain mismatch"; return false;
    }
    if (!changes.changed) { snapshot_version_=snapshot.version(); return true; }
    if (mode == WideBvhSyncMode::Rebuild) return build(snapshot,error);
    if (mode == WideBvhSyncMode::Automatic && !snapshot.empty() && changes.dirty_value_count()*5U > snapshot.size())
        return build(snapshot,error);
    refit(snapshot,changes);
    snapshot_version_=snapshot.version();
    return true;
}

SpatialQueryResult WideBvh8View::query_aabb(const SpatialSnapshot& snapshot, const Aabb& query) const {
    SpatialQueryResult result;
    if (snapshot.version()!=snapshot_version_ || snapshot.structure_revision()!=snapshot_structure_revision_) {result.error="WideBvh8View is stale";return result;}
    result.ok=true;
    if (nodes_.empty()) return result;
    std::vector<std::uint32_t> stack{0};
    while(!stack.empty()) {
        const auto ni=stack.back(); stack.pop_back();
        const auto& node=nodes_[ni];
        for(std::size_t ci=0;ci<node.child_count;++ci) {
            const auto& child=node.children[ci];
            if(!aabb_intersects(child.bounds,query)) continue;
            if(child.count>0) {
                for(std::uint32_t i=0;i<child.count;++i) {
                    const auto slot=primitive_slots_[child.index+i];
                    if(aabb_intersects(snapshot.bounds(slot),query)) result.entities.push_back(snapshot.entity(slot));
                }
            } else stack.push_back(child.index);
        }
    }
    result.entities=sorted_unique(std::move(result.entities));
    return result;
}

SpatialRayResult WideBvh8View::raycast(const SpatialSnapshot& snapshot, const Ray& ray) const {
    SpatialRayResult result;
    if(snapshot.version()!=snapshot_version_ || snapshot.structure_revision()!=snapshot_structure_revision_){result.error="WideBvh8View is stale";return result;}
    result.ok=true; if(nodes_.empty()) return result;
    std::vector<std::uint32_t> stack{0};
    while(!stack.empty()) {
        const auto ni=stack.back();stack.pop_back(); const auto& node=nodes_[ni];
        for(std::size_t ci=0;ci<node.child_count;++ci){const auto& child=node.children[ci];float ct=0.0F;if(!ray_intersects_aabb(ray,child.bounds,ct)||ct>result.t)continue;
            if(child.count>0){for(std::uint32_t i=0;i<child.count;++i){const auto slot=primitive_slots_[child.index+i];float t=0.0F;if(ray_intersects_aabb(ray,snapshot.bounds(slot),t)&&t<result.t){result.t=t;result.entity=snapshot.entity(slot);}}}
            else stack.push_back(child.index);
        }
    }
    return result;
}

std::size_t WideBvh8View::storage_bytes() const noexcept {
    return primitive_slots_.capacity()*sizeof(std::uint32_t)+nodes_.capacity()*sizeof(Node)+leaf_locations_.capacity()*sizeof(LeafLocation);
}

MortonBvh8View::MortonBvh8View(std::size_t leaf_size):leaf_size_(std::max<std::size_t>(1,leaf_size)){}

Aabb MortonBvh8View::node_bounds(std::uint32_t node_index) const noexcept {
    Aabb b=empty_aabb();const auto& n=nodes_[node_index];for(std::size_t i=0;i<n.child_count;++i)b=merge(b,n.children[i].bounds);return b;
}

bool MortonBvh8View::build(const SpatialSnapshot& snapshot,std::string& error){
    error.clear(); primitive_slots_.clear();nodes_.clear();root_=std::numeric_limits<std::uint32_t>::max();
    if(snapshot.empty()){snapshot_version_=snapshot.version();snapshot_structure_revision_=snapshot.structure_revision();return true;}
    Aabb cb=empty_aabb();for(std::uint32_t s=0;s<snapshot.size();++s){const auto c=snapshot.center(s);cb=merge(cb,{c,c});}
    const float ex=std::max(1.0e-9F,cb.max.x-cb.min.x),ey=std::max(1.0e-9F,cb.max.y-cb.min.y),ez=std::max(1.0e-9F,cb.max.z-cb.min.z);
    struct MortonPair { std::uint32_t code{}; std::uint32_t slot{}; };
    std::vector<MortonPair> coded; coded.reserve(snapshot.size());
    for(std::uint32_t s=0;s<snapshot.size();++s){const auto c=snapshot.center(s);coded.push_back({morton3d((c.x-cb.min.x)/ex,(c.y-cb.min.y)/ey,(c.z-cb.min.z)/ez),s});}

    // Stable 3-pass radix sort over the 30-bit Morton code (10 bits/pass).
    // O(N), deterministic, and directly portable to parallel/GPU histogram+scan later.
    constexpr std::uint32_t radix_bits=10U;
    constexpr std::uint32_t radix_size=1U<<radix_bits;
    constexpr std::uint32_t radix_mask=radix_size-1U;
    std::vector<MortonPair> scratch(coded.size());
    std::array<std::size_t,radix_size> counts{};
    for(std::uint32_t pass=0;pass<3U;++pass){
        counts.fill(0); const std::uint32_t shift=pass*radix_bits;
        for(const auto& item:coded) ++counts[(item.code>>shift)&radix_mask];
        std::size_t offset=0;
        for(auto& count:counts){const auto n=count;count=offset;offset+=n;}
        for(const auto& item:coded) scratch[counts[(item.code>>shift)&radix_mask]++]=item;
        coded.swap(scratch);
    }
    primitive_slots_.reserve(coded.size());for(const auto& item:coded)primitive_slots_.push_back(item.slot);

    struct Ref{Aabb bounds;std::uint32_t index;std::uint32_t count;};
    std::vector<Ref> level;
    for(std::size_t begin=0;begin<primitive_slots_.size();begin+=leaf_size_){const auto end=std::min(begin+leaf_size_,primitive_slots_.size());Aabb b=empty_aabb();for(std::size_t i=begin;i<end;++i)b=merge(b,snapshot.bounds(primitive_slots_[i]));level.push_back({b,static_cast<std::uint32_t>(begin),static_cast<std::uint32_t>(end-begin)});}
    while(level.size()>1){std::vector<Ref> next;next.reserve((level.size()+7U)/8U);for(std::size_t begin=0;begin<level.size();begin+=8U){const auto end=std::min(begin+8U,level.size());const auto ni=static_cast<std::uint32_t>(nodes_.size());nodes_.push_back({});auto& n=nodes_.back();n.child_count=static_cast<std::uint8_t>(end-begin);for(std::size_t j=begin;j<end;++j){n.children[j-begin]={level[j].bounds,level[j].index,level[j].count};}next.push_back({node_bounds(ni),ni,0});}level=std::move(next);}
    if(level.front().count>0){nodes_.push_back({});auto& n=nodes_.back();n.child_count=1;n.children[0]={level.front().bounds,level.front().index,level.front().count};root_=static_cast<std::uint32_t>(nodes_.size()-1);}else root_=level.front().index;
    snapshot_version_=snapshot.version();snapshot_structure_revision_=snapshot.structure_revision();return true;
}

bool MortonBvh8View::sync(const SpatialSnapshot& snapshot,const SpatialChangeSet&,std::string& error){return build(snapshot,error);}

SpatialQueryResult MortonBvh8View::query_aabb(const SpatialSnapshot& snapshot,const Aabb& query) const{
    SpatialQueryResult result;if(snapshot.version()!=snapshot_version_ || snapshot.structure_revision()!=snapshot_structure_revision_){result.error="MortonBvh8View is stale";return result;}result.ok=true;if(root_==std::numeric_limits<std::uint32_t>::max())return result;std::vector<std::uint32_t> stack{root_};while(!stack.empty()){const auto ni=stack.back();stack.pop_back();const auto& n=nodes_[ni];for(std::size_t ci=0;ci<n.child_count;++ci){const auto& c=n.children[ci];if(!aabb_intersects(c.bounds,query))continue;if(c.count>0){for(std::uint32_t i=0;i<c.count;++i){const auto slot=primitive_slots_[c.index+i];if(aabb_intersects(snapshot.bounds(slot),query))result.entities.push_back(snapshot.entity(slot));}}else stack.push_back(c.index);}}result.entities=sorted_unique(std::move(result.entities));return result;}

SpatialRayResult MortonBvh8View::raycast(const SpatialSnapshot& snapshot,const Ray& ray) const{
    SpatialRayResult result;if(snapshot.version()!=snapshot_version_ || snapshot.structure_revision()!=snapshot_structure_revision_){result.error="MortonBvh8View is stale";return result;}result.ok=true;if(root_==std::numeric_limits<std::uint32_t>::max())return result;std::vector<std::uint32_t> stack{root_};while(!stack.empty()){const auto ni=stack.back();stack.pop_back();const auto& n=nodes_[ni];for(std::size_t ci=0;ci<n.child_count;++ci){const auto& c=n.children[ci];float ct=0.0F;if(!ray_intersects_aabb(ray,c.bounds,ct)||ct>result.t)continue;if(c.count>0){for(std::uint32_t i=0;i<c.count;++i){const auto slot=primitive_slots_[c.index+i];float t=0.0F;if(ray_intersects_aabb(ray,snapshot.bounds(slot),t)&&t<result.t){result.t=t;result.entity=snapshot.entity(slot);}}}else stack.push_back(c.index);}}return result;}

std::size_t MortonBvh8View::storage_bytes() const noexcept{return primitive_slots_.capacity()*sizeof(std::uint32_t)+nodes_.capacity()*sizeof(Node);}

} // namespace aion
