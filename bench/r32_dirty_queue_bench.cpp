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

template<class F> double elapsed_ms(F&& fn) {
    const auto a = Clock::now();
    fn();
    const auto b = Clock::now();
    return std::chrono::duration<double, std::milli>(b-a).count();
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto idx = static_cast<std::size_t>(p * static_cast<double>(values.size()-1));
    return values[std::min(idx, values.size()-1)];
}

std::vector<aion::SpatialRecord> make_records(std::size_t count) {
    std::mt19937 rng(0xD5F32004U);
    std::uniform_real_distribution<float> pos(-25000.0F, 25000.0F);
    std::uniform_real_distribution<float> ext(0.1F, 4.0F);
    std::vector<aion::SpatialRecord> out;
    out.reserve(count);
    for (std::size_t i=0;i<count;++i)
        out.push_back({static_cast<aion::EntityId>(i+1), {pos(rng),pos(rng),pos(rng)}, {ext(rng),ext(rng),ext(rng)}});
    return out;
}

aion::PatchTransaction dense_move(const aion::SpatialSnapshot& snapshot, std::uint64_t txid,
                                  std::size_t count, std::size_t frame) {
    aion::PatchTransaction tx{.id=txid};
    if (count == 0 || snapshot.empty()) return tx;
    count = std::min(count, snapshot.size());
    const std::size_t max_first = snapshot.size() - count;
    const std::size_t first_slot = max_first == 0 ? 0 : (frame * 7919U) % (max_first + 1U);
    std::vector<aion::Vec3> values;
    values.reserve(count);
    for (std::size_t i=0;i<count;++i) {
        auto c = snapshot.center(static_cast<std::uint32_t>(first_slot+i));
        c.x += 0.035F;
        c.y -= 0.0175F;
        c.z += 0.00875F;
        values.push_back(c);
    }
    tx.vec3_patches.push_back({aion::PatchComponent::Position,
        static_cast<aion::EntityId>(first_slot+1U), std::move(values)});
    return tx;
}


aion::PatchTransaction sparse_move(const aion::SpatialSnapshot& snapshot, std::uint64_t txid,
                                   std::size_t count, std::size_t frame) {
    aion::PatchTransaction tx{.id=txid};
    if (count == 0 || snapshot.empty()) return tx;
    count = std::min(count, snapshot.size());
    tx.scalar_mutations.reserve(count);
    for (std::size_t i=0;i<count;++i) {
        const auto slot = static_cast<std::uint32_t>((i * 3571ULL + frame * 7919ULL) % snapshot.size());
        auto c = snapshot.center(slot);
        c.x -= 0.022F;
        c.y += 0.011F;
        c.z -= 0.0055F;
        tx.scalar_mutations.push_back({aion::MutationKind::SetPosition, snapshot.entity(slot), c, 0});
    }
    return tx;
}

aion::Aabb query_box(std::size_t i) {
    const float x=-22000.0F+static_cast<float>((i*977U)%44000U);
    const float y=-21000.0F+static_cast<float>((i*619U)%42000U);
    const float z=-20000.0F+static_cast<float>((i*431U)%40000U);
    return {{x-180.0F,y-180.0F,z-180.0F},{x+180.0F,y+180.0F,z+180.0F}};
}

bool run_until_ready(aion::BudgetedSahBuilder& builder, const aion::SpatialSnapshot& snapshot,
                     double slice_ms, std::size_t max_slices) {
    for (std::size_t i=0;i<max_slices && builder.state()!=aion::BudgetedSahState::Ready;++i) {
        const auto p=builder.run_slice(snapshot,slice_ms);
        if(!p.ok) return false;
    }
    return builder.state()==aion::BudgetedSahState::Ready;
}

void run_case(std::size_t count, double rate, std::size_t produce_frames, double slice_ms, bool sparse) {
    std::string error;
    aion::SpatialSnapshot snapshot;
    if(!snapshot.build(make_records(count),error)){std::cerr<<error<<'\n';std::exit(2);}
    aion::BudgetedSahBuilder builder(8,256);
    if(!builder.start(snapshot,error)){std::cerr<<error<<'\n';std::exit(3);}
    if(!run_until_ready(builder,snapshot,slice_ms,200000)){std::cerr<<"initial build failed to converge\n";std::exit(4);}

    const auto stats_before = builder.stats();
    std::uint64_t txid=2;
    std::size_t peak_raw=0, peak_leaf=0, peak_internal=0, peak_total=0;
    double maintenance_wall_ms=0.0;
    double maintenance_cpu_ms=0.0;
    double publish_ms=0.0;
    const std::size_t dirty_per_frame=static_cast<std::size_t>(static_cast<double>(count)*rate);

    for(std::size_t frame=0;frame<produce_frames;++frame){
        if(dirty_per_frame!=0){
            aion::SpatialApplyResult applied;
            publish_ms += elapsed_ms([&]{
                auto tx = sparse ? sparse_move(snapshot,txid++,dirty_per_frame,frame)
                                 : dense_move(snapshot,txid++,dirty_per_frame,frame);
                applied=snapshot.apply_patch_transaction(tx);
            });
            if(!applied.ok || !builder.observe_changes(applied.changes,error)){std::cerr<<(applied.ok?error:applied.error)<<'\n';std::exit(5);}
        }
        if(builder.state()!=aion::BudgetedSahState::Ready){
            const aion::MaintenanceBudget budget{
                .frame_budget_ms = slice_ms, .critical_path_ms = 0.0, .safety_margin_ms = 0.0,
                .max_maintenance_slice_ms = slice_ms};
            const auto r = aion::ExecutionBudgetScheduler::run_maintenance(budget,[&](double grant){return builder.run_slice(snapshot,grant);});
            if(!r.ok){std::cerr<<r.error<<'\n';std::exit(6);}
            maintenance_wall_ms += r.actual_ms;
            maintenance_cpu_ms += r.actual_cpu_ms;
        }
        const auto b=builder.backlog();
        peak_raw=std::max(peak_raw,b.raw_dirty_values);
        peak_leaf=std::max(peak_leaf,b.dirty_leaf_nodes);
        peak_internal=std::max(peak_internal,b.dirty_internal_nodes);
        peak_total=std::max(peak_total,b.total_items());
    }

    const auto after_production=builder.backlog();
    const auto stats_after_production=builder.stats();
    std::size_t drain_slices=0;
    std::vector<double> drain_wall, drain_cpu;
    while(builder.state()!=aion::BudgetedSahState::Ready && drain_slices<200000){
        const aion::MaintenanceBudget budget{
            .frame_budget_ms = slice_ms, .critical_path_ms = 0.0, .safety_margin_ms = 0.0,
            .max_maintenance_slice_ms = slice_ms};
        const auto r = aion::ExecutionBudgetScheduler::run_maintenance(budget,[&](double grant){return builder.run_slice(snapshot,grant);});
        if(!r.ok){std::cerr<<r.error<<'\n';std::exit(7);}
        drain_wall.push_back(r.actual_ms);
        drain_cpu.push_back(r.actual_cpu_ms);
        maintenance_wall_ms += r.actual_ms;
        maintenance_cpu_ms += r.actual_cpu_ms;
        ++drain_slices;
    }
    if(builder.state()!=aion::BudgetedSahState::Ready){std::cerr<<"queue failed to drain\n";std::exit(8);}

    aion::WideBvh8View promoted;
    const auto promotion=builder.try_promote(snapshot,promoted);
    if(promotion.state!=aion::BudgetedSahState::Promoted){std::cerr<<promotion.error<<'\n';std::exit(9);}
    bool exact=true;
    for(std::size_t q=0;q<128;++q){
        const auto box=query_box(q);
        exact=exact && (promoted.query_aabb(snapshot,box).entities==aion::SpatialOracle::query_aabb(snapshot,box));
    }
    if(!exact){std::cerr<<"query mismatch\n";std::exit(10);}

    const auto final_stats=promotion.stats;
    const auto final_backlog=builder.backlog();
    const std::size_t observed_delta=final_stats.observed_dirty_values-stats_before.observed_dirty_values;
    const std::size_t mapped_delta=final_stats.mapped_dirty_values-stats_before.mapped_dirty_values;
    const std::size_t leaf_delta=final_stats.refit_leaf_children-stats_before.refit_leaf_children;
    const std::size_t node_delta=final_stats.refit_internal_nodes-stats_before.refit_internal_nodes;
    const double catchup_delta=final_stats.catchup_ms-stats_before.catchup_ms;

    std::cout<<std::fixed<<std::setprecision(3)
             <<"QUEUE mode="<<(sparse?"sparse":"dense")<<" count="<<count<<" rate="<<rate<<" dirty_per_frame="<<dirty_per_frame
             <<" produce_frames="<<produce_frames<<" slice_ms="<<slice_ms
             <<" observed_values="<<observed_delta<<" mapped_values="<<mapped_delta
             <<" leaf_refits="<<leaf_delta<<" internal_refits="<<node_delta
             <<" peak_raw="<<peak_raw<<" peak_leaf="<<peak_leaf<<" peak_internal="<<peak_internal
             <<" peak_total="<<peak_total
             <<" end_raw="<<after_production.raw_dirty_values<<" end_leaf="<<after_production.dirty_leaf_nodes
             <<" end_internal="<<after_production.dirty_internal_nodes
             <<" drain_slices="<<drain_slices<<" catchup_ms="<<catchup_delta
             <<" publish_ms="<<publish_ms<<" maintenance_wall_ms="<<maintenance_wall_ms
             <<" maintenance_cpu_ms="<<maintenance_cpu_ms
             <<" drain_p50_ms="<<percentile(drain_wall,0.50)<<" drain_p99_ms="<<percentile(drain_wall,0.99)
             <<" drain_cpu_p50_ms="<<percentile(drain_cpu,0.50)<<" drain_cpu_p99_ms="<<percentile(drain_cpu,0.99)
             <<" final_backlog="<<final_backlog.total_items()<<" exact="<<(exact?1:0)
             <<" version="<<snapshot.version()<<'\n';

    // Service-rate diagnostic at the end of production, before drain.
    (void)stats_after_production;
}
}

int main(int argc,char**argv){
    const std::size_t count=argc>1?static_cast<std::size_t>(std::stoull(argv[1])):100000;
    const double rate=argc>2?std::stod(argv[2]):0.01;
    const std::size_t frames=argc>3?static_cast<std::size_t>(std::stoull(argv[3])):100;
    const double slice=argc>4?std::stod(argv[4]):0.5;
    const bool sparse=argc>5 && std::string(argv[5])=="sparse";
    run_case(count,rate,frames,slice,sparse);
    return 0;
}
