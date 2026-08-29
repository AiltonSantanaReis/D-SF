#include "aion/kernel/spatial_fabric.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

template<class F> double ms(F&& fn) {
    const auto a=Clock::now(); fn(); const auto b=Clock::now();
    return std::chrono::duration<double,std::milli>(b-a).count();
}

double percentile(std::vector<double> values,double p){if(values.empty())return 0.0;std::sort(values.begin(),values.end());const auto idx=static_cast<std::size_t>(std::min<double>(values.size()-1, p*static_cast<double>(values.size()-1)));return values[idx];}

std::vector<aion::SpatialRecord> records(std::size_t count) {
    std::mt19937 rng(0xD5F32003U);
    std::uniform_real_distribution<float> pos(-50000.0F,50000.0F), ext(0.1F,5.0F);
    std::vector<aion::SpatialRecord> out; out.reserve(count);
    for(std::size_t i=0;i<count;++i) out.push_back({static_cast<aion::EntityId>(i+1),{pos(rng),pos(rng),pos(rng)},{ext(rng),ext(rng),ext(rng)}});
    return out;
}

aion::PatchTransaction move_patch(const aion::SpatialSnapshot& snapshot,std::uint64_t id,std::size_t count) {
    aion::PatchTransaction tx{.id=id}; count=std::min(count,snapshot.size());
    if(count==0) return tx;
    std::vector<aion::Vec3> values; values.reserve(count);
    for(std::size_t i=0;i<count;++i){auto c=snapshot.center(static_cast<std::uint32_t>(i));c.x+=0.025F;c.y-=0.0125F;values.push_back(c);}
    tx.vec3_patches.push_back({aion::PatchComponent::Position,1,std::move(values)});
    return tx;
}

aion::Aabb query_box(std::size_t i){const float x=-40000.0F+static_cast<float>((i*977U)%80000U);return {{x-300.0F,-1000.0F,-1000.0F},{x+300.0F,1000.0F,1000.0F}};}

void static_budgeted(std::size_t count,double slice_ms,std::size_t chunk) {
    std::string error; aion::SpatialSnapshot snapshot; if(!snapshot.build(records(count),error)){std::cerr<<error<<"\n";std::exit(1);}
    aion::BudgetedSahBuilder builder(8,chunk); if(!builder.start(snapshot,error)){std::cerr<<error<<"\n";std::exit(1);}
    std::size_t frames=0; double scheduler_actual=0.0; double max_cpu_slice=0.0; std::vector<double> wall_slices,cpu_slices;
    while(builder.state()!=aion::BudgetedSahState::Ready && frames<200000){
        const aion::MaintenanceBudget budget{.frame_budget_ms=16.666,.critical_path_ms=10.0,.safety_margin_ms=1.0,.max_maintenance_slice_ms=slice_ms};
        const auto r=aion::ExecutionBudgetScheduler::run_maintenance(budget,[&](double grant){return builder.run_slice(snapshot,grant);});
        if(!r.ok){std::cerr<<"builder error: "<<r.error<<'\n';std::exit(2);} scheduler_actual+=r.actual_ms;max_cpu_slice=std::max(max_cpu_slice,r.actual_cpu_ms);wall_slices.push_back(r.actual_ms);cpu_slices.push_back(r.actual_cpu_ms);++frames;
    }
    aion::WideBvh8View view; const auto p=builder.try_promote(snapshot,view);
    if(p.state!=aion::BudgetedSahState::Promoted){std::cerr<<"promotion failed: "<<p.error<<'\n';std::exit(3);}
    bool exact=true;for(std::size_t i=0;i<64;++i)exact=exact&&(view.query_aabb(snapshot,query_box(i)).entities==aion::SpatialOracle::query_aabb(snapshot,query_box(i)));
    std::cout<<"STATIC count="<<count<<" slice_ms="<<slice_ms<<" chunk="<<chunk<<" frames="<<frames
             <<" total_actual_ms="<<scheduler_actual<<" p50_ms="<<percentile(wall_slices,0.50)<<" p95_ms="<<percentile(wall_slices,0.95)<<" p99_ms="<<percentile(wall_slices,0.99)<<" max_slice_ms="<<p.stats.max_slice_ms<<" p99_cpu_ms="<<percentile(cpu_slices,0.99)<<" max_cpu_slice_ms="<<max_cpu_slice
             <<" capture_ms="<<p.stats.capture_ms<<" build_ms="<<p.stats.build_ms<<" max_capture_ms="<<p.stats.max_capture_slice_ms<<" max_build_ms="<<p.stats.max_build_slice_ms<<" max_reserve_ms="<<p.stats.max_reserve_slice_ms<<" max_init_ms="<<p.stats.max_init_slice_ms<<" max_root_ms="<<p.stats.max_root_slice_ms<<" max_node_ms="<<p.stats.max_node_slice_ms<<" max_catchup_ms="<<p.stats.max_catchup_slice_ms
             <<" nodes="<<view.node_count()<<" visits="<<p.stats.primitive_visits<<" exact="<<(exact?1:0)<<'\n';
}

void integrated(std::size_t count,double slice_ms,std::size_t max_frames,std::size_t chunk) {
    std::string error; aion::SpatialSnapshot snapshot; if(!snapshot.build(records(count),error)){std::cerr<<error<<"\n";std::exit(1);}
    aion::MortonBvh8View active; if(!active.build(snapshot,error)){std::cerr<<error<<"\n";std::exit(1);}
    aion::BudgetedSahBuilder builder(8,chunk); if(!builder.start(snapshot,error)){std::cerr<<error<<"\n";std::exit(1);}
    std::size_t frames=0,ran=0,starved=0; double critical_sum=0.0,maintenance_sum=0.0,max_frame=0.0;std::uint64_t txid=2;
    while(builder.state()!=aion::BudgetedSahState::Ready && frames<max_frames){
        aion::SpatialChangeSet changes;
        const double critical=ms([&]{
            const auto applied=snapshot.apply_patch_transaction(move_patch(snapshot,txid++,std::max<std::size_t>(1,count/100U)));
            if(!applied.ok){std::cerr<<applied.error<<'\n';std::exit(4);} changes=applied.changes;
            if(!builder.observe_changes(changes,error)){std::cerr<<error<<'\n';std::exit(5);}
            if(!active.sync(snapshot,changes,error)){std::cerr<<error<<'\n';std::exit(6);}
            for(std::size_t q=0;q<32;++q)(void)active.query_aabb(snapshot,query_box(q+frames));
        });
        const aion::MaintenanceBudget budget{.frame_budget_ms=16.666,.critical_path_ms=critical,.safety_margin_ms=1.0,.max_maintenance_slice_ms=slice_ms};
        const auto r=aion::ExecutionBudgetScheduler::run_maintenance(budget,[&](double grant){return builder.run_slice(snapshot,grant);});
        if(!r.ok){std::cerr<<r.error<<'\n';std::exit(7);} if(r.ran)++ran;else ++starved;
        critical_sum+=critical;maintenance_sum+=r.actual_ms;max_frame=std::max(max_frame,critical+r.actual_ms);++frames;
    }
    bool promoted=false;double max_slice=builder.stats().max_slice_ms;
    if(builder.state()==aion::BudgetedSahState::Ready){aion::WideBvh8View view;const auto p=builder.try_promote(snapshot,view);promoted=p.state==aion::BudgetedSahState::Promoted;max_slice=p.stats.max_slice_ms;}
    std::cout<<"INTEGRATED count="<<count<<" slice_ms="<<slice_ms<<" frames="<<frames<<" ran="<<ran<<" starved="<<starved
             <<" avg_critical_ms="<<(critical_sum/static_cast<double>(std::max<std::size_t>(1,frames)))
             <<" avg_maintenance_ms="<<(maintenance_sum/static_cast<double>(std::max<std::size_t>(1,frames)))
             <<" max_frame_ms="<<max_frame<<" max_slice_ms="<<max_slice<<" catchup_ms="<<builder.stats().catchup_ms<<" max_catchup_ms="<<builder.stats().max_catchup_slice_ms<<" state="<<static_cast<int>(builder.state())
             <<" promoted="<<(promoted?1:0)<<'\n';
}
}

int main(int argc,char**argv){
    std::cout<<std::fixed<<std::setprecision(3);
    if(argc>1 && std::string(argv[1])=="integrated"){
        const std::size_t n=argc>2?static_cast<std::size_t>(std::stoull(argv[2])):100000;
        const double slice=argc>3?std::stod(argv[3]):1.0;
        const std::size_t frames=argc>4?static_cast<std::size_t>(std::stoull(argv[4])):5000;
        const std::size_t chunk=argc>5?static_cast<std::size_t>(std::stoull(argv[5])):512;
        integrated(n,slice,frames,chunk);return 0;
    }
    const std::size_t n=argc>1?static_cast<std::size_t>(std::stoull(argv[1])):100000;
    const double slice=argc>2?std::stod(argv[2]):1.0;
    const std::size_t chunk=argc>3?static_cast<std::size_t>(std::stoull(argv[3])):512;
    static_budgeted(n,slice,chunk);return 0;
}
