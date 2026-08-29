#include "aion/kernel/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace aion {
namespace {
constexpr float kEpsilon = 1.0e-6F;

[[nodiscard]] bool finite(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
[[nodiscard]] Vec3 add(Vec3 a, Vec3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
[[nodiscard]] Vec3 sub(Vec3 a, Vec3 b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
[[nodiscard]] Vec3 mul(Vec3 a, float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
[[nodiscard]] float dot(Vec3 a, Vec3 b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
[[nodiscard]] float length(Vec3 a) noexcept { return std::sqrt(dot(a,a)); }
[[nodiscard]] Vec3 normalize(Vec3 a) noexcept {
    const float l=length(a); return l>kEpsilon?mul(a,1.0F/l):Vec3{};
}
[[nodiscard]] Vec3 abs_vec(Vec3 a) noexcept { return {std::fabs(a.x),std::fabs(a.y),std::fabs(a.z)}; }
[[nodiscard]] Vec3 max_vec(Vec3 a, float b) noexcept { return {std::max(a.x,b),std::max(a.y,b),std::max(a.z,b)}; }

[[nodiscard]] Aabb empty_bounds() noexcept {
    const float inf=std::numeric_limits<float>::infinity();
    return {{inf,inf,inf},{-inf,-inf,-inf}};
}
void expand(Aabb& b, Vec3 p) noexcept {
    b.min.x=std::min(b.min.x,p.x); b.min.y=std::min(b.min.y,p.y); b.min.z=std::min(b.min.z,p.z);
    b.max.x=std::max(b.max.x,p.x); b.max.y=std::max(b.max.y,p.y); b.max.z=std::max(b.max.z,p.z);
}

[[nodiscard]] bool ray_aabb_interval(const Ray& ray, const Aabb& box, float& enter, float& exit) noexcept {
    enter=ray.t_min; exit=ray.t_max;
    const float o[3]{ray.origin.x,ray.origin.y,ray.origin.z};
    const float d[3]{ray.direction.x,ray.direction.y,ray.direction.z};
    const float mn[3]{box.min.x,box.min.y,box.min.z};
    const float mx[3]{box.max.x,box.max.y,box.max.z};
    for(int axis=0;axis<3;++axis){
        if(std::fabs(d[axis])<kEpsilon){ if(o[axis]<mn[axis]||o[axis]>mx[axis]) return false; continue; }
        const float inv=1.0F/d[axis];
        float a=(mn[axis]-o[axis])*inv, b=(mx[axis]-o[axis])*inv;
        if(a>b)std::swap(a,b);
        enter=std::max(enter,a); exit=std::min(exit,b);
        if(exit<enter)return false;
    }
    return true;
}

[[nodiscard]] bool triangle_hit(const Ray& ray, Vec3 v0, Vec3 v1, Vec3 v2, float& t, Vec3& normal) noexcept {
    const Vec3 e1=sub(v1,v0), e2=sub(v2,v0);
    const Vec3 p=cross(ray.direction,e2);
    const float det=dot(e1,p);
    if(std::fabs(det)<1.0e-8F)return false;
    const float inv=1.0F/det;
    const Vec3 s=sub(ray.origin,v0);
    const float u=dot(s,p)*inv;
    if(u<0.0F||u>1.0F)return false;
    const Vec3 q=cross(s,e1);
    const float v=dot(ray.direction,q)*inv;
    if(v<0.0F||u+v>1.0F)return false;
    const float hit_t=dot(e2,q)*inv;
    if(hit_t<ray.t_min||hit_t>ray.t_max)return false;
    t=hit_t; normal=normalize(cross(e1,e2)); return true;
}

}

bool GeometryKernel::register_provider(GeometryProviderDescriptor descriptor, GeometryProviderOps ops, std::string& error) {
    error.clear();
    if(descriptor.id==0){error="geometry provider id 0 is reserved";return false;}
    if(descriptor.name.empty()){error="geometry provider name is empty";return false;}
    if(!ops.context||!ops.valid||!ops.storage_bytes){error="geometry provider is missing required lifecycle callbacks";return false;}
    if(descriptor.capabilities.contains(GeometryCapability::Bounds)&&!ops.bounds){error="bounds capability requires bounds callback";return false;}
    if(descriptor.capabilities.contains(GeometryCapability::RaySurface)&&!ops.raycast){error="ray capability requires ray callback";return false;}
    if((descriptor.capabilities.contains(GeometryCapability::SignedDistance)||descriptor.capabilities.contains(GeometryCapability::TruncatedSignedDistance))&&!ops.signed_distance){error="distance capability requires distance callback";return false;}
    if(providers_.size()<=descriptor.id)providers_.resize(static_cast<std::size_t>(descriptor.id)+1U);
    auto& slot=providers_[descriptor.id];
    if(slot.registered){error="geometry provider id already registered";return false;}
    slot.registered=true;slot.descriptor=std::move(descriptor);slot.ops=ops;return true;
}

bool GeometryKernel::valid(GeometryHandle h) const noexcept {
    if(!h.valid()||h.provider>=providers_.size())return false;
    const auto& p=providers_[h.provider];
    return p.registered&&p.ops.valid&&p.ops.valid(p.ops.context.get(),h.resource,h.generation);
}
GeometryCapabilityMask GeometryKernel::capabilities(GeometryHandle h) const noexcept {
    if(!valid(h))return {};
    return providers_[h.provider].descriptor.capabilities;
}
std::string_view GeometryKernel::provider_name(GeometryProviderId id) const noexcept {
    if(id>=providers_.size()||!providers_[id].registered)return {};
    return providers_[id].descriptor.name;
}
GeometryBoundsResult GeometryKernel::bounds(GeometryHandle h) const noexcept {
    if(!valid(h))return {GeometryQueryStatus::InvalidHandle,{}};
    const auto& p=providers_[h.provider];
    if(!p.descriptor.capabilities.contains(GeometryCapability::Bounds)||!p.ops.bounds)return {GeometryQueryStatus::UnsupportedCapability,{}};
    return p.ops.bounds(p.ops.context.get(),h.resource,h.generation);
}
GeometryRayHit GeometryKernel::raycast(GeometryHandle h,const Ray& ray) const noexcept {
    if(!valid(h))return {GeometryQueryStatus::InvalidHandle};
    const auto& p=providers_[h.provider];
    if(!p.descriptor.capabilities.contains(GeometryCapability::RaySurface)||!p.ops.raycast)return {GeometryQueryStatus::UnsupportedCapability};
    return p.ops.raycast(p.ops.context.get(),h.resource,h.generation,ray);
}
GeometryDistanceSample GeometryKernel::signed_distance(GeometryHandle h,Vec3 point) const noexcept {
    if(!valid(h))return {GeometryQueryStatus::InvalidHandle};
    const auto& p=providers_[h.provider];
    if(!p.descriptor.capabilities.contains(GeometryCapability::SignedDistance)||!p.ops.signed_distance)return {GeometryQueryStatus::UnsupportedCapability};
    return p.ops.signed_distance(p.ops.context.get(),h.resource,h.generation,point);
}
GeometryDistanceSample GeometryKernel::truncated_signed_distance(GeometryHandle h,Vec3 point) const noexcept {
    if(!valid(h))return {GeometryQueryStatus::InvalidHandle};
    const auto& p=providers_[h.provider];
    const bool supported=p.descriptor.capabilities.contains(GeometryCapability::SignedDistance)||p.descriptor.capabilities.contains(GeometryCapability::TruncatedSignedDistance);
    if(!supported||!p.ops.signed_distance)return {GeometryQueryStatus::UnsupportedCapability};
    return p.ops.signed_distance(p.ops.context.get(),h.resource,h.generation,point);
}
std::size_t GeometryKernel::storage_bytes(GeometryHandle h) const noexcept {
    if(!valid(h)) return 0;
    const auto& p=providers_[h.provider];
    return p.ops.storage_bytes(p.ops.context.get(),h.resource,h.generation);
}

void GeometrySet::source_changed() noexcept {
    if(revision_!=std::numeric_limits<std::uint64_t>::max())++revision_;
}
bool GeometrySet::add_representation(GeometryHandle handle,float max_error,std::string& error) {
    error.clear();
    if(!handle.valid()){error="representation handle is invalid";return false;}
    if(!std::isfinite(max_error)||max_error<0.0F){error="representation error bound must be finite and non-negative";return false;}
    representations_.push_back({handle,revision_,max_error});return true;
}
std::vector<GeometryRepresentation> GeometrySet::eligible(const GeometryKernel& kernel,GeometryCapabilityMask required,float max_error) const {
    std::vector<GeometryRepresentation> out;
    if(!std::isfinite(max_error)||max_error<0.0F)return out;
    for(const auto& rep:representations_){
        if(rep.source_revision!=revision_||rep.max_geometric_error>max_error||!kernel.valid(rep.handle))continue;
        if(!kernel.capabilities(rep.handle).contains_all(required))continue;
        out.push_back(rep);
    }
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){
        if(a.max_geometric_error!=b.max_geometric_error)return a.max_geometric_error<b.max_geometric_error;
        if(a.handle.provider!=b.handle.provider)return a.handle.provider<b.handle.provider;
        return a.handle.resource<b.handle.resource;
    });
    return out;
}

bool TriangleReferenceProvider::register_with(GeometryKernel& kernel,std::string& error){
    GeometryProviderDescriptor d{.id=id_,.name="triangle-reference",.capabilities=GeometryCapabilityMask::of({GeometryCapability::Bounds,GeometryCapability::RaySurface})};
    GeometryProviderOps ops{.context=state_,.valid=&valid_cb,.bounds=&bounds_cb,.raycast=&raycast_cb,.signed_distance=&distance_cb,.storage_bytes=&storage_cb};
    return kernel.register_provider(std::move(d),ops,error);
}
GeometryHandle TriangleReferenceProvider::add_mesh(std::span<const Vec3> vertices,std::span<const std::uint32_t> indices,std::string& error){
    error.clear();
    if(vertices.size()<3||indices.empty()||indices.size()%3U!=0U){error="triangle mesh requires vertices and triangle indices";return {};}
    for(auto v:vertices)if(!finite(v)){error="triangle mesh contains non-finite vertex";return {};}
    for(auto i:indices)if(i>=vertices.size()){error="triangle mesh index is out of range";return {};}
    Resource r; r.vertices.assign(vertices.begin(),vertices.end());r.indices.assign(indices.begin(),indices.end());r.bounds=empty_bounds();
    for(auto v:r.vertices)expand(r.bounds,v);
    const auto id=static_cast<GeometryResourceId>(state_->resources.size());state_->resources.push_back(std::move(r));return {id_,state_->resources.back().generation,id};
}
bool TriangleReferenceProvider::valid_cb(const void* ctx,GeometryResourceId id,std::uint16_t gen) noexcept {const auto& state=*static_cast<const State*>(ctx);return id<state.resources.size()&&state.resources[id].generation==gen;}
GeometryBoundsResult TriangleReferenceProvider::bounds_cb(const void* ctx,GeometryResourceId id,std::uint16_t gen) noexcept {if(!valid_cb(ctx,id,gen))return {GeometryQueryStatus::InvalidHandle,{}};const auto& r=static_cast<const State*>(ctx)->resources[id];return {GeometryQueryStatus::Ok,r.bounds};}
GeometryRayHit TriangleReferenceProvider::raycast_cb(const void* ctx,GeometryResourceId id,std::uint16_t gen,const Ray& ray) noexcept {
    if(!valid_cb(ctx,id,gen))return {GeometryQueryStatus::InvalidHandle};
    if(!finite(ray.origin)||!finite(ray.direction)||!std::isfinite(ray.t_min)||std::isnan(ray.t_max)||ray.t_max<ray.t_min)return {GeometryQueryStatus::NumericalFailure};
    const auto& r=static_cast<const State*>(ctx)->resources[id];
    GeometryRayHit best{GeometryQueryStatus::Miss};
    for(std::size_t i=0;i<r.indices.size();i+=3){float t{};Vec3 n{};if(!triangle_hit(ray,r.vertices[r.indices[i]],r.vertices[r.indices[i+1]],r.vertices[r.indices[i+2]],t,n))continue;if(t<best.t){best.status=GeometryQueryStatus::Ok;best.t=t;best.position=add(ray.origin,mul(ray.direction,t));best.normal=n;}}
    return best;
}
GeometryDistanceSample TriangleReferenceProvider::distance_cb(const void*,GeometryResourceId,std::uint16_t,Vec3) noexcept {return {GeometryQueryStatus::UnsupportedCapability};}
std::size_t TriangleReferenceProvider::storage_cb(const void* ctx,GeometryResourceId id,std::uint16_t gen) noexcept {if(!valid_cb(ctx,id,gen))return 0;const auto&r=static_cast<const State*>(ctx)->resources[id];return r.vertices.capacity()*sizeof(Vec3)+r.indices.capacity()*sizeof(std::uint32_t);}

float AnalyticSdfProvider::distance_value(const Resource& r, Vec3 p) noexcept {
    const Vec3 local=sub(p,r.center);
    if(r.shape==Shape::Sphere)return length(local)-r.extent.x;
    const Vec3 q=sub(abs_vec(local),r.extent);
    const Vec3 outside=max_vec(q,0.0F);
    const float outside_len=length(outside);
    const float inside=std::min(std::max(q.x,std::max(q.y,q.z)),0.0F);
    return outside_len+inside;
}

Vec3 AnalyticSdfProvider::gradient_value(const Resource& r, Vec3 p) noexcept {
    const float scale=std::max(1.0F,std::max(r.extent.x,std::max(r.extent.y,r.extent.z)));
    const float h=1.0e-4F*scale;
    const Vec3 dx{h,0,0},dy{0,h,0},dz{0,0,h};
    return normalize({
        distance_value(r,add(p,dx))-distance_value(r,sub(p,dx)),
        distance_value(r,add(p,dy))-distance_value(r,sub(p,dy)),
        distance_value(r,add(p,dz))-distance_value(r,sub(p,dz))});
}

bool AnalyticSdfProvider::register_with(GeometryKernel& kernel,std::string& error){
    GeometryProviderDescriptor d{.id=id_,.name="analytic-sdf",.capabilities=GeometryCapabilityMask::of({GeometryCapability::Bounds,GeometryCapability::RaySurface,GeometryCapability::SignedDistance,GeometryCapability::TruncatedSignedDistance})};
    GeometryProviderOps ops{.context=state_,.valid=&valid_cb,.bounds=&bounds_cb,.raycast=&raycast_cb,.signed_distance=&distance_cb,.storage_bytes=&storage_cb};
    return kernel.register_provider(std::move(d),ops,error);
}
GeometryHandle AnalyticSdfProvider::add_sphere(Vec3 center,float radius,std::string& error){
    error.clear();if(!finite(center)||!std::isfinite(radius)||radius<=0){error="SDF sphere requires finite positive radius";return {};}
    Resource r; r.shape=Shape::Sphere;r.center=center;r.extent={radius,0,0};r.bounds={sub(center,{radius,radius,radius}),add(center,{radius,radius,radius})};
    const auto id=static_cast<GeometryResourceId>(state_->resources.size());state_->resources.push_back(r);return{id_,state_->resources.back().generation,id};
}
GeometryHandle AnalyticSdfProvider::add_box(Vec3 center,Vec3 half,std::string& error){
    error.clear();if(!finite(center)||!finite(half)||half.x<=0||half.y<=0||half.z<=0){error="SDF box requires finite positive half extents";return {};}
    Resource r;r.shape=Shape::Box;r.center=center;r.extent=half;r.bounds={sub(center,half),add(center,half)};
    const auto id=static_cast<GeometryResourceId>(state_->resources.size());state_->resources.push_back(r);return{id_,state_->resources.back().generation,id};
}
bool AnalyticSdfProvider::valid_cb(const void*ctx,GeometryResourceId id,std::uint16_t gen) noexcept {const auto&state=*static_cast<const State*>(ctx);return id<state.resources.size()&&state.resources[id].generation==gen;}
GeometryBoundsResult AnalyticSdfProvider::bounds_cb(const void*ctx,GeometryResourceId id,std::uint16_t gen) noexcept {if(!valid_cb(ctx,id,gen))return{GeometryQueryStatus::InvalidHandle,{}};return{GeometryQueryStatus::Ok,static_cast<const State*>(ctx)->resources[id].bounds};}
GeometryDistanceSample AnalyticSdfProvider::distance_cb(const void*ctx,GeometryResourceId id,std::uint16_t gen,Vec3 p) noexcept {if(!valid_cb(ctx,id,gen))return{GeometryQueryStatus::InvalidHandle};if(!finite(p))return{GeometryQueryStatus::NumericalFailure};const auto&r=static_cast<const State*>(ctx)->resources[id];return{GeometryQueryStatus::Ok,distance_value(r,p),gradient_value(r,p)};}
GeometryRayHit AnalyticSdfProvider::raycast_cb(const void*ctx,GeometryResourceId id,std::uint16_t gen,const Ray&ray) noexcept {
    if(!valid_cb(ctx,id,gen))return{GeometryQueryStatus::InvalidHandle};
    if(!finite(ray.origin)||!finite(ray.direction)||!std::isfinite(ray.t_min)||std::isnan(ray.t_max)||ray.t_max<ray.t_min)return{GeometryQueryStatus::NumericalFailure};
    const auto&r=static_cast<const State*>(ctx)->resources[id];
    const float dir_len=length(ray.direction);if(dir_len<=kEpsilon)return{GeometryQueryStatus::NumericalFailure};
    float enter{},exit{};if(!ray_aabb_interval(ray,r.bounds,enter,exit))return{GeometryQueryStatus::Miss};
    float t=std::max(ray.t_min,enter);const float end=std::min(ray.t_max,exit);const float eps=1.0e-4F*std::max(1.0F,std::max(r.extent.x,std::max(r.extent.y,r.extent.z)));
    for(int step=0;step<256&&t<=end+eps;++step){const Vec3 p=add(ray.origin,mul(ray.direction,t));const float d=distance_value(r,p);if(!std::isfinite(d))return{GeometryQueryStatus::NumericalFailure};if(std::fabs(d)<=eps)return{GeometryQueryStatus::Ok,t,p,gradient_value(r,p)};const float dt=std::max(std::fabs(d)/dir_len,eps/(2.0F*dir_len));t+=dt;}
    return{GeometryQueryStatus::Miss};
}
std::size_t AnalyticSdfProvider::storage_cb(const void*ctx,GeometryResourceId id,std::uint16_t gen) noexcept {return valid_cb(ctx,id,gen)?sizeof(Resource):0;}

} // namespace aion
