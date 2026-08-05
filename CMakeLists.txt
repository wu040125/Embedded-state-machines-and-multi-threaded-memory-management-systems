cmake_minimum_required(VERSION 3.22)

project(
    EdgeSentinel
    VERSION 0.1.0
    DESCRIPTION "Linux edge runtime for simulated motor health monitoring"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Sanitizers)

option(EDGE_SENTINEL_BUILD_TESTS "Build the EdgeSentinel tests" ON)
option(EDGE_SENTINEL_BUILD_BENCHMARKS "Build the EdgeSentinel benchmarks" ON)
option(EDGE_SENTINEL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(EDGE_SENTINEL_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(EDGE_SENTINEL_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(EDGE_SENTINEL_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

find_package(Threads REQUIRED)

add_library(edge_sentinel_core INTERFACE)
add_library(EdgeSentinel::core ALIAS edge_sentinel_core)
target_compile_features(edge_sentinel_core INTERFACE cxx_std_20)
target_include_directories(
    edge_sentinel_core
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

add_executable(edge_sentinel apps/edge_sentinel_main.cpp)
target_link_libraries(edge_sentinel PRIVATE EdgeSentinel::core Threads::Threads)
edge_sentinel_set_warnings(edge_sentinel)
edge_sentinel_enable_sanitizers(edge_sentinel)

if(EDGE_SENTINEL_BUILD_TESTS)
    include(CTest)
    enable_testing()

    function(edge_sentinel_add_test test_name source_file)
        add_executable(${test_name} ${source_file})
        target_link_libraries(${test_name} PRIVATE EdgeSentinel::core Threads::Threads)
        target_include_directories(${test_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        edge_sentinel_set_warnings(${test_name})
        edge_sentinel_enable_sanitizers(${test_name})
        add_test(NAME ${test_name} COMMAND ${test_name})
    endfunction()

    edge_sentinel_add_test(smoke_test tests/smoke_test.cpp)
    edge_sentinel_add_test(memory_pool_test tests/memory_pool_test.cpp)
    edge_sentinel_add_test(bounded_queue_test tests/bounded_queue_test.cpp)
endif()
