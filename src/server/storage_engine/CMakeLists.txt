# Storage Engine Module CMakeLists.txt
# 统一存储引擎：缓存 + 持久化

cmake_minimum_required(VERSION 3.16)

# Dependencies
find_package(RocksDB CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# 源文件列表
set(STORAGE_ENGINE_SOURCES
    # 核心接口
    interfaces/storage_backend.h

    # 主引擎
    storage_engine.h
    storage_engine.cc

    # L1 cache
    l1/memory_cache.h
    l1/memory_cache.cc

    # L2 cache
    l2/rocksdb_cache.h
    l2/rocksdb_cache.cc

    # Utils
    utils/circuit_breaker.h
    utils/circuit_breaker.cc
    utils/global_hybrid_clock.h
    utils/global_hybrid_clock.cc

    # Types
    types.h

    # Persistence
    persistence/async_persistence_queue.h
    persistence/async_persistence_queue.cc
    persistence/blocking_queue.h
)

# Backend adapters (Phase E)
set(STORAGE_ENGINE_BACKEND_SOURCES
    # E-02/E-03 add backends/storage_engine_backend.cc
    # E-02/E-03 add backends/account_storage_backend.cc
)

list(APPEND STORAGE_ENGINE_SOURCES ${STORAGE_ENGINE_BACKEND_SOURCES})

# 创建库
add_library(mir2_storage_engine STATIC ${STORAGE_ENGINE_SOURCES})

# 包含目录
target_include_directories(mir2_storage_engine PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/server
)

# 链接依赖（暂时最小化）
target_link_libraries(mir2_storage_engine PUBLIC
    legend2_common
    spdlog::spdlog
    Threads::Threads
)

target_link_libraries(mir2_storage_engine PRIVATE RocksDB::rocksdb)

# C++20标准
set_target_properties(mir2_storage_engine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# 编译选项
if(MSVC)
    target_compile_options(mir2_storage_engine PRIVATE /W4)
else()
    target_compile_options(mir2_storage_engine PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter  # TODO临时忽略
    )
endif()

# 可选：Debug模式启用update_fn时间监控
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(mir2_storage_engine PRIVATE MIR2_DEBUG_MODE)
endif()

# 导出
set(STORAGE_ENGINE_LIBRARY mir2_storage_engine PARENT_SCOPE)

message(STATUS "StorageEngine module configured")
