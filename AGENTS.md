# Legend2 C++ Development Guide

**Last Updated**: 2026-02-10

---

## Project Overview

Legend2 is a modern C++20 reimplementation of the classic Legend of Mir 2 MMORPG, comprising both a server and client.

### Architecture

Two-process architecture: `mir2_gateway` (gateway) + `mir2_logic` (logic server).

```
Client ──TCP/KCP──> mir2_gateway ──TCP──> mir2_logic
                    (connection mgmt)     (all game logic)
```

- **Gateway** (`src/server/gateway/`): Client connection management, TCP/KCP dual-channel, heartbeat detection, message routing, connection holding & reconnection buffering
- **Logic** (`src/server/logic/`): All game logic — ECS system tick, coroutine message processing, scene management, storage engine

Entry points:
- `src/server/apps/gateway_main.cc` -> `mir2_gateway` (config: `config/gateway.yaml`, TCP 7000, UDP 7001)
- `src/server/apps/logic_main.cc` -> `mir2_logic` (config: `config/logic.yaml`, TCP 8002)

### Tech Stack

| Component | Technology |
|-----------|-----------|
| **Language** | C++20 |
| **Build System** | CMake 3.25+, vcpkg (manifest mode) |
| **ECS Framework** | EnTT (ENTT_ID_TYPE=std::uint64_t) |
| **Networking** | Asio (standalone async) + KCP dual-channel |
| **Serialization** | FlatBuffers (schemas/ directory) |
| **Storage Engine** | L1 Memory + L2 RocksDB + L3 PostgreSQL (optional) |
| **Database** | PostgreSQL (libpqxx) + Redis (hiredis) — optional, conditional compilation |
| **Logging** | spdlog |
| **Crash Reporting** | breakpad |
| **Encryption** | OpenSSL |
| **Parallelism** | Intel TBB |
| **Scripting** | LuaJIT + sol2 — optional |
| **Compression** | LZ4 |
| **Testing** | GoogleTest + RapidCheck |
| **Client Rendering** | SDL2 (optional) |

---

## Directory Structure

```
mir2-cpp/
├── src/
│   ├── common/                     # Shared code (client + server)
│   │   ├── protocol/               #   Packet codec (packet_codec, message_codec, npc_message_codec)
│   │   ├── network/                #   Channel router, KCP config, fallback controller
│   │   ├── types/                  #   Base types, constants, error codes
│   │   ├── 3rd_party/              #   Third-party libs (ikcp_wrapper)
│   │   ├── character_data.cpp/h    #   Character data definitions
│   │   ├── enums.h                 #   Global enumerations
│   │   ├── compression.cpp/h       #   LZ4 compression
│   │   ├── utf8_utils.cc/h         #   UTF-8 utilities
│   │   ├── item_constants.h        #   Item constants
│   │   └── time_utils.h            #   Time utilities
│   │
│   ├── server/                     # Server
│   │   ├── apps/                   #   Entry points
│   │   │   ├── gateway_main.cc     #     -> mir2_gateway
│   │   │   └── logic_main.cc       #     -> mir2_logic
│   │   │
│   │   ├── gateway/                #   Gateway server
│   │   │   ├── gateway_server.cc/h #     Main service (Initialize/Run/Shutdown)
│   │   │   ├── message_router.cc/h #     Message routing to Logic
│   │   │   ├── connection_holder.cc/h #  Connection holding (reconnect buffer)
│   │   │   └── ring_buffer.cc/h    #     Ring buffer
│   │   │
│   │   ├── logic/                  #   Logic server
│   │   │   ├── logic_server.cc/h   #     Main service (tick loop, coroutine scheduling)
│   │   │   ├── coroutine_executor.cc/h # C++20 coroutine executor
│   │   │   ├── handler_registry.cc/h #   Handler registry (msg_id -> handler)
│   │   │   ├── handler_context.h   #     Handler context
│   │   │   ├── response_sender.cc/h #    Response sender
│   │   │   ├── prewarm_manager.cc/h #    Prewarm manager
│   │   │   ├── crash_handler.cc/h  #     Crash handler (breakpad)
│   │   │   ├── task.h              #     Coroutine Task type
│   │   │   ├── handlers/           #     Message handlers
│   │   │   │   ├── login/          #       Login (login_handler)
│   │   │   │   ├── character/      #       Character (character_handler)
│   │   │   │   ├── chat/           #       Chat (chat_handler)
│   │   │   │   ├── item/           #       Item (item_handler)
│   │   │   │   ├── guild/          #       Guild (guild_handler)
│   │   │   │   ├── movement/       #       Movement (movement_handler, movement_validator)
│   │   │   │   ├── npc/            #       NPC (npc_command_handler)
│   │   │   │   ├── effect/         #       Effect broadcast (effect_broadcast_service)
│   │   │   │   ├── attack_handler.cc/h #   Attack
│   │   │   │   ├── skill_handler.cc/h #    Skill
│   │   │   │   └── handler_error_utils.h # Error utilities
│   │   │   ├── services/           #     Business services
│   │   │   │   ├── ecs_combat_service.cc/h #  ECS combat service
│   │   │   │   ├── ecs_inventory_service.cc/h # ECS inventory service
│   │   │   │   ├── storage_login_service.cc/h # Storage login service
│   │   │   │   ├── client_registry.cc/h #     Client registry
│   │   │   │   ├── merchant_service.cc/h #    Merchant service
│   │   │   │   ├── player_presence_service.cc/h # Player presence
│   │   │   │   ├── session_role_store.cc/h #  Session role store
│   │   │   │   └── role_record.h   #          Role record
│   │   │   └── events/             #     Event pipeline
│   │   │       ├── hot_event_pipeline.cc/h #  MPSC hot event pipeline
│   │   │       ├── hot_event.h     #        Event definitions
│   │   │       ├── event_arena.cc/h #       Event memory pool
│   │   │       └── var_ref.h       #        Variable reference
│   │   │
│   │   ├── ecs/                    #   ECS framework (EnTT)
│   │   │   ├── world.cc/h          #     ECS World management
│   │   │   ├── character_entity_manager.cc/h # Character entity manager
│   │   │   ├── character_snapshot_codec.cc/h # Character snapshot codec
│   │   │   ├── registry_manager.cc/h #   Global Registry management
│   │   │   ├── skill_registry.cc/h #     Skill registry
│   │   │   ├── inventory_migration.cc/h # Inventory data migration
│   │   │   ├── event_bus.h         #     Event bus
│   │   │   ├── component_utils.h   #     Component utilities
│   │   │   ├── dirty_tracker.h     #     Dirty tracker
│   │   │   ├── persistence/        #     Persistence codec
│   │   │   │   └── character_codec.cc/h # Character codec
│   │   │   ├── components/         #     20 components
│   │   │   │   ├── character_components.h  # Character base attributes
│   │   │   │   ├── attribute_component.h   # Attribute calculation
│   │   │   │   ├── combat_component.h      # Combat attributes
│   │   │   │   ├── equipment_component.h   # Equipment system
│   │   │   │   ├── item_component.h        # Items / inventory
│   │   │   │   ├── skill_component.h       # Skills
│   │   │   │   ├── skill_template_component.h # Skill templates
│   │   │   │   ├── effect_component.h      # Effects / buffs
│   │   │   │   ├── transform_component.h   # Position / movement
│   │   │   │   ├── monster_component.h     # Monsters
│   │   │   │   ├── npc_component.h         # NPCs
│   │   │   │   ├── npc_shop_component.h    # NPC shops
│   │   │   │   ├── guild_component.h       # Guild
│   │   │   │   ├── party_component.h       # Party
│   │   │   │   ├── pk_component.h          # PK system
│   │   │   │   ├── summon_component.h      # Summons
│   │   │   │   ├── trade_component.h       # Trading
│   │   │   │   ├── storage_component.h     # Warehouse
│   │   │   │   ├── ground_item_component.h # Ground items
│   │   │   │   └── entity_version_component.h # Entity versioning
│   │   │   ├── systems/            #     27 systems
│   │   │   │   ├── combat_core.cc/h       # Combat core algorithm
│   │   │   │   ├── combat_system.cc/h     # Combat system
│   │   │   │   ├── damage_calculator.cc/h # Damage calculation
│   │   │   │   ├── inventory_system.cc/h  # Inventory
│   │   │   │   ├── skill_system.cc/h      # Skill execution
│   │   │   │   ├── passive_skill_system.cc/h # Passive skills
│   │   │   │   ├── movement_system.cc/h   # Movement validation
│   │   │   │   ├── teleport_system.cc/h   # Teleportation
│   │   │   │   ├── effect_system.cc/h     # Effects / buffs
│   │   │   │   ├── effect_broadcaster.cc/h # Effect broadcast
│   │   │   │   ├── level_up_system.cc/h   # Level-up
│   │   │   │   ├── monster_ai_system.cc/h # Monster AI
│   │   │   │   ├── monster_drop_system.cc/h # Monster drops
│   │   │   │   ├── monster_spawn_system.h # Monster spawning
│   │   │   │   ├── npc_ai_system.cc/h     # NPC AI
│   │   │   │   ├── guild_system.cc/h      # Guild
│   │   │   │   ├── trade_system.cc/h      # Trading
│   │   │   │   ├── storage_system.cc/h    # Warehouse
│   │   │   │   ├── ground_item_system.cc/h # Ground items
│   │   │   │   ├── amulet_consumer.cc/h   # Amulet consumption
│   │   │   │   ├── pk_system.cc/h         # PK system
│   │   │   │   ├── summon_system.cc/h     # Summons
│   │   │   │   ├── equipment_bonus_system.h # Equipment bonuses
│   │   │   │   ├── character_utils.cc/h   # Character utilities
│   │   │   │   ├── spatial_query.cc/h     # Spatial queries
│   │   │   │   ├── pathfinding_helper.cc/h # Pathfinding
│   │   │   │   └── skill_result.h         # Skill result
│   │   │   └── events/             #     13 event types
│   │   │       ├── combat_events.h        # Combat events
│   │   │       ├── skill_events.h         # Skill events
│   │   │       ├── inventory_events.h     # Inventory events
│   │   │       ├── map_events.h           # Map events
│   │   │       ├── guild_events.h         # Guild events
│   │   │       ├── trade_events.h         # Trade events
│   │   │       ├── storage_events.h       # Storage events
│   │   │       ├── monster_events.h       # Monster events
│   │   │       ├── boss_events.h          # Boss events
│   │   │       ├── npc_events.h           # NPC events
│   │   │       ├── area_events.h          # Area events
│   │   │       ├── attribute_events.h     # Attribute events
│   │   │       └── lifecycle_events.h     # Lifecycle events
│   │   │
│   │   ├── game/                   #   Game logic
│   │   │   ├── map/                #     Map system
│   │   │   │   ├── map_loader.cc        # Map file parsing (XOR encrypted)
│   │   │   │   ├── map_instance.cc      # Map instance (entity management, AOI)
│   │   │   │   ├── scene_manager.cc     # Scene manager
│   │   │   │   ├── aoi_manager.cc       # Area of Interest management
│   │   │   │   ├── door_manager.cc      # Door system
│   │   │   │   ├── gate_manager.cc      # Portal system (O(1) hash index)
│   │   │   │   ├── scroll_teleport.cc   # Scroll teleport
│   │   │   │   ├── cross_server_teleport.cc # Cross-server teleport
│   │   │   │   ├── map_event_manager.cc # Map events (fire/mining/shield)
│   │   │   │   ├── chunk_manager.cc     # Chunk loading
│   │   │   │   └── area_event_processor.cc # Area effects
│   │   │   ├── entity/             #     Entity management
│   │   │   │   ├── monster.cc           # Monster
│   │   │   │   ├── monster_manager.cc   # Monster manager
│   │   │   │   ├── boss_behavior.cc     # Boss AI
│   │   │   │   └── boss_manager.cc      # Boss manager
│   │   │   ├── npc/                #     NPC system
│   │   │   │   ├── npc_entity.cc        # NPC entity
│   │   │   │   ├── npc_manager.cc       # NPC manager
│   │   │   │   ├── npc_interaction_handler.cc # Interaction handler
│   │   │   │   ├── npc_script_engine.cc # Script engine
│   │   │   │   ├── npc_state_machine.cc # State machine
│   │   │   │   ├── npc_shop_service.cc  # Shop service
│   │   │   │   ├── lua_script_engine.cc # Lua engine (optional)
│   │   │   │   └── lua_bindings.cc      # Lua bindings (optional)
│   │   │   ├── chat/               #     Chat
│   │   │   │   └── chat_service.cc      # Chat service
│   │   │   ├── guild/              #     Guild
│   │   │   │   └── guild_manager.cc     # Guild manager
│   │   │   ├── item/               #     Items
│   │   │   │   └── item_effect_processor.cc # Item effects
│   │   │   └── event/              #     Event system
│   │   │       ├── event_handler.cc     # Event handler
│   │   │       ├── timed_event_scheduler.cc # Timed events
│   │   │       └── global_event_manager.cc  # Global events
│   │   │
│   │   ├── network/                #   Network layer
│   │   │   ├── network_manager.cc/h     # Network manager
│   │   │   ├── tcp_server.cc/h          # TCP server
│   │   │   ├── tcp_connection.cc/h      # TCP connection
│   │   │   ├── tcp_session.cc/h         # TCP session
│   │   │   ├── tcp_client.cc/h          # TCP client (Logic connection)
│   │   │   ├── kcp_server.cc/h          # KCP server
│   │   │   ├── kcp_session.cc/h         # KCP session
│   │   │   ├── dual_channel_manager.cc/h # Dual-channel manager
│   │   │   ├── message_dispatcher.cc/h  # Message dispatcher
│   │   │   ├── packet_codec.cc/h        # Packet codec
│   │   │   ├── ip_rate_limiter.cc/h     # IP rate limiter
│   │   │   ├── conv_blacklist.cc/h      # KCP conv blacklist
│   │   │   └── handlers/
│   │   │       └── kcp_upgrade_handler.cc/h # KCP upgrade handler
│   │   │
│   │   ├── storage_engine/         #   Unified storage engine
│   │   │   ├── storage_engine.cc/h      # Engine core
│   │   │   ├── types.h                  # Storage types
│   │   │   ├── interfaces/              # Backend interfaces
│   │   │   │   └── storage_backend.h
│   │   │   ├── l1/                      # L1 in-memory cache
│   │   │   │   └── memory_cache.cc/h
│   │   │   ├── l2/                      # L2 RocksDB cache
│   │   │   │   └── rocksdb_cache.cc/h
│   │   │   ├── persistence/             # Async persistence
│   │   │   │   ├── async_persistence_queue.cc/h
│   │   │   │   └── blocking_queue.h
│   │   │   ├── backends/               # Storage backends
│   │   │   │   ├── account_storage_backend.cc/h
│   │   │   │   ├── storage_engine_backend.cc/h
│   │   │   │   ├── common/
│   │   │   │   │   └── account_storage_codec.cc/h
│   │   │   │   ├── postgres/           # PostgreSQL backend
│   │   │   │   │   ├── pg_connection_pool.cc/h
│   │   │   │   │   └── postgres_database.cc/h
│   │   │   │   ├── redis/              # Redis backend
│   │   │   │   │   ├── redis_cache.cc/h
│   │   │   │   │   └── redis_manager.cc/h
│   │   │   │   └── repository/         # Repository pattern
│   │   │   │       └── character_repository.cc/h
│   │   │   └── utils/                  # Utilities
│   │   │       ├── circuit_breaker.cc/h # Circuit breaker
│   │   │       └── global_hybrid_clock.cc/h # Hybrid logical clock
│   │   │
│   │   ├── config/                 #   Configuration loading
│   │   │   ├── config_manager.cc/h      # Config manager
│   │   │   ├── map_config_loader.cc/h   # Map config
│   │   │   └── skill_config_loader.cc/h # Skill config
│   │   │
│   │   ├── core/                   #   Core utilities
│   │   │   ├── application.cc/h         # Application lifecycle
│   │   │   ├── timer.cc/h               # Timer
│   │   │   ├── utils.cc/h               # Utility functions
│   │   │   ├── singleton.h              # Singleton template
│   │   │   ├── non_copyable.h           # Non-copyable base
│   │   │   └── concurrency/             # Concurrency utilities
│   │   │       ├── mpsc_ring.h          #   MPSC lock-free ring queue
│   │   │       └── spsc_ring.h          #   SPSC lock-free ring queue
│   │   │
│   │   ├── common/                 #   Server-internal shared code
│   │   │   ├── internal_message_helper.cc/h # Internal message helpers
│   │   │   ├── crypto_utils.cc/h        # Crypto utilities
│   │   │   ├── error_codes.h            # Error codes
│   │   │   └── snowflake_id.h           # Snowflake ID generator
│   │   │
│   │   ├── data/                   #   Data templates
│   │   │   └── item_template.cc/h       # Item template
│   │   │
│   │   ├── log/                    #   Logging
│   │   │   └── logger.cc/h
│   │   │
│   │   ├── monitor/                #   Metrics (optional)
│   │   │   ├── metrics.cc/h             # Prometheus metrics
│   │   │   └── metrics_stub.cc          # Stub when Prometheus disabled
│   │   │
│   │   └── security/               #   Security
│   │       ├── anti_cheat.cc/h          # Anti-cheat
│   │       └── rate_limiter.cc/h        # Rate limiter
│   │
│   └── client/                     # Client (SDL2, optional)
│       ├── main.cc                 #   Entry point
│       ├── core/                   #   Core (application, timer, event_dispatcher, path_utils)
│       ├── game/                   #   Game logic
│       │   ├── game_client.cc/h         # Client main loop
│       │   ├── entity_manager.cc/h      # Entity manager
│       │   ├── monster/                 # Monster management
│       │   ├── skill/                   # Skill (executor, manager)
│       │   └── map/                     # Map (renderer, path_finder, portal)
│       ├── network/                #   Network
│       │   ├── network_client.cc/h      # TCP client
│       │   ├── network_manager.cpp/h    # Network manager
│       │   ├── dual_channel_client.cc/h # Dual-channel client
│       │   ├── kcp_channel.cc/h         # KCP channel
│       │   ├── kcp_upgrade_handler.cc/h # KCP upgrade
│       │   └── udp_transport.cc/h       # UDP transport
│       ├── render/                 #   Rendering
│       │   ├── renderer.cc/h            # Renderer
│       │   ├── actor_renderer.cc/h      # Actor renderer
│       │   ├── effect_player.cc/h       # Effect player
│       │   ├── sprite_batch.cc/h        # Sprite batch
│       │   └── camera.h                 # Camera
│       ├── scene/                  #   Scenes
│       │   ├── scene_manager.cc/h       # Scene manager
│       │   └── login_scene.cc/h         # Login scene
│       ├── ui/                     #   UI
│       │   ├── login_screen.cc/h        # Login screen
│       │   ├── skill/                   # Skill UI (bar, book, cast_bar, target_indicator)
│       │   ├── states/                  # UI state machine (login, character_select, ...)
│       │   ├── input_validation.cc/h    # Input validation
│       │   ├── npc_dialog_ui.cpp/h      # NPC dialog
│       │   ├── ui_manager.h             # UI manager
│       │   ├── ui_renderer.cc/h         # UI renderer
│       │   └── ui_layout_constants.h    # Layout constants
│       ├── handlers/               #   Message handlers
│       │   ├── login_handler.cpp/h      # Login
│       │   ├── character_handler.cpp/h  # Character
│       │   ├── movement_handler.cpp/h   # Movement
│       │   ├── combat_handler.cpp/h     # Combat
│       │   ├── skill_handler.cc/h       # Skill
│       │   ├── effect_handler.cc/h      # Effect
│       │   ├── npc_handler.cpp/h        # NPC
│       │   ├── system_handler.cpp/h     # System
│       │   └── handler_registry.cpp/h   # Handler registry
│       ├── resource/               #   Resources
│       │   ├── resource_loader.cpp/h    # Resource loader
│       │   └── async_loader.cc/h        # Async loader
│       └── audio/                  #   Audio
│           └── audio_engine.cc/h        # Audio engine
│
├── schemas/                        # FlatBuffers protocol definitions
│   ├── common.fbs                  #   Common types (coordinates, direction, base messages)
│   ├── login.fbs                   #   Login protocol
│   ├── game.fbs                    #   Game messages (movement, NPC, map)
│   ├── combat.fbs                  #   Combat messages
│   ├── item.fbs                    #   Item messages
│   ├── guild.fbs                   #   Guild messages
│   ├── chat.fbs                    #   Chat messages
│   ├── system.fbs                  #   System messages (heartbeat, announcements)
│   ├── internal.fbs                #   Internal service messages (Gateway <-> Logic)
│   └── persistence.fbs             #   Persistence messages
│
├── config/                         # Server configuration
│   ├── gateway.yaml                #   Gateway config (TCP 7000, UDP 7001)
│   ├── logic.yaml                  #   Logic server config (TCP 8002)
│   ├── server.yaml                 #   Common server config
│   ├── game.yaml                   #   Game parameters
│   ├── world.yaml                  #   World config
│   ├── combat_config.yaml          #   Combat parameters
│   ├── gates.yaml                  #   Portal config
│   ├── global_events.yaml          #   Global event config
│   ├── timed_events.yaml           #   Timed event config
│   ├── tables/                     #   Data tables (map attributes etc.)
│   ├── npc_scripts/                #   NPC scripts
│   └── prometheus/                 #   Prometheus config
│
├── tests/                          # Tests (~179 test files)
│   ├── server/                     #   Server tests
│   │   ├── ecs/                    #     ECS tests
│   │   ├── gateway/                #     Gateway tests
│   │   ├── logic/                  #     Logic server tests
│   │   ├── network/                #     Network tests
│   │   ├── guild/                  #     Guild tests
│   │   ├── game/                   #     Game logic tests
│   │   ├── storage_engine/         #     Storage engine tests
│   │   ├── db/                     #     Database tests
│   │   └── mocks/                  #     Mock objects
│   ├── client/                     #   Client tests
│   ├── common/                     #   Common code tests
│   └── integration/                #   Integration tests
│
├── benchmarks/                     # Performance benchmarks
├── migrations/                     # Database migration scripts (11 SQL files)
├── scripts/                        # Build/deploy/test scripts
└── docs/                           # Documentation
```

---

## Quick Start

### Requirements

- **Compiler**: GCC 13.3+ or Clang 16+ (C++20)
- **CMake**: 3.25+
- **vcpkg**: Latest version
- **OS**: Linux (WSL2), macOS, Windows

### Build

```bash
# WSL (recommended)
cmake --preset vcpkg-wsl-debug
cmake --build --preset vcpkg-wsl-debug -j$(nproc)

# Linux
cmake --preset vcpkg-linux-debug
cmake --build --preset vcpkg-linux-debug -j$(nproc)

# Windows
cmake --preset vcpkg-debug
cmake --build --preset vcpkg-debug
```

### Run Services

```bash
# Start gateway
./build-wsl/bin/mir2_gateway

# Start logic server
./build-wsl/bin/mir2_logic

# With custom config
./build-wsl/bin/mir2_gateway --config config/gateway.yaml
./build-wsl/bin/mir2_logic --config config/logic.yaml
```

### Run Tests

```bash
cmake --build --preset vcpkg-wsl-debug --target legend2_tests -j$(nproc)
ctest --test-dir build-wsl --output-on-failure
```

---

## Code Style

Follows [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).

### Naming Conventions

| Type | Style | Example |
|------|-------|---------|
| File names | snake_case | `map_instance.h`, `gate_manager.cc` |
| Classes/structs | PascalCase | `MapInstance`, `GateInfo` |
| Functions/methods | PascalCase | `LoadMap()`, `CheckGateTrigger()` |
| Variables | snake_case | `map_width`, `door_index` |
| Constants | kPascalCase | `kDefaultGridSize`, `kMaxTileCount` |
| Enum values | kPascalCase | `AreaEffectType::kFire` |
| Namespaces | snake_case | `mir2::game::map` |
| Private members | trailing `_` | `map_id_`, `tile_data_` |
| Macros | ALL_CAPS | `MIR2_GAME_MAP_INSTANCE_H_` |

### Key Guidelines

- 2-space indentation, 1 space before access modifiers
- Header guards using `#ifndef` (Google style)
- Prefer `const` references, `std::optional` returns, `std::unique_ptr` ownership
- No raw pointer memory management
- RAII for locks (`std::lock_guard`)
- Use initializer lists over constructor body assignments

### Git Commit Convention

```
<type>(<scope>): <subject>

<body>

Co-Authored-By: <author>
```

Types: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `chore`

Scopes: `gateway`, `logic`, `ecs`, `combat`, `network`, `map`, `guild`, `kcp`, `storage`

---

## Testing

### Frameworks

- **GoogleTest**: Unit tests
- **RapidCheck**: Property-based tests
- **GoogleMock**: Mock objects

### Running Tests

```bash
# All tests
ctest --test-dir build-wsl --output-on-failure

# Specific modules
ctest --test-dir build-wsl -R "gateway_|combat_|ecs_|guild_"

# Verbose output
./build-wsl/bin/legend2_tests --gtest_filter=GateManagerTest.*
```

---

## Core Module Reference

### Gateway Server (`src/server/gateway/`)

- `GatewayServer`: Main service, manages client connection lifecycle
- `MessageRouter`: Routes client messages to Logic server based on `config/gateway.yaml` route table
- `ConnectionHolder`: Buffers messages when Logic disconnects for reconnection
- Supports TCP + KCP dual-channel via `DualChannelManager`

### Logic Server (`src/server/logic/`)

- `LogicServer`: Main tick loop, coroutine scheduling, handler registration
- `CoroutineExecutor`: C++20 coroutine executor
- `HandlerRegistry`: Maps message ID to handler functions
- `HotEventPipeline`: MPSC lock-free event pipeline (IO thread -> Logic thread)

### ECS System (`src/server/ecs/`)

- `RegistryManager`: Global management, multiple Worlds indexed by map_id
- `World`: Single map's ECS container with its own `WorldSystemBundle`
- ECS Registry is not thread-safe; all operations run in the Logic single thread
- 20 components, 27 systems, 13 event types

### Storage Engine (`src/server/storage_engine/`)

- L1: In-memory cache (`MemoryCache`) — hot data
- L2: RocksDB cache (`RocksDBCache`) — warm data
- L3: PostgreSQL (via `backends/`) — cold data persistence
- `AsyncPersistenceQueue`: Async write queue
- `CircuitBreaker`: Backend fault circuit breaker
- `GlobalHybridClock`: Hybrid logical clock

### Network Layer (`src/server/network/`)

- TCP: Primary reliable ordered channel
- KCP: Optional UDP acceleration channel (low latency)
- `DualChannelManager`: Manages TCP/KCP dual-channel
- `IPRateLimiter`: IP-level rate limiting
- `ConvBlacklist`: KCP conv ID blacklist

### Client (`src/client/`)

- SDL2-based with dual-channel networking (TCP + KCP)
- State-machine-driven login UI
- Map rendering, skill bar, NPC dialog
- Audio engine with SDL2_mixer

---

## Build Configuration

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SERVER` | ON | Build server binaries |
| `BUILD_CLIENT` | auto-detect | Build client (requires SDL2) |
| `BUILD_TESTS` | ON | Build tests |
| `BUILD_BENCHMARKS` | OFF | Build performance benchmarks |
| `BUILD_DB` | ON | Build database support (requires libpqxx + hiredis) |
| `BUILD_LUA_SUPPORT` | auto-detect | Enable Lua scripting (requires LuaJIT + sol2) |
| `LEGEND2_ENABLE_PROMETHEUS` | OFF | Enable Prometheus metrics |
| `LEGEND2_ALLOW_FETCHCONTENT` | OFF | Allow auto-downloading missing dependencies |

### CMake Presets

| Preset | Description |
|--------|-------------|
| `vcpkg-wsl-debug` | WSL Debug build (recommended for development) |
| `vcpkg-wsl-release` | WSL Release build |
| `vcpkg-linux-debug` | Native Linux Debug |
| `vcpkg-linux-release` | Native Linux Release |
| `vcpkg-debug` | Windows Debug |
| `vcpkg-release` | Windows Release |
| `vcpkg-benchmark` | Release + benchmarks enabled |

### Build Targets

| Target | Binary | Description |
|--------|--------|-------------|
| `mir2_gateway` | `bin/mir2_gateway` | Gateway server |
| `mir2_logic` | `bin/mir2_logic` | Logic server |
| `legend2_client` | `bin/legend2_client` | Game client (optional) |
| `legend2_tests` | `bin/legend2_tests` | Test suite |
| `mir2_server_lib` | (static lib) | Server core library |
| `mir2_storage_engine` | (static lib) | Storage engine library |
| `mir2_db_optional` | (static lib) | Optional DB adapters |
| `legend2_common` | (static lib) | Shared common library |
