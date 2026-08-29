#include "aion/kernel/geometry.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while(false)

namespace {
std::vector<aion::Vec3> cube_vertices() {
    return {
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1, 1},{1,-1, 1},{1,1, 1},{-1,1, 1},
    };
}
std::vector<std::uint32_t> cube_indices() {
    return {
        0,2,1, 0,3,2, // -Z
        4,5,6, 4,6,7, // +Z
        0,1,5, 0,5,4, // -Y
        3,7,6, 3,6,2, // +Y
        0,4,7, 0,7,3, // -X
        1,2,6, 1,6,5, // +X
    };
}
bool near(float a,float b,float eps=2.0e-3F){return std::fabs(a-b)<=eps;}
}

int main(){
    std::string error;
    aion::GeometryKernel kernel;
    aion::TriangleReferenceProvider triangles;
    aion::AnalyticSdfProvider sdf;
    CHECK(triangles.register_with(kernel,error));
    CHECK(sdf.register_with(kernel,error));
    CHECK(kernel.provider_name(1)=="triangle-reference");
    CHECK(kernel.provider_name(2)=="analytic-sdf");

    const auto vertices=cube_vertices();
    const auto indices=cube_indices();
    const auto mesh=triangles.add_mesh(vertices,indices,error);
    const auto box=sdf.add_box({0,0,0},{1,1,1},error);
    CHECK(mesh.valid()&&box.valid());
    CHECK(kernel.valid(mesh)&&kernel.valid(box));

    const auto mb=kernel.bounds(mesh), sb=kernel.bounds(box);
    CHECK(mb.status==aion::GeometryQueryStatus::Ok);
    CHECK(sb.status==aion::GeometryQueryStatus::Ok);
    CHECK(mb.bounds==sb.bounds);
    CHECK((mb.bounds.min==aion::Vec3{-1,-1,-1}));
    CHECK((mb.bounds.max==aion::Vec3{1,1,1}));

    // Same semantic cube, radically different representation. Surface-ray behavior must agree.
    std::size_t compared=0;
    for(int yi=-15;yi<=15;++yi){
        for(int zi=-15;zi<=15;++zi){
            const float y=static_cast<float>(yi)*0.1F;
            const float z=static_cast<float>(zi)*0.1F;
            const aion::Ray ray{{-3.0F,y,z},{1,0,0},0,10};
            const auto a=kernel.raycast(mesh,ray);
            const auto b=kernel.raycast(box,ray);
            CHECK((a.status==aion::GeometryQueryStatus::Ok)==(b.status==aion::GeometryQueryStatus::Ok));
            if(a.status==aion::GeometryQueryStatus::Ok){CHECK(near(a.t,b.t));++compared;}
        }
    }
    for(int xi=-12;xi<=12;++xi){
        for(int zi=-12;zi<=12;++zi){
            const float x=static_cast<float>(xi)*0.1F;
            const float z=static_cast<float>(zi)*0.1F;
            const aion::Ray ray{{x,-3.0F,z},{0,1,0},0,10};
            const auto a=kernel.raycast(mesh,ray),b=kernel.raycast(box,ray);
            CHECK((a.status==aion::GeometryQueryStatus::Ok)==(b.status==aion::GeometryQueryStatus::Ok));
            if(a.status==aion::GeometryQueryStatus::Ok){CHECK(near(a.t,b.t));++compared;}
        }
    }
    CHECK(compared>800);

    // Rays starting inside and non-normalized directions preserve the same t semantics.
    {
        const aion::Ray inside_ray{{0,0,0},{1,0,0},0,10};
        const auto a=kernel.raycast(mesh,inside_ray), b=kernel.raycast(box,inside_ray);
        CHECK(a.status==aion::GeometryQueryStatus::Ok&&b.status==aion::GeometryQueryStatus::Ok);
        CHECK(near(a.t,1.0F)&&near(a.t,b.t));
        const aion::Ray scaled_ray{{-3,0,0},{2,0,0},0,10};
        const auto c=kernel.raycast(mesh,scaled_ray), d=kernel.raycast(box,scaled_ray);
        CHECK(c.status==aion::GeometryQueryStatus::Ok&&d.status==aion::GeometryQueryStatus::Ok);
        CHECK(near(c.t,1.0F)&&near(c.t,d.t));
    }

    // Capability gating: triangle surface is not forced to invent signed-distance semantics.
    CHECK(kernel.signed_distance(mesh,{2,0,0}).status==aion::GeometryQueryStatus::UnsupportedCapability);
    const auto outside=kernel.signed_distance(box,{2,0,0});
    const auto inside=kernel.signed_distance(box,{0,0,0});
    const auto surface=kernel.signed_distance(box,{1,0.2F,-0.4F});
    CHECK(outside.status==aion::GeometryQueryStatus::Ok&&near(outside.signed_distance,1.0F,1.0e-4F));
    CHECK(inside.status==aion::GeometryQueryStatus::Ok&&near(inside.signed_distance,-1.0F,1.0e-4F));
    CHECK(surface.status==aion::GeometryQueryStatus::Ok&&near(surface.signed_distance,0.0F,1.0e-4F));

    // Invalid generations are rejected at the provider boundary.
    auto stale_handle=box;
    ++stale_handle.generation;
    CHECK(!kernel.valid(stale_handle));
    CHECK(kernel.raycast(stale_handle,{{-3,0,0},{1,0,0},0,10}).status==aion::GeometryQueryStatus::InvalidHandle);

    // Logical geometry set tracks source revision independently from provider/resource identity.
    aion::GeometrySet set;
    CHECK(set.add_representation(mesh,0.0F,error));
    CHECK(set.add_representation(box,0.0F,error));
    const auto ray_candidates=set.eligible(kernel,aion::GeometryCapabilityMask::of({aion::GeometryCapability::Bounds,aion::GeometryCapability::RaySurface}),0.0F);
    CHECK(ray_candidates.size()==2);
    const auto distance_candidates=set.eligible(kernel,aion::GeometryCapabilityMask::of({aion::GeometryCapability::SignedDistance}),0.0F);
    CHECK(distance_candidates.size()==1&&distance_candidates.front().handle==box);

    aion::GeometrySet error_budget_set;
    CHECK(error_budget_set.add_representation(mesh,0.25F,error));
    CHECK(error_budget_set.add_representation(box,0.0F,error));
    const auto strict=error_budget_set.eligible(kernel,aion::GeometryCapabilityMask::of({aion::GeometryCapability::RaySurface}),0.10F);
    CHECK(strict.size()==1&&strict.front().handle==box);
    const auto relaxed=error_budget_set.eligible(kernel,aion::GeometryCapabilityMask::of({aion::GeometryCapability::RaySurface}),0.30F);
    CHECK(relaxed.size()==2);

    set.source_changed();
    CHECK(set.eligible(kernel,aion::GeometryCapabilityMask::of({aion::GeometryCapability::RaySurface}),0.0F).empty());
    CHECK(set.add_representation(box,0.0F,error));
    const auto refreshed=set.eligible(kernel,aion::GeometryCapabilityMask::of({aion::GeometryCapability::SignedDistance}),0.0F);
    CHECK(refreshed.size()==1&&refreshed.front().source_revision==set.revision());

    // Provider registration rejects capability contracts whose callbacks do not exist.
    aion::GeometryKernel invalid_kernel;
    aion::GeometryProviderDescriptor bad{.id=7,.name="bad",.capabilities=aion::GeometryCapabilityMask::of({aion::GeometryCapability::RaySurface})};
    aion::GeometryProviderOps missing{};
    missing.context=std::make_shared<int>(0);
    CHECK(!invalid_kernel.register_provider(std::move(bad),missing,error));

    // Registry owns provider state: handles remain safe even after authoring wrappers leave scope.
    aion::GeometryKernel owned_kernel;
    aion::GeometryHandle owned_mesh, owned_box;
    {
        aion::TriangleReferenceProvider local_tri(11);
        aion::AnalyticSdfProvider local_sdf(12);
        CHECK(local_tri.register_with(owned_kernel,error));
        CHECK(local_sdf.register_with(owned_kernel,error));
        owned_mesh=local_tri.add_mesh(vertices,indices,error);
        owned_box=local_sdf.add_box({0,0,0},{1,1,1},error);
    }
    CHECK(owned_kernel.valid(owned_mesh));
    CHECK(owned_kernel.valid(owned_box));
    CHECK(owned_kernel.raycast(owned_mesh,{{-3,0,0},{1,0,0},0,10}).status==aion::GeometryQueryStatus::Ok);
    CHECK(owned_kernel.raycast(owned_box,{{-3,0,0},{1,0,0},0,10}).status==aion::GeometryQueryStatus::Ok);

    std::cout<<"r4_geometry_contract_tests: PASS\n"
             <<"ray_equivalence_hits="<<compared<<'\n'
             <<"triangle_bytes="<<kernel.storage_bytes(mesh)<<'\n'
             <<"sdf_bytes="<<kernel.storage_bytes(box)<<'\n';
    return EXIT_SUCCESS;
}
