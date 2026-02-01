# Legend2 Common Library CMakeLists.txt
# Shared code between client and server

# Core sources (always built)
# Note: network_client.cpp moved to src/client/network/
set(COMMON_SOURCES
    types.cpp
    character_data.cpp
    protocol/packet_codec.cpp
    protocol/message_codec.cpp
    protocol/npc_message_codec.cpp
)

# Configure version header from project version
set(VERSION_HEADER_IN ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in)
set(VERSION_HEADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/version.h)
configure_file(${VERSION_HEADER_IN} ${VERSION_HEADER_OUT} @ONLY)

# FlatBuffers - already found in root CMakeLists.txt
find_program(FLATC flatc HINTS ${flatbuffers_BINARY_DIR} ${CMAKE_BINARY_DIR}/_deps/flatbuffers-build)
if(NOT FLATC)
    # If FetchContent was used, flatc is built as a target
    if(TARGET flatc)
        set(FLATC $<TARGET_FILE:flatc>)
    else()
        message(FATAL_ERROR "flatc not found")
    endif()
endif()

set(SCHEMA_DIR ${CMAKE_SOURCE_DIR}/schemas)
set(GENERATED_SCHEMA_DIR ${CMAKE_BINARY_DIR}/schemas_generated_common)
file(MAKE_DIRECTORY ${GENERATED_SCHEMA_DIR})

set(SCHEMA_FILES
    ${SCHEMA_DIR}/common.fbs
    ${SCHEMA_DIR}/login.fbs
    ${SCHEMA_DIR}/game.fbs
    ${SCHEMA_DIR}/combat.fbs
)

set(GENERATED_SCHEMA_HEADERS
    ${GENERATED_SCHEMA_DIR}/common_generated.h
    ${GENERATED_SCHEMA_DIR}/login_generated.h
    ${GENERATED_SCHEMA_DIR}/game_generated.h
    ${GENERATED_SCHEMA_DIR}/combat_generated.h
)

add_custom_command(
    OUTPUT ${GENERATED_SCHEMA_HEADERS}
    COMMAND ${FLATC} --cpp --scoped-enums -o ${GENERATED_SCHEMA_DIR} ${SCHEMA_FILES}
    DEPENDS ${SCHEMA_FILES}
    COMMENT "Generating FlatBuffers schemas for common"
)

add_custom_target(legend2_common_schemas DEPENDS ${GENERATED_SCHEMA_HEADERS})

add_library(legend2_common STATIC ${COMMON_SOURCES})
add_dependencies(legend2_common legend2_common_schemas)

target_include_directories(legend2_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${GENERATED_SCHEMA_DIR}
)

# yaml-cpp already found in root CMakeLists.txt

target_link_libraries(legend2_common PUBLIC
    nlohmann_json::nlohmann_json
    flatbuffers
    yaml-cpp::yaml-cpp
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(legend2_common PUBLIC
        _WIN32_WINNT=0x0601  # Windows 7+
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
