# Legend2 Client CMakeLists.txt

find_package(flatbuffers CONFIG REQUIRED)
find_program(FLATC flatc REQUIRED)

set(SCHEMA_DIR ${CMAKE_SOURCE_DIR}/schemas)
set(GENERATED_SCHEMA_DIR ${CMAKE_BINARY_DIR}/schemas_generated_client)
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
    COMMENT "Generating FlatBuffers schemas for client"
)

add_custom_target(legend2_client_schemas DEPENDS ${GENERATED_SCHEMA_HEADERS})

add_executable(legend2_client
    # Main entry
    main.cc

    # Core module
    core/application.cc
    core/application.h
    core/timer.cc
    core/timer.h
    core/event_dispatcher.cc
    core/event_dispatcher.h
    core/path_utils.cc
    core/path_utils.h

    # Render module
    render/renderer.cc
    render/renderer.h
    render/camera.h
    render/texture.h
    render/animation.cc
    render/animation.h
    render/effect_player.cc
    render/actor_renderer.cc
    render/actor_renderer.h

    # Audio module
    audio/audio_engine.cc
    audio/audio_engine.h

    # Resource module
    resource/resource_loader.cpp
    resource/resource_loader.h
    resource/async_loader.cc
    resource/async_loader.h

    # Game module
    game/game_client.cc
    game/game_client.h
    game/actor_data.cc
    game/client_character.h
    game/entity_manager.cc
    game/entity_manager.h
    game/movement_controller.h
    game/actor_data.h
    game/map/map_renderer.cc
    game/map/map_renderer.h
    game/map/map_system.cpp
    game/map/map_system.h
    game/map/path_finder.cpp
    game/map/portal_manager.cpp
    game/skill/skill_data.h
    game/skill/skill_manager.cc
    game/skill/skill_manager.h
    game/skill/skill_executor.cc
    game/skill/skill_executor.h
    game/monster/monster_data.h
    game/monster/monster_template_mapper.cc
    game/monster/monster_template_mapper.h
    game/monster/monster_manager.cc
    game/monster/monster_manager.h

    # UI module
    ui/ui_manager.h
    ui/ui_renderer.cc
    ui/ui_renderer.h
    ui/input_validation.cc
    ui/input_validation.h
    ui/ui_layout_loader.cc
    ui/ui_layout_loader.h
    ui/ui_layout_calculator.cc
    ui/ui_layout_calculator.h
    ui/ui_layout_constants.h
    ui/npc_dialog_ui.cpp
    ui/npc_dialog_ui.h
    ui/login_screen.cc
    ui/login_screen.h
    ui/skill/skill_bar.cc
    ui/skill/skill_bar.h
    ui/skill/skill_book.cc
    ui/skill/skill_book.h
    ui/skill/cast_bar.cc
    ui/skill/cast_bar.h
    ui/skill/skill_target_indicator.cc
    ui/skill/skill_target_indicator.h
    ui/states/login_screen_state.h
    ui/states/login_state_interface.h
    ui/states/login_state_helpers.cc
    ui/states/login_state_helpers.h
    ui/states/login_input_state.cc
    ui/states/login_input_state.h
    ui/states/character_select_state.cc
    ui/states/character_select_state.h
    ui/states/character_create_state.cc
    ui/states/character_create_state.h
    ui/states/connecting_state.cc
    ui/states/connecting_state.h
    ui/states/error_state.cc
    ui/states/error_state.h

    # Scene module
    scene/scene_manager.cc
    scene/scene_manager.h
    scene/login_scene.cc
    scene/login_scene.h

    # Handler module
    handlers/character_handler.cpp
    handlers/character_handler.h
    handlers/handler_registry.cpp
    handlers/handler_registry.h
    handlers/login_handler.cpp
    handlers/login_handler.h
    handlers/movement_handler.cpp
    handlers/movement_handler.h
    handlers/npc_handler.cpp
    handlers/npc_handler.h
    handlers/combat_handler.cpp
    handlers/combat_handler.h
    handlers/skill_handler.cc
    handlers/skill_handler.h
    handlers/effect_handler.cc
    handlers/effect_handler.h
    handlers/system_handler.cpp
    handlers/system_handler.h

    # Network module
    network/message_dispatcher.cpp
    network/message_dispatcher.h
    network/network_manager.cpp
    network/network_manager.h
    network/network_client.cc
    network/network_client.h
)

add_dependencies(legend2_client legend2_client_schemas)

target_include_directories(legend2_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${GENERATED_SCHEMA_DIR}
)

target_link_libraries(legend2_client PRIVATE
    legend2_common
    yaml-cpp::yaml-cpp
    flatbuffers::flatbuffers
    asio
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    SDL2::SDL2
)

# Optional SDL2 extensions
if(TARGET SDL2_image::SDL2_image)
    target_link_libraries(legend2_client PRIVATE SDL2_image::SDL2_image)
    target_compile_definitions(legend2_client PRIVATE HAS_SDL2_IMAGE)
endif()

if(TARGET SDL2_ttf::SDL2_ttf)
    target_link_libraries(legend2_client PRIVATE SDL2_ttf::SDL2_ttf)
    target_compile_definitions(legend2_client PRIVATE HAS_SDL2_TTF)
endif()

if(TARGET SDL2_mixer::SDL2_mixer)
    target_link_libraries(legend2_client PRIVATE SDL2_mixer::SDL2_mixer)
    target_compile_definitions(legend2_client PRIVATE HAS_SDL2_MIXER)
endif()

# Platform-specific settings
if(WIN32)
    target_link_libraries(legend2_client PRIVATE ws2_32 wsock32)
    # Don't use WinMain - we use standard main()
endif()

# Threading support
find_package(Threads REQUIRED)
target_link_libraries(legend2_client PRIVATE Threads::Threads)

# Copy client assets to the runtime output directory.
if(EXISTS "${CMAKE_SOURCE_DIR}/Data")
    file(COPY ${CMAKE_SOURCE_DIR}/Data DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/Map")
    file(COPY ${CMAKE_SOURCE_DIR}/Map DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/Wav")
    file(COPY ${CMAKE_SOURCE_DIR}/Wav DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/MUSIC")
    file(COPY ${CMAKE_SOURCE_DIR}/MUSIC DESTINATION ${CMAKE_BINARY_DIR}/bin)
endif()
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin/Data/fonts)
