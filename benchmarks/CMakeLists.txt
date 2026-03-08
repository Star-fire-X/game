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

add_executable(handler_registry_dispatch_benchmark
    handler_registry_dispatch_benchmark.cpp
)

if(MSVC)
    target_link_libraries(handler_registry_dispatch_benchmark PRIVATE
        legend2_common
        mir2_server_lib
        mir2_storage_engine
        benchmark::benchmark
    )
else()
    target_link_libraries(handler_registry_dispatch_benchmark PRIVATE
        legend2_common
        -Wl,--start-group
        mir2_server_lib
        mir2_storage_engine
        -Wl,--end-group
        benchmark::benchmark
    )
endif()

target_include_directories(handler_registry_dispatch_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/src/server
)

if(MSVC)
    target_compile_options(handler_registry_dispatch_benchmark PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(handler_registry_dispatch_benchmark PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()

target_compile_definitions(handler_registry_dispatch_benchmark PRIVATE
    $<$<CONFIG:Release>:NDEBUG>
)

add_executable(coroutine_executor_stress_benchmark
    coroutine_executor_stress_benchmark.cpp
)

if(MSVC)
    target_link_libraries(coroutine_executor_stress_benchmark PRIVATE
        legend2_common
        mir2_server_lib
        mir2_storage_engine
        benchmark::benchmark
    )
else()
    target_link_libraries(coroutine_executor_stress_benchmark PRIVATE
        legend2_common
        -Wl,--start-group
        mir2_server_lib
        mir2_storage_engine
        -Wl,--end-group
        benchmark::benchmark
    )
endif()

target_include_directories(coroutine_executor_stress_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/src/server
)

if(MSVC)
    target_compile_options(coroutine_executor_stress_benchmark PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(coroutine_executor_stress_benchmark PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()

target_compile_definitions(coroutine_executor_stress_benchmark PRIVATE
    $<$<CONFIG:Release>:NDEBUG>
)
