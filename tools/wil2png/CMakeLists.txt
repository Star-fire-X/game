# wil2png - WIL to PNG Converter Tool
cmake_minimum_required(VERSION 3.16)

project(wil2png LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2 and SDL2_image
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)

# Create executable
add_executable(wil2png
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/client/resource/resource_loader.cpp
)

# Link libraries
target_link_libraries(wil2png PRIVATE
    legend2_common
    SDL2::SDL2
    SDL2::SDL2main
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
)

# Include directories
target_include_directories(wil2png PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Installation
install(TARGETS wil2png RUNTIME DESTINATION bin)
