#include "aion/kernel/spatial.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
template<class F> double ms(F&&f){auto a=Clock::now();f();auto b=Clock::now();return std::chrono::duration<double,std::milli>(b-a).count();}

std::vector<aion::SpatialRecord> records(std::size_t n){std::mt19937 r(0xD5F30101U);std::uniform_real_distribution<float> p(-100000,100000),e(.2F,5.F);std::vector<aion::SpatialRecord>x;x.reserve(n);for(std::size_t i=0;i<n;++i)x.push_back({static_cast<aion::EntityId>(i+1),{p(r),p(r),p(r)},{e(r),e(r),e(r)}});return x;}
std::vector<aion::Aabb> queries(std::size_t n){std::mt19937 r(0xD5F30102U);std::uniform_real_distribution<float> p(-100000,100000),h(50,2500);std::vector<aion::Aabb>x;x.reserve(n);for(std::size_t i=0;i<n;++i){aion::Vec3 c{p(r),p(r),p(r)};float z=h(r);x.push_back({{c.x-z,c.y-z,c.z-z},{c.x+z,c.y+z,c.z+z}});}return x;}

template<class View> double query_ms(const View&v,const aion::SpatialSnapshot&s,const std::vector<aion::Aabb>&q,std::uint64_t&checksum){return ms([&]{for(const auto& box:q){auto x=v.query_aabb(s,box);if(!x.ok)std::abort();checksum+=x.entities.size();}});}

aion::PatchTransaction motion_patch(const aion::SpatialSnapshot&s,std::size_t count,std::mt19937&rng,std::uint64_t txid,double motion_fraction){
    std::uniform_real_distribution<float>absolute(-100000,100000);
    const float radius=static_cast<float>(std::max(0.0,motion_fraction))*s.coherence_scale();
    std::uniform_real_distribution<float>delta(-radius,radius);
    aion::PatchTransaction tx{.id=txid};count=std::min(count,s.size());tx.scalar_mutations.reserve(count);const std::size_t stride=7919;
    for(std::size_t i=0;i<count;++i){const auto slot=static_cast<std::uint32_t>((i*stride)%s.size());const auto c=s.center(slot);const aion::Vec3 next=motion_fraction<0.0? aion::Vec3{absolute(rng),absolute(rng),absolute(rng)} : aion::Vec3{c.x+delta(rng),c.y+delta(rng),c.z+delta(rng)};tx.scalar_mutations.push_back({aion::MutationKind::SetPosition,s.entity(slot),next,0});}
    return tx;
}

void run(std::size_t n,double churn,int frames,double motion_fraction,std::size_t query_count){std::string err;auto rec=records(n);aion::SpatialSnapshot s_refit,s_morton;if(!s_refit.build(rec,err)||!s_morton.build(rec,err))std::abort();aion::WideBvh8View refit;aion::MortonBvh8View morton;if(!refit.build(s_refit,err)||!morton.build(s_morton,err))std::abort();auto q=queries(query_count);std::mt19937 ra(0xD5F30103U),rb(0xD5F30103U);const auto change_count=static_cast<std::size_t>(std::llround(static_cast<double>(n)*churn));double refit_update=0,morton_update=0,refit_query=0,morton_query=0,topology_debt=0;std::uint64_t ca=0,cb=0;
for(int f=0;f<frames;++f){auto ta=motion_patch(s_refit,change_count,ra,static_cast<std::uint64_t>(f+2),motion_fraction);auto tb=motion_patch(s_morton,change_count,rb,static_cast<std::uint64_t>(f+2),motion_fraction);auto aa=s_refit.apply_patch_transaction(ta);auto ab=s_morton.apply_patch_transaction(tb);if(!aa.ok||!ab.ok)std::abort();topology_debt+=aa.changes.topology_debt_delta;refit_update+=ms([&]{if(!refit.sync(s_refit,aa.changes,err,aion::WideBvhSyncMode::RefitOnly))std::abort();});morton_update+=ms([&]{if(!morton.sync(s_morton,ab.changes,err))std::abort();});refit_query+=query_ms(refit,s_refit,q,ca);morton_query+=query_ms(morton,s_morton,q,cb);if(ca!=cb){std::cerr<<"checksum divergence frame "<<f<<'\n';std::abort();}}
std::cout<<std::fixed<<std::setprecision(3)<<"entities="<<n<<" churn="<<churn<<" frames="<<frames<<" motion_fraction="<<motion_fraction<<" query_count="<<query_count<<'\n'<<"refit_update_total_ms="<<refit_update<<'\n'<<"morton_update_total_ms="<<morton_update<<'\n'<<"refit_query_total_ms="<<refit_query<<'\n'<<"morton_query_total_ms="<<morton_query<<'\n'<<"refit_avg_update_ms="<<refit_update/frames<<'\n'<<"morton_avg_update_ms="<<morton_update/frames<<'\n'<<"refit_avg_query_batch_ms="<<refit_query/frames<<'\n'<<"morton_avg_query_batch_ms="<<morton_query/frames<<'\n'<<"coherence_scale="<<s_refit.coherence_scale()<<'\n'<<"topology_debt="<<topology_debt<<'\n'<<"checksum="<<ca<<'\n';}
}
int main(int argc,char**argv){std::size_t n=100000;double churn=.5;int frames=10;double motion_fraction=-1.0;std::size_t query_count=500;if(argc>1)n=std::strtoull(argv[1],nullptr,10);if(argc>2)churn=std::strtod(argv[2],nullptr);if(argc>3)frames=std::atoi(argv[3]);if(argc>4)motion_fraction=std::strtod(argv[4],nullptr);if(argc>5)query_count=std::strtoull(argv[5],nullptr,10);run(n,churn,frames,motion_fraction,query_count);}
