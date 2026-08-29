#include "aion/kernel/clustered_triangle.hpp"
#include "aion/kernel/geometry_fabric.hpp"
#include "aion/kernel/sparse_sdf.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)

namespace {
struct Mesh { std::vector<aion::Vec3> vertices; std::vector<std::uint32_t> indices; };
Mesh make_uv_sphere(std::size_t lon, std::size_t lat) {
    Mesh m;
    constexpr float pi = std::numbers::pi_v<float>;
    m.vertices.push_back({0,0,1});
    for (std::size_t j=1;j<lat;++j) {
        const float v=pi*static_cast<float>(j)/static_cast<float>(lat);
        const float sv=std::sin(v), cv=std::cos(v);
        for(std::size_t i=0;i<lon;++i){const float u=2*pi*static_cast<float>(i)/static_cast<float>(lon);m.vertices.push_back({sv*std::cos(u),sv*std::sin(u),cv});}
    }
    const std::uint32_t south=static_cast<std::uint32_t>(m.vertices.size());m.vertices.push_back({0,0,-1});
    auto ring=[&](std::size_t j,std::size_t i){return static_cast<std::uint32_t>(1U+(j-1U)*lon+(i%lon));};
    for(std::size_t i=0;i<lon;++i)m.indices.insert(m.indices.end(),{0U,ring(1,i+1),ring(1,i)});
    for(std::size_t j=1;j+1<lat;++j)for(std::size_t i=0;i<lon;++i)m.indices.insert(m.indices.end(),{ring(j,i),ring(j,i+1),ring(j+1,i),ring(j,i+1),ring(j+1,i+1),ring(j+1,i)});
    for(std::size_t i=0;i<lon;++i)m.indices.insert(m.indices.end(),{ring(lat-1,i),ring(lat-1,i+1),south});
    return m;
}
}

int main(){
    aion::GeometryKernel kernel;std::string error;
    aion::AnalyticSdfProvider analytic(2);aion::SparseSdfProvider sparse(3);aion::ClusteredTriangleProvider clustered(4);
    CHECK(analytic.register_with(kernel,error));CHECK(sparse.register_with(kernel,error));CHECK(clustered.register_with(kernel,error));
    const auto source=analytic.add_sphere({0,0,0},1.0F,error);CHECK(source.valid());
    aion::SparseSdfCompileOptions so;so.voxel_size=1.0F/64.0F;const auto sb=sparse.compile_from(kernel,source,so);CHECK(sb.ok());
    const auto mesh=make_uv_sphere(16,8);aion::ClusteredTriangleOptions co;co.encoding=aion::ClusteredTriangleEncoding::QuantizedU16;const auto cb=clustered.add_mesh(mesh.vertices,mesh.indices,co);CHECK(cb.ok());

    aion::GeometrySet set;CHECK(set.add_representation(sb.handle,sb.max_geometric_error,error));
    const float clustered_declared=0.06F+cb.max_geometric_error;CHECK(set.add_representation(cb.handle,clustered_declared,error));
    aion::GeometryTelemetryStore telemetry(7);
    constexpr std::uint64_t radial=1001;
    for(int i=0;i<5;++i){telemetry.record_normalized(sb.handle,set.revision(),radial,aion::ExecutionDevice::CpuReference,2048,1.0,0.9);telemetry.record_normalized(cb.handle,set.revision(),radial,aion::ExecutionDevice::CpuReference,2048,2.0,1.8);}

    aion::GeometrySelectionRequest req;req.constraints.required=aion::GeometryCapabilityMask::of({aion::GeometryCapability::RaySurface});req.constraints.max_geometric_error=0.1F;req.workload_id=radial;req.work_units=2048;req.min_observed_batches=3;
    req.objective=aion::GeometryObjective::MinStorage;auto d=aion::select_geometry(set,kernel,telemetry,req);CHECK(d.ok);CHECK(d.handle==cb.handle);
    req.objective=aion::GeometryObjective::MinGeometricError;d=aion::select_geometry(set,kernel,telemetry,req);CHECK(d.ok);CHECK(d.handle==sb.handle);
    req.objective=aion::GeometryObjective::MinQueryLatency;d=aion::select_geometry(set,kernel,telemetry,req);CHECK(d.ok);CHECK(d.handle==sb.handle);CHECK(d.pareto.size()==2U);

    req.constraints.max_geometric_error=(sb.max_geometric_error+clustered_declared)*0.5F;d=aion::select_geometry(set,kernel,telemetry,req);CHECK(d.ok);CHECK(d.handle==sb.handle);
    req.constraints.max_geometric_error=0.1F;req.constraints.max_storage_bytes=kernel.storage_bytes(cb.handle)+64U;req.objective=aion::GeometryObjective::MinStorage;d=aion::select_geometry(set,kernel,telemetry,req);CHECK(d.ok);CHECK(d.handle==cb.handle);

    constexpr std::uint64_t alternate=1002;
    for(int i=0;i<5;++i){telemetry.record_normalized(sb.handle,set.revision(),alternate,aion::ExecutionDevice::CpuReference,2048,1.0,1.0);telemetry.record_normalized(cb.handle,set.revision(),alternate,aion::ExecutionDevice::CpuReference,2048,0.5,0.5);}
    req.constraints.max_storage_bytes=std::numeric_limits<std::size_t>::max();req.objective=aion::GeometryObjective::MinQueryLatency;req.workload_id=alternate;d=aion::select_geometry(set,kernel,telemetry,req);CHECK(d.ok);CHECK(d.handle==cb.handle);

    set.source_changed();req.workload_id=radial;d=aion::select_geometry(set,kernel,telemetry,req);CHECK(!d.ok);

    std::cout<<"r4_geometry_fabric_tests: PASS\n"<<"sparse_bytes="<<kernel.storage_bytes(sb.handle)<<"\nclustered_bytes="<<kernel.storage_bytes(cb.handle)<<"\n";
    return EXIT_SUCCESS;
}
