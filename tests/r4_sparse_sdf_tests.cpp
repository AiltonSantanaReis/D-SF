#include "aion/kernel/geometry.hpp"
#include "aion/kernel/sparse_sdf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while(false)

namespace {
[[nodiscard]] bool near(float a,float b,float eps) noexcept { return std::fabs(a-b)<=eps; }
}

int main(){
    aion::GeometryKernel kernel;
    aion::AnalyticSdfProvider analytic(2);
    aion::SparseSdfProvider sparse(3);
    aion::TriangleReferenceProvider triangle(1);
    std::string error;
    CHECK(triangle.register_with(kernel,error));
    CHECK(analytic.register_with(kernel,error));
    CHECK(sparse.register_with(kernel,error));

    const auto sphere=analytic.add_sphere({0,0,0},1.0F,error);
    CHECK(sphere.valid());
    const aion::SparseSdfCompileOptions options{
        .voxel_size=1.0F/32.0F,
        .half_band_voxels=3.0F,
        .source={.max_distance_error=0.0F,.lipschitz_bound=1.0F}
    };
    const auto built=sparse.compile_from(kernel,sphere,options);
    CHECK(built.ok());
    CHECK(kernel.valid(built.handle));
    CHECK(kernel.capabilities(built.handle).contains(aion::GeometryCapability::TruncatedSignedDistance));
    CHECK(!kernel.capabilities(built.handle).contains(aion::GeometryCapability::SignedDistance));
    CHECK(kernel.signed_distance(built.handle,{0,0,0}).status==aion::GeometryQueryStatus::UnsupportedCapability);
    CHECK(kernel.truncated_signed_distance(built.handle,{0,0,0}).status==aion::GeometryQueryStatus::Ok);

    CHECK(built.stats.leaf_bricks>0);
    CHECK(built.stats.quantized_samples==built.stats.leaf_bricks*729U);
    CHECK(built.stats.storage_bytes>0);
    CHECK(built.stats.dense_equivalent_bytes>built.stats.storage_bytes);
    CHECK(built.stats.source_samples<built.stats.dense_equivalent_samples);
    CHECK(near(built.max_geometric_error,built.stats.max_geometric_error,1.0e-7F));

    // Truncated field must remain inside its conservative approximation certificate.
    float observed_max_error=0.0F;
    std::size_t samples=0;
    for(int zi=-38;zi<=38;++zi){
        for(int yi=-38;yi<=38;++yi){
            for(int xi=-38;xi<=38;++xi){
                if((xi+2*yi+3*zi)%7!=0)continue;
                const aion::Vec3 p{static_cast<float>(xi)/32.0F,static_cast<float>(yi)/32.0F,static_cast<float>(zi)/32.0F};
                const auto exact=kernel.signed_distance(sphere,p);
                const auto approx=kernel.truncated_signed_distance(built.handle,p);
                CHECK(exact.status==aion::GeometryQueryStatus::Ok);
                CHECK(approx.status==aion::GeometryQueryStatus::Ok);
                const float target=std::clamp(exact.signed_distance,-built.stats.band_distance,built.stats.band_distance);
                const float e=std::fabs(approx.signed_distance-target);
                observed_max_error=std::max(observed_max_error,e);
                CHECK(e<=built.max_geometric_error+1.0e-5F);
                ++samples;
            }
        }
    }
    CHECK(samples>60000);

    // Surface rays compare against the exact analytic source under the declared geometry error.
    std::size_t hit_pairs=0;
    float max_t_error=0.0F;
    for(int yi=-36;yi<=36;++yi){
        for(int zi=-36;zi<=36;++zi){
            const float y=static_cast<float>(yi)/32.0F;
            const float z=static_cast<float>(zi)/32.0F;
            const float radial=std::sqrt(y*y+z*z);
            if(std::fabs(radial-1.0F)<=2.0F*built.max_geometric_error)continue; // silhouette is ambiguous by contract
            const aion::Ray ray{{-2.0F,y,z},{1,0,0},0,5};
            const auto exact=kernel.raycast(sphere,ray);
            const auto approx=kernel.raycast(built.handle,ray);
            CHECK((exact.status==aion::GeometryQueryStatus::Ok)==(approx.status==aion::GeometryQueryStatus::Ok));
            if(exact.status==aion::GeometryQueryStatus::Ok){
                const float e=std::fabs(exact.t-approx.t);
                max_t_error=std::max(max_t_error,e);
                CHECK(e<=2.0F*built.max_geometric_error+options.voxel_size);
                ++hit_pairs;
            }
        }
    }
    CHECK(hit_pairs>2000);

    // Non-smooth exact SDF: box edges/corners must also respect the certificate.
    const auto exact_box=analytic.add_box({0,0,0},{1.0F,0.75F,0.5F},error);
    CHECK(exact_box.valid());
    const auto sparse_box=sparse.compile_from(kernel,exact_box,options);
    CHECK(sparse_box.ok());
    float box_max_error=0.0F;
    for(int zi=-24;zi<=24;++zi){for(int yi=-32;yi<=32;++yi){for(int xi=-40;xi<=40;++xi){
        if((xi+yi+zi)%5!=0)continue;
        const aion::Vec3 p{static_cast<float>(xi)/32.0F,static_cast<float>(yi)/32.0F,static_cast<float>(zi)/32.0F};
        const auto ex=kernel.signed_distance(exact_box,p);const auto ap=kernel.truncated_signed_distance(sparse_box.handle,p);
        CHECK(ex.status==aion::GeometryQueryStatus::Ok&&ap.status==aion::GeometryQueryStatus::Ok);
        const float target=std::clamp(ex.signed_distance,-sparse_box.stats.band_distance,sparse_box.stats.band_distance);
        box_max_error=std::max(box_max_error,std::fabs(ap.signed_distance-target));
        CHECK(std::fabs(ap.signed_distance-target)<=sparse_box.max_geometric_error+1.0e-5F);
    }}}
    CHECK(sparse_box.stats.storage_bytes>0&&sparse_box.stats.dense_equivalent_bytes>0);

    // GeometrySet distinguishes exact signed distance from truncated sparse distance.
    aion::GeometrySet set;
    CHECK(set.add_representation(sphere,0.0F,error));
    CHECK(set.add_representation(built.handle,built.max_geometric_error,error));
    const auto exact_candidates=set.eligible(kernel,aion::GeometryCapabilityMask::of({aion::GeometryCapability::SignedDistance}),0.0F);
    CHECK(exact_candidates.size()==1&&exact_candidates.front().handle==sphere);
    const auto sparse_candidates=set.eligible(kernel,aion::GeometryCapabilityMask::of({aion::GeometryCapability::TruncatedSignedDistance}),built.max_geometric_error+1.0e-5F);
    CHECK(sparse_candidates.size()==2);

    // Multiple compiled resolutions coexist; error budget filters representations without naming a format.
    auto fine_options=options;fine_options.voxel_size=1.0F/64.0F;
    const auto fine=sparse.compile_from(kernel,sphere,fine_options);
    CHECK(fine.ok());
    CHECK(fine.max_geometric_error<built.max_geometric_error);
    aion::GeometrySet lod_set;
    CHECK(lod_set.add_representation(sphere,0.0F,error));
    CHECK(lod_set.add_representation(built.handle,built.max_geometric_error,error));
    CHECK(lod_set.add_representation(fine.handle,fine.max_geometric_error,error));
    const float mid_budget=0.5F*(built.max_geometric_error+fine.max_geometric_error);
    const auto mid_candidates=lod_set.eligible(kernel,aion::GeometryCapabilityMask::of({aion::GeometryCapability::TruncatedSignedDistance}),mid_budget);
    CHECK(mid_candidates.size()==2);
    CHECK(mid_candidates[0].handle==sphere);
    CHECK(mid_candidates[1].handle==fine.handle);

    // Published resources outlive authoring wrappers; registry/provider state owns runtime lifetime.
    aion::GeometryKernel owned_kernel;
    aion::GeometryHandle owned_sparse;
    {
        aion::AnalyticSdfProvider local_analytic(21);
        aion::SparseSdfProvider local_sparse(22);
        CHECK(local_analytic.register_with(owned_kernel,error));
        CHECK(local_sparse.register_with(owned_kernel,error));
        const auto local_source=local_analytic.add_sphere({0,0,0},1.0F,error);
        const auto local_build=local_sparse.compile_from(owned_kernel,local_source,options);
        CHECK(local_build.ok());owned_sparse=local_build.handle;
    }
    CHECK(owned_kernel.valid(owned_sparse));
    CHECK(owned_kernel.truncated_signed_distance(owned_sparse,{1,0,0}).status==aion::GeometryQueryStatus::Ok);
    CHECK(owned_kernel.raycast(owned_sparse,{{-2,0,0},{1,0,0},0,5}).status==aion::GeometryQueryStatus::Ok);

    // A triangle surface cannot be silently treated as an SDF compiler source.
    const aion::Vec3 vertices[3]={{0,0,0},{1,0,0},{0,1,0}};
    const std::uint32_t indices[3]={0,1,2};
    const auto tri=triangle.add_mesh(vertices,indices,error);
    CHECK(tri.valid());
    const auto rejected=sparse.compile_from(kernel,tri,options);
    CHECK(!rejected.ok());

    auto invalid_options=options;invalid_options.source.lipschitz_bound=0.0F;
    CHECK(!sparse.compile_from(kernel,sphere,invalid_options).ok());

    std::cout<<"r4_sparse_sdf_tests: PASS\n"
             <<"leaf_bricks="<<built.stats.leaf_bricks<<'\n'
             <<"storage_bytes="<<built.stats.storage_bytes<<'\n'
             <<"dense_bytes="<<built.stats.dense_equivalent_bytes<<'\n'
             <<"source_samples="<<built.stats.source_samples<<'\n'
             <<"field_samples="<<samples<<'\n'
             <<"observed_max_field_error="<<observed_max_error<<'\n'
             <<"declared_max_error="<<built.max_geometric_error<<'\n'
             <<"box_observed_max_field_error="<<box_max_error<<'\n'
             <<"box_sparse_bytes="<<sparse_box.stats.storage_bytes<<'\n'
             <<"box_dense_bytes="<<sparse_box.stats.dense_equivalent_bytes<<'\n'
             <<"ray_hit_pairs="<<hit_pairs<<'\n'
             <<"max_t_error="<<max_t_error<<'\n';
    return EXIT_SUCCESS;
}
