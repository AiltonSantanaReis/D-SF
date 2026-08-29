#include "aion/kernel/spatial.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;

template<class F> double ms(F&& f){const auto a=Clock::now();f();const auto b=Clock::now();return std::chrono::duration<double,std::milli>(b-a).count();}

std::vector<aion::SpatialRecord> make_records(std::size_t n,std::uint32_t seed){
    std::mt19937 rng(seed);std::uniform_real_distribution<float> p(-100000.0F,100000.0F),e(0.2F,5.0F);
    std::vector<aion::SpatialRecord> r;r.reserve(n);for(std::size_t i=0;i<n;++i)r.push_back({static_cast<aion::EntityId>(i+1),{p(rng),p(rng),p(rng)},{e(rng),e(rng),e(rng)}});return r;
}

std::vector<aion::Aabb> make_queries(std::size_t n,std::uint32_t seed){
    std::mt19937 rng(seed);std::uniform_real_distribution<float> p(-100000.0F,100000.0F),h(50.0F,2500.0F);std::vector<aion::Aabb> q;q.reserve(n);for(std::size_t i=0;i<n;++i){aion::Vec3 c{p(rng),p(rng),p(rng)};float x=h(rng);q.push_back({{c.x-x,c.y-x,c.z-x},{c.x+x,c.y+x,c.z+x}});}return q;
}

std::vector<aion::Ray> make_rays(std::size_t n,std::uint32_t seed){
    std::mt19937 rng(seed);std::uniform_real_distribution<float> p(-100000.0F,100000.0F),d(-0.25F,0.25F);std::vector<aion::Ray> r;r.reserve(n);for(std::size_t i=0;i<n;++i)r.push_back({{-120000.0F,p(rng),p(rng)},{1.0F,d(rng),d(rng)},0.0F,300000.0F});return r;
}

struct QueryStats{double aabb_ms{};double ray_ms{};std::uint64_t checksum{};};

template<class View> QueryStats query_stats(const View& v,const aion::SpatialSnapshot& s,const std::vector<aion::Aabb>& qs,const std::vector<aion::Ray>& rs){QueryStats st;st.aabb_ms=ms([&]{for(const auto&q:qs){auto x=v.query_aabb(s,q);if(!x.ok)std::abort();st.checksum+=x.entities.size();if(!x.entities.empty())st.checksum+=x.entities.front();}});st.ray_ms=ms([&]{for(const auto&r:rs){auto x=v.raycast(s,r);if(!x.ok)std::abort();st.checksum+=x.entity;}});return st;}

aion::PatchTransaction make_sparse_patch(const aion::SpatialSnapshot&s,std::size_t changes,std::uint64_t txid){aion::PatchTransaction tx{.id=txid};changes=std::min(changes,s.size());tx.scalar_mutations.reserve(changes);for(std::size_t i=0;i<changes;++i){const auto slot=static_cast<std::uint32_t>((i*7919U)%s.size());const auto id=s.entity(slot);const auto c=s.center(slot);tx.scalar_mutations.push_back({aion::MutationKind::SetPosition,id,{c.x+3.0F,c.y-1.0F,c.z+0.5F},0});}return tx;}

aion::PatchTransaction make_dense_patch(const aion::SpatialSnapshot&s,std::size_t changes,std::uint64_t txid){aion::PatchTransaction tx{.id=txid};changes=std::min(changes,s.size());aion::Vec3RangePatch p{.component=aion::PatchComponent::Position,.first=1};p.values.reserve(changes);for(std::size_t i=0;i<changes;++i){const auto c=s.center(static_cast<std::uint32_t>(i));p.values.push_back({c.x-2.0F,c.y+1.5F,c.z+0.25F});}tx.vec3_patches.push_back(std::move(p));return tx;}

void run(std::size_t n){
    std::string err;auto records=make_records(n,0xD5F30011U);aion::SpatialSnapshot s;
    const double snapshot_build=ms([&]{if(!s.build(records,err)){std::cerr<<err<<'\n';std::abort();}});
    aion::WideBvh8View sah; aion::MortonBvh8View morton;
    const double sah_build=ms([&]{if(!sah.build(s,err))std::abort();});
    const double morton_build=ms([&]{if(!morton.build(s,err))std::abort();});
    auto qs=make_queries(500,0xD5F30012U);auto rays=make_rays(500,0xD5F30013U);
    auto sah_q=query_stats(sah,s,qs,rays);auto morton_q=query_stats(morton,s,qs,rays);

    // Sparse 2% spatial change: snapshot publishes once; SAH path refits, Morton rebuilds.
    const auto sparse_tx=make_sparse_patch(s,std::max<std::size_t>(1,n/50),2);
    aion::SpatialApplyResult sparse_apply;const double sparse_snapshot=ms([&]{sparse_apply=s.apply_patch_transaction(sparse_tx);if(!sparse_apply.ok)std::abort();});
    const double sparse_sah=ms([&]{if(!sah.sync(s,sparse_apply.changes,err))std::abort();});
    const double sparse_morton=ms([&]{if(!morton.sync(s,sparse_apply.changes,err))std::abort();});

    // High churn 50%: current SAH policy rebuilds above 20%; Morton is rebuild-native.
    const auto dense_tx=make_dense_patch(s,n/2,3);aion::SpatialApplyResult dense_apply;
    const double dense_snapshot=ms([&]{dense_apply=s.apply_patch_transaction(dense_tx);if(!dense_apply.ok)std::abort();});
    const double dense_sah=ms([&]{if(!sah.sync(s,dense_apply.changes,err))std::abort();});
    const double dense_morton=ms([&]{if(!morton.sync(s,dense_apply.changes,err))std::abort();});

    auto sah_after=query_stats(sah,s,qs,rays);auto morton_after=query_stats(morton,s,qs,rays);
    if(sah_after.checksum!=morton_after.checksum){std::cerr<<"query checksum mismatch\n";std::abort();}

    const std::size_t shared=s.storage_bytes()+sah.storage_bytes()+morton.storage_bytes();
    const std::size_t modeled_duplicate=sah.storage_bytes()+morton.storage_bytes()+2U*n*(sizeof(aion::Aabb)+sizeof(aion::EntityId));
    const double saved_pct=modeled_duplicate?100.0*(1.0-static_cast<double>(shared)/static_cast<double>(modeled_duplicate)):0.0;

    std::cout<<std::fixed<<std::setprecision(3)
      <<"entities="<<n<<'\n'
      <<"snapshot_build_ms="<<snapshot_build<<'\n'
      <<"sah_build_ms="<<sah_build<<'\n'
      <<"morton_build_ms="<<morton_build<<'\n'
      <<"sah_aabb_500_ms="<<sah_q.aabb_ms<<'\n'
      <<"sah_ray_500_ms="<<sah_q.ray_ms<<'\n'
      <<"morton_aabb_500_ms="<<morton_q.aabb_ms<<'\n'
      <<"morton_ray_500_ms="<<morton_q.ray_ms<<'\n'
      <<"sparse_2pct_snapshot_ms="<<sparse_snapshot<<'\n'
      <<"sparse_2pct_sah_sync_ms="<<sparse_sah<<'\n'
      <<"sparse_2pct_morton_sync_ms="<<sparse_morton<<'\n'
      <<"dense_50pct_snapshot_ms="<<dense_snapshot<<'\n'
      <<"dense_50pct_sah_sync_ms="<<dense_sah<<'\n'
      <<"dense_50pct_morton_sync_ms="<<dense_morton<<'\n'
      <<"sparse_dirty_slots="<<sparse_apply.changes.dirty_slots.size()<<'\n'
      <<"sparse_dirty_ranges="<<sparse_apply.changes.dirty_ranges.size()<<'\n'
      <<"dense_dirty_slots="<<dense_apply.changes.dirty_slots.size()<<'\n'
      <<"dense_dirty_ranges="<<dense_apply.changes.dirty_ranges.size()<<'\n'
      <<"snapshot_bytes="<<s.storage_bytes()<<'\n'
      <<"sah_view_bytes="<<sah.storage_bytes()<<'\n'
      <<"morton_view_bytes="<<morton.storage_bytes()<<'\n'
      <<"shared_total_bytes="<<shared<<'\n'
      <<"modeled_two_view_duplicate_bytes="<<modeled_duplicate<<'\n'
      <<"modeled_memory_saved_pct="<<saved_pct<<'\n'
      <<"checksum="<<sah_after.checksum<<'\n';
}
}

int main(int argc,char**argv){std::size_t n=100000;if(argc>1)n=static_cast<std::size_t>(std::strtoull(argv[1],nullptr,10));run(n);return 0;}
