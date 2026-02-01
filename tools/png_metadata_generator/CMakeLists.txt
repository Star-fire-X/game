# PNG元数据生成器工具

cmake_minimum_required(VERSION 3.15)

# 添加可执行文件
add_executable(png_metadata_generator
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/client/resource/resource_loader.cpp
)

# 链接公共库
target_link_libraries(png_metadata_generator PRIVATE
    legend2_common
    nlohmann_json::nlohmann_json
)

# 包含目录
target_include_directories(png_metadata_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# 安装到bin目录
install(TARGETS png_metadata_generator
    RUNTIME DESTINATION bin
)
