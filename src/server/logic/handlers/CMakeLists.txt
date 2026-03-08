# Logic Handlers

if(TARGET mir2_server_lib)
  target_sources(mir2_server_lib PRIVATE
    attack_handler.cc
    skill_handler.cc

    # Character Handler
    character/character_handler.cc
    character/bonus_point_handler.cc

    # Chat Handler
    chat/chat_handler.cc

    # Effect Broadcast
    effect/effect_broadcast_service.cc

    # Guild Handler
    guild/guild_handler.cc

    # Item Handler
    item/item_handler.cc

    # Login Handler
    login/login_handler.cc

    # Movement Handler
    movement/movement_validator.cc
    movement/movement_handler.cc

    # NPC Handler
    npc/npc_command_handler.cc

    # Party Handler
    party/party_handler.cc

    # Mail Handler
    mail/mail_handler.cc

    # Ranking Handler
    ranking/ranking_handler.cc

    # Achievement Handler
    achievement/achievement_handler.cc

    # Auction Handler
    auction/auction_handler.cc

    # Trade Handler
    trade/trade_handler.cc
  )
endif()
