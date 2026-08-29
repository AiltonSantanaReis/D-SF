#include "aion/kernel/geometry.hpp"
#include "aion/kernel/sparse_sdf.hpp"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

int main(){
    using Clock=std::chrono::steady_clock;
    aion::GeometryKernel kernel;aion::AnalyticSdfProvider analytic(2);aion::SparseSdfProvider sparse(3);std::string error;
    if(!analytic.register_with(kernel,error)||!sparse.register_with(kernel,error)){std::cerr<<error<<'\n';return EXIT_FAILURE;}
    std::cout<<std::fixed<<std::setprecision(4);
    for(const float radius:{1.0F,2.0F,4.0F}){
        const auto sphere=analytic.add_sphere({0,0,0},radius,error);
        const aion::SparseSdfCompileOptions opt{.voxel_size=1.0F/32.0F,.half_band_voxels=3.0F,.source={0.0F,1.0F}};
        const auto t0=Clock::now();const auto b=sparse.compile_from(kernel,sphere,opt);const auto t1=Clock::now();if(!b.ok()){std::cerr<<b.error<<'\n';return EXIT_FAILURE;}
        std::cout<<"radius="<<radius<<" build_ms="<<std::chrono::duration<double,std::milli>(t1-t0).count()<<" leaves="<<b.stats.leaf_bricks<<" sparse="<<b.stats.storage_bytes<<" dense_f32="<<b.stats.dense_equivalent_bytes<<" dense_i16="<<b.stats.dense_equivalent_quantized_bytes<<" ratio_f32="<<(static_cast<double>(b.stats.storage_bytes)/static_cast<double>(b.stats.dense_equivalent_bytes))<<" ratio_i16="<<(static_cast<double>(b.stats.storage_bytes)/static_cast<double>(b.stats.dense_equivalent_quantized_bytes))<<" source_samples="<<b.stats.source_samples<<" dense_samples="<<b.stats.dense_equivalent_samples<<'\n';
    }
    return EXIT_SUCCESS;
}
