#include "aion/kernel/sparse_sdf.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace aion {
namespace {
constexpr std::int32_t kLeafCells = 8;
constexpr std::int32_t kLowerChildren = 4;
constexpr std::int32_t kUpperChildren = 4;
constexpr std::int32_t kLowerSpan = kLeafCells * kLowerChildren;   // 32 cells
constexpr std::int32_t kRootSpan = kLowerSpan * kUpperChildren;    // 128 cells
constexpr std::uint32_t kNegativeRootBit = 0x80000000U;
constexpr float kSqrt3 = 1.7320508075688772F;
constexpr float kEpsilon = 1.0e-6F;

struct WideNode { std::uint64_t child_mask{}; std::uint64_t negative_mask{}; std::uint32_t first_child{}; };
struct RootEntry { std::int32_t x{}; std::int32_t y{}; std::int32_t z{}; std::uint32_t payload{}; };
struct LeafBrick {
    static constexpr std::size_t kSamplesPerAxis=9;
    static constexpr std::size_t kSampleCount=kSamplesPerAxis*kSamplesPerAxis*kSamplesPerAxis;
    std::int32_t cell_x{}; std::int32_t cell_y{}; std::int32_t cell_z{};
    std::uint32_t sample_offset{};
};
struct SparseResource {
    std::uint16_t generation{1}; Aabb bounds{}; Vec3 origin{};
    std::uint32_t cell_dim_x{},cell_dim_y{},cell_dim_z{};
    float voxel_size{},band_distance{},max_geometric_error{},quant_scale{};
    std::vector<RootEntry> roots; std::vector<WideNode> upper_nodes; std::vector<WideNode> lower_nodes; std::vector<LeafBrick> leaf_bricks; std::vector<std::int16_t> samples; SparseSdfStats stats{};
};

enum class CellClass : std::uint8_t { Positive, Negative, Descend };

[[nodiscard]] bool finite(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
[[nodiscard]] Vec3 add(Vec3 a, Vec3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
[[nodiscard]] Vec3 sub(Vec3 a, Vec3 b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
[[nodiscard]] Vec3 mul(Vec3 a, float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
[[nodiscard]] float dot(Vec3 a, Vec3 b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
[[nodiscard]] float length(Vec3 a) noexcept { return std::sqrt(dot(a,a)); }
[[nodiscard]] Vec3 normalize(Vec3 a) noexcept {
    const float l=length(a); return l>kEpsilon?mul(a,1.0F/l):Vec3{};
}
[[nodiscard]] Aabb expand_bounds(Aabb b, float amount) noexcept {
    const Vec3 d{amount,amount,amount}; return {sub(b.min,d),add(b.max,d)};
}
[[nodiscard]] std::uint32_t slot4(std::int32_t x,std::int32_t y,std::int32_t z) noexcept {
    return static_cast<std::uint32_t>(x + 4*(y + 4*z));
}
[[nodiscard]] std::size_t sample_index(std::int32_t x,std::int32_t y,std::int32_t z) noexcept {
    constexpr std::size_t n=LeafBrick::kSamplesPerAxis;
    return static_cast<std::size_t>(x) + n*(static_cast<std::size_t>(y)+n*static_cast<std::size_t>(z));
}
[[nodiscard]] std::uint32_t rank_before(std::uint64_t mask,std::uint32_t slot) noexcept {
    if(slot==0U)return 0U;
    const std::uint64_t lower=mask & ((std::uint64_t{1}<<slot)-1U);
    return static_cast<std::uint32_t>(std::popcount(lower));
}
[[nodiscard]] std::int16_t quantize(float value,float band) noexcept {
    const float n=std::clamp(value/band,-1.0F,1.0F);
    const long q=std::lround(n*32767.0F);
    return static_cast<std::int16_t>(std::clamp(q,-32767L,32767L));
}
[[nodiscard]] float decode(std::int16_t value,float scale) noexcept {
    return static_cast<float>(value)*scale;
}

[[nodiscard]] bool ray_aabb_interval(const Ray& ray,const Aabb& box,float& enter,float& exit) noexcept {
    enter=ray.t_min; exit=ray.t_max;
    const auto axis=[&](float o,float d,float lo,float hi)->bool{
        if(std::fabs(d)<=kEpsilon)return o>=lo&&o<=hi;
        float a=(lo-o)/d,b=(hi-o)/d;if(a>b)std::swap(a,b);
        enter=std::max(enter,a);exit=std::min(exit,b);return enter<=exit;
    };
    return axis(ray.origin.x,ray.direction.x,box.min.x,box.max.x)&&
           axis(ray.origin.y,ray.direction.y,box.min.y,box.max.y)&&
           axis(ray.origin.z,ray.direction.z,box.min.z,box.max.z);
}

struct Compiler {
    const GeometryKernel& kernel;
    GeometryHandle source;
    SparseSdfCompileOptions options;
    SparseResource resource;
    std::string error;

    [[nodiscard]] bool sample_source(Vec3 p,float& value) {
        const auto s=kernel.signed_distance(source,p);
        ++resource.stats.source_samples;
        if(s.status!=GeometryQueryStatus::Ok||!std::isfinite(s.signed_distance)){
            error="source signed-distance query failed during sparse compilation";
            return false;
        }
        value=s.signed_distance;
        return true;
    }

    [[nodiscard]] Vec3 cell_center(std::int32_t x,std::int32_t y,std::int32_t z,std::int32_t span) const noexcept {
        const float half=0.5F*static_cast<float>(span);
        return add(resource.origin,{(static_cast<float>(x)+half)*resource.voxel_size,
                                    (static_cast<float>(y)+half)*resource.voxel_size,
                                    (static_cast<float>(z)+half)*resource.voxel_size});
    }

    [[nodiscard]] CellClass classify(std::int32_t x,std::int32_t y,std::int32_t z,std::int32_t span) {
        float d{}; if(!sample_source(cell_center(x,y,z,span),d))return CellClass::Descend;
        const float radius=0.5F*static_cast<float>(span)*resource.voxel_size*kSqrt3;
        const float uncertainty=options.source.max_distance_error + options.source.lipschitz_bound*radius;
        if(d>resource.band_distance+uncertainty)return CellClass::Positive;
        if(d<-resource.band_distance-uncertainty)return CellClass::Negative;
        return CellClass::Descend;
    }

    [[nodiscard]] std::uint32_t compile_leaf(std::int32_t x,std::int32_t y,std::int32_t z) {
        LeafBrick leaf;
        leaf.cell_x=x;leaf.cell_y=y;leaf.cell_z=z;
        if(resource.samples.size()>std::numeric_limits<std::uint32_t>::max()-LeafBrick::kSampleCount){error="sparse SDF sample buffer exceeds 32-bit leaf offsets";return 0U;}
        leaf.sample_offset=static_cast<std::uint32_t>(resource.samples.size());
        resource.samples.resize(resource.samples.size()+LeafBrick::kSampleCount);
        for(std::int32_t sz=0;sz<=kLeafCells;++sz){
            for(std::int32_t sy=0;sy<=kLeafCells;++sy){
                for(std::int32_t sx=0;sx<=kLeafCells;++sx){
                    const Vec3 p=add(resource.origin,{static_cast<float>(x+sx)*resource.voxel_size,
                                                      static_cast<float>(y+sy)*resource.voxel_size,
                                                      static_cast<float>(z+sz)*resource.voxel_size});
                    float d{}; if(!sample_source(p,d))return 0U;
                    resource.samples[static_cast<std::size_t>(leaf.sample_offset)+sample_index(sx,sy,sz)]=quantize(d,resource.band_distance);
                }
            }
        }
        if(resource.leaf_bricks.size()>=std::numeric_limits<std::uint32_t>::max()){error="sparse SDF leaf index overflow";return 0U;}
        const auto index=static_cast<std::uint32_t>(resource.leaf_bricks.size());
        resource.leaf_bricks.push_back(leaf);
        return index;
    }

    [[nodiscard]] std::uint32_t compile_lower(std::int32_t x,std::int32_t y,std::int32_t z) {
        if(resource.lower_nodes.size()>=std::numeric_limits<std::uint32_t>::max()){error="sparse SDF lower-node index overflow";return 0U;}
        const auto node_index=static_cast<std::uint32_t>(resource.lower_nodes.size());
        resource.lower_nodes.emplace_back();
        bool first=true;
        for(std::int32_t cz=0;cz<4;++cz){for(std::int32_t cy=0;cy<4;++cy){for(std::int32_t cx=0;cx<4;++cx){
            const auto slot=slot4(cx,cy,cz);const std::uint64_t bit=std::uint64_t{1}<<slot;
            const std::int32_t lx=x+cx*kLeafCells,ly=y+cy*kLeafCells,lz=z+cz*kLeafCells;
            const auto cls=classify(lx,ly,lz,kLeafCells);if(!error.empty())return node_index;
            if(cls==CellClass::Negative){resource.lower_nodes[node_index].negative_mask|=bit;continue;}
            if(cls==CellClass::Positive)continue;
            const auto child=compile_leaf(lx,ly,lz);if(!error.empty())return node_index;
            if(first){resource.lower_nodes[node_index].first_child=child;first=false;}
            resource.lower_nodes[node_index].child_mask|=bit;
        }}}
        return node_index;
    }

    [[nodiscard]] std::uint32_t compile_upper(std::int32_t x,std::int32_t y,std::int32_t z) {
        if(resource.upper_nodes.size()>=kNegativeRootBit){error="sparse SDF upper-node index overflow";return 0U;}
        const auto node_index=static_cast<std::uint32_t>(resource.upper_nodes.size());
        resource.upper_nodes.emplace_back();
        bool first=true;
        for(std::int32_t cz=0;cz<4;++cz){for(std::int32_t cy=0;cy<4;++cy){for(std::int32_t cx=0;cx<4;++cx){
            const auto slot=slot4(cx,cy,cz);const std::uint64_t bit=std::uint64_t{1}<<slot;
            const std::int32_t lx=x+cx*kLowerSpan,ly=y+cy*kLowerSpan,lz=z+cz*kLowerSpan;
            const auto cls=classify(lx,ly,lz,kLowerSpan);if(!error.empty())return node_index;
            if(cls==CellClass::Negative){resource.upper_nodes[node_index].negative_mask|=bit;continue;}
            if(cls==CellClass::Positive)continue;
            const auto child=compile_lower(lx,ly,lz);if(!error.empty())return node_index;
            if(first){resource.upper_nodes[node_index].first_child=child;first=false;}
            resource.upper_nodes[node_index].child_mask|=bit;
        }}}
        return node_index;
    }

    [[nodiscard]] bool build() {
        const auto caps=kernel.capabilities(source);
        if(!kernel.valid(source)||!caps.contains(GeometryCapability::Bounds)||!caps.contains(GeometryCapability::SignedDistance)){
            error="sparse SDF compiler requires a valid source with Bounds + exact SignedDistance capabilities";return false;
        }
        if(!std::isfinite(options.voxel_size)||options.voxel_size<=0.0F||
           !std::isfinite(options.half_band_voxels)||options.half_band_voxels<1.0F||
           !std::isfinite(options.source.max_distance_error)||options.source.max_distance_error<0.0F||
           !std::isfinite(options.source.lipschitz_bound)||options.source.lipschitz_bound<=0.0F){
            error="invalid sparse SDF compile options";return false;
        }
        const auto b=kernel.bounds(source);if(b.status!=GeometryQueryStatus::Ok||!finite(b.bounds.min)||!finite(b.bounds.max)){
            error="source bounds query failed during sparse compilation";return false;
        }
        resource.bounds=b.bounds;resource.voxel_size=options.voxel_size;
        resource.band_distance=options.voxel_size*options.half_band_voxels;
        resource.quant_scale=resource.band_distance/32767.0F;
        const float quant_error=0.5F*resource.quant_scale;
        resource.max_geometric_error=options.source.max_distance_error + options.source.lipschitz_bound*(0.5F*kSqrt3)*resource.voxel_size + quant_error;
        const float pad=resource.band_distance+options.source.max_distance_error+resource.max_geometric_error;
        const Aabb domain=expand_bounds(resource.bounds,pad);
        resource.origin=domain.min;
        const auto dim=[&](float lo,float hi){return static_cast<std::uint32_t>(std::max(1.0F,std::ceil((hi-lo)/resource.voxel_size)));};
        resource.cell_dim_x=dim(domain.min.x,domain.max.x);resource.cell_dim_y=dim(domain.min.y,domain.max.y);resource.cell_dim_z=dim(domain.min.z,domain.max.z);
        const std::int32_t rx=static_cast<std::int32_t>((resource.cell_dim_x+kRootSpan-1U)/kRootSpan);
        const std::int32_t ry=static_cast<std::int32_t>((resource.cell_dim_y+kRootSpan-1U)/kRootSpan);
        const std::int32_t rz=static_cast<std::int32_t>((resource.cell_dim_z+kRootSpan-1U)/kRootSpan);
        for(std::int32_t x=0;x<rx;++x){for(std::int32_t y=0;y<ry;++y){for(std::int32_t z=0;z<rz;++z){
            const std::int32_t cx=x*kRootSpan,cy=y*kRootSpan,cz=z*kRootSpan;
            const auto cls=classify(cx,cy,cz,kRootSpan);if(!error.empty())return false;
            if(cls==CellClass::Positive)continue;
            RootEntry entry{.x=x,.y=y,.z=z};
            if(cls==CellClass::Negative){entry.payload=kNegativeRootBit;resource.roots.push_back(entry);continue;}
            const auto child=compile_upper(cx,cy,cz);if(!error.empty())return false;
            if(child>=kNegativeRootBit){error="sparse SDF upper-node index overflow";return false;}
            entry.payload=child;resource.roots.push_back(entry);
        }}}
        // Topology is immutable after compilation: compact all payloads before publishing the resource.
        resource.roots.shrink_to_fit();
        resource.upper_nodes.shrink_to_fit();
        resource.lower_nodes.shrink_to_fit();
        resource.leaf_bricks.shrink_to_fit();
        resource.samples.shrink_to_fit();

        const auto mul_safe=[](std::size_t lhs,std::size_t rhs)->std::size_t{
            if(lhs!=0U&&rhs>std::numeric_limits<std::size_t>::max()/lhs){
                return std::numeric_limits<std::size_t>::max();
            }
            return lhs*rhs;
        };
        const std::size_t sx=static_cast<std::size_t>(resource.cell_dim_x)+1U,sy=static_cast<std::size_t>(resource.cell_dim_y)+1U,sz=static_cast<std::size_t>(resource.cell_dim_z)+1U;
        resource.stats.root_entries=resource.roots.size();
        resource.stats.negative_root_tiles=static_cast<std::size_t>(std::count_if(resource.roots.begin(),resource.roots.end(),[](const auto&e){return (e.payload&kNegativeRootBit)!=0U;}));
        resource.stats.upper_nodes=resource.upper_nodes.size();resource.stats.lower_nodes=resource.lower_nodes.size();
        for(const auto& node:resource.upper_nodes)resource.stats.negative_upper_tiles+=static_cast<std::size_t>(std::popcount(node.negative_mask));
        for(const auto& node:resource.lower_nodes)resource.stats.negative_lower_tiles+=static_cast<std::size_t>(std::popcount(node.negative_mask));
        resource.stats.leaf_bricks=resource.leaf_bricks.size();
        resource.stats.quantized_samples=resource.samples.size();
        resource.stats.voxel_size=resource.voxel_size;resource.stats.band_distance=resource.band_distance;resource.stats.max_geometric_error=resource.max_geometric_error;
        resource.stats.dense_equivalent_samples=mul_safe(mul_safe(sx,sy),sz);
        resource.stats.dense_equivalent_bytes=mul_safe(resource.stats.dense_equivalent_samples,sizeof(float));
        resource.stats.dense_equivalent_quantized_bytes=mul_safe(resource.stats.dense_equivalent_samples,sizeof(std::int16_t));
        resource.stats.topology_bytes=resource.roots.capacity()*sizeof(RootEntry)+resource.upper_nodes.capacity()*sizeof(WideNode)+resource.lower_nodes.capacity()*sizeof(WideNode)+resource.leaf_bricks.capacity()*sizeof(LeafBrick);
        resource.stats.sample_payload_bytes=resource.samples.capacity()*sizeof(std::int16_t);
        resource.stats.storage_bytes=resource.stats.topology_bytes+resource.stats.sample_payload_bytes;
        return true;
    }
};

[[nodiscard]] const RootEntry* find_root(const SparseResource& r,std::int32_t x,std::int32_t y,std::int32_t z) noexcept {
    const auto key=std::tuple{x,y,z};
    const auto it=std::lower_bound(r.roots.begin(),r.roots.end(),key,[](const auto& e,const auto& k){return std::tuple{e.x,e.y,e.z}<k;});
    if(it==r.roots.end()||std::tuple{it->x,it->y,it->z}!=key){
        return nullptr;
    }
    return &*it;
}

[[nodiscard]] float sample_leaf_value(const SparseResource& r,const LeafBrick& leaf,Vec3 p) noexcept {
    const float gx=(p.x-r.origin.x)/r.voxel_size,gy=(p.y-r.origin.y)/r.voxel_size,gz=(p.z-r.origin.z)/r.voxel_size;
    const float local_x=gx-static_cast<float>(leaf.cell_x),local_y=gy-static_cast<float>(leaf.cell_y),local_z=gz-static_cast<float>(leaf.cell_z);
    const auto cx=std::clamp(static_cast<std::int32_t>(std::floor(local_x)),0,kLeafCells-1),cy=std::clamp(static_cast<std::int32_t>(std::floor(local_y)),0,kLeafCells-1),cz=std::clamp(static_cast<std::int32_t>(std::floor(local_z)),0,kLeafCells-1);
    const float fx=std::clamp(local_x-static_cast<float>(cx),0.0F,1.0F),fy=std::clamp(local_y-static_cast<float>(cy),0.0F,1.0F),fz=std::clamp(local_z-static_cast<float>(cz),0.0F,1.0F);
    const auto q=[&](std::int32_t x,std::int32_t y,std::int32_t z){return decode(r.samples[static_cast<std::size_t>(leaf.sample_offset)+sample_index(x,y,z)],r.quant_scale);};
    const float c000=q(cx,cy,cz),c100=q(cx+1,cy,cz),c010=q(cx,cy+1,cz),c110=q(cx+1,cy+1,cz),c001=q(cx,cy,cz+1),c101=q(cx+1,cy,cz+1),c011=q(cx,cy+1,cz+1),c111=q(cx+1,cy+1,cz+1);
    const auto lerp=[](float a,float b,float t){return a+(b-a)*t;};
    const float c00=lerp(c000,c100,fx),c10=lerp(c010,c110,fx),c01=lerp(c001,c101,fx),c11=lerp(c011,c111,fx);
    return lerp(lerp(c00,c10,fy),lerp(c01,c11,fy),fz);
}

[[nodiscard]] float sample_value(const SparseResource& r,Vec3 p) noexcept {
    const float gx=(p.x-r.origin.x)/r.voxel_size,gy=(p.y-r.origin.y)/r.voxel_size,gz=(p.z-r.origin.z)/r.voxel_size;
    if(gx<0.0F||gy<0.0F||gz<0.0F||gx>=static_cast<float>(r.cell_dim_x)||gy>=static_cast<float>(r.cell_dim_y)||gz>=static_cast<float>(r.cell_dim_z))return r.band_distance;
    const auto ix=static_cast<std::int32_t>(std::floor(gx)),iy=static_cast<std::int32_t>(std::floor(gy)),iz=static_cast<std::int32_t>(std::floor(gz));
    const std::int32_t rx=ix/kRootSpan,ry=iy/kRootSpan,rz=iz/kRootSpan;
    const auto* root=find_root(r,rx,ry,rz);if(!root)return r.band_distance;
    if((root->payload&kNegativeRootBit)!=0U)return -r.band_distance;
    const auto& upper=r.upper_nodes[root->payload];
    const std::int32_t ux=(ix%kRootSpan)/kLowerSpan,uy=(iy%kRootSpan)/kLowerSpan,uz=(iz%kRootSpan)/kLowerSpan;
    const auto uslot=slot4(ux,uy,uz);const std::uint64_t ubit=std::uint64_t{1}<<uslot;
    if((upper.child_mask&ubit)==0U)return (upper.negative_mask&ubit)!=0U?-r.band_distance:r.band_distance;
    const auto lower_index=upper.first_child+rank_before(upper.child_mask,uslot);if(lower_index>=r.lower_nodes.size())return r.band_distance;
    const auto& lower=r.lower_nodes[lower_index];
    const std::int32_t lx=(ix%kLowerSpan)/kLeafCells,ly=(iy%kLowerSpan)/kLeafCells,lz=(iz%kLowerSpan)/kLeafCells;
    const auto lslot=slot4(lx,ly,lz);const std::uint64_t lbit=std::uint64_t{1}<<lslot;
    if((lower.child_mask&lbit)==0U)return (lower.negative_mask&lbit)!=0U?-r.band_distance:r.band_distance;
    const auto leaf_index=lower.first_child+rank_before(lower.child_mask,lslot);if(leaf_index>=r.leaf_bricks.size())return r.band_distance;
    return sample_leaf_value(r,r.leaf_bricks[leaf_index],p);
}


[[nodiscard]] const LeafBrick* find_leaf_at_cell(const SparseResource& r,std::int32_t ix,std::int32_t iy,std::int32_t iz) noexcept {
    if(ix<0||iy<0||iz<0||ix>=static_cast<std::int32_t>(r.cell_dim_x)||iy>=static_cast<std::int32_t>(r.cell_dim_y)||iz>=static_cast<std::int32_t>(r.cell_dim_z))return nullptr;
    const std::int32_t rx=ix/kRootSpan,ry=iy/kRootSpan,rz=iz/kRootSpan;const auto* root=find_root(r,rx,ry,rz);
    if(!root||(root->payload&kNegativeRootBit)!=0U)return nullptr;
    const auto& upper=r.upper_nodes[root->payload];
    const auto uslot=slot4((ix%kRootSpan)/kLowerSpan,(iy%kRootSpan)/kLowerSpan,(iz%kRootSpan)/kLowerSpan);const std::uint64_t ubit=std::uint64_t{1}<<uslot;
    if((upper.child_mask&ubit)==0U)return nullptr;
    const auto lower_index=upper.first_child+rank_before(upper.child_mask,uslot);if(lower_index>=r.lower_nodes.size())return nullptr;
    const auto& lower=r.lower_nodes[lower_index];
    const auto lslot=slot4((ix%kLowerSpan)/kLeafCells,(iy%kLowerSpan)/kLeafCells,(iz%kLowerSpan)/kLeafCells);const std::uint64_t lbit=std::uint64_t{1}<<lslot;
    if((lower.child_mask&lbit)==0U)return nullptr;
    const auto leaf_index=lower.first_child+rank_before(lower.child_mask,lslot);if(leaf_index>=r.leaf_bricks.size())return nullptr;
    return &r.leaf_bricks[leaf_index];
}

[[nodiscard]] Aabb domain_bounds(const SparseResource& r) noexcept {
    return {r.origin,add(r.origin,{static_cast<float>(r.cell_dim_x)*r.voxel_size,static_cast<float>(r.cell_dim_y)*r.voxel_size,static_cast<float>(r.cell_dim_z)*r.voxel_size})};
}

[[nodiscard]] Vec3 sample_gradient(const SparseResource& r,Vec3 p) noexcept {
    const float h=r.voxel_size;
    const Vec3 dx{h,0,0},dy{0,h,0},dz{0,0,h};
    return normalize({sample_value(r,add(p,dx))-sample_value(r,sub(p,dx)),sample_value(r,add(p,dy))-sample_value(r,sub(p,dy)),sample_value(r,add(p,dz))-sample_value(r,sub(p,dz))});
}
}

struct SparseSdfProvider::State { std::vector<SparseResource> resources; };

SparseSdfProvider::SparseSdfProvider(GeometryProviderId id) : id_(id), state_(std::make_shared<State>()) {}

bool SparseSdfProvider::register_with(GeometryKernel& kernel,std::string& error) {
    GeometryProviderDescriptor d{.id=id_,.name="sparse-sdf-static",.capabilities=GeometryCapabilityMask::of({GeometryCapability::Bounds,GeometryCapability::RaySurface,GeometryCapability::TruncatedSignedDistance})};
    GeometryProviderOps ops{.context=state_,.valid=&valid_cb,.bounds=&bounds_cb,.raycast=&raycast_cb,.signed_distance=&distance_cb,.storage_bytes=&storage_cb};
    return kernel.register_provider(std::move(d),ops,error);
}

SparseSdfBuildResult SparseSdfProvider::compile_from(const GeometryKernel& kernel,GeometryHandle source,const SparseSdfCompileOptions& options) {
    Compiler compiler{.kernel=kernel,.source=source,.options=options,.resource={},.error={}};
    if(!compiler.build())return {.error=std::move(compiler.error)};
    if(state_->resources.size()>=std::numeric_limits<GeometryResourceId>::max())return {.error="sparse SDF resource id overflow"};
    const auto resource_id=static_cast<GeometryResourceId>(state_->resources.size());
    const auto generation=compiler.resource.generation;const auto max_error=compiler.resource.max_geometric_error;const auto stats=compiler.resource.stats;
    state_->resources.push_back(std::move(compiler.resource));
    return {.handle={id_,generation,resource_id},.max_geometric_error=max_error,.stats=stats,.error={}};
}

SparseSdfStats SparseSdfProvider::stats(GeometryHandle h) const noexcept {
    if(h.provider!=id_||h.resource>=state_->resources.size()||state_->resources[h.resource].generation!=h.generation)return {};
    return state_->resources[h.resource].stats;
}

bool SparseSdfProvider::export_archive(GeometryHandle h, RepresentationArchive& archive, std::string& error) const {
    archive = {};
    if (h.provider != id_ || h.resource >= state_->resources.size() || state_->resources[h.resource].generation != h.generation) {
        error = "invalid sparse SDF handle for archive export";
        return false;
    }
    const auto& r = state_->resources[h.resource];
    CanonicalByteWriter w(archive.topology);
    w.u32(0x53444631U); // SDF1
    w.u16(r.generation);
    w.f32(r.bounds.min.x); w.f32(r.bounds.min.y); w.f32(r.bounds.min.z);
    w.f32(r.bounds.max.x); w.f32(r.bounds.max.y); w.f32(r.bounds.max.z);
    w.f32(r.origin.x); w.f32(r.origin.y); w.f32(r.origin.z);
    w.u32(r.cell_dim_x); w.u32(r.cell_dim_y); w.u32(r.cell_dim_z);
    w.f32(r.voxel_size); w.f32(r.band_distance); w.f32(r.max_geometric_error); w.f32(r.quant_scale);
    w.u32(static_cast<std::uint32_t>(r.roots.size()));
    w.u32(static_cast<std::uint32_t>(r.upper_nodes.size()));
    w.u32(static_cast<std::uint32_t>(r.lower_nodes.size()));
    w.u32(static_cast<std::uint32_t>(r.leaf_bricks.size()));
    for (const auto& x : r.roots) { w.i32(x.x); w.i32(x.y); w.i32(x.z); w.u32(x.payload); }
    for (const auto& x : r.upper_nodes) { w.u64(x.child_mask); w.u64(x.negative_mask); w.u32(x.first_child); }
    for (const auto& x : r.lower_nodes) { w.u64(x.child_mask); w.u64(x.negative_mask); w.u32(x.first_child); }
    for (const auto& x : r.leaf_bricks) { w.i32(x.cell_x); w.i32(x.cell_y); w.i32(x.cell_z); w.u32(x.sample_offset); }
    archive.payloads.resize(1);
    CanonicalByteWriter samples(archive.payloads[0]);
    for (const auto value : r.samples) samples.i16(value);
    error.clear();
    return true;
}

bool SparseSdfProvider::valid_cb(const void* ctx,GeometryResourceId id,std::uint16_t gen) noexcept {const auto& s=*static_cast<const State*>(ctx);return id<s.resources.size()&&s.resources[id].generation==gen;}
GeometryBoundsResult SparseSdfProvider::bounds_cb(const void* ctx,GeometryResourceId id,std::uint16_t gen) noexcept {if(!valid_cb(ctx,id,gen))return {GeometryQueryStatus::InvalidHandle,{}};return {GeometryQueryStatus::Ok,static_cast<const State*>(ctx)->resources[id].bounds};}
GeometryDistanceSample SparseSdfProvider::distance_cb(const void* ctx,GeometryResourceId id,std::uint16_t gen,Vec3 p) noexcept {
    if(!valid_cb(ctx,id,gen))return {GeometryQueryStatus::InvalidHandle};
    if(!finite(p))return {GeometryQueryStatus::NumericalFailure};
    const auto&r=static_cast<const State*>(ctx)->resources[id];
    return {GeometryQueryStatus::Ok,sample_value(r,p),sample_gradient(r,p)};
}
GeometryRayHit SparseSdfProvider::raycast_cb(const void* ctx,GeometryResourceId id,std::uint16_t gen,const Ray& ray) noexcept {
    if(!valid_cb(ctx,id,gen))return {GeometryQueryStatus::InvalidHandle};
    if(!finite(ray.origin)||!finite(ray.direction)||!std::isfinite(ray.t_min)||std::isnan(ray.t_max)||ray.t_max<ray.t_min)return {GeometryQueryStatus::NumericalFailure};
    const auto&r=static_cast<const State*>(ctx)->resources[id];const float dir_len=length(ray.direction);if(dir_len<=kEpsilon)return {GeometryQueryStatus::NumericalFailure};
    float enter{},exit{};if(!ray_aabb_interval(ray,domain_bounds(r),enter,exit))return {GeometryQueryStatus::Miss};
    float t=std::max(ray.t_min,enter);const float end=std::min(ray.t_max,exit);if(t>end)return {GeometryQueryStatus::Miss};
    const float brick_world=static_cast<float>(kLeafCells)*r.voxel_size;
    const Vec3 start=add(ray.origin,mul(ray.direction,t));
    auto brick_coord=[&](float value,float origin){return static_cast<std::int32_t>(std::floor((value-origin)/brick_world));};
    std::int32_t bx=brick_coord(start.x,r.origin.x),by=brick_coord(start.y,r.origin.y),bz=brick_coord(start.z,r.origin.z);
    const std::int32_t max_bx=static_cast<std::int32_t>((r.cell_dim_x+kLeafCells-1U)/kLeafCells),max_by=static_cast<std::int32_t>((r.cell_dim_y+kLeafCells-1U)/kLeafCells),max_bz=static_cast<std::int32_t>((r.cell_dim_z+kLeafCells-1U)/kLeafCells);
    bx=std::clamp(bx,0,max_bx-1);by=std::clamp(by,0,max_by-1);bz=std::clamp(bz,0,max_bz-1);
    const float inf=std::numeric_limits<float>::infinity();
    const auto setup_axis=[&](float o,float d,float origin,std::int32_t b,std::int32_t& step,float& t_max,float& t_delta){
        if(std::fabs(d)<=kEpsilon){step=0;t_max=inf;t_delta=inf;return;}
        step=d>0.0F?1:-1;const float boundary=origin+static_cast<float>(b+(step>0?1:0))*brick_world;t_max=(boundary-o)/d;t_delta=brick_world/std::fabs(d);
        while(t_max<t-kEpsilon)t_max+=t_delta;
    };
    std::int32_t sx{},sy{},sz{};float tx{},ty{},tz{},dx{},dy{},dz{};
    setup_axis(ray.origin.x,ray.direction.x,r.origin.x,bx,sx,tx,dx);setup_axis(ray.origin.y,ray.direction.y,r.origin.y,by,sy,ty,dy);setup_axis(ray.origin.z,ray.direction.z,r.origin.z,bz,sz,tz,dz);
    const float hit_eps=std::max(r.max_geometric_error,0.25F*r.voxel_size);const float min_step=0.125F*r.voxel_size/dir_len;
    const std::size_t max_steps=static_cast<std::size_t>(max_bx+max_by+max_bz+16);
    for(std::size_t dda=0;dda<max_steps&&t<=end+min_step;++dda){
        const float interval_end=std::min({tx,ty,tz,end});
        const auto* leaf=find_leaf_at_cell(r,bx*kLeafCells,by*kLeafCells,bz*kLeafCells);
        if(leaf){
            float local_t=t;
            for(int step_count=0;step_count<128&&local_t<=interval_end+min_step;++step_count){
                const Vec3 p=add(ray.origin,mul(ray.direction,local_t));const float d=sample_leaf_value(r,*leaf,p);if(!std::isfinite(d))return {GeometryQueryStatus::NumericalFailure};
                if(std::fabs(d)<=hit_eps)return {GeometryQueryStatus::Ok,local_t,p,sample_gradient(r,p)};
                const float safe=std::max(std::fabs(d)-r.max_geometric_error,0.125F*r.voxel_size);local_t+=std::max(safe/dir_len,min_step);
            }
        }
        if(interval_end>=end)return {GeometryQueryStatus::Miss};
        const float next=interval_end;
        if(tx<=next+kEpsilon){bx+=sx;tx+=dx;}if(ty<=next+kEpsilon){by+=sy;ty+=dy;}if(tz<=next+kEpsilon){bz+=sz;tz+=dz;}
        if(bx<0||by<0||bz<0||bx>=max_bx||by>=max_by||bz>=max_bz)return {GeometryQueryStatus::Miss};
        t=next;
    }
    return {GeometryQueryStatus::Miss};
}
std::size_t SparseSdfProvider::storage_cb(const void* ctx,GeometryResourceId id,std::uint16_t gen) noexcept {if(!valid_cb(ctx,id,gen))return 0;return static_cast<const State*>(ctx)->resources[id].stats.storage_bytes;}

} // namespace aion
