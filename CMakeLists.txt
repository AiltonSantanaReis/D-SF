cmake_minimum_required(VERSION 3.24)
project(AionResearchLab VERSION 0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AION_ENABLE_WARNINGS "Enable strict compiler warnings" ON)

add_library(aion_kernel
    src/world.cpp
    src/journal.cpp
    src/execution.cpp
    src/patch.cpp
    src/spatial.cpp
    src/spatial_fabric.cpp
    src/geometry.cpp
    src/sparse_sdf.cpp
    src/clustered_triangle.cpp
    src/geometry_fabric.cpp
    src/device.cpp
    src/device_geometry.cpp
    src/device_work.cpp
    src/device_backend.cpp
)
target_include_directories(aion_kernel PUBLIC include)

if(AION_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(aion_kernel PRIVATE /W4 /permissive-)
    else()
        target_compile_options(aion_kernel PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endif()

add_executable(aion_lab src/main.cpp)
target_link_libraries(aion_lab PRIVATE aion_kernel)

add_executable(aion_tests tests/kernel_tests.cpp)
target_link_libraries(aion_tests PRIVATE aion_kernel)

enable_testing()
add_test(NAME kernel_tests COMMAND aion_tests)

add_executable(aion_bench bench/state_bench.cpp)
target_link_libraries(aion_bench PRIVATE aion_kernel)

add_executable(aion_r1_bench bench/r1_journal_bench.cpp)
target_link_libraries(aion_r1_bench PRIVATE aion_kernel)

add_executable(aion_r2_tests tests/r2_execution_tests.cpp)
target_link_libraries(aion_r2_tests PRIVATE aion_kernel)
add_test(NAME r2_execution_tests COMMAND aion_r2_tests)

add_executable(aion_r2_bench bench/r2_execution_bench.cpp)
target_link_libraries(aion_r2_bench PRIVATE aion_kernel)

add_executable(aion_r21_tests tests/r21_patch_tests.cpp)
target_link_libraries(aion_r21_tests PRIVATE aion_kernel)
add_test(NAME r21_patch_tests COMMAND aion_r21_tests)

add_executable(aion_r21_bench bench/r21_patch_bench.cpp)
target_link_libraries(aion_r21_bench PRIVATE aion_kernel)

add_executable(aion_r21_sparse_bench bench/r21_sparse_bench.cpp)
target_link_libraries(aion_r21_sparse_bench PRIVATE aion_kernel)

add_executable(aion_r21_execution_tests tests/r21_execution_patch_tests.cpp)
target_link_libraries(aion_r21_execution_tests PRIVATE aion_kernel)
add_test(NAME r21_execution_patch_tests COMMAND aion_r21_execution_tests)

add_executable(aion_r21_execution_bench bench/r21_execution_bench.cpp)
target_link_libraries(aion_r21_execution_bench PRIVATE aion_kernel)

add_executable(aion_r3_snapshot_tests tests/r3_snapshot_tests.cpp)
target_link_libraries(aion_r3_snapshot_tests PRIVATE aion_kernel)
add_test(NAME r3_snapshot_tests COMMAND aion_r3_snapshot_tests)

add_executable(aion_r3_snapshot_bench bench/r3_snapshot_bench.cpp)
target_link_libraries(aion_r3_snapshot_bench PRIVATE aion_kernel)

add_executable(aion_r3_dynamic_policy_bench bench/r3_dynamic_policy_bench.cpp)
target_link_libraries(aion_r3_dynamic_policy_bench PRIVATE aion_kernel)

add_executable(aion_r3_fabric_tests tests/r3_fabric_tests.cpp)
target_link_libraries(aion_r3_fabric_tests PRIVATE aion_kernel)
add_test(NAME r3_fabric_tests COMMAND aion_r3_fabric_tests)

add_executable(aion_r3_fabric_bench bench/r3_fabric_bench.cpp)
target_link_libraries(aion_r3_fabric_bench PRIVATE aion_kernel)
add_executable(aion_r3_cost_policy_bench bench/r3_cost_policy_bench.cpp)
target_link_libraries(aion_r3_cost_policy_bench PRIVATE aion_kernel)

add_executable(aion_r32_budgeted_tests tests/r32_budgeted_spatial_tests.cpp)
target_link_libraries(aion_r32_budgeted_tests PRIVATE aion_kernel)
add_test(NAME r32_budgeted_spatial_tests COMMAND aion_r32_budgeted_tests)

add_executable(aion_r32_budgeted_bench bench/r32_budgeted_build_bench.cpp)
target_link_libraries(aion_r32_budgeted_bench PRIVATE aion_kernel)

add_executable(aion_r32_quality_bench bench/r32_quality_bench.cpp)
target_link_libraries(aion_r32_quality_bench PRIVATE aion_kernel)

add_executable(aion_r32_dirty_queue_bench bench/r32_dirty_queue_bench.cpp)
target_link_libraries(aion_r32_dirty_queue_bench PRIVATE aion_kernel)

add_executable(aion_r4_geometry_tests tests/r4_geometry_contract_tests.cpp)
target_link_libraries(aion_r4_geometry_tests PRIVATE aion_kernel)
add_test(NAME r4_geometry_contract_tests COMMAND aion_r4_geometry_tests)

add_executable(aion_r4_geometry_bench bench/r4_geometry_contract_bench.cpp)
target_link_libraries(aion_r4_geometry_bench PRIVATE aion_kernel)

add_executable(aion_r4_sparse_sdf_tests tests/r4_sparse_sdf_tests.cpp)
target_link_libraries(aion_r4_sparse_sdf_tests PRIVATE aion_kernel)
add_test(NAME r4_sparse_sdf_tests COMMAND aion_r4_sparse_sdf_tests)
add_executable(aion_r4_sparse_sdf_bench bench/r4_sparse_sdf_bench.cpp)
target_link_libraries(aion_r4_sparse_sdf_bench PRIVATE aion_kernel)
add_executable(aion_r4_sparse_sdf_shape_bench bench/r4_sparse_sdf_shape_bench.cpp)
target_link_libraries(aion_r4_sparse_sdf_shape_bench PRIVATE aion_kernel)
add_executable(aion_r4_sparse_sdf_scale_bench bench/r4_sparse_sdf_scale_bench.cpp)
target_link_libraries(aion_r4_sparse_sdf_scale_bench PRIVATE aion_kernel)
add_executable(aion_r4_sparse_vs_dense_bench bench/r4_sparse_vs_dense_bench.cpp)
target_link_libraries(aion_r4_sparse_vs_dense_bench PRIVATE aion_kernel)

add_executable(aion_r4_clustered_triangle_tests tests/r4_clustered_triangle_tests.cpp)
target_link_libraries(aion_r4_clustered_triangle_tests PRIVATE aion_kernel)
add_test(NAME r4_clustered_triangle_tests COMMAND aion_r4_clustered_triangle_tests)

add_executable(aion_r4_geometry_fabric_tests tests/r4_geometry_fabric_tests.cpp)
target_link_libraries(aion_r4_geometry_fabric_tests PRIVATE aion_kernel)
add_test(NAME aion_r4_geometry_fabric_tests COMMAND aion_r4_geometry_fabric_tests)

add_executable(aion_r4_online_geometry_telemetry_tests tests/r4_online_geometry_telemetry_tests.cpp)
target_link_libraries(aion_r4_online_geometry_telemetry_tests PRIVATE aion_kernel)
add_test(NAME aion_r4_online_geometry_telemetry_tests COMMAND aion_r4_online_geometry_telemetry_tests)

add_executable(aion_r4_online_geometry_routing_tests tests/r4_online_geometry_routing_tests.cpp)
target_link_libraries(aion_r4_online_geometry_routing_tests PRIVATE aion_kernel)
add_test(NAME aion_r4_online_geometry_routing_tests COMMAND aion_r4_online_geometry_routing_tests)

add_executable(aion_r4f_inactive_drift_sweep bench/r4f_inactive_drift_sweep.cpp)
target_link_libraries(aion_r4f_inactive_drift_sweep PRIVATE aion_kernel)

add_executable(aion_r5_device_tests tests/r5_device_residency_tests.cpp)
target_link_libraries(aion_r5_device_tests PRIVATE aion_kernel)
add_test(NAME aion_r5_device_tests COMMAND aion_r5_device_tests)

add_executable(aion_r5_geometry_device_tests tests/r5_geometry_device_tests.cpp)
target_link_libraries(aion_r5_geometry_device_tests PRIVATE aion_kernel)
add_test(NAME aion_r5_geometry_device_tests COMMAND aion_r5_geometry_device_tests)

add_executable(aion_r5_device_work_tests tests/r5_device_work_tests.cpp)
target_link_libraries(aion_r5_device_work_tests PRIVATE aion_kernel)
add_test(NAME aion_r5_device_work_tests COMMAND aion_r5_device_work_tests)

add_executable(aion_r5_device_work_scaling_bench bench/r5_device_work_scaling_bench.cpp)
target_link_libraries(aion_r5_device_work_scaling_bench PRIVATE aion_kernel)

add_executable(aion_r5_backend_translation_tests tests/r5_backend_translation_tests.cpp)
target_link_libraries(aion_r5_backend_translation_tests PRIVATE aion_kernel)
add_test(NAME aion_r5_backend_translation_tests COMMAND aion_r5_backend_translation_tests)

add_executable(aion_r5_backend_translation_bench bench/r5_backend_translation_bench.cpp)
target_link_libraries(aion_r5_backend_translation_bench PRIVATE aion_kernel)
