#include "aion/kernel/spatial_fabric.hpp"

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
std::vector<aion::SpatialRecord> records(std::size_t n){std::mt19937 r(0xD5F35001U);std::uniform_real_distribution<float>p(-100000,100000),e(.2F,5.F);std::vector<aion::SpatialRecord>x;x.reserve(n);for(std::size_t i=0;i<n;++i)x.push_back({static_cast<aion::EntityId>(i+1),{p(r),p(r),p(r)},{e(r),e(r),e(r)}});return x;}
std::vector<aion::Aabb> queries(std::size_t n){std::mt19937 r(0xD5F35002U);std::uniform_real_distribution<float>p(-100000,100000),h(50,2500);std::vector<aion::Aabb>x;x.reserve(n);for(std::size_t i=0;i<n;++i){aion::Vec3 c{p(r),p(r),p(r)};float z=h(r);x.push_back({{c.x-z,c.y-z,c.z-z},{c.x+z,c.y+z,c.z+z}});}return x;}
aion::PatchTransaction patch(const aion::SpatialSnapshot&s,std::uint64_t id,double churn,double motion){const auto count=std::max<std::size_t>(1,static_cast<std::size_t>(std::llround(static_cast<double>(s.size())*churn)));const float radius=static_cast<float>(motion)*s.coherence_scale();aion::PatchTransaction tx{.id=id};tx.scalar_mutations.reserve(count);for(std::size_t i=0;i<count;++i){auto slot=static_cast<std::uint32_t>((i*7919U)%s.size());auto c=s.center(slot);float f=static_cast<float>((i%31)+1)/31.0F;c.x+=radius*f;c.y-=radius*f*.7F;c.z+=radius*f*.3F;tx.scalar_mutations.push_back({aion::MutationKind::SetPosition,s.entity(slot),c,0});}return tx;}
template<class V> double qb(const V&v,const aion::SpatialSnapshot&s,const std::vector<aion::Aabb>&q,std::size_t n,std::uint64_t&sum){n=std::min(n,q.size());return ms([&]{for(std::size_t i=0;i<n;++i){auto r=v.query_aabb(s,q[i]);if(!r.ok)std::abort();sum+=r.entities.size();}});}
}
int main(int argc,char**argv){std::size_t n=100000,qcount=500;double churn=.02,motion=5.0;int warm=9;if(argc>1)qcount=std::strtoull(argv[1],nullptr,10);if(argc>2)motion=std::strtod(argv[2],nullptr);std::string err;auto rec=records(n);aion::SpatialSnapshot a,b;if(!a.build(rec,err)||!b.build(rec,err))return 2;aion::WideBvh8View sah;aion::MortonBvh8View morton;if(!sah.build(a,err)||!morton.build(b,err))return 2;std::uint64_t txid=2;for(int f=0;f<warm;++f){auto pa=a.apply_patch_transaction(patch(a,txid,churn,motion));auto pb=b.apply_patch_transaction(patch(b,txid,churn,motion));if(!pa.ok||!pb.ok)std::abort();if(!sah.sync(a,pa.changes,err,aion::WideBvhSyncMode::RefitOnly)||!morton.sync(b,pb.changes,err))std::abort();++txid;}
const auto pa=a.apply_patch_transaction(patch(a,txid,churn,motion));const auto pb=b.apply_patch_transaction(patch(b,txid,churn,motion));double su=ms([&]{if(!sah.sync(a,pa.changes,err,aion::WideBvhSyncMode::RefitOnly))std::abort();});double mu=ms([&]{if(!morton.sync(b,pb.changes,err))std::abort();});auto q=queries(std::max<std::size_t>(qcount,64));std::uint64_t s1=0,m1=0;double ss=qb(sah,a,q,64,s1),msamp=qb(morton,b,q,64,m1);if(s1!=m1)std::abort();auto d=aion::choose_spatial_backend({su,ss,64},{mu,msamp,64},qcount);std::uint64_t sf=0,mf=0;double sq=qb(sah,a,q,qcount,sf),mq=qb(morton,b,q,qcount,mf);if(sf!=mf)std::abort();double st=su+sq,mt=mu+mq;auto actual=st<=mt?aion::SpatialBackend::WideSah:aion::SpatialBackend::Morton;std::cout<<std::fixed<<std::setprecision(3)<<"queries="<<qcount<<" motion="<<motion<<" debt="<<pa.changes.topology_debt_delta*(warm+1)<<'\n'<<"sah_update_ms="<<su<<" morton_update_ms="<<mu<<'\n'<<"sah_sample64_ms="<<ss<<" morton_sample64_ms="<<msamp<<'\n'<<"pred_sah="<<d.predicted_sah_ms<<" pred_morton="<<d.predicted_morton_ms<<'\n'<<"actual_sah="<<st<<" actual_morton="<<mt<<'\n'<<"decision="<<(d.backend==aion::SpatialBackend::WideSah?"SAH":"Morton")<<" actual="<<(actual==aion::SpatialBackend::WideSah?"SAH":"Morton")<<" match="<<(d.backend==actual?"yes":"no")<<'\n';}
