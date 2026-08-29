#include "aion/kernel/geometry.hpp"
#include "aion/kernel/sparse_sdf.hpp"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

int main(){
    using Clock=std::chrono::steady_clock;
    aion::GeometryKernel kernel;aion::AnalyticSdfProvider analytic(2);aion::SparseSdfProvider sparse(3);std::string error;
    if(!analytic.register_with(kernel,error)||!sparse.register_with(kernel,error)){std::cerr<<error<<'\n';return EXIT_FAILURE;}
    struct Shape{const char* name; aion::GeometryHandle handle;};
    const Shape shapes[]={{"sphere",analytic.add_sphere({0,0,0},1.0F,error)},{"box",analytic.add_box({0,0,0},{1.0F,0.75F,0.5F},error)}};
    std::cout<<std::fixed<<std::setprecision(4);
    for(const auto& shape:shapes){
        for(const int div:{32,64,128}){
            aion::SparseSdfCompileOptions opt{.voxel_size=1.0F/static_cast<float>(div),.half_band_voxels=3.0F,.source={0.0F,1.0F}};
            const auto t0=Clock::now();const auto b=sparse.compile_from(kernel,shape.handle,opt);const auto t1=Clock::now();if(!b.ok()){std::cerr<<b.error<<'\n';return EXIT_FAILURE;}
            const double build=std::chrono::duration<double,std::milli>(t1-t0).count();
            std::cout<<"shape="<<shape.name<<" div="<<div<<" build_ms="<<build<<" leaves="<<b.stats.leaf_bricks<<" lower="<<b.stats.lower_nodes<<" upper="<<b.stats.upper_nodes<<" sparse="<<b.stats.storage_bytes<<" dense_f32="<<b.stats.dense_equivalent_bytes<<" dense_i16="<<b.stats.dense_equivalent_quantized_bytes<<" ratio_f32="<<(static_cast<double>(b.stats.storage_bytes)/static_cast<double>(b.stats.dense_equivalent_bytes))<<" ratio_i16="<<(static_cast<double>(b.stats.storage_bytes)/static_cast<double>(b.stats.dense_equivalent_quantized_bytes))<<" source_ratio="<<(static_cast<double>(b.stats.source_samples)/static_cast<double>(b.stats.dense_equivalent_samples))<<'\n';
        }
    }
    return EXIT_SUCCESS;
}
