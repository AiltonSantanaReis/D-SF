#include "aion/kernel/geometry_fabric.hpp"
#include "aion/kernel/geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <vector>

namespace {
struct Trial {
    std::size_t detect_obs{};
    std::size_t detect_exploit{};
    std::size_t explorations{};
    double pre_regret{};
    double post_regret{};
};

Trial simulate(std::uint64_t refresh_age,std::size_t drift){
    aion::GeometryKernel kernel;std::string error;aion::AnalyticSdfProvider provider(2);if(!provider.register_with(kernel,error))std::abort();
    const auto active=provider.add_sphere({0,0,0},1,error);const auto inactive=provider.add_sphere({0,0,0},1,error);
    aion::GeometrySet set;if(!set.add_representation(active,0.01F,error)||!set.add_representation(inactive,0.02F,error))std::abort();
    aion::GeometryTelemetryStore telemetry(7);aion::GeometryRouteRequest req;req.constraints.required=aion::GeometryCapabilityMask::of({aion::GeometryCapability::RaySurface});req.constraints.max_geometric_error=0.1F;req.workload_id=701;req.work_units=2048;req.min_observed_batches=3;req.refresh_age=refresh_age;req.allow_exploration=true;
    Trial out;out.detect_obs=9999;out.detect_exploit=9999;const std::size_t total=drift+192U;
    for(std::size_t step=0;step<total;++step){const auto route=aion::route_geometry_batch(set,kernel,telemetry,req);if(!route.ok)std::abort();if(route.exploration)++out.explorations;if(route.reset_telemetry_before_observe)telemetry.reset_epoch(route.handle,set.revision(),req.workload_id,req.device,req.work_units);const double active_cost=1.0;const double inactive_cost=step<drift?2.0:0.5;const bool chose_inactive=route.handle==inactive;const double actual=chose_inactive?inactive_cost:active_cost;const double oracle=std::min(active_cost,inactive_cost);if(step<drift)out.pre_regret+=actual-oracle;else out.post_regret+=actual-oracle;telemetry.record_normalized(route.handle,set.revision(),req.workload_id,req.device,req.work_units,actual,actual);if(step>=drift&&chose_inactive&&out.detect_obs==9999)out.detect_obs=step-drift;if(step>=drift&&chose_inactive&&!route.exploration&&out.detect_exploit==9999)out.detect_exploit=step-drift;}
    return out;
}

double quantile(std::vector<double> values,double q){std::sort(values.begin(),values.end());if(values.empty())return 0;const std::size_t idx=static_cast<std::size_t>(std::ceil(q*static_cast<double>(values.size())))-1U;return values[std::min(idx,values.size()-1U)];}
}

int main(){
    constexpr std::array<std::uint64_t,6> ages{3,4,8,16,32,64};
    std::cout<<"refresh_age,trials,explore_mean,pre_regret_mean,post_regret_mean,detect_obs_p50,detect_obs_p90,detect_obs_max,detect_exploit_p50,detect_exploit_p90,detect_exploit_max\n";
    for(const auto age:ages){std::vector<double> obs,exploit;double explores=0,pre=0,post=0;std::size_t trials=0;for(std::size_t drift=128;drift<256;++drift){const auto t=simulate(age,drift);obs.push_back(static_cast<double>(t.detect_obs));exploit.push_back(static_cast<double>(t.detect_exploit));explores+=static_cast<double>(t.explorations);pre+=t.pre_regret;post+=t.post_regret;++trials;}std::cout<<age<<','<<trials<<','<<explores/trials<<','<<pre/trials<<','<<post/trials<<','<<quantile(obs,0.5)<<','<<quantile(obs,0.9)<<','<<*std::max_element(obs.begin(),obs.end())<<','<<quantile(exploit,0.5)<<','<<quantile(exploit,0.9)<<','<<*std::max_element(exploit.begin(),exploit.end())<<'\n';}
    return 0;
}
