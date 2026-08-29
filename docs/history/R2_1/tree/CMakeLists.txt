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
