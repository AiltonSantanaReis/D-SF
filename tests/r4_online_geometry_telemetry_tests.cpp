#include "aion/kernel/geometry_fabric.hpp"
#include "aion/kernel/geometry.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)

int main(){
    aion::GeometryTelemetryStore store(7);const aion::GeometryHandle a{2,1,0},b{2,1,1};constexpr std::uint64_t rev=1,work=77;
    for(int i=0;i<6;++i)store.record_normalized(a,rev,work,aion::ExecutionDevice::CpuReference,32,1.0,0.9);
    store.record_normalized(a,rev,work,aion::ExecutionDevice::CpuReference,32,50.0,49.0);
    auto s=store.summary(a,rev,work,aion::ExecutionDevice::CpuReference,32);CHECK(s.available);CHECK(s.samples==7U);CHECK(s.median_us_per_unit==1.0);CHECK(s.p90_us_per_unit==50.0);

    for(int i=0;i<7;++i)store.record_normalized(a,rev,work,aion::ExecutionDevice::CpuReference,32,4.0,3.8);
    s=store.summary(a,rev,work,aion::ExecutionDevice::CpuReference,32);CHECK(s.median_us_per_unit==4.0);

    store.record_normalized(b,rev,work,aion::ExecutionDevice::CpuReference,32,2.0,2.0);
    const auto aged=store.summary(a,rev,work,aion::ExecutionDevice::CpuReference,32);CHECK(aged.age==1U);
    store.record_normalized(b,rev,work+1U,aion::ExecutionDevice::CpuReference,32,2.0,2.0);
    CHECK(store.summary(a,rev,work,aion::ExecutionDevice::CpuReference,32).age==1U);
    store.record_normalized(b,rev,work,aion::ExecutionDevice::Gpu,32,2.0,2.0);
    CHECK(store.summary(a,rev,work,aion::ExecutionDevice::CpuReference,32).age==1U);
    store.record_normalized(b,rev,work,aion::ExecutionDevice::CpuReference,2048,2.0,2.0);
    CHECK(store.summary(a,rev,work,aion::ExecutionDevice::CpuReference,32).age==1U);

    store.reset_epoch(a,rev,work,aion::ExecutionDevice::CpuReference,32);CHECK(store.summary(a,rev,work,aion::ExecutionDevice::CpuReference,32).samples==0U);
    store.record_normalized(a,rev,work,aion::ExecutionDevice::CpuReference,32,0.5,0.5);s=store.summary(a,rev,work,aion::ExecutionDevice::CpuReference,32);CHECK(s.samples==1U);CHECK(s.epoch==2U);

    int executions=0;const auto observed=aion::ExecutionKernel::observe_batch(aion::ExecutionDevice::CpuReference,123,16,[&]{++executions;});CHECK(executions==1);CHECK(observed.work_units==16U);CHECK(observed.workload_id==123U);

    aion::GeometryObservationSampler sampler;aion::GeometryObservationSamplingPolicy policy;policy.warmup_samples=3;policy.work_units_per_sample=128;policy.max_gap_batches=8;
    aion::GeometryTelemetryStore sampling_store(7);int observed_count=0;
    for(int batch=0;batch<20;++batch){if(sampler.should_observe(a,rev,55,aion::ExecutionDevice::CpuReference,1,sampling_store,policy)){++observed_count;sampling_store.record_normalized(a,rev,55,aion::ExecutionDevice::CpuReference,1,1.0,1.0);}}
    CHECK(observed_count>=4);CHECK(observed_count<20);

    // High variance remains visible via MAD.
    aion::GeometryTelemetryStore noisy(7);for(int i=1;i<=7;++i)noisy.record_normalized(a,rev,88,aion::ExecutionDevice::CpuReference,32,static_cast<double>(i),static_cast<double>(i));
    const auto ns=noisy.summary(a,rev,88,aion::ExecutionDevice::CpuReference,32);CHECK(ns.mad_us_per_unit>=2.0);

    std::cout<<"r4_online_geometry_telemetry_tests: PASS\nobserved_micro_batches="<<observed_count<<'\n';return EXIT_SUCCESS;
}
