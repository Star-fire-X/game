# ECS State Persistence Module CMakeLists.txt

# Persistence library sources
set(PERSISTENCE_SOURCES
    # Core interfaces
    persistence_error.h
    serializer.h
    storage_backend.h
    component_registry.h

    # Serializers
    json_serializer.h
    json_serializer.cc
    guild_serializer.h
    guild_serializer.cc

    # Storage backends
    postgres_backend.h
    postgres_backend.cc
    postgres_connection_pool.h
    postgres_connection_pool.cc
    postgres_transaction.h
    postgres_transaction.cc

    # Core persistence manager
    persistence_manager.h
    persistence_manager.cc

    # Save strategies (Epic 2)
    save_scheduler.h
    save_scheduler.cc
    graceful_shutdown.h
    graceful_shutdown.cc

    # Recovery systems (Epic 3)
    startup_loader.h
    startup_loader.cc
    snapshot_recovery.h
    snapshot_recovery.cc
    data_validator.h
    data_validator.cc

    # Performance optimization (Epic 4)
    batch_writer.h
    batch_writer.cc
    state_cache.h
    state_cache.cc
)

# Create persistence library
add_library(mir2_persistence STATIC ${PERSISTENCE_SOURCES})

# Include directories
target_include_directories(mir2_persistence PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/server
)

# Link dependencies
target_link_libraries(mir2_persistence PUBLIC
    legend2_common
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# C++ standard
set_target_properties(mir2_persistence PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Compiler warnings
if(MSVC)
    target_compile_options(mir2_persistence PRIVATE /W4 /WX)
else()
    target_compile_options(mir2_persistence PRIVATE -Wall -Wextra -Werror -Wno-error=deprecated-declarations)
endif()

# Optional dependencies (if available)
if(TARGET libpqxx::pqxx)
    target_link_libraries(mir2_persistence PUBLIC libpqxx::pqxx)
    target_compile_definitions(mir2_persistence PUBLIC HAVE_LIBPQXX)
endif()

if(TARGET redis-plus-plus)
    target_link_libraries(mir2_persistence PUBLIC redis-plus-plus)
    target_compile_definitions(mir2_persistence PUBLIC HAVE_REDIS)
endif()

# OpenSSL for SHA256 checksum
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(mir2_persistence PUBLIC OpenSSL::Crypto)
    target_compile_definitions(mir2_persistence PUBLIC HAVE_OPENSSL)
    message(STATUS "OpenSSL found for persistence module")
endif()

# Export for other modules
set(PERSISTENCE_LIBRARY mir2_persistence PARENT_SCOPE)
