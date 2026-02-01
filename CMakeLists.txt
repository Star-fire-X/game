# Legend2 MVP - Root CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(Legend2 VERSION 0.1.0 LANGUAGES CXX)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
# C++20 Standard (server architecture v2 requires C++20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type (single-config generators)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# =============================================================================
# Dependencies (vcpkg recommended)
# =============================================================================

# SDL2 (optional - only needed for client)
# Prefer CONFIG (vcpkg/SDL2Config.cmake), fall back to MODULE (FindSDL2.cmake).
set(SDL2_FOUND_MODE "")
find_package(SDL2 CONFIG QUIET)
if(TARGET SDL2::SDL2)
    set(SDL2_FOUND_MODE "CONFIG")
else()
    find_package(SDL2 MODULE QUIET)
    if(SDL2_FOUND)
        set(SDL2_FOUND_MODE "MODULE")
        if(NOT TARGET SDL2::SDL2)
            add_library(SDL2::SDL2 INTERFACE IMPORTED)
            if(SDL2_INCLUDE_DIRS)
                target_include_directories(SDL2::SDL2 INTERFACE ${SDL2_INCLUDE_DIRS})
            elseif(SDL2_INCLUDE_DIR)
                target_include_directories(SDL2::SDL2 INTERFACE ${SDL2_INCLUDE_DIR})
            endif()
            if(SDL2_LIBRARIES)
                target_link_libraries(SDL2::SDL2 INTERFACE ${SDL2_LIBRARIES})
            elseif(SDL2_LIBRARY)
                target_link_libraries(SDL2::SDL2 INTERFACE ${SDL2_LIBRARY})
            endif()
        endif()
        # SDL2main is only required on Windows (optional elsewhere).
        if(SDL2MAIN_LIBRARY AND NOT TARGET SDL2::SDL2main)
            add_library(SDL2::SDL2main INTERFACE IMPORTED)
            target_link_libraries(SDL2::SDL2main INTERFACE ${SDL2MAIN_LIBRARY})
        endif()
    endif()
endif()

find_package(SDL2_image CONFIG QUIET)
if(NOT TARGET SDL2_image::SDL2_image)
    find_package(SDL2_image MODULE QUIET)
    if(SDL2_IMAGE_FOUND AND NOT TARGET SDL2_image::SDL2_image)
        add_library(SDL2_image::SDL2_image INTERFACE IMPORTED)
        if(SDL2_IMAGE_INCLUDE_DIRS)
            target_include_directories(SDL2_image::SDL2_image INTERFACE ${SDL2_IMAGE_INCLUDE_DIRS})
        elseif(SDL2_IMAGE_INCLUDE_DIR)
            target_include_directories(SDL2_image::SDL2_image INTERFACE ${SDL2_IMAGE_INCLUDE_DIR})
        endif()
        if(SDL2_IMAGE_LIBRARIES)
            target_link_libraries(SDL2_image::SDL2_image INTERFACE ${SDL2_IMAGE_LIBRARIES})
        elseif(SDL2_IMAGE_LIBRARY)
            target_link_libraries(SDL2_image::SDL2_image INTERFACE ${SDL2_IMAGE_LIBRARY})
        endif()
    endif()
endif()

find_package(SDL2_ttf CONFIG QUIET)
if(NOT TARGET SDL2_ttf::SDL2_ttf)
    find_package(SDL2_ttf MODULE QUIET)
    if(SDL2_TTF_FOUND AND NOT TARGET SDL2_ttf::SDL2_ttf)
        add_library(SDL2_ttf::SDL2_ttf INTERFACE IMPORTED)
        if(SDL2_TTF_INCLUDE_DIRS)
            target_include_directories(SDL2_ttf::SDL2_ttf INTERFACE ${SDL2_TTF_INCLUDE_DIRS})
        elseif(SDL2_TTF_INCLUDE_DIR)
            target_include_directories(SDL2_ttf::SDL2_ttf INTERFACE ${SDL2_TTF_INCLUDE_DIR})
        endif()
        if(SDL2_TTF_LIBRARIES)
            target_link_libraries(SDL2_ttf::SDL2_ttf INTERFACE ${SDL2_TTF_LIBRARIES})
        elseif(SDL2_TTF_LIBRARY)
            target_link_libraries(SDL2_ttf::SDL2_ttf INTERFACE ${SDL2_TTF_LIBRARY})
        endif()
    endif()
endif()

find_package(SDL2_mixer CONFIG QUIET)
if(NOT TARGET SDL2_mixer::SDL2_mixer)
    find_package(SDL2_mixer MODULE QUIET)
    if(SDL2_MIXER_FOUND AND NOT TARGET SDL2_mixer::SDL2_mixer)
        add_library(SDL2_mixer::SDL2_mixer INTERFACE IMPORTED)
        if(SDL2_MIXER_INCLUDE_DIRS)
            target_include_directories(SDL2_mixer::SDL2_mixer INTERFACE ${SDL2_MIXER_INCLUDE_DIRS})
        elseif(SDL2_MIXER_INCLUDE_DIR)
            target_include_directories(SDL2_mixer::SDL2_mixer INTERFACE ${SDL2_MIXER_INCLUDE_DIR})
        endif()
        if(SDL2_MIXER_LIBRARIES)
            target_link_libraries(SDL2_mixer::SDL2_mixer INTERFACE ${SDL2_MIXER_LIBRARIES})
        elseif(SDL2_MIXER_LIBRARY)
            target_link_libraries(SDL2_mixer::SDL2_mixer INTERFACE ${SDL2_MIXER_LIBRARY})
        endif()
    endif()
endif()

if(TARGET SDL2::SDL2)
    if(SDL2_FOUND_MODE STREQUAL "CONFIG")
        message(STATUS "SDL2 found via CONFIG - client will be built")
    elseif(SDL2_FOUND_MODE STREQUAL "MODULE")
        message(STATUS "SDL2 found via MODULE - client will be built")
    else()
        message(STATUS "SDL2 found - client will be built")
    endif()
    set(BUILD_CLIENT ON)
else()
    message(STATUS "SDL2 not found (CONFIG + MODULE) - client will not be built")
    message(STATUS "  CONFIG: ensure SDL2Config.cmake is on CMAKE_PREFIX_PATH (vcpkg toolchain).")
    message(STATUS "  MODULE: ensure SDL2 headers/libs are in system paths (FindSDL2.cmake).")
    message(STATUS "  Install via vcpkg: vcpkg install sdl2 sdl2-image sdl2-ttf sdl2-mixer")
    set(BUILD_CLIENT OFF)
endif()

# Allow FetchContent fallback for missing deps (off by default to avoid network).
option(LEGEND2_ALLOW_FETCHCONTENT "Allow FetchContent fallback for missing deps" OFF)

# Asio (header-only, standalone)
find_package(asio CONFIG QUIET)
if(TARGET asio::asio)
    message(STATUS "Asio found via vcpkg")
    add_library(asio ALIAS asio::asio)
else()
    # Check if asio headers exist in common locations
    find_path(ASIO_INCLUDE_DIR asio.hpp PATHS /usr/include /usr/local/include)
    if(NOT ASIO_INCLUDE_DIR AND LEGEND2_ALLOW_FETCHCONTENT)
        include(FetchContent)
        FetchContent_Declare(
            asio_fetch
            GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
            GIT_TAG asio-1-30-2
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(asio_fetch)
        set(ASIO_INCLUDE_DIR ${asio_fetch_SOURCE_DIR}/asio/include)
        message(STATUS "Asio fetched via FetchContent")
    endif()
    # Fallback: create interface library
    add_library(asio INTERFACE)
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
    if(DEFINED ASIO_INCLUDE_DIR AND ASIO_INCLUDE_DIR)
        target_include_directories(asio INTERFACE ${ASIO_INCLUDE_DIR})
    else()
        message(STATUS "Asio not found - using fallback configuration")
        message(STATUS "  Install via vcpkg: vcpkg install asio")
    endif()
endif()

# FlatBuffers
find_package(flatbuffers CONFIG QUIET)
if(NOT flatbuffers_FOUND)
    if(LEGEND2_ALLOW_FETCHCONTENT)
        include(FetchContent)
        FetchContent_Declare(
            flatbuffers
            GIT_REPOSITORY https://github.com/google/flatbuffers.git
            GIT_TAG v24.3.25
            GIT_SHALLOW TRUE
        )
        set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(flatbuffers)
        message(STATUS "flatbuffers fetched via FetchContent")
    else()
        message(FATAL_ERROR "flatbuffers not found. Install via vcpkg or enable LEGEND2_ALLOW_FETCHCONTENT.")
    endif()
else()
    message(STATUS "flatbuffers found via vcpkg")
endif()
if(TARGET flatbuffers::flatbuffers AND NOT TARGET flatbuffers)
    add_library(flatbuffers ALIAS flatbuffers::flatbuffers)
endif()

# yaml-cpp
find_package(yaml-cpp CONFIG QUIET)
if(NOT yaml-cpp_FOUND)
    if(LEGEND2_ALLOW_FETCHCONTENT)
        include(FetchContent)
        FetchContent_Declare(
            yaml-cpp
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG 0.8.0
            GIT_SHALLOW TRUE
        )
        set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(yaml-cpp)
        message(STATUS "yaml-cpp fetched via FetchContent")
    else()
        message(FATAL_ERROR "yaml-cpp not found. Install via vcpkg or enable LEGEND2_ALLOW_FETCHCONTENT.")
    endif()
else()
    message(STATUS "yaml-cpp found via vcpkg")
endif()

# nlohmann/json (header-only)
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    if(LEGEND2_ALLOW_FETCHCONTENT)
        include(FetchContent)
        FetchContent_Declare(
            json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(json)
        message(STATUS "nlohmann_json fetched via FetchContent")
    else()
        message(FATAL_ERROR "nlohmann_json not found. Install via vcpkg or enable LEGEND2_ALLOW_FETCHCONTENT.")
    endif()
else()
    message(STATUS "nlohmann_json found via vcpkg")
endif()

# LuaJIT + sol2 (optional - NPC Lua scripts/tests)
set(LEGEND2_LUAJIT_TARGET "")
set(LEGEND2_SOL2_TARGET "")
set(LUAJIT_FOUND_MODE "")
set(SOL2_FOUND_MODE "")

find_package(LuaJIT CONFIG QUIET)
if(TARGET LuaJIT::LuaJIT)
    set(LEGEND2_LUAJIT_TARGET LuaJIT::LuaJIT)
    set(LUAJIT_FOUND_MODE "CONFIG")
elseif(TARGET luajit::luajit AND NOT TARGET LuaJIT::LuaJIT)
    add_library(LuaJIT::LuaJIT ALIAS luajit::luajit)
    set(LEGEND2_LUAJIT_TARGET LuaJIT::LuaJIT)
    set(LUAJIT_FOUND_MODE "CONFIG")
elseif(TARGET unofficial::luajit::luajit AND NOT TARGET LuaJIT::LuaJIT)
    add_library(LuaJIT::LuaJIT ALIAS unofficial::luajit::luajit)
    set(LEGEND2_LUAJIT_TARGET LuaJIT::LuaJIT)
    set(LUAJIT_FOUND_MODE "CONFIG")
endif()

if(NOT LEGEND2_LUAJIT_TARGET)
    find_package(LuaJIT MODULE QUIET)
    if(LUAJIT_FOUND OR LuaJIT_FOUND)
        set(LUAJIT_FOUND_MODE "MODULE")
        if(NOT TARGET LuaJIT::LuaJIT)
            add_library(LuaJIT::LuaJIT INTERFACE IMPORTED)
            if(LUAJIT_INCLUDE_DIRS)
                target_include_directories(LuaJIT::LuaJIT INTERFACE ${LUAJIT_INCLUDE_DIRS})
            elseif(LUAJIT_INCLUDE_DIR)
                target_include_directories(LuaJIT::LuaJIT INTERFACE ${LUAJIT_INCLUDE_DIR})
            elseif(LuaJIT_INCLUDE_DIRS)
                target_include_directories(LuaJIT::LuaJIT INTERFACE ${LuaJIT_INCLUDE_DIRS})
            elseif(LuaJIT_INCLUDE_DIR)
                target_include_directories(LuaJIT::LuaJIT INTERFACE ${LuaJIT_INCLUDE_DIR})
            endif()
            if(LUAJIT_LIBRARIES)
                target_link_libraries(LuaJIT::LuaJIT INTERFACE ${LUAJIT_LIBRARIES})
            elseif(LUAJIT_LIBRARY)
                target_link_libraries(LuaJIT::LuaJIT INTERFACE ${LUAJIT_LIBRARY})
            elseif(LuaJIT_LIBRARIES)
                target_link_libraries(LuaJIT::LuaJIT INTERFACE ${LuaJIT_LIBRARIES})
            elseif(LuaJIT_LIBRARY)
                target_link_libraries(LuaJIT::LuaJIT INTERFACE ${LuaJIT_LIBRARY})
            endif()
        endif()
        set(LEGEND2_LUAJIT_TARGET LuaJIT::LuaJIT)
    endif()
endif()

find_package(sol2 CONFIG QUIET)
if(TARGET sol2::sol2)
    set(LEGEND2_SOL2_TARGET sol2::sol2)
    set(SOL2_FOUND_MODE "CONFIG")
elseif(TARGET sol2 AND NOT TARGET sol2::sol2)
    add_library(sol2::sol2 ALIAS sol2)
    set(LEGEND2_SOL2_TARGET sol2::sol2)
    set(SOL2_FOUND_MODE "CONFIG")
elseif(sol2_FOUND OR Sol2_FOUND OR SOL2_FOUND)
    set(SOL2_FOUND_MODE "CONFIG")
    if(NOT TARGET sol2::sol2)
        add_library(sol2::sol2 INTERFACE IMPORTED)
        if(sol2_INCLUDE_DIRS)
            target_include_directories(sol2::sol2 INTERFACE ${sol2_INCLUDE_DIRS})
        elseif(sol2_INCLUDE_DIR)
            target_include_directories(sol2::sol2 INTERFACE ${sol2_INCLUDE_DIR})
        elseif(Sol2_INCLUDE_DIRS)
            target_include_directories(sol2::sol2 INTERFACE ${Sol2_INCLUDE_DIRS})
        elseif(Sol2_INCLUDE_DIR)
            target_include_directories(sol2::sol2 INTERFACE ${Sol2_INCLUDE_DIR})
        endif()
    endif()
    set(LEGEND2_SOL2_TARGET sol2::sol2)
endif()

set(LEGEND2_LUA_SUPPORT_AVAILABLE OFF)
if(LEGEND2_LUAJIT_TARGET AND LEGEND2_SOL2_TARGET)
    set(LEGEND2_LUA_SUPPORT_AVAILABLE ON)
endif()

option(BUILD_LUA_SUPPORT "Enable Lua support (LuaJIT + sol2)" ${LEGEND2_LUA_SUPPORT_AVAILABLE})
if(BUILD_LUA_SUPPORT AND NOT LEGEND2_LUA_SUPPORT_AVAILABLE)
    message(WARNING "BUILD_LUA_SUPPORT=ON but LuaJIT/sol2 not found. Disabling Lua support.")
    set(BUILD_LUA_SUPPORT OFF CACHE BOOL "Enable Lua support (LuaJIT + sol2)" FORCE)
endif()

if(LEGEND2_LUAJIT_TARGET)
    message(STATUS "LuaJIT found via ${LUAJIT_FOUND_MODE}")
else()
    message(STATUS "LuaJIT not found - Lua support will be disabled")
    message(STATUS "  Install via vcpkg: vcpkg install luajit")
endif()

if(LEGEND2_SOL2_TARGET)
    message(STATUS "sol2 found via ${SOL2_FOUND_MODE}")
else()
    message(STATUS "sol2 not found - Lua support will be disabled")
    message(STATUS "  Install via vcpkg: vcpkg install sol2")
endif()

if(BUILD_LUA_SUPPORT)
    message(STATUS "Lua support enabled")
else()
    if(LEGEND2_LUA_SUPPORT_AVAILABLE)
        message(STATUS "Lua support disabled by BUILD_LUA_SUPPORT=OFF")
    else()
        message(STATUS "Lua support disabled (missing LuaJIT and/or sol2)")
    endif()
endif()

# Google Test for testing
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        if(LEGEND2_ALLOW_FETCHCONTENT)
            include(FetchContent)
            FetchContent_Declare(
                googletest
                URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
            )
            # For Windows: Prevent overriding the parent project's compiler/linker settings
            set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
            FetchContent_MakeAvailable(googletest)
            message(STATUS "GTest fetched via FetchContent")
        else()
            message(FATAL_ERROR "GTest not found. Install via vcpkg or enable LEGEND2_ALLOW_FETCHCONTENT.")
        endif()
    else()
        message(STATUS "GTest found via vcpkg")
    endif()
    enable_testing()

    # RapidCheck for property-based testing (optional)
    find_package(rapidcheck CONFIG QUIET)
    if(NOT rapidcheck_FOUND AND LEGEND2_ALLOW_FETCHCONTENT)
        include(FetchContent)
        set(RC_ENABLE_GTEST ON CACHE BOOL "Enable RapidCheck GTest integration" FORCE)
        FetchContent_Declare(
            rapidcheck
            GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
            GIT_TAG master
        )
        FetchContent_MakeAvailable(rapidcheck)
        message(STATUS "RapidCheck fetched via FetchContent")
    elseif(rapidcheck_FOUND)
        message(STATUS "RapidCheck found via vcpkg")
    else()
        message(STATUS "RapidCheck not found - property tests disabled")
    endif()
endif()

# =============================================================================
# Common Library (shared between client and server)
# =============================================================================

add_subdirectory(src/common)

# =============================================================================
# Server
# =============================================================================

option(BUILD_SERVER "Build server" ON)
option(BUILD_DB "Build DB server" ON)
option(LEGEND2_ENABLE_PROMETHEUS "Enable Prometheus metrics" OFF)
set(LEGEND2_BUILD_DB OFF)
if(BUILD_SERVER)
    # Server dependencies (v2 architecture)
    # flatbuffers and yaml-cpp already found above

    # TBB
    find_package(TBB CONFIG QUIET)
    if(NOT TBB_FOUND)
        if(LEGEND2_ALLOW_FETCHCONTENT)
            include(FetchContent)
            FetchContent_Declare(
                TBB
                GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
                GIT_TAG v2021.13.0
                GIT_SHALLOW TRUE
            )
            set(TBB_TEST OFF CACHE BOOL "" FORCE)
            set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
            FetchContent_MakeAvailable(TBB)
            message(STATUS "TBB fetched via FetchContent")
        else()
            message(FATAL_ERROR "TBB not found. Install via vcpkg or enable LEGEND2_ALLOW_FETCHCONTENT.")
        endif()
    else()
        message(STATUS "TBB found via vcpkg")
    endif()
    if(TARGET TBB::tbb AND NOT TARGET tbb)
        add_library(tbb ALIAS TBB::tbb)
    endif()

    find_package(EnTT CONFIG QUIET)
    if(NOT EnTT_FOUND)
        if(LEGEND2_ALLOW_FETCHCONTENT)
            include(FetchContent)
            FetchContent_Declare(
                EnTT
                GIT_REPOSITORY https://github.com/skypjack/entt.git
                GIT_TAG v3.13.2
                GIT_SHALLOW TRUE
            )
            FetchContent_MakeAvailable(EnTT)
            message(STATUS "EnTT fetched via FetchContent")
        else()
            message(FATAL_ERROR "EnTT not found. Install via vcpkg or enable LEGEND2_ALLOW_FETCHCONTENT.")
        endif()
    else()
        message(STATUS "EnTT found via vcpkg")
    endif()

    find_package(spdlog CONFIG QUIET)
    if(NOT spdlog_FOUND)
        if(LEGEND2_ALLOW_FETCHCONTENT)
            include(FetchContent)
            FetchContent_Declare(
                spdlog
                GIT_REPOSITORY https://github.com/gabime/spdlog.git
                GIT_TAG v1.14.1
                GIT_SHALLOW TRUE
            )
            FetchContent_MakeAvailable(spdlog)
            message(STATUS "spdlog fetched via FetchContent")
        else()
            message(FATAL_ERROR "spdlog not found. Install via vcpkg or enable LEGEND2_ALLOW_FETCHCONTENT.")
        endif()
    else()
        message(STATUS "spdlog found via vcpkg")
    endif()

    find_package(libpqxx CONFIG QUIET)
    find_package(hiredis CONFIG QUIET)
    if(LEGEND2_ENABLE_PROMETHEUS)
        find_package(prometheus-cpp CONFIG REQUIRED)
    endif()

    add_subdirectory(src/server)
endif()

# =============================================================================
# Client
# =============================================================================

if(BUILD_CLIENT)
    add_subdirectory(src/client)
endif()

# =============================================================================
# Tests
# =============================================================================

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# =============================================================================
# Benchmarks
# =============================================================================

option(BUILD_BENCHMARKS "Build benchmarks" OFF)
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# =============================================================================
# Tools
# =============================================================================

option(BUILD_TOOLS "Build utility tools" ON)
if(BUILD_TOOLS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/wil2png")
#         add_subdirectory(tools/wil2png)
#     endif()
#     if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/png_metadata_generator")
#         add_subdirectory(tools/png_metadata_generator)
#     endif()
#     if(BUILD_CLIENT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/map_visualizer")
#         add_subdirectory(tools/map_visualizer)
    endif()
endif()

# =============================================================================
# Installation
# =============================================================================

set(INSTALL_TARGETS legend2_common)
if(BUILD_CLIENT)
    list(APPEND INSTALL_TARGETS legend2_client)
endif()
if(BUILD_SERVER)
    list(APPEND INSTALL_TARGETS mir2_gateway mir2_world mir2_game)
    if(LEGEND2_BUILD_DB)
        list(APPEND INSTALL_TARGETS mir2_db)
    endif()
endif()

install(TARGETS ${INSTALL_TARGETS}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Copy server config to build output
if(BUILD_SERVER AND EXISTS ${CMAKE_SOURCE_DIR}/config)
    file(COPY ${CMAKE_SOURCE_DIR}/config DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()
