#include "aion/kernel/geometry_fabric.hpp"
#include "aion/kernel/geometry.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)

namespace {
struct SweepResult {std::uint64_t age{};std::size_t explorations{};std::size_t stale_refreshes{};std::size_t first_post_drift_cluster_obs{9999};std::size_t first_post_drift_cluster_exploit{9999};std::size_t wrong_after_drift{};double regret{};};

SweepResult simulate(std::uint64_t refresh_age){
    aion::GeometryKernel kernel;std::string error;aion::AnalyticSdfProvider provider(2);if(!provider.register_with(kernel,error))std::abort();
    const auto sparse=provider.add_sphere({0,0,0},1,error);const auto clustered=provider.add_sphere({0,0,0},1,error);
    aion::GeometrySet set;if(!set.add_representation(sparse,0.01F,error)||!set.add_representation(clustered,0.02F,error))std::abort();
    aion::GeometryTelemetryStore telemetry(7);aion::GeometryRouteRequest req;req.constraints.required=aion::GeometryCapabilityMask::of({aion::GeometryCapability::RaySurface});req.constraints.max_geometric_error=0.1F;req.workload_id=700;req.work_units=2048;req.min_observed_batches=3;req.refresh_age=refresh_age;req.allow_exploration=true;
    constexpr std::size_t drift=120,total=240;SweepResult out;out.age=refresh_age;
    for(std::size_t step=0;step<total;++step){
        const auto route=aion::route_geometry_batch(set,kernel,telemetry,req);if(!route.ok)std::abort();
        if(route.exploration)++out.explorations;if(route.stale_refresh)++out.stale_refreshes;
        if(route.reset_telemetry_before_observe)telemetry.reset_epoch(route.handle,set.revision(),req.workload_id,req.device,req.work_units);
        const double sparse_cost=1.0;const double clustered_cost=step<drift?2.0:0.5;const bool is_cluster=route.handle==clustered;const double actual=is_cluster?clustered_cost:sparse_cost;const double oracle=std::min(sparse_cost,clustered_cost);out.regret+=actual-oracle;
        telemetry.record_normalized(route.handle,set.revision(),req.workload_id,req.device,req.work_units,actual,actual);
        if(step>=drift&&is_cluster&&out.first_post_drift_cluster_obs==9999)out.first_post_drift_cluster_obs=step-drift;
        if(step>=drift&&is_cluster&&!route.exploration&&out.first_post_drift_cluster_exploit==9999)out.first_post_drift_cluster_exploit=step-drift;
        if(step>=drift&&!is_cluster)++out.wrong_after_drift;
    }
    return out;
}
}

int main(){
    // No shadow work: one route produces exactly one synthetic observation in the controlled loop.
    constexpr std::array<std::uint64_t,6> ages{3,4,8,16,32,64};std::array<SweepResult,ages.size()> results{};
    for(std::size_t i=0;i<ages.size();++i)results[i]=simulate(ages[i]);
    for(const auto&r:results){std::cout<<"refresh_age="<<r.age<<" exploration="<<r.explorations<<" stale="<<r.stale_refreshes<<" first_obs="<<r.first_post_drift_cluster_obs<<" first_exploit="<<r.first_post_drift_cluster_exploit<<" wrong_after="<<r.wrong_after_drift<<" regret="<<r.regret<<'\n';}
    CHECK(results.front().explorations>results.back().explorations);

    {
        aion::GeometryKernel guard_kernel; std::string guard_error; aion::AnalyticSdfProvider guard_provider(8);
        CHECK(guard_provider.register_with(guard_kernel, guard_error));
        const auto g0=guard_provider.add_sphere({0,0,0},1,guard_error); const auto g1=guard_provider.add_sphere({0,0,0},1,guard_error);
        aion::GeometrySet guard_set; CHECK(guard_set.add_representation(g0,0.01F,guard_error)); CHECK(guard_set.add_representation(g1,0.02F,guard_error));
        aion::GeometryTelemetryStore guard_store(7); aion::GeometryRouteRequest guard_req; guard_req.constraints.required=aion::GeometryCapabilityMask::of({aion::GeometryCapability::RaySurface}); guard_req.min_observed_batches=3; guard_req.refresh_age=2;
        const auto invalid=aion::route_geometry_batch(guard_set,guard_kernel,guard_store,guard_req); CHECK(!invalid.ok);
    }

    // Exploration disabled refuses stale/insufficient knowledge instead of silently pretending certainty.
    aion::GeometryKernel kernel;std::string error;aion::AnalyticSdfProvider provider(9);CHECK(provider.register_with(kernel,error));const auto a=provider.add_sphere({0,0,0},1,error);const auto b=provider.add_sphere({0,0,0},1,error);aion::GeometrySet set;CHECK(set.add_representation(a,0.01F,error));CHECK(set.add_representation(b,0.02F,error));aion::GeometryTelemetryStore store(7);aion::GeometryRouteRequest req;req.constraints.required=aion::GeometryCapabilityMask::of({aion::GeometryCapability::RaySurface});req.allow_exploration=false;req.min_observed_batches=3;auto route=aion::route_geometry_batch(set,kernel,store,req);CHECK(!route.ok);

    std::cout<<"r4_online_geometry_routing_tests: PASS\n";return EXIT_SUCCESS;
}
