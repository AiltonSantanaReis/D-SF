#include "aion/kernel/spatial_fabric.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
template<class F> double ms(F&&f){const auto a=Clock::now();f();const auto b=Clock::now();return std::chrono::duration<double,std::milli>(b-a).count();}

std::vector<aion::SpatialRecord> records(std::size_t n){std::mt19937 r(0xD5F34101U);std::uniform_real_distribution<float>p(-100000,100000),e(.2F,5.F);std::vector<aion::SpatialRecord>x;x.reserve(n);for(std::size_t i=0;i<n;++i)x.push_back({static_cast<aion::EntityId>(i+1),{p(r),p(r),p(r)},{e(r),e(r),e(r)}});return x;}
std::vector<aion::Aabb> queries(std::size_t n){std::mt19937 r(0xD5F34102U);std::uniform_real_distribution<float>p(-100000,100000),h(50,2500);std::vector<aion::Aabb>x;x.reserve(n);for(std::size_t i=0;i<n;++i){aion::Vec3 c{p(r),p(r),p(r)};const float z=h(r);x.push_back({{c.x-z,c.y-z,c.z-z},{c.x+z,c.y+z,c.z+z}});}return x;}

aion::PatchTransaction local_patch(const aion::SpatialSnapshot&s,std::uint64_t id,std::size_t count,float fraction){aion::PatchTransaction tx{.id=id};count=std::min(count,s.size());tx.scalar_mutations.reserve(count);const float radius=fraction*s.coherence_scale();for(std::size_t i=0;i<count;++i){const auto slot=static_cast<std::uint32_t>((i*7919U)%s.size());auto c=s.center(slot);const float f=static_cast<float>((i%17)+1)/17.0F;c.x+=radius*f;c.y-=radius*f*.5F;c.z+=radius*f*.25F;tx.scalar_mutations.push_back({aion::MutationKind::SetPosition,s.entity(slot),c,0});}return tx;}

template<class V> double query_batch(const V&v,const aion::SpatialSnapshot&s,const std::vector<aion::Aabb>&q,std::size_t limit,std::uint64_t&sum){limit=std::min(limit,q.size());return ms([&]{for(std::size_t i=0;i<limit;++i){const auto r=v.query_aabb(s,q[i]);if(!r.ok)std::abort();sum+=r.entities.size();}});}
}

int main(int argc,char**argv){
    std::size_t n=250000,expected_queries=500,sample_queries=64;
    if(argc>1)n=std::strtoull(argv[1],nullptr,10);
    if(argc>2)expected_queries=std::strtoull(argv[2],nullptr,10);
    std::string error; auto rec=records(n); auto q=queries(expected_queries);
    aion::SpatialSnapshot snapshot; if(!snapshot.build(rec,error)){std::cerr<<error<<'\n';return 2;}
    aion::MortonBvh8View morton; if(!morton.build(snapshot,error)){std::cerr<<error<<'\n';return 2;}

    aion::AsyncSahBuilder builder; if(!builder.start(snapshot,error)){std::cerr<<error<<'\n';return 2;}
    std::size_t foreground_frames=0; double foreground_update=0,foreground_query=0; std::uint64_t foreground_sum=0; std::uint64_t txid=2;
    while(builder.busy() && foreground_frames<200){
        const auto applied=snapshot.apply_patch_transaction(local_patch(snapshot,txid++,std::max<std::size_t>(1,n/200),0.03F)); if(!applied.ok)std::abort(); if(!builder.observe_changes(applied.changes,error))std::abort();
        foreground_update+=ms([&]{if(!morton.sync(snapshot,applied.changes,error))std::abort();});
        foreground_query+=query_batch(morton,snapshot,q,std::min<std::size_t>(64,q.size()),foreground_sum);
        ++foreground_frames;
    }

    aion::WideBvh8View sah; const auto promotion=builder.wait_promote(snapshot,sah); if(promotion.state!=aion::AsyncSahPromoteState::Promoted){std::cerr<<promotion.error<<'\n';return 3;}

    // One shadow-calibration frame. Both candidates see exactly the same SpatialChangeSet.
    const auto calibration=snapshot.apply_patch_transaction(local_patch(snapshot,txid++,std::max<std::size_t>(1,n/500),0.02F)); if(!calibration.ok)std::abort();
    const double sah_update=ms([&]{if(!sah.sync(snapshot,calibration.changes,error,aion::WideBvhSyncMode::RefitOnly))std::abort();});
    const double morton_update=ms([&]{if(!morton.sync(snapshot,calibration.changes,error))std::abort();});
    std::uint64_t sah_sample_sum=0,morton_sample_sum=0;
    const double sah_sample=query_batch(sah,snapshot,q,sample_queries,sah_sample_sum);
    const double morton_sample=query_batch(morton,snapshot,q,sample_queries,morton_sample_sum);
    if(sah_sample_sum!=morton_sample_sum)std::abort();
    const auto decision=aion::choose_spatial_backend({sah_update,sah_sample,sample_queries},{morton_update,morton_sample,sample_queries},expected_queries);

    std::uint64_t sah_full_sum=0,morton_full_sum=0;
    const double sah_full=query_batch(sah,snapshot,q,expected_queries,sah_full_sum);
    const double morton_full=query_batch(morton,snapshot,q,expected_queries,morton_full_sum);
    if(sah_full_sum!=morton_full_sum)std::abort();
    const double sah_actual=sah_update+sah_full;
    const double morton_actual=morton_update+morton_full;
    const auto actual=sah_actual<=morton_actual?aion::SpatialBackend::WideSah:aion::SpatialBackend::Morton;

    std::cout<<std::fixed<<std::setprecision(3)
      <<"entities="<<n<<'\n'<<"expected_queries="<<expected_queries<<'\n'
      <<"snapshot_copy_ms="<<promotion.stats.snapshot_copy_ms<<'\n'
      <<"background_sah_build_ms="<<promotion.stats.background_build_ms<<'\n'
      <<"catchup_full_refit_ms="<<promotion.stats.catchup_refit_ms<<'\n'
      <<"foreground_frames_during_build="<<foreground_frames<<'\n'
      <<"foreground_avg_morton_update_ms="<<(foreground_frames?foreground_update/foreground_frames:0.0)<<'\n'
      <<"foreground_avg_query64_ms="<<(foreground_frames?foreground_query/foreground_frames:0.0)<<'\n'
      <<"calibration_sah_update_ms="<<sah_update<<'\n'<<"calibration_morton_update_ms="<<morton_update<<'\n'
      <<"calibration_sah_query"<<sample_queries<<"_ms="<<sah_sample<<'\n'
      <<"calibration_morton_query"<<sample_queries<<"_ms="<<morton_sample<<'\n'
      <<"predicted_sah_total_ms="<<decision.predicted_sah_ms<<'\n'
      <<"predicted_morton_total_ms="<<decision.predicted_morton_ms<<'\n'
      <<"actual_sah_total_ms="<<sah_actual<<'\n'<<"actual_morton_total_ms="<<morton_actual<<'\n'
      <<"decision="<<(decision.backend==aion::SpatialBackend::WideSah?"SAH":"Morton")<<'\n'
      <<"actual_winner="<<(actual==aion::SpatialBackend::WideSah?"SAH":"Morton")<<'\n'
      <<"decision_matches_actual="<<(decision.backend==actual?"yes":"no")<<'\n'
      <<"checksum="<<sah_full_sum<<'\n';
}
