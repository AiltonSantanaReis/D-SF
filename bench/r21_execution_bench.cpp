#include "aion/kernel/execution.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using aion::AccessMask; using aion::ResourceKey; using aion::SystemContext; using aion::SystemSpec;
constexpr float kDt = 1.0F/60.0F;
struct RunResult { double ms{}; aion::StateHash hash{}; };

aion::World make_world(std::size_t entities) {
    aion::World world(entities+1); aion::Transaction tx{.id=1}; tx.mutations.reserve(entities*4);
    for(std::size_t i=0;i<entities;++i){const auto id=static_cast<aion::EntityId>(i+1);tx.mutations.push_back({aion::MutationKind::CreateEntity,id,{},0});tx.mutations.push_back({aion::MutationKind::SetPosition,id,{static_cast<float>(i),0,0},0});tx.mutations.push_back({aion::MutationKind::SetVelocity,id,{0.1F,0.02F,-0.03F},0});tx.mutations.push_back({aion::MutationKind::SetHealth,id,{},static_cast<std::uint32_t>(100-(i%25))});}
    if(!world.commit(tx).committed)std::abort();return world;
}

std::vector<SystemSpec> systems(bool ranged){const auto er=AccessMask::of({ResourceKey::Identity,ResourceKey::EntityState});return{
{.id=10,.name="Integrate",.reads=er|AccessMask::of({ResourceKey::Position,ResourceKey::Velocity}),.writes=AccessMask::of({ResourceKey::Position}),.run=[ranged](SystemContext&c){auto n=c.entity_capacity();if(ranged){std::vector<aion::Vec3>x;x.reserve(n-1);for(std::size_t i=1;i<n;++i){auto p=*c.position(i),v=*c.velocity(i);x.push_back({p.x+v.x*kDt,p.y+v.y*kDt,p.z+v.z*kDt});}c.set_position_range(1,std::move(x));}else for(std::size_t i=1;i<n;++i){auto p=*c.position(i),v=*c.velocity(i);c.set_position(i,{p.x+v.x*kDt,p.y+v.y*kDt,p.z+v.z*kDt});}}},
{.id=20,.name="Health",.reads=er|AccessMask::of({ResourceKey::Health}),.writes=AccessMask::of({ResourceKey::Health}),.run=[ranged](SystemContext&c){auto n=c.entity_capacity();if(ranged){std::vector<std::uint32_t>x;x.reserve(n-1);for(std::size_t i=1;i<n;++i){auto h=*c.health(i);x.push_back(h?h-1:0);}c.set_health_range(1,std::move(x));}else for(std::size_t i=1;i<n;++i){auto h=*c.health(i);c.set_health(i,h?h-1:0);}}},
{.id=30,.name="Velocity",.reads=er|AccessMask::of({ResourceKey::Velocity}),.writes=AccessMask::of({ResourceKey::Velocity}),.run=[ranged](SystemContext&c){auto n=c.entity_capacity();if(ranged){std::vector<aion::Vec3>x;x.reserve(n-1);for(std::size_t i=1;i<n;++i){auto v=*c.velocity(i);x.push_back({v.x*.9995F,v.y*.9995F,v.z*.9995F});}c.set_velocity_range(1,std::move(x));}else for(std::size_t i=1;i<n;++i){auto v=*c.velocity(i);c.set_velocity(i,{v.x*.9995F,v.y*.9995F,v.z*.9995F});}}},
{.id=40,.name="Clamp",.reads=er|AccessMask::of({ResourceKey::Position}),.writes=AccessMask::of({ResourceKey::Position}),.run=[ranged](SystemContext&c){auto n=c.entity_capacity();if(ranged){std::vector<aion::Vec3>x;x.reserve(n-1);for(std::size_t i=1;i<n;++i){auto p=*c.position(i);if(p.x>100000)p.x=100000;x.push_back(p);}c.set_position_range(1,std::move(x));}else for(std::size_t i=1;i<n;++i){auto p=*c.position(i);if(p.x>100000)p.x=100000;c.set_position(i,p);}}}
};}

RunResult run(std::size_t entities,std::size_t frames,bool ranged,std::size_t workers,bool parallel_commit){aion::ExecutionPlan plan;if(!aion::ExecutionPlan::build(systems(ranged),plan).ok)std::abort();auto world=make_world(entities);aion::ExecutionRuntime rt({.kind=workers>1?aion::ExecutorKind::WorkerPool:aion::ExecutorKind::SerialReference,.workers=workers});std::optional<aion::PatchCommitRuntime> publisher;if(parallel_commit)publisher.emplace(workers);auto t=Clock::now();for(std::size_t f=0;f<frames;++f){auto r=ranged?rt.execute_patched(plan,world,nullptr,publisher?&*publisher:nullptr):rt.execute(plan,world);if(!r.ok){std::cerr<<r.error<<'\n';std::abort();}}return{std::chrono::duration<double,std::milli>(Clock::now()-t).count(),world.state_hash()};}

void scenario(std::size_t entities,std::size_t frames){struct C{const char*n;bool ranged;std::size_t workers;bool pc;};const C cs[]={{"r2_scalar_serial",false,1,false},{"r2_scalar_workers4",false,4,false},{"r21_range_serial",true,1,false},{"r21_range_workers4",true,4,false},{"r21_range_workers4_parallel_commit",true,4,true}};std::cout<<"scenario entities="<<entities<<" frames="<<frames<<'\n';aion::StateHash ref{};bool have=false;double base=0;for(auto c:cs){std::vector<double> samples; aion::StateHash h{};for(int r=0;r<3;++r){auto x=run(entities,frames,c.ranged,c.workers,c.pc);samples.push_back(x.ms);if(r==0)h=x.hash;else if(!(h==x.hash))std::abort();}std::sort(samples.begin(),samples.end());double med=samples[1];if(!have){ref=h;base=med;have=true;}else if(!(ref==h)){std::cerr<<"hash mismatch "<<c.n<<'\n';std::exit(EXIT_FAILURE);}std::cout<<"candidate="<<c.n<<" median_ms="<<std::fixed<<std::setprecision(3)<<med<<" speedup="<<(base/med)<<" sha256="<<h.hex()<<'\n';}}
} // namespace
int main(){scenario(8192,60);scenario(100000,20);scenario(1000000,3);}
