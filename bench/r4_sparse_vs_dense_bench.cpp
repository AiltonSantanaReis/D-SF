#include "aion/kernel/geometry.hpp"
#include "aion/kernel/sparse_sdf.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
[[nodiscard]] double ms(Clock::duration d){return std::chrono::duration<double,std::milli>(d).count();}
struct DenseGrid{aion::Vec3 origin{};std::uint32_t nx{},ny{},nz{};float h{},band{},scale{};std::vector<std::int16_t> q;};
[[nodiscard]] aion::Vec3 sub(aion::Vec3 a,aion::Vec3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
[[nodiscard]] std::int16_t quant(float v,float band){const float n=std::clamp(v/band,-1.0F,1.0F);return static_cast<std::int16_t>(std::lround(n*32767.0F));}
[[nodiscard]] std::size_t idx(const DenseGrid&g,std::uint32_t x,std::uint32_t y,std::uint32_t z){return static_cast<std::size_t>(x)+(static_cast<std::size_t>(g.nx)+1U)*(static_cast<std::size_t>(y)+(static_cast<std::size_t>(g.ny)+1U)*z);}
[[nodiscard]] float dense_sample(const DenseGrid&g,aion::Vec3 p){
    const float x=(p.x-g.origin.x)/g.h,y=(p.y-g.origin.y)/g.h,z=(p.z-g.origin.z)/g.h;if(x<0||y<0||z<0||x>=g.nx||y>=g.ny||z>=g.nz)return g.band;
    const auto ix=static_cast<std::uint32_t>(std::floor(x)),iy=static_cast<std::uint32_t>(std::floor(y)),iz=static_cast<std::uint32_t>(std::floor(z));const float fx=x-ix,fy=y-iy,fz=z-iz;
    const auto v=[&](std::uint32_t dx,std::uint32_t dy,std::uint32_t dz){return static_cast<float>(g.q[idx(g,ix+dx,iy+dy,iz+dz)])*g.scale;};const auto l=[](float a,float b,float t){return a+(b-a)*t;};
    return l(l(l(v(0,0,0),v(1,0,0),fx),l(v(0,1,0),v(1,1,0),fx),fy),l(l(v(0,0,1),v(1,0,1),fx),l(v(0,1,1),v(1,1,1),fx),fy),fz);
}
}
int main(){
    aion::GeometryKernel kernel;aion::AnalyticSdfProvider analytic(2);aion::SparseSdfProvider sparse(3);std::string error;if(!analytic.register_with(kernel,error)||!sparse.register_with(kernel,error)){std::cerr<<error;return EXIT_FAILURE;}const auto sphere=analytic.add_sphere({0,0,0},1,error);const auto bounds=kernel.bounds(sphere).bounds;
    std::cout<<std::fixed<<std::setprecision(4);
    for(const int div:{32,64,128}){
        const float h=1.0F/div,band=3.0F*h,error_bound=0.5F*1.7320508075688772F*h+0.5F*(band/32767.0F),pad=band+error_bound;DenseGrid dense;dense.h=h;dense.band=band;dense.scale=band/32767.0F;dense.origin=sub(bounds.min,{pad,pad,pad});dense.nx=static_cast<std::uint32_t>(std::ceil((bounds.max.x-bounds.min.x+2*pad)/h));dense.ny=dense.nx;dense.nz=dense.nx;
        const std::size_t count=(static_cast<std::size_t>(dense.nx)+1U)*(dense.ny+1U)*(dense.nz+1U);const auto d0=Clock::now();dense.q.resize(count);for(std::uint32_t z=0;z<=dense.nz;++z)for(std::uint32_t y=0;y<=dense.ny;++y)for(std::uint32_t x=0;x<=dense.nx;++x){const aion::Vec3 p{dense.origin.x+x*h,dense.origin.y+y*h,dense.origin.z+z*h};dense.q[idx(dense,x,y,z)]=quant(kernel.signed_distance(sphere,p).signed_distance,band);}const auto d1=Clock::now();
        const aion::SparseSdfCompileOptions opt{.voxel_size=h,.half_band_voxels=3,.source={0,1}};const auto s0=Clock::now();const auto sp=sparse.compile_from(kernel,sphere,opt);const auto s1=Clock::now();if(!sp.ok()){std::cerr<<sp.error;return EXIT_FAILURE;}
        std::mt19937 rng(42U+div);std::uniform_real_distribution<float> dist(-1.25F,1.25F);std::vector<aion::Vec3> points;points.reserve(200000);for(int i=0;i<200000;++i)points.push_back({dist(rng),dist(rng),dist(rng)});
        volatile float sink=0;const auto qd0=Clock::now();for(const auto p:points)sink+=dense_sample(dense,p);const auto qd1=Clock::now();const auto qs0=Clock::now();for(const auto p:points)sink+=kernel.truncated_signed_distance(sp.handle,p).signed_distance;const auto qs1=Clock::now();
        std::cout<<"div="<<div<<" dense_build_ms="<<ms(d1-d0)<<" sparse_build_ms="<<ms(s1-s0)<<" dense_bytes="<<dense.q.capacity()*sizeof(std::int16_t)<<" sparse_bytes="<<sp.stats.storage_bytes<<" dense_query200k_ms="<<ms(qd1-qd0)<<" sparse_query200k_ms="<<ms(qs1-qs0)<<" query_ratio="<<(ms(qs1-qs0)/ms(qd1-qd0))<<" sink="<<sink<<'\n';
    }
    return EXIT_SUCCESS;
}
