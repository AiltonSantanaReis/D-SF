#include "aion/kernel/spatial_fabric.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
template<class F> double ms(F&&fn){const auto a=Clock::now();fn();const auto b=Clock::now();return std::chrono::duration<double,std::milli>(b-a).count();}
std::vector<aion::SpatialRecord> records(std::size_t n){std::mt19937 r(0xD5F32004U);std::uniform_real_distribution<float> p(-50000,50000),e(.1F,5);std::vector<aion::SpatialRecord> o;o.reserve(n);for(std::size_t i=0;i<n;++i)o.push_back({static_cast<aion::EntityId>(i+1),{p(r),p(r),p(r)},{e(r),e(r),e(r)}});return o;}
aion::Aabb box(std::size_t i){const float x=-45000+static_cast<float>((i*997U)%90000U),y=-40000+static_cast<float>((i*619U)%80000U),z=-35000+static_cast<float>((i*431U)%70000U);return {{x-500,y-500,z-500},{x+500,y+500,z+500}};}
aion::Ray ray(std::size_t i){const float y=-45000+static_cast<float>((i*773U)%90000U),z=-45000+static_cast<float>((i*521U)%90000U);return {{-60000,y,z},{1.0F,0.01F,0.005F}};}
}
int main(int argc,char**argv){const std::size_t n=argc>1?static_cast<std::size_t>(std::stoull(argv[1])):100000;std::string error;aion::SpatialSnapshot s;if(!s.build(records(n),error)){std::cerr<<error;return 2;}
 aion::WideBvh8View mono;const double mono_build=ms([&]{if(!mono.build(s,error)){std::cerr<<error;std::exit(3);}});
 aion::BudgetedSahBuilder b(8,512);if(!b.start(s,error)){std::cerr<<error;return 4;}double budget_cpu=0;while(b.state()!=aion::BudgetedSahState::Ready){const auto r=aion::ExecutionBudgetScheduler::run_maintenance({.frame_budget_ms=16.666,.critical_path_ms=10,.safety_margin_ms=1,.max_maintenance_slice_ms=.5},[&](double g){return b.run_slice(s,g);});if(!r.ok){std::cerr<<r.error;return 5;}budget_cpu+=r.actual_cpu_ms;}
 aion::WideBvh8View coop;auto pr=b.try_promote(s,coop);if(pr.state!=aion::BudgetedSahState::Promoted){std::cerr<<pr.error;return 6;}
 constexpr std::size_t qn=512;std::size_t mh=0,ch=0;const double mq=ms([&]{for(std::size_t i=0;i<qn;++i)mh+=mono.query_aabb(s,box(i)).entities.size();});const double cq=ms([&]{for(std::size_t i=0;i<qn;++i)ch+=coop.query_aabb(s,box(i)).entities.size();});
 std::size_t mr=0,cr=0;const double mray=ms([&]{for(std::size_t i=0;i<qn;++i)mr+=mono.raycast(s,ray(i)).entity!=0;});const double cray=ms([&]{for(std::size_t i=0;i<qn;++i)cr+=coop.raycast(s,ray(i)).entity!=0;});
 std::cout<<std::fixed<<std::setprecision(3)<<"QUALITY count="<<n<<" mono_build_ms="<<mono_build<<" coop_cpu_ms="<<budget_cpu<<" coop_slices="<<pr.stats.slices<<" coop_p99_soft_target_ms=0.5 mono_nodes="<<mono.node_count()<<" coop_nodes="<<coop.node_count()<<" aabb_mono_ms="<<mq<<" aabb_coop_ms="<<cq<<" ray_mono_ms="<<mray<<" ray_coop_ms="<<cray<<" aabb_equal="<<(mh==ch)<<" ray_equal="<<(mr==cr)<<'\n';}
