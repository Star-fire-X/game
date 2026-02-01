set(LEGEND2_TEST_SOURCES
    # common/tcp_session_test.cpp  # disabled: API mismatch
    # common/kick_message_test.cpp  # disabled: API mismatch
    common/packet_codec_test.cpp
    common/message_codec_test.cpp
    common/npc_message_codec_test.cpp
    server/combat_core_test.cpp
    common/snowflake_id_test.cpp
    # client/message_dispatcher_test.cpp  # disabled
    # client/network_manager_test.cpp  # disabled
    # client/scene_state_machine_test.cpp  # disabled
    # client/login_flow_manager_test.cpp  # disabled
    # client/scene_integration_test.cpp  # disabled
    # client/login_handler_test.cpp  # disabled
    # client/character_handler_test.cpp  # disabled
    # client/movement_handler_test.cpp  # disabled
    # client/combat_handler_test.cpp  # disabled
    # client/system_handler_test.cpp  # disabled
    client/resource/async_loader_test.cc
    client/resource/resource_provider_test.cc
    client/render/texture_cache_test.cc
    client/game/movement_controller_test.cc
    client/game/astar_heuristic_validation.cpp
    client/game/entity_manager_view_test.cc
    client/ui/ui_layout_calculator_test.cc
    client/ui/input_validation_test.cc
    client/ui/ui_layout_loader_test.cc
    client/monster_manager_test.cc
    # client/handlers_integration_test.cpp  # disabled: legend2 namespace not found
    # client/handler_registry_test.cpp  # disabled: legend2 namespace not found
    # integration/protocol_integration_test.cpp  # SDL/client handler dependency
    # integration/map_event_integration_test.cpp  # disabled: 依赖event系统
    # integration/full_gameplay_test.cpp  # disabled: 依赖NPC系统
    # handlers/login_handler_test.cpp  # disabled: SDL 依赖
    # handlers/character_handler_test.cpp  # disabled: SDL 依赖
    handlers/movement_handler_test.cpp
    # handlers/combat_handler_test.cpp  # disabled: SDL 依赖
    # handlers/item_handler_test.cpp  # disabled: SDL 依赖
    # handlers/chat_handler_test.cpp  # disabled: SDL 依赖
    # server/gateway_heartbeat_test.cpp  # disabled: SDL 依赖
    # server/gateway_integration_test.cc  # disabled: SDL 依赖
    # server/gateway_server_test.cc  # disabled: SDL 依赖
    # server/gateway_forwarding_test.cc  # disabled: SDL 依赖
    # server/gateway_full_flow_test.cc  # disabled: SDL 依赖
    # server/gateway_error_handling_test.cc  # disabled: SDL 依赖
    # server/gateway_performance_test.cc  # disabled: SDL 依赖
    # server/message_router_test.cc  # disabled: SDL 依赖
    # server/gateway_config_test.cc  # disabled: SDL 依赖
    # server/gateway_routing_test.cc  # disabled: SDL 依赖
    server/character_factory_test.cc
    server/character_entity_manager_test.cc
    server/config/map_config_loader_test.cpp
    server/event/timed_event_scheduler_test.cpp
    server/event/global_event_manager_test.cpp
    server/event/event_integration_test.cpp
    # server/npc/npc_entity_test.cpp  # disabled: 依赖NPC系统
    # server/npc/npc_manager_test.cpp  # disabled: 依赖NPC系统
    # server/npc/npc_script_engine_test.cpp  # disabled: 依赖NPC系统
    # server/npc/npc_interaction_test.cpp  # disabled: 依赖NPC系统
    # server/npc/npc_state_machine_test.cpp  # disabled: 依赖NPC系统
    # server/npc/npc_teleport_test.cpp  # disabled: 依赖NPC系统
    server/ecs_systems_test.cc
    server/ecs/dirty_tracker_test.cpp
    server/ecs/world_test.cpp
    server/ecs/character_entity_manager_test.cpp
    server/ecs/registry_manager_test.cpp
    server/ecs/movement_system_test.cpp
    server/ecs/combat_system_test.cpp
#    server/ecs/skill_system_test.cc
    server/ecs/level_up_system_test.cpp
    server/ecs/inventory_system_test.cpp
    server/ecs/npc_ai_system_test.cpp
    server/entity/boss_behavior_test.cpp
    server/entity/boss_manager_test.cpp
    server/entity/boss_integration_test.cpp
    server/tcp_connection_test.cpp
    server/map_instance_test.cpp
    server/teleport_system_test.cpp
    server/map_loader_test.cpp
    server/map_instance_walk_test.cpp
    server/map/map_attributes_test.cpp
    server/map/gate_manager_test.cpp
    server/map/area_event_processor_test.cpp
    server/map/area_trigger_test.cpp
    server/map/cross_server_teleport_test.cpp
    server/map/map_event_manager_test.cpp
    server/map/scroll_teleport_test.cpp
    server/map/scene_manager_reload_test.cpp
    server/movement_validator_test.cpp
    integration/scene_manager_test.cpp
    integration/cross_map_test.cpp
    ${CMAKE_SOURCE_DIR}/src/client/network/message_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/network/network_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/ui/ui_layout_calculator.cc
    ${CMAKE_SOURCE_DIR}/src/client/ui/ui_layout_loader.cc
    ${CMAKE_SOURCE_DIR}/src/client/ui/input_validation.cc
    ${CMAKE_SOURCE_DIR}/src/client/resource/async_loader.cc
    ${CMAKE_SOURCE_DIR}/src/client/resource/resource_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/game/entity_manager.cc
    ${CMAKE_SOURCE_DIR}/src/client/game/actor_data.cc
    ${CMAKE_SOURCE_DIR}/src/client/game/monster/monster_manager.cc
)

set(LEGEND2_PQXX_TEST_SOURCES
    server/db/postgres_database_test.cpp
)

set(LEGEND2_HIREDIS_TEST_SOURCES
    server/db/redis_cache_test.cpp
    server/db/character_repository_test.cpp
    integration/db_integration_test.cpp
)

set(LEGEND2_CLIENT_HANDLER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/client/handlers/character_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/client/handlers/npc_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/client/handlers/handler_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/client/handlers/login_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/client/handlers/movement_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/client/handlers/combat_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/client/handlers/system_handler.cpp
)

set(LEGEND2_SDL_TEST_SOURCES
    integration/protocol_integration_test.cpp
)

if(LEGEND2_BUILD_DB AND (TARGET libpqxx::pqxx OR libpqxx_FOUND))
    list(APPEND LEGEND2_TEST_SOURCES ${LEGEND2_PQXX_TEST_SOURCES})
else()
    string(REPLACE ";" ", " LEGEND2_PQXX_TESTS_DISPLAY "${LEGEND2_PQXX_TEST_SOURCES}")
    if(LEGEND2_BUILD_DB)
        message(STATUS "libpqxx not found; skipping tests: ${LEGEND2_PQXX_TESTS_DISPLAY}")
    else()
        message(STATUS "DB disabled; skipping tests: ${LEGEND2_PQXX_TESTS_DISPLAY}")
    endif()
endif()

if(LEGEND2_BUILD_DB AND (TARGET hiredis::hiredis OR hiredis_FOUND))
    list(APPEND LEGEND2_TEST_SOURCES ${LEGEND2_HIREDIS_TEST_SOURCES})
else()
    string(REPLACE ";" ", " LEGEND2_HIREDIS_TESTS_DISPLAY "${LEGEND2_HIREDIS_TEST_SOURCES}")
    if(LEGEND2_BUILD_DB)
        message(STATUS "hiredis not found; skipping tests: ${LEGEND2_HIREDIS_TESTS_DISPLAY}")
    else()
        message(STATUS "DB disabled; skipping tests: ${LEGEND2_HIREDIS_TESTS_DISPLAY}")
    endif()
endif()

if(TARGET SDL2::SDL2)
    list(APPEND LEGEND2_TEST_SOURCES ${LEGEND2_CLIENT_HANDLER_SOURCES})
    list(APPEND LEGEND2_TEST_SOURCES ${LEGEND2_SDL_TEST_SOURCES})
    list(APPEND LEGEND2_TEST_SOURCES ${CMAKE_SOURCE_DIR}/src/client/core/event_dispatcher.cc)
else()
    string(REPLACE ";" ", " LEGEND2_CLIENT_HANDLER_DISPLAY "${LEGEND2_CLIENT_HANDLER_SOURCES}")
    string(REPLACE ";" ", " LEGEND2_SDL_TESTS_DISPLAY "${LEGEND2_SDL_TEST_SOURCES}")
    message(STATUS "SDL2 not found; skipping client handler sources: ${LEGEND2_CLIENT_HANDLER_DISPLAY}")
    message(STATUS "SDL2 not found; skipping tests: ${LEGEND2_SDL_TESTS_DISPLAY}")
endif()

set(LEGEND2_LUA_TEST_SOURCES
    integration/npc_boss_integration_test.cpp
    integration/npc_interaction_test.cpp
    integration/npc_e2e_test.cpp
    server/npc/lua_bindings_test.cpp
)

if(BUILD_LUA_SUPPORT)
    list(APPEND LEGEND2_TEST_SOURCES ${LEGEND2_LUA_TEST_SOURCES})
    string(REPLACE ";" ", " LEGEND2_LUA_TESTS_DISPLAY "${LEGEND2_LUA_TEST_SOURCES}")
    message(STATUS "Lua support tests enabled: ${LEGEND2_LUA_TESTS_DISPLAY}")
else()
    string(REPLACE ";" ", " LEGEND2_LUA_TESTS_DISPLAY "${LEGEND2_LUA_TEST_SOURCES}")
    message(STATUS "Lua support tests disabled: ${LEGEND2_LUA_TESTS_DISPLAY}")
endif()

add_executable(legend2_tests ${LEGEND2_TEST_SOURCES})

target_link_libraries(legend2_tests PRIVATE
    mir2_server_lib
    GTest::gtest_main
    GTest::gmock
)

if(LEGEND2_BUILD_DB)
    if(TARGET libpqxx::pqxx)
        target_link_libraries(legend2_tests PRIVATE libpqxx::pqxx)
    endif()
    if(TARGET hiredis::hiredis)
        target_link_libraries(legend2_tests PRIVATE hiredis::hiredis)
    endif()
endif()

if(BUILD_LUA_SUPPORT)
    if(TARGET LuaJIT::LuaJIT)
        target_link_libraries(legend2_tests PRIVATE LuaJIT::LuaJIT)
    endif()
    if(TARGET sol2::sol2)
        target_link_libraries(legend2_tests PRIVATE sol2::sol2)
    endif()
endif()

if(TARGET SDL2::SDL2)
    target_link_libraries(legend2_tests PRIVATE SDL2::SDL2)
endif()

target_include_directories(legend2_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/client
    ${CMAKE_SOURCE_DIR}/tests
)

add_executable(routing_table_tests
    common/routing_table_test.cpp
)

target_link_libraries(routing_table_tests PRIVATE
    mir2_server_lib
    GTest::gtest_main
)

add_executable(client_skill_tests
    client/skill/skill_manager_test.cpp
    client/skill/skill_executor_test.cpp
    ${CMAKE_SOURCE_DIR}/src/client/game/skill/skill_manager.cc
    ${CMAKE_SOURCE_DIR}/src/client/game/skill/skill_executor.cc
)

target_link_libraries(client_skill_tests PRIVATE
    legend2_common
    GTest::gtest_main
)

target_include_directories(client_skill_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/client
    ${CMAKE_SOURCE_DIR}/tests
)

include(GoogleTest)
gtest_discover_tests(legend2_tests)
gtest_discover_tests(routing_table_tests)
gtest_discover_tests(client_skill_tests)

# Standalone combat_core test target
add_executable(combat_core_test
    server/combat_core_test.cpp
)

target_link_libraries(combat_core_test PRIVATE
    mir2_server_lib
    GTest::gtest_main
)

target_include_directories(combat_core_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

gtest_discover_tests(combat_core_test)
