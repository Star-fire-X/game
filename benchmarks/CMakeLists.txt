cmake_minimum_required(VERSION 3.16)

find_package(benchmark CONFIG REQUIRED)

add_executable(combat_core_benchmark
    combat_core_benchmark.cpp
    ../src/server/ecs/systems/combat_core.cc
)

target_link_libraries(combat_core_benchmark PRIVATE
    legend2_common
    benchmark::benchmark
)

target_include_directories(combat_core_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/src/server
)

if(MSVC)
    target_compile_options(combat_core_benchmark PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(combat_core_benchmark PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()

target_compile_definitions(combat_core_benchmark PRIVATE
    $<$<CONFIG:Release>:NDEBUG>
)

add_executable(ecs_performance_benchmark
    ecs_performance_benchmark.cpp
)

if(MSVC)
    target_link_libraries(ecs_performance_benchmark PRIVATE
        legend2_common
        mir2_server_lib
        mir2_storage_engine
        benchmark::benchmark
    )
else()
    target_link_libraries(ecs_performance_benchmark PRIVATE
        legend2_common
        -Wl,--start-group
        mir2_server_lib
        mir2_storage_engine
        -Wl,--end-group
        benchmark::benchmark
    )
endif()

if(MSVC)
    target_compile_options(ecs_performance_benchmark PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(ecs_performance_benchmark PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()

target_compile_definitions(ecs_performance_benchmark PRIVATE
    $<$<CONFIG:Release>:NDEBUG>
)
