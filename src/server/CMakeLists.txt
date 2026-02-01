# Mir2 Server v2 - CMakeLists.txt

find_package(Threads REQUIRED)
# TBB already found in root CMakeLists.txt

find_program(FLATC flatc HINTS ${flatbuffers_BINARY_DIR} ${CMAKE_BINARY_DIR}/_deps/flatbuffers-build)
if(NOT FLATC)
    if(TARGET flatc)
        set(FLATC $<TARGET_FILE:flatc>)
    else()
        message(FATAL_ERROR "flatc not found")
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
    ${SCHEMA_DIR}/chat.fbs
    ${SCHEMA_DIR}/system.fbs
)

set(GENERATED_SCHEMA_HEADERS
    ${GENERATED_SCHEMA_DIR}/common_generated.h
    ${GENERATED_SCHEMA_DIR}/internal_generated.h
    ${GENERATED_SCHEMA_DIR}/login_generated.h
    ${GENERATED_SCHEMA_DIR}/game_generated.h
    ${GENERATED_SCHEMA_DIR}/combat_generated.h
    ${GENERATED_SCHEMA_DIR}/item_generated.h
    ${GENERATED_SCHEMA_DIR}/guild_generated.h
    ${GENERATED_SCHEMA_DIR}/chat_generated.h
    ${GENERATED_SCHEMA_DIR}/system_generated.h
)

add_custom_command(
    OUTPUT ${GENERATED_SCHEMA_HEADERS}
    COMMAND ${FLATC} --cpp --scoped-enums -o ${GENERATED_SCHEMA_DIR} ${SCHEMA_FILES}
    DEPENDS ${SCHEMA_FILES}
    COMMENT "Generating FlatBuffers schemas"
)

add_custom_target(mir2_schemas DEPENDS ${GENERATED_SCHEMA_HEADERS})

set(MIR2_SERVER_SOURCES
    combat/combat_core.cpp
    common/internal_message_helper.cc
    core/application.cc
    core/timer.cc
    core/utils.cc
    config/config_manager.cc
    config/map_config_loader.cc
    config/skill_config_loader.cc
    log/logger.cc
    security/anti_cheat.cc
    security/rate_limiter.cc
    network/network_manager.cc
    network/tcp_server.cc
    network/tcp_connection.cc
    network/tcp_session.cc
    network/tcp_client.cc
    network/message_dispatcher.cc
    network/packet_codec.cc
    handlers/base_handler.cc
    handlers/handler_registry.cc
    handlers/client_registry.cc
    handlers/login/login_handler.cc
    handlers/character/character_handler.cc
    handlers/movement/movement_handler.cc
    handlers/movement/entity_broadcast_service.cc
    handlers/movement/movement_validator.cc
    handlers/combat/combat_handler.cc
    handlers/effect/effect_broadcast_service.cc
    handlers/item/item_handler.cc
    handlers/chat/chat_handler.cc
    handlers/merchant_handler.cc
    handlers/npc/npc_command_handler.cc
    legacy/character_factory.cc
    legacy/inventory_system.cpp
    legacy/monster_ai.cpp
    legacy/legacy_monster_adapter.cc
    legacy/skill_system.cpp
    ecs/world.cc
    ecs/character_entity_manager.cc
    ecs/inventory_migration.cc
    ecs/registry_manager.cc
    ecs/skill_registry.cc
    ecs/systems/combat_system.cc
    ecs/systems/character_utils.cc
    ecs/systems/damage_calculator.cc
    ecs/systems/effect_broadcaster.cc
    ecs/systems/effect_system.cc
    ecs/systems/inventory_system.cc
    ecs/systems/trade_system.cc
    ecs/systems/storage_system.cc
    ecs/systems/level_up_system.cc
    ecs/systems/movement_system.cc
    ecs/systems/npc_ai_system.cc
    ecs/systems/passive_skill_system.cc
    ecs/systems/spatial_query.cc
    ecs/systems/skill_system.cc
    ecs/systems/summon_system.cc
    ecs/systems/teleport_system.cc
    game/game_server.cc
    game/entity/player.cc
    game/entity/player_manager.cc
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
    gateway/gateway_server.cc
    gateway/message_router.cc
    world/world_server.cc
    world/role_store.cc
)

if(BUILD_LUA_SUPPORT)
    list(APPEND MIR2_SERVER_SOURCES
        game/npc/lua_script_engine.cc
        game/npc/lua_bindings.cc
    )
endif()

# Database sources (optional, requires libpqxx and hiredis)
set(LEGEND2_BUILD_DB OFF)
if(BUILD_DB)
    if(TARGET libpqxx::pqxx AND TARGET hiredis::hiredis)
        set(LEGEND2_BUILD_DB ON)
    else()
        message(WARNING "BUILD_DB=ON but libpqxx/hiredis not found; mir2_db will not be built.")
    endif()
endif()

if(LEGEND2_BUILD_DB)
    list(APPEND MIR2_SERVER_SOURCES
        db/db_server.cc
        db/database_manager.cc
        db/pg_connection_pool.cc
        db/postgres_database.cc
        db/character_repository.cc
        db/redis_cache.cc
        db/redis_manager.cc
    )
endif()

if(LEGEND2_ENABLE_PROMETHEUS)
    list(APPEND MIR2_SERVER_SOURCES monitor/metrics.cc)
else()
    list(APPEND MIR2_SERVER_SOURCES monitor/metrics_stub.cc)
endif()

add_library(mir2_server_lib STATIC ${MIR2_SERVER_SOURCES})
add_dependencies(mir2_server_lib mir2_schemas)

target_include_directories(mir2_server_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GENERATED_SCHEMA_DIR}
    ${SCHEMA_DIR}
)

target_link_libraries(mir2_server_lib PUBLIC
    legend2_common
    Threads::Threads
    tbb
    asio
    flatbuffers
    EnTT::EnTT
    spdlog::spdlog
    yaml-cpp::yaml-cpp
)

if(LEGEND2_BUILD_DB)
    target_link_libraries(mir2_server_lib PUBLIC
        libpqxx::pqxx
        hiredis::hiredis
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

add_executable(mir2_world apps/world_main.cc)
target_link_libraries(mir2_world PRIVATE mir2_server_lib)

if(LEGEND2_BUILD_DB)
    add_executable(mir2_db apps/db_main.cc)
    target_link_libraries(mir2_db PRIVATE mir2_server_lib)
endif()

set(LEGEND2_BUILD_DB ${LEGEND2_BUILD_DB} PARENT_SCOPE)

add_executable(mir2_game apps/game_main.cc)
target_link_libraries(mir2_game PRIVATE mir2_server_lib)

if(WIN32)
    target_link_libraries(mir2_server_lib PUBLIC ws2_32 wsock32)
endif()
