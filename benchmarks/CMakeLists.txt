cmake_minimum_required(VERSION 3.16)

find_package(benchmark CONFIG REQUIRED)

add_executable(combat_core_benchmark
    combat_core_benchmark.cpp
)

target_link_libraries(combat_core_benchmark PRIVATE
    legend2_common
    benchmark::benchmark
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

target_link_libraries(ecs_performance_benchmark PRIVATE
    legend2_common
    mir2_server_lib
    benchmark::benchmark
)

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
