# MIR2-CPP 测试优先级矩阵

## 测试优先级评分系统

### 评分维度
1. **关键性 (Criticality)**: 对系统稳定性的影响 (0-10分)
2. **复杂度 (Complexity)**: 代码复杂度 (0-10分)
3. **变更频率 (Change Frequency)**: 代码变更频率 (0-10分)
4. **依赖度 (Dependency)**: 被其他模块依赖的程度 (0-10分)
5. **风险 (Risk)**: 缺陷可能造成的风险 (0-10分)

### 优先级计算
```
Priority Score = (Criticality × 3) + (Complexity × 2) + (Change Frequency × 2) +
                 (Dependency × 2) + (Risk × 3)
```

---

## P0 - 关键路径 (Critical Path) [Score: 35-50]

### 🔴 Ultra-Critical (45-50分)

| 组件 | 关键性 | 复杂度 | 变更频率 | 依赖度 | 风险 | 总分 | 测试状态 |
|------|--------|--------|----------|--------|------|------|----------|
| logic_server.cc | 10 | 9 | 7 | 10 | 10 | 49 | ✅ logic_server_test.cc |
| damage_calculator.cc | 10 | 8 | 8 | 9 | 10 | 48 | ✅ damage_calculator_test.cc |
| attack_handler.cc | 10 | 8 | 8 | 9 | 10 | 48 | ✅ attack_handler_test.cc |
| crash_handler.cc | 10 | 7 | 5 | 8 | 10 | 46 | ❌ 无测试 (Breakpad依赖) |
| spatial_query.cc (AOI) | 9 | 9 | 7 | 10 | 9 | 47 | ✅ spatial_query_test.cc |

**测试需求**:
- ✅ 单元测试 (必须)
- ✅ 集成测试 (必须)
- ✅ 压力测试 (必须)
- ✅ 边界条件测试 (必须)
- ✅ 并发安全测试 (必须)

### 🔴 Critical (40-44分)

| 组件 | 总分 | 测试状态 | 关键问题 |
|------|------|----------|----------|
| skill_handler.cc | 44 | ✅ skill_system_test.cc | 技能系统核心 |
| equipment_bonus_system.cc | 43 | ✅ equipment_bonus_system_test.cc | 装备属性计算 |
| effect_broadcaster.cc | 42 | ✅ effect_broadcaster_test.cc | 效果同步核心 |
| aoi_manager.cc | 44 | ✅ aoi_manager_test.cc | 视野管理核心 |
| ecs_combat_service.cc | 43 | ✅ combat_system_test.cpp | 战斗服务 |
| response_sender.cc | 41 | ⚠️ mock存在，无独立测试 | 消息发送核心 |

### 🟠 High-Critical (35-39分)

| 组件 | 总分 | 测试状态 | 关键问题 |
|------|------|----------|----------|
| entity_lane_scheduler.cc | 39 | ✅ entity_lane_scheduler_test.cc | 实体调度 |
| storage_engine_backend.cc | 38 | ✅ storage_engine_test.cc | 存储后端 |
| pg_connection_pool.cc | 38 | ✅ postgres_database_test.cpp | 数据库连接池 |
| client_registry.cc | 37 | ✅ client_registry_test.cc | 客户端注册 |
| ecs_inventory_service.cc | 37 | ✅ inventory_system_test.cpp | 背包服务 |
| passive_skill_system.cc | 36 | ✅ passive_skill_system_test.cc | 被动技能 |
| recovery_system.cc | 35 | ⚠️ 可能不完整 | 回复系统 |

---

## P1 - 核心功能 (Core Features) [Score: 25-34]

### 🟡 High Priority (30-34分)

| 组件 | 总分 | 测试状态 | 模块 |
|------|------|----------|------|
| game_client.cc | 34 | ❌ 无测试 | 客户端核心 |
| map_renderer.cc | 33 | ❌ 无测试 | 地图渲染 |
| path_finder.cpp | 33 | ✅ pathfinding_helper_test.cpp | 寻路算法 |
| lua_script_engine.cc | 32 | ✅ lua_bindings_test.cpp (需Lua) | NPC脚本 |
| trade_system.cc | 32 | ❌ 无测试 | 交易系统 |
| storage_system.cc | 31 | ❌ 无测试 | 仓库系统 |
| summon_system.cc | 31 | ❌ 无测试 | 召唤系统 |
| luck_system.cc | 30 | ❌ 无测试 | 幸运系统 |
| chunk_manager.cc | 30 | ❌ 无测试 | 区块管理 |

### 🟡 Medium-High Priority (25-29分)

| 组件 | 总分 | 测试状态 | 模块 |
|------|------|----------|------|
| guild_manager.cc | 29 | ✅ guild_handler/system_test.cc | 行会管理 |
| chat_service.cc | 28 | ✅ chat_handler_test.cc | 聊天服务 |
| merchant_service.cc | 28 | ✅ npc_shop_service_test.cc | 商人服务 |
| npc_interaction_handler.cc | 27 | ✅ npc_interaction_test.cpp | NPC交互 |
| entity_manager.cc (client) | 27 | ✅ entity_manager_view_test.cc | 实体管理 |
| actor_renderer.cc | 26 | ❌ 无测试 | 角色渲染 |
| renderer.cc | 26 | ❌ 无测试 | 主渲染器 |
| player_presence_service.cc | 25 | ❌ 无测试 | 在线状态 |
| door_manager.cc | 25 | ❌ 无测试 | 门管理 |

---

## P2 - 支撑功能 (Supporting Features) [Score: 15-24]

### 🟢 Medium Priority (20-24分)

| 组件 | 总分 | 测试状态 | 模块 |
|------|------|----------|------|
| application.cc (client) | 24 | ❌ 无测试 | 客户端应用 |
| application.cc (server) | 24 | ❌ 无测试 | 服务器应用 |
| prewarm_manager.cc | 23 | ❌ 无测试 | 预热管理 |
| effect_player.cc | 22 | ❌ 无测试 | 特效播放 |
| network_client.cc | 22 | ❌ 无测试 | 网络客户端 |
| udp_transport.cc | 22 | ❌ 无测试 | UDP传输 |
| login_screen.cc | 21 | ❌ 无测试 | 登录界面 |
| ui_renderer.cc | 21 | ❌ 无测试 | UI渲染 |
| npc_dialog_ui.cpp | 20 | ❌ 无测试 | NPC对话UI |

### 🟢 Low-Medium Priority (15-19分)

| 组件 | 总分 | 测试状态 | 模块 |
|------|------|----------|------|
| config_manager.cc | 19 | ✅ config_manager_storage_engine_test.cc | 配置管理 |
| skill_config_loader.cc | 19 | ❌ 无测试 | 技能配置 |
| crypto_utils.cc | 18 | ❌ 无测试 | 加密工具 |
| memory_cache.cc (L1) | 18 | ✅ local_lru_cache_test.cc | 内存缓存 |
| account_storage_codec.cc | 18 | ✅ account_storage_backend_test.cpp | 账号编码 |
| event_dispatcher.cc | 17 | ❌ 无测试 | 事件分发 |
| timer.cc (client) | 16 | ❌ 无测试 | 定时器 |
| timer.cc (server) | 16 | ❌ 无测试 | 定时器 |
| utils.cc | 15 | ❌ 无测试 | 工具函数 |

---

## P3 - 辅助功能 (Auxiliary Features) [Score: 0-14]

### ⚪ Low Priority (10-14分)

| 组件 | 总分 | 测试状态 | 模块 |
|------|------|----------|------|
| logger.cc | 14 | ❌ 无测试 | 日志 |
| metrics.cc | 14 | ❌ 无测试 | 监控指标 |
| anti_cheat.cc | 13 | ❌ 无测试 | 反作弊 |
| tcp_client.cc | 12 | ❌ 无测试 | TCP客户端 |
| tcp_server.cc | 12 | ❌ 无测试 | TCP服务器 |
| path_utils.cc | 11 | ❌ 无测试 | 路径工具 |
| internal_message_helper.cc | 11 | ❌ 无测试 | 消息辅助 |
| audio_engine.cc | 10 | ❌ 无测试 | 音频引擎 |

### ⚪ Minimal Priority (0-9分)

| 组件 | 总分 | 测试状态 | 模块 |
|------|------|----------|------|
| login_scene.cc | 9 | ❌ 无测试 | 登录场景 |
| character_data.cpp | 8 | ❌ 无测试 | 角色数据 |
| types.cpp | 7 | ❌ 无测试 | 类型定义 |
| utf8_utils.cc | 7 | ❌ 无测试 | UTF8工具 |
| metrics_stub.cc | 5 | ❌ 无测试 | 监控桩 |

---

## 测试实施优先级路线图

### Week 1-2: Ultra-Critical (P0 Top 5)
```
Day 1-2:   logic_server_test.cc
Day 3-4:   damage_calculator_test.cc + attack_handler_test.cc
Day 5-6:   spatial_query_test.cc (AOI)
Day 7-8:   crash_handler_test.cc
Day 9-10:  集成测试 + 代码审查
```

### Week 3-4: Critical (P0 Remaining)
```
Day 11-12: skill_handler_test.cc
Day 13-14: equipment_bonus_system_test.cc + effect_broadcaster_test.cc
Day 15-16: aoi_manager_test.cc
Day 17-18: ecs_combat_service_test.cc + response_sender_test.cc
Day 19-20: 集成测试
```

### Week 5-6: High-Critical (P0 Final)
```
Day 21-22: entity_lane_scheduler_test.cc
Day 23-24: storage_engine_backend_test.cc + pg_connection_pool_test.cc
Day 25-26: client_registry_test.cc + ecs_inventory_service_test.cc
Day 27-28: passive_skill_system_test.cc + recovery_system_test.cc
Day 29-30: 全面集成测试 + 回归测试
```

### Week 7-10: Core Features (P1)
按优先级顺序补充 P1 测试 (30-40个文件)

### Week 11-14: Supporting & Auxiliary (P2-P3)
补充剩余测试，重点关注 P2，P3 可选

---

## 测试类型建议

### Ultra-Critical 组件必须测试
1. ✅ **单元测试**: 100% 函数覆盖
2. ✅ **集成测试**: 模块间交互
3. ✅ **边界测试**: 极限值、空值、异常值
4. ✅ **并发测试**: 线程安全、竞态条件
5. ✅ **压力测试**: 高负载场景
6. ✅ **故障注入**: 异常处理验证
7. ✅ **回归测试**: 每次修改后验证

### Critical 组件必须测试
1. ✅ 单元测试
2. ✅ 集成测试
3. ✅ 边界测试
4. ✅ 并发测试
5. ⚠️ 压力测试 (可选)

### High/Medium 组件必须测试
1. ✅ 单元测试
2. ✅ 集成测试 (关键路径)
3. ⚠️ 边界测试 (可选)

### Low Priority 组件
1. ✅ 基础单元测试
2. ⚠️ 其他测试 (可选)

---

## 风险矩阵

```
高风险 │ ■ logic_server      ■ damage_calc     ■ attack_handler │
      │ ■ spatial_query     ■ crash_handler   ■ skill_handler  │
      │ ■ aoi_manager       ■ equipment_bonus ■ ecs_combat     │
───────┼──────────────────────────────────────────────────────────
中风险 │ ▲ game_client       ▲ map_renderer    ▲ path_finder    │
      │ ▲ lua_script        ▲ trade_system    ▲ storage_system │
      │ ▲ guild_manager     ▲ chat_service    ▲ merchant       │
───────┼──────────────────────────────────────────────────────────
低风险 │ ○ config_manager    ○ crypto_utils    ○ logger         │
      │ ○ timer             ○ utils            ○ path_utils     │
      │ ○ audio_engine      ○ metrics          ○ tcp_client     │
───────┴──────────────────────────────────────────────────────────
        低复杂度            中复杂度            高复杂度
```

---

## 成功指标

### 短期目标 (1个月)
- [ ] 所有 Ultra-Critical (45+分) 组件测试完成
- [ ] 所有 Critical (40+分) 组件测试完成
- [ ] P0 整体覆盖率 > 90%

### 中期目标 (3个月)
- [ ] 所有 P0 组件测试完成
- [ ] P1 组件测试完成 80%+
- [ ] 整体覆盖率 > 75%

### 长期目标 (6个月)
- [ ] 所有 P0-P1 组件测试完成
- [ ] P2 组件测试完成 70%+
- [ ] 整体覆盖率 > 85%

---

**优先级矩阵更新日期**: 2026-02-15
**下次审查日期**: 2026-03-01 (每2周审查一次)
