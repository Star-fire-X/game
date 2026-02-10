# Legend2

传奇2风格MMORPG游戏服务器与客户端的C++20现代化重构实现。

## 项目概述

Legend2 采用双进程架构，包含以下核心组件：

| 组件 | 说明 |
|------|------|
| `mir2_gateway` | 网关服务器，负责客户端连接管理（TCP + KCP 双通道）与消息路由 |
| `mir2_logic` | 逻辑服务器，承载全部游戏逻辑（ECS、战斗、背包、地图、公会等） |
| `legend2_client` | SDL2 客户端（可选编译） |

## 技术栈

| 类别 | 技术 |
|------|------|
| **语言** | C++20 |
| **构建** | CMake 3.25+, vcpkg |
| **网络** | Asio (standalone), KCP 双通道 |
| **序列化** | FlatBuffers |
| **ECS** | EnTT |
| **并行** | Intel TBB |
| **日志** | spdlog |
| **崩溃收集** | breakpad |
| **加密** | OpenSSL |
| **存储引擎** | RocksDB (L2 cache) |
| **数据库** | PostgreSQL (libpqxx), Redis (hiredis) — 可选 |
| **脚本** | LuaJIT + sol2 — 可选 |
| **压缩** | LZ4 |
| **客户端** | SDL2, SDL2_image, SDL2_ttf, SDL2_mixer — 可选 |
| **测试** | GoogleTest, RapidCheck |

## 快速开始

### 依赖安装

```bash
# 使用 vcpkg manifest 模式自动安装（推荐）
# cmake configure 时会自动根据 vcpkg.json 安装所有依赖

# 或手动安装核心依赖
vcpkg install asio flatbuffers yaml-cpp nlohmann-json entt tbb spdlog breakpad openssl rocksdb lz4
```

### 编译

#### 方式 A: WSL 环境（推荐）

```bash
# 配置
cmake --preset vcpkg-wsl-debug

# 编译
cmake --build --preset vcpkg-wsl-debug -j$(nproc)
```

#### 方式 B: Linux 环境

```bash
cmake --preset vcpkg-linux-debug
cmake --build --preset vcpkg-linux-debug -j$(nproc)
```

#### 方式 C: Windows 环境

```bash
cmake --preset vcpkg-debug
cmake --build --preset vcpkg-debug
```

### 运行

```bash
# 启动网关服务器（默认配置 config/gateway.yaml）
./build-wsl/bin/mir2_gateway

# 启动逻辑服务器（默认配置 config/logic.yaml）
./build-wsl/bin/mir2_logic

# 指定配置文件
./build-wsl/bin/mir2_gateway --config path/to/gateway.yaml
./build-wsl/bin/mir2_logic --config path/to/logic.yaml
```

### 运行测试

```bash
# 编译测试
cmake --build --preset vcpkg-wsl-debug --target legend2_tests -j$(nproc)

# 运行所有测试
ctest --test-dir build-wsl --output-on-failure

# 运行特定模块测试
ctest --test-dir build-wsl -R "gateway_|combat_|ecs_"
```

### CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_SERVER` | ON | 编译服务器 |
| `BUILD_CLIENT` | 自动检测 | 编译客户端（需要 SDL2） |
| `BUILD_TESTS` | ON | 编译测试 |
| `BUILD_BENCHMARKS` | OFF | 编译性能测试 |
| `BUILD_DB` | ON | 编译数据库支持（需要 libpqxx + hiredis） |
| `BUILD_LUA_SUPPORT` | 自动检测 | 启用 Lua 脚本支持（需要 LuaJIT + sol2） |
| `LEGEND2_ENABLE_PROMETHEUS` | OFF | 启用 Prometheus 指标采集 |
| `LEGEND2_ALLOW_FETCHCONTENT` | OFF | 允许自动下载缺失依赖 |

## 项目结构

```
mir2-cpp/
├── src/
│   ├── common/                 # 客户端/服务端共享代码
│   │   ├── protocol/           # 网络协议编解码
│   │   ├── network/            # 通道路由、KCP 配置、降级控制
│   │   └── types/              # 基础类型定义、常量、错误码
│   │
│   ├── server/                 # 服务端
│   │   ├── apps/               # 入口点 (gateway_main.cc, logic_main.cc)
│   │   ├── gateway/            # 网关服务器 (连接管理、消息路由、连接保持)
│   │   ├── logic/              # 逻辑服务器
│   │   │   ├── handlers/       # 消息处理器 (login, character, combat, chat,
│   │   │   │                   #   item, guild, movement, npc, attack, skill, effect)
│   │   │   ├── services/       # 业务服务 (ecs_combat, ecs_inventory, storage_login)
│   │   │   └── events/         # 事件管线 (HotEventPipeline, EventArena)
│   │   ├── ecs/                # ECS 框架 (EnTT)
│   │   │   ├── components/     # 20 个组件 (character, combat, equipment, item, guild, ...)
│   │   │   ├── systems/        # 25+ 系统 (combat, inventory, skill, movement, guild, ...)
│   │   │   └── events/         # 13 类事件 (combat, inventory, map, skill, guild, ...)
│   │   ├── game/               # 游戏逻辑
│   │   │   ├── map/            # 地图系统 (加载、AOI、传送、门、事件)
│   │   │   ├── entity/         # 实体管理 (player, monster, boss)
│   │   │   ├── npc/            # NPC 系统 (交互、脚本引擎、状态机、商店)
│   │   │   ├── chat/           # 聊天服务
│   │   │   ├── guild/          # 公会管理
│   │   │   ├── item/           # 物品效果处理
│   │   │   └── event/          # 定时事件、全局事件
│   │   ├── network/            # 网络层 (TCP + KCP 双通道)
│   │   ├── storage_engine/     # 统一存储引擎
│   │   │   ├── l1/             # L1 内存缓存
│   │   │   ├── l2/             # L2 RocksDB 缓存
│   │   │   ├── persistence/    # 异步持久化队列
│   │   │   └── utils/          # 断路器、混合时钟
│   │   ├── db/                 # 数据库访问 (PostgreSQL + Redis, 可选)
│   │   ├── world/              # 角色存储 (RoleStore)
│   │   ├── combat/             # 战斗核心算法
│   │   ├── config/             # 配置加载 (地图、技能)
│   │   ├── core/               # 核心工具 (Application, Timer, 并发队列)
│   │   ├── data/               # 数据模板 (物品模板)
│   │   ├── handlers/           # 共享处理器 (移动验证、实体广播、KCP 升级)
│   │   ├── legacy/             # 旧版兼容代码
│   │   ├── log/                # 日志
│   │   ├── monitor/            # Prometheus 指标 (可选)
│   │   └── security/           # 反作弊、频率限制
│   │
│   └── client/                 # 客户端 (SDL2)
│       ├── game/               # 游戏客户端逻辑
│       ├── network/            # 网络通信 (含 KCP 双通道)
│       ├── render/             # 渲染
│       ├── scene/              # 场景管理
│       ├── ui/                 # UI 系统
│       └── resource/           # 资源加载
│
├── schemas/                    # FlatBuffers 协议定义
│   ├── common.fbs              # 通用消息
│   ├── login.fbs               # 登录协议
│   ├── game.fbs                # 游戏消息
│   ├── combat.fbs              # 战斗消息
│   ├── item.fbs                # 物品消息
│   ├── guild.fbs               # 公会消息
│   ├── chat.fbs                # 聊天消息
│   ├── system.fbs              # 系统消息
│   ├── internal.fbs            # 内部服务间消息
│   └── persistence.fbs         # 持久化消息
│
├── config/                     # 服务端配置
│   ├── gateway.yaml            # 网关配置 (端口 7000, UDP 7001)
│   ├── logic.yaml              # 逻辑服务配置 (端口 8002)
│   ├── server.yaml             # 通用服务配置
│   ├── game.yaml               # 游戏参数
│   ├── world.yaml              # 世界配置
│   ├── combat_config.yaml      # 战斗参数
│   ├── gates.yaml              # 传送门配置
│   ├── global_events.yaml      # 全局事件
│   ├── timed_events.yaml       # 定时事件
│   ├── tables/                 # 数据表 (地图属性等)
│   └── npc_scripts/            # NPC 脚本
│
├── tests/                      # 单元测试 / 集成测试
├── migrations/                 # 数据库迁移脚本
└── docs/                       # 文档
```

## 架构设计

### 双进程架构

```
客户端 ──TCP/KCP──> mir2_gateway ──TCP──> mir2_logic
                     (连接管理)          (全部游戏逻辑)
```

- **Gateway** 负责客户端连接管理、TCP/KCP 双通道、心跳检测、消息路由、连接保持与断线重连缓冲
- **Logic** 负责全部游戏逻辑：ECS 系统 tick、消息处理（协程调度）、场景管理、存储引擎

### ECS 架构

服务器采用 Entity-Component-System 架构，基于 EnTT 实现：

- **Entity**: 游戏对象的唯一标识符
- **Component**: 纯数据结构（CharacterAttributes, CombatComponent, GuildComponent 等）
- **System**: 处理逻辑（CombatSystem, InventorySystem, SkillSystem, GuildSystem 等）
- **World**: 每个地图对应一个独立的 ECS World，由 RegistryManager 统一管理

详见 [ECS 系统指南](src/server/ecs/README.md) 和 [线程模型](src/server/ecs/THREADING.md)。

### 网络协议

- **客户端 <-> Gateway**: TCP + KCP 双通道（可升级/降级）
- **Gateway <-> Logic**: TCP 内部连接
- **序列化**: FlatBuffers 二进制协议，支持零拷贝反序列化
- **协议定义**: `schemas/` 目录下 10 个 `.fbs` 文件

### 存储引擎

三层存储架构：L1 内存缓存 -> L2 RocksDB -> L3 PostgreSQL（可选）

## 许可证

MIT License
