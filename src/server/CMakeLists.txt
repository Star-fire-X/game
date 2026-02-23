# Mir2 Server v2 - CMakeLists.txt

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
# TBB already found in root CMakeLists.txt

# Prefer vcpkg-provided flatc target when available.
if(TARGET flatbuffers::flatc)
    set(FLATC $<TARGET_FILE:flatbuffers::flatc>)
else()
    find_program(FLATC flatc HINTS ${flatbuffers_BINARY_DIR} ${CMAKE_BINARY_DIR}/_deps/flatbuffers-build)
    if(NOT FLATC)
        if(TARGET flatc)
            set(FLATC $<TARGET_FILE:flatc>)
        else()
            message(FATAL_ERROR "flatc not found")
        endif()
    endif()
endif()

set(SCHEMA_DIR ${CMAKE_SOURCE_DIR}/schemas)
set(GENERATED_SCHEMA_DIR ${CMAKE_BINARY_DIR}/schemas_generated)
file(MAKE_DIRECTORY ${GENERATED_SCHEMA_DIR})

set(SCHEMA_FILES
    ${SCHEMA_DIR}/common.fbs
    ${SCHEMA_DIR}/internal.fbs
    ${SCHEMA_DIR}/login.fbs
    ${SCHEMA_DIR}/game.fbs
    ${SCHEMA_DIR}/combat.fbs
    ${SCHEMA_DIR}/item.fbs
    ${SCHEMA_DIR}/guild.fbs
    ${SCHEMA_DIR}/party.fbs
    ${SCHEMA_DIR}/trade.fbs
    ${SCHEMA_DIR}/ranking.fbs
    ${SCHEMA_DIR}/mail.fbs
    ${SCHEMA_DIR}/achievement.fbs
    ${SCHEMA_DIR}/auction.fbs
    ${SCHEMA_DIR}/chat.fbs
    ${SCHEMA_DIR}/system.fbs
    ${SCHEMA_DIR}/persistence.fbs
)

set(GENERATED_SCHEMA_HEADERS
    ${GENERATED_SCHEMA_DIR}/common_generated.h
    ${GENERATED_SCHEMA_DIR}/internal_generated.h
    ${GENERATED_SCHEMA_DIR}/login_generated.h
    ${GENERATED_SCHEMA_DIR}/game_generated.h
    ${GENERATED_SCHEMA_DIR}/combat_generated.h
    ${GENERATED_SCHEMA_DIR}/item_generated.h
    ${GENERATED_SCHEMA_DIR}/guild_generated.h
    ${GENERATED_SCHEMA_DIR}/party_generated.h
    ${GENERATED_SCHEMA_DIR}/trade_generated.h
    ${GENERATED_SCHEMA_DIR}/ranking_generated.h
    ${GENERATED_SCHEMA_DIR}/mail_generated.h
    ${GENERATED_SCHEMA_DIR}/achievement_generated.h
    ${GENERATED_SCHEMA_DIR}/auction_generated.h
    ${GENERATED_SCHEMA_DIR}/chat_generated.h
    ${GENERATED_SCHEMA_DIR}/system_generated.h
    ${GENERATED_SCHEMA_DIR}/persistence_generated.h
)

add_custom_command(
    OUTPUT ${GENERATED_SCHEMA_HEADERS}
    COMMAND ${FLATC} --cpp --scoped-enums -o ${GENERATED_SCHEMA_DIR} ${SCHEMA_FILES}
    DEPENDS ${SCHEMA_FILES}
    COMMENT "Generating FlatBuffers schemas"
)

add_custom_target(mir2_schemas DEPENDS ${GENERATED_SCHEMA_HEADERS})

# Link-audit invariant: default mir2_server_lib must not depend on legacy modules.
set(MIR2_SERVER_SOURCES
    ecs/systems/combat_core.cc
    common/internal_message_helper.cc
    common/crypto_utils.cc
    core/application.cc
    core/timer.cc
    core/utils.cc
    config/config_manager.cc
    config/map_config_loader.cc
    config/skill_config_loader.cc
    data/item_template.cc
    log/logger.cc
    security/anti_cheat.cc
    security/rate_limiter.cc
    network/network_manager.cc
    network/tcp_server.cc
    network/tcp_connection.cc
    network/tcp_session.cc
    network/tcp_client.cc
    network/kcp_server.cc
    network/kcp_session.cc
    network/dual_channel_manager.cc
    network/ip_rate_limiter.cc
    network/conv_blacklist.cc
    network/message_dispatcher.cc
    network/packet_codec.cc
    network/handlers/kcp_upgrade_handler.cc
    game/chat/chat_service.cc
    game/guild/guild_manager.cc
    game/item/item_effect_processor.cc
    storage_engine/backends/common/account_storage_codec.cc
    ecs/persistence/character_codec.cc
    ecs/world.cc
    ecs/character_entity_manager.cc
    ecs/character_snapshot_codec.cc
    ecs/inventory_migration.cc
    ecs/registry_manager.cc
    ecs/skill_registry.cc
    ecs/systems/combat_system.cc
    ecs/systems/character_utils.cc
    ecs/systems/damage_calculator.cc
    ecs/systems/effect_broadcaster.cc
    ecs/systems/effect_system.cc
    ecs/systems/amulet_consumer.cc
    ecs/systems/equipment_bonus_system.cc
    ecs/systems/guild_system.cc
    ecs/systems/inventory_system.cc
    ecs/systems/ground_item_system.cc
    ecs/systems/trade_system.cc
    ecs/systems/storage_system.cc
    ecs/systems/level_up_system.cc
    ecs/systems/growth_formulas.cc
    ecs/systems/attribute_recalc_system.cc
    ecs/systems/bonus_point_system.cc
    ecs/systems/recovery_system.cc
    ecs/systems/luck_system.cc
    ecs/systems/movement_system.cc
    ecs/systems/monster_ai_system.cc
    ecs/systems/monster_drop_system.cc
    ecs/systems/npc_ai_system.cc
    ecs/systems/passive_skill_system.cc
    ecs/systems/pathfinding_helper.cc
    ecs/systems/spatial_query.cc
    ecs/systems/skill_system.cc
    ecs/systems/summon_system.cc
    ecs/systems/teleport_system.cc
    game/entity/monster.cc
    game/entity/monster_manager.cc
    game/entity/boss_behavior.cc
    game/entity/boss_manager.cc
    game/event/event_handler.cc
    game/event/timed_event_scheduler.cc
    game/event/global_event_manager.cc
    game/npc/npc_entity.cc
    game/npc/npc_interaction_handler.cc
    game/npc/npc_script_engine.cc
    game/npc/npc_manager.cc
    game/npc/npc_state_machine.cc
    game/npc/npc_shop_service.cc
    game/map/aoi_manager.cc
    game/map/area_event_processor.cc
    game/map/door_manager.cc
    game/map/gate_manager.cc
    game/map/map_event_manager.cc
    game/map/map_loader.cc
    game/map/map_instance.cc
    game/map/chunk_manager.cc
    game/map/scene_manager.cc
    game/map/cross_server_teleport.cc
    game/map/scroll_teleport.cc
    gateway/connection_holder.cc
    gateway/gateway_server.cc
    gateway/message_router.cc
    gateway/ring_buffer.cc
)

if(BUILD_LUA_SUPPORT)
    list(APPEND MIR2_SERVER_SOURCES
        game/npc/lua_script_engine.cc
        game/npc/lua_bindings.cc
    )
endif()

# Database sources (optional, requires libpqxx)
set(LEGEND2_BUILD_DB OFF)
if(BUILD_DB)
    if(TARGET libpqxx::pqxx)
        set(LEGEND2_BUILD_DB ON)
    else()
        message(WARNING "BUILD_DB=ON but libpqxx not found; database support disabled.")
    endif()
endif()

set(LEGEND2_DB_OPTIONAL_SOURCES)
if(LEGEND2_BUILD_DB)
    # Main DB chain used by LogicServer + storage_engine path.
    list(APPEND MIR2_SERVER_SOURCES
        storage_engine/backends/account_storage_backend.cc
        storage_engine/backends/storage_engine_backend.cc
        storage_engine/backends/postgres/pg_connection_pool.cc
    )

    # Optional DB adapters/repository path (not part of default server main chain).
    list(APPEND LEGEND2_DB_OPTIONAL_SOURCES
        storage_engine/backends/postgres/postgres_database.cc
    )
endif()

if(LEGEND2_ENABLE_PROMETHEUS)
    list(APPEND MIR2_SERVER_SOURCES monitor/metrics.cc)
else()
    list(APPEND MIR2_SERVER_SOURCES monitor/metrics_stub.cc)
endif()

add_library(mir2_server_lib STATIC ${MIR2_SERVER_SOURCES})
add_dependencies(mir2_server_lib mir2_schemas)

if(LEGEND2_BUILD_DB)
    add_library(mir2_db_optional STATIC ${LEGEND2_DB_OPTIONAL_SOURCES})
    target_include_directories(mir2_db_optional PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GENERATED_SCHEMA_DIR}
        ${SCHEMA_DIR}
    )
    target_link_libraries(mir2_db_optional PUBLIC
        legend2_common
        Threads::Threads
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        libpqxx::pqxx
    )
endif()

add_subdirectory(logic)

target_include_directories(mir2_server_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GENERATED_SCHEMA_DIR}
    ${SCHEMA_DIR}
)

target_link_libraries(mir2_server_lib PUBLIC
    legend2_common
    Threads::Threads
    OpenSSL::Crypto
    tbb
    asio
    flatbuffers
    EnTT::EnTT
    spdlog::spdlog
    ${LEGEND2_BREAKPAD_TARGET}
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
)

target_compile_definitions(mir2_server_lib PUBLIC HAVE_OPENSSL)

# System crypt library for bcrypt support (Linux/BSD)
if(UNIX AND NOT APPLE)
    target_link_libraries(mir2_server_lib PUBLIC crypt)
endif()

if(LEGEND2_BUILD_DB)
    target_link_libraries(mir2_server_lib PUBLIC
        libpqxx::pqxx
    )
endif()

if(BUILD_LUA_SUPPORT)
    if(TARGET LuaJIT::LuaJIT)
        target_link_libraries(mir2_server_lib PUBLIC LuaJIT::LuaJIT)
    endif()
    if(TARGET sol2::sol2)
        target_link_libraries(mir2_server_lib PUBLIC sol2::sol2)
    endif()
endif()

if(LEGEND2_ENABLE_PROMETHEUS)
    target_link_libraries(mir2_server_lib PUBLIC
        prometheus-cpp::core
        prometheus-cpp::pull
    )
    target_compile_definitions(mir2_server_lib PUBLIC LEGEND2_ENABLE_PROMETHEUS)
endif()

add_executable(mir2_gateway apps/gateway_main.cc)
target_link_libraries(mir2_gateway PRIVATE mir2_server_lib)

set(LEGEND2_BUILD_DB ${LEGEND2_BUILD_DB} PARENT_SCOPE)

# Storage Engine Module (统一存储引擎)
add_subdirectory(storage_engine)
target_link_libraries(mir2_server_lib PUBLIC mir2_storage_engine)

# === 已废弃的旧模块（保留以供参考，不编译） ===
# ECS State Persistence Module (已迁移到 storage_engine/persistence)
# add_subdirectory(persistence)

# Tiered Cache Module (已迁移到 storage_engine/l1, l2, utils)
# add_subdirectory(cache)
# target_link_libraries(mir2_server_lib PUBLIC mir2_cache)

add_executable(mir2_logic apps/logic_main.cc)
target_link_libraries(mir2_logic PRIVATE mir2_server_lib)

if(WIN32)
    target_link_libraries(mir2_server_lib PUBLIC ws2_32 wsock32)
endif()
