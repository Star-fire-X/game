# Logic Server Module

if(TARGET mir2_server_lib)
    target_sources(mir2_server_lib PRIVATE
        logic_server.cc
        coroutine_executor.cc
        crash_handler.cc
        services/client_registry.cc
        services/merchant_service.cc
        services/ecs_combat_service.cc
        services/ecs_inventory_service.cc
        services/storage_login_service.cc
        services/player_presence_service.cc
        services/session_role_store.cc
        handler_registry.cc
        response_sender.cc
        prewarm_manager.cc
        events/hot_event_pipeline.cc
        events/event_arena.cc
    )
    add_subdirectory(handlers)
endif()
