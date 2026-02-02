# ECS State Persistence Module CMakeLists.txt

# Persistence library sources
set(PERSISTENCE_SOURCES
    # Core interfaces
    persistence/persistence_error.h
    persistence/serializer.h
    persistence/storage_backend.h
    persistence/component_registry.h

    # Serializers
    persistence/json_serializer.h
    persistence/json_serializer.cc

    # Storage backends
    persistence/postgres_backend.h
    persistence/postgres_backend.cc
    persistence/postgres_connection_pool.h
    persistence/postgres_connection_pool.cc
    persistence/postgres_transaction.h
    persistence/postgres_transaction.cc

    # Core persistence manager
    persistence/persistence_manager.h
    persistence/persistence_manager.cc

    # Save strategies (Epic 2)
    persistence/save_scheduler.h
    persistence/save_scheduler.cc
    persistence/graceful_shutdown.h
    persistence/graceful_shutdown.cc

    # Recovery systems (Epic 3)
    persistence/startup_loader.h
    persistence/startup_loader.cc
    persistence/snapshot_recovery.h
    persistence/snapshot_recovery.cc
    persistence/data_validator.h
    persistence/data_validator.cc

    # Performance optimization (Epic 4)
    persistence/batch_writer.h
    persistence/batch_writer.cc
    persistence/state_cache.h
    persistence/state_cache.cc
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
    target_compile_options(mir2_persistence PRIVATE -Wall -Wextra -Werror)
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
