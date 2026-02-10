 -# Legend2 C++ 开发指南
      2 -
      3 -**最后更新**: 2026-02-09
      4 -
      5 ----
      6 -
      7 -## 项目概览
      8 -
      9 -Legend2（传奇2）C++ 重制版，使用现代 C++20 技术栈实现的经典 MMORPG 服务端和客户端。
     10 -
     11 -### 架构
     12 -
     13 -双进程架构：`mir2_gateway`（网关）+ `mir2_logic`（逻辑服务器）。
     14 -
     15 -```
     16 -客户端 ──TCP/KCP──> mir2_gateway ──TCP──> mir2_logic
     17 -                    (连接管理)           (全部游戏逻辑)
     18 -```
     19 -
     20 -- **Gateway** (`src/server/gateway/`): 客户端连接管理、TCP/KCP 双通道、心跳检测、消息路由、连接保持与断线重连缓
冲
     21 -- **Logic** (`src/server/logic/`): 全部游戏逻辑 — ECS 系统 tick、协程消息处理、场景管理、存储引擎
     22 -
     23 -入口点：
     24 -- `src/server/apps/gateway_main.cc` → `mir2_gateway`（配置：`config/gateway.yaml`，端口 7000/TCP, 7001/UDP）
     25 -- `src/server/apps/logic_main.cc` → `mir2_logic`（配置：`config/logic.yaml`，端口 8002/TCP）
     26 -
     27 -### 技术栈
     28 -
     29 -| 组件 | 技术 |
     30 -|------|------|
     31 -| **语言** | C++20 |
     32 -| **构建系统** | CMake 3.25+, vcpkg (manifest 模式) |
     33 -| **ECS 框架** | EnTT (ENTT_ID_TYPE=std::uint64_t) |
     34 -| **网络** | Asio (standalone async) + KCP 双通道 |
     35 -| **序列化** | FlatBuffers (schemas/ 目录) |
     36 -| **存储引擎** | L1 内存 + L2 RocksDB + L3 PostgreSQL (可选) |
     37 -| **数据库** | PostgreSQL (libpqxx) + Redis (hiredis) — 可选，条件编译 |
     38 -| **日志** | spdlog |
     39 -| **崩溃收集** | breakpad |
     40 -| **加密** | OpenSSL |
     41 -| **并行** | Intel TBB |
     42 -| **脚本** | LuaJIT + sol2 — 可选 |
     43 -| **压缩** | LZ4 |
     44 -| **测试** | GoogleTest + RapidCheck |
     45 -| **客户端渲染** | SDL2 (可选) |
     46 -
     47 ----
     48 -
     49 -## 目录结构
     50 -
     51 -```
     52 -mir2-cpp/
     53 -├── src/
     54 -│   ├── common/                     # 客户端/服务端共享代码
     55 -│   │   ├── protocol/               #   网络协议编解码 (packet_codec)
     56 -│   │   ├── network/                #   通道路由 (channel_router)、KCP 配置、降级控制
     57 -│   │   ├── types/                  #   基础类型、常量、错误码
     58 -│   │   ├── character_data.cpp/h    #   角色数据定义
     59 -│   │   ├── enums.h                 #   全局枚举
     60 -│   │   ├── compression.cpp/h       #   LZ4 压缩
     61 -│   │   ├── utf8_utils.cc/h         #   UTF-8 工具
     62 -│   │   └── 3rd_party/              #   第三方库
     63 -│   │
     64 -│   ├── server/                     # 服务端
     65 -│   │   ├── apps/                   #   入口点
     66 -│   │   │   ├── gateway_main.cc     #     → mir2_gateway
     67 -│   │   │   └── logic_main.cc       #     → mir2_logic
     68 -│   │   │
     69 -│   │   ├── gateway/                #   网关服务器
     70 -│   │   │   ├── gateway_server.cc/h #     主服务 (Initialize/Run/Shutdown)
     71 -│   │   │   ├── message_router.cc/h #     消息路由
     72 -│   │   │   ├── connection_holder.cc/h #  连接保持 (断线缓冲)
     73 -│   │   │   └── ring_buffer.cc/h    #     环形缓冲区
     74 -│   │   │
     75 -│   │   ├── logic/                  #   逻辑服务器
     76 -│   │   │   ├── logic_server.cc/h   #     主服务 (Tick 循环、协程调度)
     77 -│   │   │   ├── coroutine_executor.cc/h # C++20 协程执行器
     78 -│   │   │   ├── handler_registry.cc/h #   Handler 注册表
     79 -│   │   │   ├── handler_context.h   #     Handler 上下文
     80 -│   │   │   ├── response_sender.cc/h #    响应发送
     81 -│   │   │   ├── prewarm_manager.cc/h #    预热管理
     82 -│   │   │   ├── crash_handler.cc/h  #     崩溃处理 (breakpad)
     83 -│   │   │   ├── task.h              #     协程 Task 类型
     84 -│   │   │   ├── handlers/           #     消息处理器
     85 -│   │   │   │   ├── login/          #       登录 (login_handler)
     86 -│   │   │   │   ├── character/      #       角色 (character_handler)
     87 -│   │   │   │   ├── combat/         #       战斗 (combat_handler)
     88 -│   │   │   │   ├── chat/           #       聊天 (chat_handler)
     89 -│   │   │   │   ├── item/           #       物品 (item_handler)
     90 -│   │   │   │   ├── guild/          #       公会 (guild_handler)
     91 -│   │   │   │   ├── movement/       #       移动 (movement_handler)
     92 -│   │   │   │   ├── npc/            #       NPC (npc_command_handler)
     93 -│   │   │   │   ├── effect/         #       效果广播 (effect_broadcast_service)
     94 -│   │   │   │   ├── attack_handler.cc/h #   攻击
     95 -│   │   │   │   ├── move_handler.cc/h #     移动 (顶层)
     96 -│   │   │   │   └── skill_handler.cc/h #    技能
     97 -│   │   │   ├── services/           #     业务服务
     98 -│   │   │   │   ├── ecs_combat_service.cc/h #  ECS 战斗服务
     99 -│   │   │   │   ├── ecs_inventory_service.cc/h # ECS 背包服务
    100 -│   │   │   │   └── storage_login_service.cc/h # 存储登录服务
    101 -│   │   │   └── events/             #     事件管线
    102 -│   │   │       ├── hot_event_pipeline.cc/h #  MPSC 热事件管线
    103 -│   │   │       ├── hot_event.h     #        事件定义
    104 -│   │   │       ├── event_arena.cc/h #       事件内存池
    105 -│   │   │       └── var_ref.h       #        变量引用
    106 -│   │   │
    107 -│   │   ├── ecs/                    #   ECS 框架 (EnTT)
    108 -│   │   │   ├── world.cc            #     ECS World 管理
    109 -│   │   │   ├── character_entity_manager.cc/h # 角色实体管理
    110 -│   │   │   ├── character_snapshot_codec.cc/h # 角色快照编解码
    111 -│   │   │   ├── registry_manager.cc  #    全局 Registry 管理
    112 -│   │   │   ├── skill_registry.cc    #    技能注册表
    113 -│   │   │   ├── inventory_migration.cc # 背包数据迁移
    114 -│   │   │   ├── event_bus.h          #    事件总线
    115 -│   │   │   ├── component_utils.h    #    组件工具
    116 -│   │   │   ├── components/          #    20 个组件
    117 -│   │   │   │   ├── character_components.h  # 角色基础属性
    118 -│   │   │   │   ├── attribute_component.h   # 属性计算
    119 -│   │   │   │   ├── combat_component.h      # 战斗属性
    120 -│   │   │   │   ├── equipment_component.h   # 装备系统
    121 -│   │   │   │   ├── item_component.h        # 物品/背包
    122 -│   │   │   │   ├── skill_component.h       # 技能
    123 -│   │   │   │   ├── skill_template_component.h # 技能模板
    124 -│   │   │   │   ├── effect_component.h      # 效果/Buff
    125 -│   │   │   │   ├── transform_component.h   # 位置/移动
    126 -│   │   │   │   ├── monster_component.h     # 怪物
    127 -│   │   │   │   ├── npc_component.h         # NPC
    128 -│   │   │   │   ├── npc_shop_component.h    # NPC 商店
    129 -│   │   │   │   ├── guild_component.h       # 公会
    130 -│   │   │   │   ├── party_component.h       # 组队
    131 -│   │   │   │   ├── pk_component.h          # PK 系统
    132 -│   │   │   │   ├── summon_component.h      # 召唤
    133 -│   │   │   │   ├── trade_component.h       # 交易
    134 -│   │   │   │   ├── storage_component.h     # 仓库
    135 -│   │   │   │   ├── ground_item_component.h # 地面物品
    136 -│   │   │   │   └── entity_version_component.h # 实体版本
    137 -│   │   │   ├── systems/             #    25+ 系统
    138 -│   │   │   │   ├── combat_system.cc/h      # 战斗
    139 -│   │   │   │   ├── damage_calculator.cc/h  # 伤害计算
    140 -│   │   │   │   ├── inventory_system.cc/h   # 背包
    141 -│   │   │   │   ├── skill_system.cc/h       # 技能执行
    142 -│   │   │   │   ├── passive_skill_system.cc/h # 被动技能
    143 -│   │   │   │   ├── movement_system.cc/h    # 移动验证
    144 -│   │   │   │   ├── teleport_system.cc/h    # 传送
    145 -│   │   │   │   ├── effect_system.cc/h      # 效果/Buff
    146 -│   │   │   │   ├── effect_broadcaster.cc/h # 效果广播
    147 -│   │   │   │   ├── level_up_system.cc/h    # 升级
    148 -│   │   │   │   ├── monster_ai_system.cc/h  # 怪物 AI
    149 -│   │   │   │   ├── monster_drop_system.cc/h # 怪物掉落
    150 -│   │   │   │   ├── monster_spawn_system.cc/h # 怪物生成
    151 -│   │   │   │   ├── npc_ai_system.cc/h      # NPC AI
    152 -│   │   │   │   ├── guild_system.cc/h       # 公会
    153 -│   │   │   │   ├── trade_system.cc/h       # 交易
    154 -│   │   │   │   ├── storage_system.cc/h     # 仓库
    155 -│   │   │   │   ├── ground_item_system.cc/h # 地面物品
    156 -│   │   │   │   ├── amulet_consumer.cc/h    # 护身符消耗
    157 -│   │   │   │   ├── pk_system.cc/h          # PK 系统
    158 -│   │   │   │   ├── summon_system.cc/h      # 召唤
    159 -│   │   │   │   ├── equipment_bonus_system.cc/h # 装备加成
    160 -│   │   │   │   ├── character_utils.cc/h    # 角色工具
    161 -│   │   │   │   ├── spatial_query.cc/h      # 空间查询
    162 -│   │   │   │   ├── pathfinding_helper.cc/h # 寻路
    163 -│   │   │   │   └── skill_result.h          # 技能结果
    164 -│   │   │   └── events/              #    13 类事件
    165 -│   │   │       ├── combat_events.h         # 战斗事件
    166 -│   │   │       ├── skill_events.h          # 技能事件
    167 -│   │   │       ├── inventory_events.h      # 背包事件
    168 -│   │   │       ├── map_events.h            # 地图事件
    169 -│   │   │       ├── guild_events.h          # 公会事件
    170 -│   │   │       ├── trade_events.h          # 交易事件
    171 -│   │   │       ├── storage_events.h        # 仓库事件
    172 -│   │   │       ├── monster_events.h        # 怪物事件
    173 -│   │   │       ├── boss_events.h           # BOSS 事件
    174 -│   │   │       ├── npc_events.h            # NPC 事件
    175 -│   │   │       ├── area_events.h           # 区域事件
    176 -│   │   │       ├── attribute_events.h      # 属性事件
    177 -│   │   │       └── lifecycle_events.h      # 生命周期事件
    178 -│   │   │
    179 -│   │   ├── game/                   #   游戏逻辑
    180 -│   │   │   ├── map/                #     地图系统
    181 -│   │   │   │   ├── map_loader.cc         # 地图文件解析 (支持 XOR 加密)
    182 -│   │   │   │   ├── map_instance.cc       # 地图实例 (实体管理、AOI)
    183 -│   │   │   │   ├── scene_manager.cc      # 场景管理器
    184 -│   │   │   │   ├── aoi_manager.cc        # 视野管理 (AOI)
    185 -│   │   │   │   ├── door_manager.cc       # 门系统
    186 -│   │   │   │   ├── gate_manager.cc       # 传送门 (O(1) 哈希索引)
    187 -│   │   │   │   ├── scroll_teleport.cc    # 卷轴传送
    188 -│   │   │   │   ├── cross_server_teleport.cc # 跨服传送
    189 -│   │   │   │   ├── map_event_manager.cc  # 地图事件 (火焰/采矿/圣盾)
    190 -│   │   │   │   ├── chunk_manager.cc      # 分块加载
    191 -│   │   │   │   └── area_event_processor.cc # 区域效果
    192 -│   │   │   ├── entity/             #     实体管理
    193 -│   │   │   │   ├── player.cc/h           # 玩家
    194 -│   │   │   │   ├── player_manager.cc/h   # 玩家管理器
    195 -│   │   │   │   ├── monster.cc            # 怪物
    196 -│   │   │   │   ├── monster_manager.cc    # 怪物管理器
    197 -│   │   │   │   ├── boss_behavior.cc      # BOSS AI
    198 -│   │   │   │   └── boss_manager.cc       # BOSS 管理器
    199 -│   │   │   ├── npc/                #     NPC 系统
    200 -│   │   │   │   ├── npc_entity.cc         # NPC 实体
    201 -│   │   │   │   ├── npc_manager.cc        # NPC 管理器
    202 -│   │   │   │   ├── npc_interaction_handler.cc # 交互处理
    203 -│   │   │   │   ├── npc_script_engine.cc  # 脚本引擎
    204 -│   │   │   │   ├── npc_state_machine.cc  # 状态机
    205 -│   │   │   │   ├── npc_shop_service.cc   # 商店服务
    206 -│   │   │   │   ├── lua_script_engine.cc  # Lua 引擎 (可选)
    207 -│   │   │   │   └── lua_bindings.cc       # Lua 绑定 (可选)
    208 -│   │   │   ├── chat/               #     聊天
    209 -│   │   │   │   └── chat_service.cc       # 聊天服务
    210 -│   │   │   ├── guild/              #     公会
    211 -│   │   │   │   └── guild_manager.cc      # 公会管理器
    212 -│   │   │   ├── item/               #     物品
    213 -│   │   │   │   └── item_effect_processor.cc # 物品效果
    214 -│   │   │   └── event/              #     事件系统
    215 -│   │   │       ├── event_handler.cc      # 事件处理
    216 -│   │   │       ├── timed_event_scheduler.cc # 定时事件
    217 -│   │   │       └── global_event_manager.cc  # 全局事件
    218 -│   │   │
    219 -│   │   ├── network/                #   网络层
    220 -│   │   │   ├── network_manager.cc/h      # 网络管理器
    221 -│   │   │   ├── tcp_server.cc/h           # TCP 服务端
    222 -│   │   │   ├── tcp_connection.cc/h       # TCP 连接
    223 -│   │   │   ├── tcp_session.cc/h          # TCP 会话
    224 -│   │   │   ├── tcp_client.cc/h           # TCP 客户端 (Logic 连接)
    225 -│   │   │   ├── kcp_server.cc/h           # KCP 服务端
    226 -│   │   │   ├── kcp_session.cc/h          # KCP 会话
    227 -│   │   │   ├── dual_channel_manager.cc/h # 双通道管理
    228 -│   │   │   ├── message_dispatcher.cc/h   # 消息分发
    229 -│   │   │   ├── packet_codec.cc/h         # 封包编解码
    230 -│   │   │   ├── ip_rate_limiter.cc/h      # IP 频率限制
    231 -│   │   │   └── conv_blacklist.cc/h       # KCP conv 黑名单
    232 -│   │   │
    233 -│   │   ├── storage_engine/         #   统一存储引擎
    234 -│   │   │   ├── storage_engine.cc/h       # 引擎主体
    235 -│   │   │   ├── types.h                   # 存储类型
    236 -│   │   │   ├── interfaces/               # 后端接口
    237 -│   │   │   │   └── storage_backend.h
    238 -│   │   │   ├── l1/                       # L1 内存缓存
    239 -│   │   │   │   └── memory_cache.cc/h
    240 -│   │   │   ├── l2/                       # L2 RocksDB 缓存
    241 -│   │   │   │   └── rocksdb_cache.cc/h
    242 -│   │   │   ├── persistence/              # 异步持久化
    243 -│   │   │   │   ├── async_persistence_queue.cc/h
    244 -│   │   │   │   └── blocking_queue.h
    245 -│   │   │   └── utils/                    # 工具
    246 -│   │   │       ├── circuit_breaker.cc/h  # 断路器
    247 -│   │   │       └── global_hybrid_clock.cc/h # 混合时钟
    248 -│   │   │
    249 -│   │   ├── db/                     #   数据库 (可选, 需要 libpqxx + hiredis)
    250 -│   │   │   ├── postgres_database.cc/h    # PostgreSQL 连接
    251 -│   │   │   ├── pg_connection_pool.cc/h   # 连接池
    252 -│   │   │   ├── character_repository.cc/h # 角色数据仓库
    253 -│   │   │   ├── account_storage.cc/h      # 账号存储
    254 -│   │   │   ├── account_storage_backend.cc/h # 账号后端
    255 -│   │   │   ├── storage_engine_backend.cc/h  # 存储引擎后端
    256 -│   │   │   ├── redis_cache.cc/h          # Redis 缓存
    257 -│   │   │   └── redis_manager.cc/h        # Redis 管理
    258 -│   │   │
    259 -│   │   ├── world/                  #   角色存储
    260 -│   │   │   ├── role_store.cc/h           # 角色数据存储
    261 -│   │   │   └── role_record.h             # 角色记录
    262 -│   │   │
    263 -│   │   ├── combat/                 #   战斗核心
    264 -│   │   │   └── combat_core.cpp/h         # 战斗算法
    265 -│   │   │
    266 -│   │   ├── config/                 #   配置加载
    267 -│   │   │   ├── config_manager.cc/h       # 配置管理器
    268 -│   │   │   ├── map_config_loader.cc      # 地图配置
    269 -│   │   │   └── skill_config_loader.cc    # 技能配置
    270 -│   │   │
    271 -│   │   ├── core/                   #   核心工具
    272 -│   │   │   ├── application.cc/h          # 应用生命周期
    273 -│   │   │   ├── timer.cc/h                # 定时器
    274 -│   │   │   ├── utils.cc/h                # 工具函数
    275 -│   │   │   ├── singleton.h               # 单例模板
    276 -│   │   │   ├── non_copyable.h            # 不可拷贝基类
    277 -│   │   │   └── concurrency/              # 并发工具
    278 -│   │   │       ├── mpsc_ring.h           #   MPSC 无锁环形队列
    279 -│   │   │       └── spsc_ring.h           #   SPSC 无锁环形队列
    280 -│   │   │
    281 -│   │   ├── data/                   #   数据模板
    282 -│   │   │   └── item_template.cc/h        # 物品模板
    283 -│   │   │
    284 -│   │   ├── handlers/               #   共享处理器 (Gateway/Logic 共用)
    285 -│   │   │   ├── client_registry.cc/h      # 客户端注册表
    286 -│   │   │   ├── merchant_handler.cc/h     # 商人处理
    287 -│   │   │   ├── movement/                 # 移动
    288 -│   │   │   │   ├── movement_validator.cc/h # 移动验证
    289 -│   │   │   │   └── entity_broadcast_service.cc/h # 实体广播
    290 -│   │   │   └── network/                  # 网络
    291 -│   │   │       └── kcp_upgrade_handler.cc/h # KCP 升级处理
    292 -│   │   │
    293 -│   │   ├── legacy/                 #   旧版兼容 (逐步迁移中)
    294 -│   │   │   ├── character_factory.cc/h
    295 -│   │   │   ├── inventory_system.cpp/h
    296 -│   │   │   ├── monster_ai.cpp/h
    297 -│   │   │   ├── skill_system.cpp/h
    298 -│   │   │   └── legacy_monster_adapter.cc/h
    299 -│   │   │
    300 -│   │   ├── log/                    #   日志
    301 -│   │   │   └── logger.cc/h
    302 -│   │   │
    303 -│   │   ├── monitor/                #   指标 (可选)
    304 -│   │   │   ├── metrics.cc/h              # Prometheus 指标
    305 -│   │   │   └── metrics_stub.cc           # 无 Prometheus 时的桩
    306 -│   │   │
    307 -│   │   └── security/               #   安全
    308 -│   │       ├── anti_cheat.cc/h           # 反作弊
    309 -│   │       └── rate_limiter.cc/h         # 频率限制
    310 -│   │
    311 -│   └── client/                     # 客户端 (SDL2, 可选)
    312 -│       ├── main.cc                 #   入口点
    313 -│       ├── game/                   #   游戏逻辑
    314 -│       │   ├── game_client.cc/h          # 客户端主循环
    315 -│       │   ├── entity_manager.cc/h       # 实体管理
    316 -│       │   ├── monster/                  # 怪物管理
    317 -│       │   └── skill/                    # 技能 (executor, manager)
    318 -│       ├── network/                #   网络
    319 -│       │   ├── network_client.cc/h       # TCP 客户端
    320 -│       │   ├── network_manager.cpp/h     # 网络管理
    321 -│       │   ├── dual_channel_client.cc/h  # 双通道客户端
    322 -│       │   ├── kcp_channel.cc/h          # KCP 通道
    323 -│       │   ├── kcp_upgrade_handler.cc/h  # KCP 升级
    324 -│       │   └── udp_transport.cc/h        # UDP 传输
    325 -│       ├── render/                 #   渲染
    326 -│       │   ├── renderer.cc/h             # 渲染器
    327 -│       │   ├── actor_renderer.cc/h       # 角色渲染
    328 -│       │   ├── effect_player.cc/h        # 特效播放
    329 -│       │   ├── sprite_batch.cc/h         # 精灵批次
    330 -│       │   ├── camera.h                  # 相机
    331 -│       │   ├── i_renderer.h              # 渲染器接口
    332 -│       │   └── i_texture_cache.h         # 纹理缓存接口
    333 -│       ├── scene/                  #   场景
    334 -│       │   ├── scene_manager.cc          # 场景管理
    335 -│       │   └── login_scene.cc/h          # 登录场景
    336 -│       ├── ui/                     #   UI
    337 -│       │   ├── login_screen.cc/h         # 登录界面
    338 -│       │   ├── skill/                    # 技能 UI (skill_bar, skill_book)
    339 -│       │   ├── states/                   # UI 状态机 (login, character_select, ...)
    340 -│       │   ├── input_validation.cc/h     # 输入验证
    341 -│       │   ├── ui_manager.h              # UI 管理
    342 -│       │   ├── ui_renderer.h             # UI 渲染
    343 -│       │   └── ui_layout_constants.h     # 布局常量
    344 -│       └── resource/               #   资源
    345 -│           └── resource_loader.cpp/h     # 资源加载
    346 -│
    347 -├── schemas/                        # FlatBuffers 协议
    348 -│   ├── common.fbs                  #   通用类型 (坐标、方向、基础消息)
    349 -│   ├── login.fbs                   #   登录协议
    350 -│   ├── game.fbs                    #   游戏消息 (移动、NPC、地图)
    351 -│   ├── combat.fbs                  #   战斗消息
    352 -│   ├── item.fbs                    #   物品消息
    353 -│   ├── guild.fbs                   #   公会消息
    354 -│   ├── chat.fbs                    #   聊天消息
    355 -│   ├── system.fbs                  #   系统消息 (心跳、公告)
    356 -│   ├── internal.fbs                #   内部服务间消息 (Gateway <-> Logic)
    357 -│   └── persistence.fbs             #   持久化消息
    358 -│
    359 -├── config/                         # 服务端配置
    360 -│   ├── gateway.yaml                #   网关配置 (端口、连接数、心跳)
    361 -│   ├── logic.yaml                  #   逻辑服务配置
    362 -│   ├── server.yaml                 #   通用服务配置
    363 -│   ├── game.yaml                   #   游戏参数
    364 -│   ├── world.yaml                  #   世界配置
    365 -│   ├── combat_config.yaml          #   战斗参数
    366 -│   ├── gates.yaml                  #   传送门配置
    367 -│   ├── global_events.yaml          #   全局事件配置
    368 -│   ├── timed_events.yaml           #   定时事件配置
    369 -│   ├── tables/                     #   数据表 (地图属性等)
    370 -│   ├── npc_scripts/                #   NPC 脚本
    371 -│   └── prometheus/                 #   Prometheus 配置
    372 -│
    373 -├── tests/                          # 测试
    374 -│   ├── server/                     #   服务端测试
    375 -│   │   ├── ecs/                    #     ECS 测试
    376 -│   │   ├── gateway/                #     网关测试
    377 -│   │   ├── logic/                  #     逻辑服务测试
    378 -│   │   ├── network/                #     网络测试
    379 -│   │   ├── guild/                  #     公会测试
    380 -│   │   ├── game/                   #     游戏逻辑测试
    381 -│   │   ├── storage_engine/         #     存储引擎测试
    382 -│   │   └── mocks/                  #     Mock 对象
    383 -│   ├── client/                     #   客户端测试
    384 -│   ├── common/                     #   共享代码测试
    385 -│   └── integration/                #   集成测试
    386 -│
    387 -├── migrations/                     # 数据库迁移脚本
    388 -└── docs/                           # 文档
    389 -```
    390 -
    391 ----
    392 -
    393 -## 快速开始
    394 -
    395 -### 环境要求
    396 -
    397 -- **编译器**: GCC 13.3+ 或 Clang 16+ (C++20)
    398 -- **CMake**: 3.25+
    399 -- **vcpkg**: 最新版本
    400 -- **操作系统**: Linux (WSL2), macOS, Windows
    401 -
    402 -### 构建
    403 -
    404 -```bash
    405 -# WSL 环境（推荐）
    406 -cmake --preset vcpkg-wsl-debug
    407 -cmake --build --preset vcpkg-wsl-debug -j$(nproc)
    408 -
    409 -# Linux 环境
    410 -cmake --preset vcpkg-linux-debug
    411 -cmake --build --preset vcpkg-linux-debug -j$(nproc)
    412 -
    413 -# Windows 环境
    414 -cmake --preset vcpkg-debug
    415 -cmake --build --preset vcpkg-debug
    416 -```
    417 -
    418 -### 运行服务
    419 -
    420 -```bash
    421 -# 启动网关
    422 -./build-wsl/bin/mir2_gateway
    423 -
    424 -# 启动逻辑服务器
    425 -./build-wsl/bin/mir2_logic
    426 -
    427 -# 指定配置文件
    428 -./build-wsl/bin/mir2_gateway --config config/gateway.yaml
    429 -./build-wsl/bin/mir2_logic --config config/logic.yaml
    430 -```
    431 -
    432 -### 运行测试
    433 -
    434 -```bash
    435 -cmake --build --preset vcpkg-wsl-debug --target legend2_tests -j$(nproc)
    436 -ctest --test-dir build-wsl --output-on-failure
    437 -```
    438 -
    439 ----
    440 -
    441 -## 代码规范
    442 -
    443 -遵循 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)。
    444 -
    445 -### 命名约定
    446 -
    447 -| 类型 | 风格 | 示例 |
    448 -|------|------|------|
    449 -| 文件名 | snake_case | `map_instance.h`, `gate_manager.cc` |
    450 -| 类/结构体 | PascalCase | `MapInstance`, `GateInfo` |
    451 -| 函数/方法 | PascalCase | `LoadMap()`, `CheckGateTrigger()` |
    452 -| 变量 | snake_case | `map_width`, `door_index` |
    453 -| 常量 | kPascalCase | `kDefaultGridSize`, `kMaxTileCount` |
    454 -| 枚举值 | kPascalCase | `AreaEffectType::kFire` |
    455 -| 命名空间 | snake_case | `mir2::game::map` |
    456 -| 私有成员 | trailing `_` | `map_id_`, `tile_data_` |
    457 -| 宏定义 | ALL_CAPS | `MIR2_GAME_MAP_INSTANCE_H_` |
    458 -
    459 -### 关键规范
    460 -
    461 -- 缩进 2 空格，访问修饰符前留 1 空格
    462 -- 头文件使用 `#ifndef` 防护 (Google 风格)
    463 -- 优先 `const` 引用传参、`std::optional` 返回、`std::unique_ptr` 管理所有权
    464 -- 禁止裸指针管理内存
    465 -- RAII 管理锁 (`std::lock_guard`)
    466 -- 使用初始化列表而非构造函数体赋值
    467 -
    468 -### Git Commit 规范
    469 -
    470 -```
    471 -<type>(<scope>): <subject>
    472 -
    473 -<body>
    474 -
    475 -Co-Authored-By: <author>
    476 -```
    477 -
    478 -类型: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `chore`
    479 -
    480 -范围示例: `gateway`, `logic`, `ecs`, `combat`, `network`, `map`, `guild`, `kcp`, `storage`
    481 -
    482 ----
    483 -
    484 -## 测试
    485 -
    486 -### 框架
    487 -
    488 -- **GoogleTest**: 单元测试
    489 -- **RapidCheck**: 属性测试
    490 -- **GoogleMock**: Mock 对象
    491 -
    492 -### 运行
    493 -
    494 -```bash
    495 -# 全部测试
    496 -ctest --test-dir build-wsl --output-on-failure
    497 -
    498 -# 特定模块
    499 -ctest --test-dir build-wsl -R "gateway_|combat_|ecs_|guild_"
    500 -
    501 -# 详细输出
    502 -./build-wsl/bin/legend2_tests --gtest_filter=GateManagerTest.*
    503 -```
    504 -
    505 ----
    506 -
    507 -## 核心模块说明
    508 -
    509 -### Gateway 服务器 (`src/server/gateway/`)
    510 -
    511 -- `GatewayServer`: 主服务，管理客户端连接生命周期
    512 -- `MessageRouter`: 消息路由，将客户端消息转发给 Logic
    513 -- `ConnectionHolder`: 连接保持，Logic 断线时缓冲消息
    514 -- 支持 TCP + KCP 双通道，通过 `DualChannelManager` 管理
    515 -
    516 -### Logic 服务器 (`src/server/logic/`)
    517 -
    518 -- `LogicServer`: 主循环 (Tick)、协程调度、Handler 注册
    519 -- `CoroutineExecutor`: C++20 协程执行器
    520 -- `HandlerRegistry`: 消息 ID 到 Handler 的映射
    521 -- `HotEventPipeline`: MPSC 无锁事件管线，IO 线程 -> Logic 线程
    522 -- 每个玩家有独立的 `PlayerMailbox`，保证消息顺序处理
    523 -
    524 -### ECS 系统 (`src/server/ecs/`)
    525 -
    526 -- `RegistryManager`: 全局管理，按 map_id 管理多个 World
    527 -- `World`: 单个地图的 ECS 容器
    528 -- 每个 World 有独立的 `WorldSystemBundle`，包含该地图所有 System 实例
    529 -- ECS Registry 非线程安全，所有操作在 Logic 单线程内执行
    530 -
    531 -### 存储引擎 (`src/server/storage_engine/`)
    532 -
    533 -- L1: 内存缓存 (`MemoryCache`) — 热数据
    534 -- L2: RocksDB 缓存 (`RocksDBCache`) — 温数据
    535 -- L3: PostgreSQL (通过 `db/` 模块) — 冷数据持久化
    536 -- `AsyncPersistenceQueue`: 异步写入队列
    537 -- `CircuitBreaker`: 后端故障熔断
    538 -- `GlobalHybridClock`: 混合逻辑时钟
    539 -
    540 -### 网络层 (`src/server/network/`)
    541 -
    542 -- TCP: 主连接通道，可靠有序
    543 -- KCP: 可选加速通道 (UDP)，低延迟
    544 -- `DualChannelManager`: 管理 TCP/KCP 双通道
    545 -- `IPRateLimiter`: IP 级别频率限制
    546 -- `ConvBlacklist`: KCP conv ID 黑名单