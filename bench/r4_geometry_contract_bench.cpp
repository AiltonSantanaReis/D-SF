#include "aion/kernel/geometry.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
template<class F> double ms(F&&fn){const auto a=Clock::now();fn();const auto b=Clock::now();return std::chrono::duration<double,std::milli>(b-a).count();}
std::vector<aion::Vec3> cube_vertices(){return{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};}
std::vector<std::uint32_t> cube_indices(){return{0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};}
}
int main(int argc,char**argv){
    const std::size_t rays=argc>1?static_cast<std::size_t>(std::stoull(argv[1])):200000;
    std::string error;aion::GeometryKernel kernel;aion::TriangleReferenceProvider tri;aion::AnalyticSdfProvider sdf;
    if(!tri.register_with(kernel,error)||!sdf.register_with(kernel,error)){std::cerr<<error<<'\n';return 2;}
    const auto v=cube_vertices(); const auto i=cube_indices(); const auto mesh=tri.add_mesh(v,i,error);const auto box=sdf.add_box({0,0,0},{1,1,1},error);
    std::vector<aion::Ray> batch;batch.reserve(rays);
    for(std::size_t n=0;n<rays;++n){const float y=-1.5F+3.0F*static_cast<float>((n*7919U)%10000U)/9999.0F;const float z=-1.5F+3.0F*static_cast<float>((n*3571U)%10000U)/9999.0F;batch.push_back({{-3,y,z},{1,0,0},0,10});}
    std::size_t tri_hits=0,sdf_hits=0;double tri_ms=ms([&]{for(const auto&r:batch)if(kernel.raycast(mesh,r).status==aion::GeometryQueryStatus::Ok)++tri_hits;});
    double sdf_ms=ms([&]{for(const auto&r:batch)if(kernel.raycast(box,r).status==aion::GeometryQueryStatus::Ok)++sdf_hits;});
    std::cout<<std::fixed<<std::setprecision(3)<<"R4A_REFERENCE rays="<<rays<<" triangle_ms="<<tri_ms<<" sdf_ms="<<sdf_ms
             <<" triangle_hits="<<tri_hits<<" sdf_hits="<<sdf_hits<<" exact_hit_count="<<(tri_hits==sdf_hits?1:0)<<'\n';
    return tri_hits==sdf_hits?0:3;
}
