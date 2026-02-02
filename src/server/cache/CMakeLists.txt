cmake_minimum_required(VERSION 3.16)

# 查找 RocksDB 依赖
find_package(rocksdb CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# 定义 mir2_cache 库
add_library(mir2_cache
    cache_types.h
    global_hybrid_clock.h
    global_hybrid_clock.cc
    local_lru_cache.h
    local_lru_cache.cc
    rocksdb_cache.h
    rocksdb_cache.cc
)

# 设置包含目录
target_include_directories(mir2_cache
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_BINARY_DIR}
)

# 链接依赖库
target_link_libraries(mir2_cache
    PUBLIC spdlog::spdlog
    PRIVATE rocksdb
)

# 编译选项
if(MSVC)
    target_compile_options(mir2_cache
        PRIVATE /W4 /WX-
    )
else()
    target_compile_options(mir2_cache
        PRIVATE -Wall -Wextra -Wpedantic -O3
    )
endif()

# C++20 标准
target_compile_features(mir2_cache PUBLIC cxx_std_20)
