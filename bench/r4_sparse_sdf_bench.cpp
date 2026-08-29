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
struct Row {int div{};double build_ms{};aion::SparseSdfStats stats{};float observed_field_error{};double sample_ms{};double ray_ms{};std::size_t ray_hits{};};
}

int main(int argc,char**argv){
    const int repeats=argc>1?std::max(1,std::atoi(argv[1])):1;
    aion::GeometryKernel kernel;aion::AnalyticSdfProvider analytic(2);aion::SparseSdfProvider sparse(3);std::string error;
    if(!analytic.register_with(kernel,error)||!sparse.register_with(kernel,error)){std::cerr<<error<<'\n';return EXIT_FAILURE;}
    const auto sphere=analytic.add_sphere({0,0,0},1.0F,error);if(!sphere.valid()){std::cerr<<error<<'\n';return EXIT_FAILURE;}
    const std::vector<int> divs={32,64,128};
    std::cout<<std::fixed<<std::setprecision(4);
    for(const int div:divs){
        Row best{};best.div=div;best.build_ms=1.0e30;
        for(int rep=0;rep<repeats;++rep){
            const aion::SparseSdfCompileOptions opt{.voxel_size=1.0F/static_cast<float>(div),.half_band_voxels=3.0F,.source={0.0F,1.0F}};
            const auto t0=Clock::now();const auto built=sparse.compile_from(kernel,sphere,opt);const auto t1=Clock::now();
            if(!built.ok()){std::cerr<<built.error<<'\n';return EXIT_FAILURE;}
            const double build=ms(t1-t0);
            if(build<best.build_ms){best.build_ms=build;best.stats=built.stats;}
            std::mt19937 rng(12345U+static_cast<std::uint32_t>(div));std::uniform_real_distribution<float> dist(-1.25F,1.25F);
            float max_err=0.0F;const auto s0=Clock::now();
            for(std::size_t i=0;i<200000;++i){const aion::Vec3 p{dist(rng),dist(rng),dist(rng)};const auto ex=kernel.signed_distance(sphere,p);const auto ap=kernel.truncated_signed_distance(built.handle,p);const float target=std::clamp(ex.signed_distance,-built.stats.band_distance,built.stats.band_distance);max_err=std::max(max_err,std::fabs(ap.signed_distance-target));}
            const auto s1=Clock::now();
            std::size_t hits=0;const auto r0=Clock::now();
            for(std::size_t i=0;i<100000;++i){const float y=dist(rng),z=dist(rng);const auto h=kernel.raycast(built.handle,{{-2.0F,y,z},{1,0,0},0,5});if(h.status==aion::GeometryQueryStatus::Ok)++hits;}
            const auto r1=Clock::now();
            if(rep==0){best.observed_field_error=max_err;best.sample_ms=ms(s1-s0);best.ray_ms=ms(r1-r0);best.ray_hits=hits;}
        }
        const double ratio=best.stats.dense_equivalent_bytes?static_cast<double>(best.stats.storage_bytes)/static_cast<double>(best.stats.dense_equivalent_bytes):0.0;
        std::cout<<"div="<<best.div
                 <<" voxel="<<best.stats.voxel_size
                 <<" build_ms="<<best.build_ms
                 <<" roots="<<best.stats.root_entries
                 <<" upper="<<best.stats.upper_nodes
                 <<" lower="<<best.stats.lower_nodes
                 <<" leaves="<<best.stats.leaf_bricks
                 <<" sparse_bytes="<<best.stats.storage_bytes
                 <<" dense_f32_bytes="<<best.stats.dense_equivalent_bytes
                 <<" dense_i16_bytes="<<best.stats.dense_equivalent_quantized_bytes
                 <<" ratio_f32="<<ratio
                 <<" ratio_i16="<<(static_cast<double>(best.stats.storage_bytes)/static_cast<double>(best.stats.dense_equivalent_quantized_bytes))
                 <<" topology_bytes="<<best.stats.topology_bytes
                 <<" payload_bytes="<<best.stats.sample_payload_bytes
                 <<" source_samples="<<best.stats.source_samples
                 <<" dense_samples="<<best.stats.dense_equivalent_samples
                 <<" observed_error="<<best.observed_field_error
                 <<" declared_error="<<best.stats.max_geometric_error
                 <<" sample200k_ms="<<best.sample_ms
                 <<" ray100k_ms="<<best.ray_ms
                 <<" ray_hits="<<best.ray_hits<<'\n';
    }
    return EXIT_SUCCESS;
}
