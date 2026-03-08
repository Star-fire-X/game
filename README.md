# Legend2

Modern C++20 reimplementation of the classic Legend of Mir 2 MMORPG server and client.

## Architecture

Two-process server design: **Gateway** handles connections, **Logic** runs all game logic.

```
Client ──TCP/KCP──> mir2_gateway ──TCP──> mir2_logic
                    (connections)         (game logic)
```

| Binary | Role |
|--------|------|
| `mir2_gateway` | Client connection management, TCP/KCP dual-channel, message routing, reconnection buffering |
| `mir2_logic` | ECS-based game logic, coroutine message processing, scene management, storage engine |
| `legend2_client` | SDL2 game client (optional) |

## Tech Stack

| Category | Technology |
|----------|-----------|
| Language | C++20 |
| Build | CMake 3.25+, vcpkg (manifest mode) |
| Networking | Asio (standalone) + KCP dual-channel |
| Serialization | FlatBuffers |
| ECS | EnTT |
| Parallelism | Intel TBB |
| Logging | spdlog |
| Crash Reporting | breakpad |
| Encryption | OpenSSL |
| Storage | L1 Memory -> L2 RocksDB -> L3 PostgreSQL |
| Database | PostgreSQL (libpqxx), Redis (hiredis) -- optional |
| Scripting | LuaJIT + sol2 -- optional |
| Compression | LZ4 |
| Client | SDL2, SDL2_image, SDL2_ttf, SDL2_mixer -- optional |
| Testing | GoogleTest, RapidCheck |

## Quick Start

### Prerequisites

- GCC 13.3+ or Clang 16+ (C++20 support)
- CMake 3.25+
- vcpkg (latest)

### Build

```bash
# Dependencies install automatically via vcpkg manifest mode

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

### Run

```bash
# Start both processes
./build-wsl/bin/mir2_gateway    # TCP 7000, UDP 7001
./build-wsl/bin/mir2_logic      # TCP 8002

# With custom config
./build-wsl/bin/mir2_gateway --config config/gateway.yaml
./build-wsl/bin/mir2_logic --config config/logic.yaml
```

### Test

```bash
cmake --build --preset vcpkg-wsl-debug --target legend2_tests -j$(nproc)
ctest --test-dir build-wsl --output-on-failure

# Filter by module
ctest --test-dir build-wsl -R "gateway_|combat_|ecs_|guild_"
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SERVER` | ON | Build server binaries |
| `BUILD_CLIENT` | auto | Build SDL2 client |
| `BUILD_TESTS` | ON | Build test suite |
| `BUILD_BENCHMARKS` | OFF | Build performance benchmarks |
| `BUILD_DB` | ON | Database support (libpqxx + hiredis) |
| `BUILD_LUA_SUPPORT` | auto | Lua scripting (LuaJIT + sol2) |
| `LEGEND2_ENABLE_PROMETHEUS` | OFF | Prometheus metrics |
| `LEGEND2_ALLOW_FETCHCONTENT` | OFF | Auto-download missing deps |

## Project Structure

```
mir2-cpp/
├── src/
│   ├── common/              # Shared code (client + server)
│   │   ├── protocol/        #   Packet / message codecs
│   │   ├── network/         #   Channel router, KCP config, fallback
│   │   └── types/           #   Types, constants, error codes
│   │
│   ├── server/
│   │   ├── apps/            #   Entry points (gateway_main, logic_main)
│   │   ├── gateway/         #   Gateway server (connection mgmt, routing, reconnect buffer)
│   │   ├── logic/           #   Logic server
│   │   │   ├── handlers/    #     Message handlers (login, character, chat, item,
│   │   │   │                #       guild, movement, npc, attack, skill, effect)
│   │   │   ├── services/    #     Business services (combat, inventory, login, merchant)
│   │   │   └── events/      #     MPSC hot event pipeline
│   │   ├── ecs/             #   ECS framework (EnTT)
│   │   │   ├── components/  #     20 components (character, combat, equipment, item, ...)
│   │   │   ├── systems/     #     27 systems (combat, inventory, skill, movement, ...)
│   │   │   └── events/      #     13 event types (combat, skill, map, guild, ...)
│   │   ├── game/            #   Game logic
│   │   │   ├── map/         #     Map system (loading, AOI, teleport, portals, events)
│   │   │   ├── entity/      #     Monster & boss management
│   │   │   ├── npc/         #     NPC system (interaction, scripting, shops)
│   │   │   ├── chat/        #     Chat service
│   │   │   ├── guild/       #     Guild manager
│   │   │   ├── item/        #     Item effects
│   │   │   └── event/       #     Timed & global events
│   │   ├── network/         #   Network layer (TCP + KCP dual-channel)
│   │   ├── storage_engine/  #   Unified storage (L1 Memory, L2 RocksDB, L3 PostgreSQL)
│   │   │   ├── backends/    #     DB backends (PostgreSQL, Redis, repository)
│   │   │   └── persistence/ #     Async persistence queue
│   │   ├── config/          #   Config loading (map, skill)
│   │   ├── core/            #   Utilities (timer, singleton, lock-free queues)
│   │   ├── monitor/         #   Prometheus metrics (optional)
│   │   └── security/        #   Anti-cheat, rate limiting
│   │
│   └── client/              # SDL2 client (optional)
│       ├── game/            #   Game client logic, map, monsters, skills
│       ├── network/         #   TCP + KCP dual-channel client
│       ├── render/          #   Renderer, sprite batch, effects
│       ├── scene/           #   Scene manager
│       ├── ui/              #   Login, skill bar, NPC dialog, state machine
│       ├── handlers/        #   Network message handlers
│       ├── resource/        #   Resource & async loader
│       └── audio/           #   Audio engine
│
├── schemas/                 # FlatBuffers protocol definitions (10 .fbs files)
├── config/                  # Server config (gateway, logic, game, combat, maps, ...)
├── tests/                   # Unit & integration tests (~179 files)
├── benchmarks/              # Performance benchmarks
├── migrations/              # PostgreSQL migration scripts (11 files)
├── scripts/                 # Build, deploy, test scripts
└── docs/                    # Documentation
```

## Architecture Details

### ECS (Entity-Component-System)

The server uses EnTT-based ECS architecture:

- **RegistryManager**: Manages multiple `World` instances, one per game map
- **World**: Contains an ECS registry with its own system bundle
- **Components**: Pure data structs (character, combat, equipment, inventory, skills, effects, etc.)
- **Systems**: Logic processors (combat, movement, skill execution, monster AI, guild, trade, etc.)
- All ECS operations run single-threaded in the Logic server tick loop

### Networking

- **Client <-> Gateway**: TCP + KCP dual-channel (upgradeable/degradable at runtime)
- **Gateway <-> Logic**: TCP internal connection
- **Serialization**: FlatBuffers binary protocol with zero-copy deserialization
- **Message routing**: Configured in `config/gateway.yaml` with per-message auth requirements

### Storage Engine

Three-tier storage with automatic data flow:

```
L1 Memory Cache  ->  L2 RocksDB  ->  L3 PostgreSQL
   (hot data)        (warm data)      (cold/persistent)
```

- `AsyncPersistenceQueue` for non-blocking writes
- `CircuitBreaker` for backend fault tolerance
- `GlobalHybridClock` for distributed timestamp ordering

### Coroutine-Based Message Processing

The Logic server uses C++20 coroutines for message handling:

- `CoroutineExecutor` schedules handler coroutines
- `HotEventPipeline` (MPSC lock-free queue) bridges IO threads to the Logic thread
- Each handler is a coroutine that can await async operations

## Environment Configuration

Copy `.env.example` to `.env` and customize. Key settings:

- `POSTGRES_HOST/PORT/USER/PASSWORD` -- PostgreSQL connection
- `REDIS_HOST/PORT` -- Redis cache
- `GATEWAY_PORT` (7000) -- Client-facing TCP port
- `GATEWAY_BIND_IP` -- Listen address

## Database

PostgreSQL is used for persistent storage. Initialize with migration scripts:

```bash
# Apply migrations in order
psql -U mir2 -d mir2_game -f migrations/001_create_accounts.sql
# ... through 011_create_kv_store.sql
```

## License

MIT License
