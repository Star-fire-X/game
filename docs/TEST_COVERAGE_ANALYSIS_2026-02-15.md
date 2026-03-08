# MIR2-CPP 测试覆盖率全量分析报告

**生成日期**: 2026-02-15
**分析范围**: 完整代码库 (client/server/common)
**总体覆盖率**: 55.2% (111/201 实现文件有测试)

---

## 执行摘要 (Executive Summary)

### 关键发现
- **总源文件**: 533 个 (不含第三方库)
- **实现文件**: 218 个 (.cc/.cpp文件，排除测试文件)
- **测试文件**: 201 个
- **已测试文件**: 111 个 (55.2%)
- **缺失测试**: 90 个文件 (44.8%)

### 测试覆盖率分级
| 等级 | 覆盖率 | 模块数 | 状态 |
|------|--------|--------|------|
| 优秀 | 80-100% | 3 | ✅ client/handlers (81.8%), server/network (84.6%), server/ecs (部分) |
| 良好 | 60-79% | 4 | ⚠️ server/ecs (65.8%), server/game (69.0%), server/storage_engine (63.6%) |
| 需改进 | 40-59% | 4 | ⚠️ client/ui (52.2%), client/network (71.4%), server/logic (51.7%) |
| 严重不足 | 0-39% | 12 | ❌ 多个核心模块 (详见下文) |

---

## 第一部分：按优先级分类的缺失测试

### P0 - 关键路径缺失测试 (Critical)

#### 1. 服务器核心逻辑 (server/logic)
**覆盖率**: 51.7% (15/29) | **风险**: 🔴 极高

缺失测试的关键文件：
```
- logic_server.cc              # 主服务器入口，无测试！
- crash_handler.cc             # 崩溃处理，无测试验证
- entity_lane_scheduler.cc     # 实体调度器
- response_sender.cc           # 响应发送器
```

**攻击处理器 (Handlers)**:
```
- attack_handler.cc            # 攻击处理无测试
- skill_handler.cc             # 技能处理无测试
- bonus_point_handler.cc       # 属性点分配无测试
- effect_broadcast_service.cc  # 效果广播无测试
```

**核心服务 (Services)**:
```
- client_registry.cc           # 客户端注册表
- ecs_combat_service.cc        # ECS战斗服务
- ecs_inventory_service.cc     # ECS背包服务
- merchant_service.cc          # 商人服务
- player_presence_service.cc   # 玩家在线状态服务
- prewarm_manager.cc           # 预热管理器
```

**影响**: 战斗系统、物品系统、玩家管理核心逻辑未验证

#### 2. 网关服务器核心 (server/gateway)
**覆盖率**: 测试存在但不完整 | **风险**: 🔴 高

虽然有基础测试，但 `gateway_server.cc` 的复杂流程缺乏深度测试。

#### 3. ECS 核心系统 (server/ecs/systems)
**覆盖率**: 65.8% (25/38) | **风险**: 🟡 中高

缺失关键系统测试：
```
- damage_calculator.cc         # 伤害计算核心
- effect_broadcaster.cc        # 效果广播
- equipment_bonus_system.cc    # 装备加成系统
- passive_skill_system.cc      # 被动技能系统
- recovery_system.cc           # 回复系统 (有测试但可能不完整)
- spatial_query.cc             # 空间查询 (AOI核心)
- storage_system.cc            # 仓库系统
- trade_system.cc              # 交易系统
- summon_system.cc             # 召唤系统
- luck_system.cc               # 幸运系统
```

**影响**: 战斗公式、装备属性、空间查询等核心机制

#### 4. 存储引擎 (server/storage_engine)
**覆盖率**: 63.6% (7/11) | **风险**: 🟡 中

缺失测试：
```
- backends/storage_engine_backend.cc
- backends/postgres/pg_connection_pool.cc
- backends/common/account_storage_codec.cc
- l1/memory_cache.cc
```

**影响**: 数据持久化、账号存储、连接池可靠性

### P1 - 游戏功能缺失测试 (High Priority)

#### 5. 地图与空间管理 (server/game/map)
**覆盖率**: 约 60% | **风险**: 🟡 中

缺失测试：
```
- aoi_manager.cc               # AOI管理器 (视野管理核心)
- chunk_manager.cc             # 区块管理
- door_manager.cc              # 门管理器
```

#### 6. NPC 系统 (server/game/npc)
**覆盖率**: 约 70% | **风险**: 🟡 中

缺失测试：
```
- lua_script_engine.cc         # Lua脚本引擎
- npc_interaction_handler.cc   # NPC交互处理
```

#### 7. 聊天与社交 (server/game/chat, guild)
**覆盖率**: 低 | **风险**: 🟡 中

缺失测试：
```
- chat_service.cc              # 聊天服务
- guild_manager.cc             # 行会管理器
```

### P2 - 客户端核心缺失测试 (Medium Priority)

#### 8. 客户端核心 (client/core)
**覆盖率**: 0% (0/4) | **风险**: 🟡 中

```
- application.cc               # 客户端应用主循环
- event_dispatcher.cc          # 事件分发器
- path_utils.cc                # 路径工具
- timer.cc                     # 定时器
```

#### 9. 客户端游戏逻辑 (client/game)
**覆盖率**: 25% (3/12) | **风险**: 🟡 中

缺失测试：
```
- game_client.cc               # 游戏客户端主类
- game_client_input.cc         # 输入处理
- entity_manager.cc            # 实体管理器
- actor_data.cc                # 角色数据
```

**地图子系统**:
```
- map/map_renderer.cc          # 地图渲染
- map/map_system.cpp           # 地图系统
- map/path_finder.cpp          # 寻路算法 (A*)
- map/portal_manager.cpp       # 传送门管理
```

#### 10. 客户端渲染 (client/render)
**覆盖率**: 20% (1/5) | **风险**: 🟡 中

缺失测试：
```
- renderer.cc                  # 主渲染器
- actor_renderer.cc            # 角色渲染
- effect_player.cc             # 特效播放
- ground_item_renderer.cc      # 地面物品渲染
```

#### 11. 客户端 UI (client/ui)
**覆盖率**: 52.2% (12/23) | **风险**: 🟡 中

缺失测试：
```
- login_screen.cc              # 登录界面
- ui_renderer.cc               # UI渲染器
- npc_dialog_ui.cpp            # NPC对话界面
```

**登录状态机 (states/)**:
```
- character_create_state.cc    # 角色创建状态
- character_select_state.cc    # 角色选择状态
- connecting_state.cc          # 连接中状态
- error_state.cc               # 错误状态
- login_input_state.cc         # 登录输入状态
- login_state_helpers.cc       # 状态辅助函数
```

**HUD组件**:
```
- hud/player_info_panel.cc     # 玩家信息面板
- hud/status_bar.cc            # 状态栏
```

#### 12. 客户端网络 (client/network)
**覆盖率**: 71.4% (5/7) | **风险**: 🟢 低

缺失测试：
```
- network_client.cc            # 网络客户端
- udp_transport.cc             # UDP传输层
```

### P3 - 支撑模块缺失测试 (Low Priority)

#### 13. 通用模块 (common)
**覆盖率**: 不完整

缺失测试：
```
- character_data.cpp           # 角色数据结构
- types.cpp                    # 类型定义
- utf8_utils.cc                # UTF-8工具
```

#### 14. 服务器工具/支撑模块
**覆盖率**: 0-50%

```
server/common:
- crypto_utils.cc              # 加密工具
- internal_message_helper.cc   # 内部消息辅助

server/core:
- application.cc               # 应用框架
- timer.cc                     # 定时器
- utils.cc                     # 通用工具

server/config:
- config_manager.cc            # 配置管理器
- skill_config_loader.cc       # 技能配置加载

server/monitor:
- metrics.cc                   # 监控指标
- metrics_stub.cc              # 监控桩

server/log:
- logger.cc                    # 日志器

server/security:
- anti_cheat.cc                # 反作弊
```

#### 15. 客户端支撑模块
**覆盖率**: 0-50%

```
client/audio:
- audio_engine.cc              # 音频引擎

client/scene:
- login_scene.cc               # 登录场景

client/handlers:
- effect_handler.cc            # 效果处理器
- skill_handler.cc             # 技能处理器
```

---

## 第二部分：测试覆盖率热力图

### 模块覆盖率排名

| 排名 | 模块 | 覆盖率 | 已测试/总计 | 状态 |
|------|------|--------|-------------|------|
| 1 | server/network | 84.6% | 11/13 | ✅ 优秀 |
| 2 | client/handlers | 81.8% | 9/11 | ✅ 优秀 |
| 3 | client/network | 71.4% | 5/7 | ✅ 良好 |
| 4 | server/game | 69.0% | 20/29 | ⚠️ 良好 |
| 5 | server/ecs | 65.8% | 25/38 | ⚠️ 良好 |
| 6 | server/storage_engine | 63.6% | 7/11 | ⚠️ 良好 |
| 7 | client/ui | 52.2% | 12/23 | ⚠️ 需改进 |
| 8 | server/logic | 51.7% | 15/29 | ⚠️ 需改进 |
| 9 | client/scene | 50.0% | 1/2 | ⚠️ 需改进 |
| 10 | server/security | 50.0% | 1/2 | ⚠️ 需改进 |
| 11 | server/config | 33.3% | 1/3 | ❌ 严重不足 |
| 12 | client/game | 25.0% | 3/12 | ❌ 严重不足 |
| 13 | client/render | 20.0% | 1/5 | ❌ 严重不足 |
| 14 | client/core | 0.0% | 0/4 | ❌ 无测试 |
| 15 | client/audio | 0.0% | 0/1 | ❌ 无测试 |
| 16 | server/core | 0.0% | 0/3 | ❌ 无测试 |
| 17 | server/monitor | 0.0% | 0/2 | ❌ 无测试 |
| 18 | server/log | 0.0% | 0/1 | ❌ 无测试 |
| 19 | server/common | 0.0% | 0/2 | ❌ 无测试 |
| 20 | common (部分) | 0.0% | 0/3 | ❌ 无测试 |

---

## 第三部分：测试架构策略

### 测试金字塔现状分析

```
        /\
       /E2E\        ← 当前: 18 个集成测试 (偏少)
      /------\
     /  集成   \     ← 当前: 约 30 个
    /----------\
   /    单元     \   ← 当前: 约 153 个 (基础尚可)
  /--------------\
```

**问题**:
1. **单元测试不全**: 44.8% 的实现文件无测试
2. **集成测试薄弱**: 模块间交互测试不足
3. **E2E测试缺乏**: 完整业务流程测试较少

### 推荐测试策略

#### 1. 单元测试策略
**原则**:
- 每个 `.cc` 文件应有对应的 `_test.cc`
- 目标覆盖率: 80%+ (语句覆盖)
- 使用 GoogleTest + GMock

**优先级**:
```
P0: logic_server, crash_handler, damage_calculator, attack_handler
P1: aoi_manager, spatial_query, equipment_bonus_system
P2: game_client, map_renderer, path_finder
P3: 工具类、辅助函数
```

#### 2. 集成测试策略
**需要增加的集成测试**:

```cpp
// 战斗系统集成测试
tests/integration/combat_integration_test.cc
- 完整攻击流程: 客户端发起 → 服务器计算 → 广播结果
- 技能使用流程
- 伤害计算验证

// 物品系统集成测试
tests/integration/item_integration_test.cc
- 物品拾取 → 背包操作 → 装备穿戴 → 属性更新
- 商人交易流程
- 仓库存取流程

// 地图系统集成测试
tests/integration/map_integration_test.cc
- 场景加载 → AOI更新 → 跨地图传送
- 寻路与移动验证

// 社交系统集成测试
tests/integration/social_integration_test.cc
- 聊天消息路由
- 行会操作流程
- 组队功能

// 登录流程集成测试
tests/integration/login_flow_integration_test.cc
- 完整登录流程: 连接 → 认证 → 角色选择 → 进入游戏
```

#### 3. E2E 测试策略
**场景测试**:
```
tests/e2e/complete_gameplay_scenario_test.cc
- 新玩家注册 → 创建角色 → 完成新手任务 → 战斗 → 升级
- 多玩家协同场景
- PvP 场景
- Boss战场景
```

---

## 第四部分：测试实施计划

### Phase 1: 紧急修复 (1-2周)
**目标**: 补充 P0 关键路径测试

| 任务 | 工作量 | 负责模块 |
|------|--------|----------|
| logic_server 核心流程测试 | 3天 | server/logic |
| attack_handler + skill_handler 测试 | 2天 | server/logic/handlers |
| damage_calculator 测试 | 2天 | server/ecs/systems |
| spatial_query (AOI) 测试 | 2天 | server/ecs/systems |
| crash_handler 测试 | 1天 | server/logic |
| storage_engine 后端测试 | 2天 | server/storage_engine |

### Phase 2: 核心功能完善 (2-3周)
**目标**: 补充 P1 游戏功能测试

| 任务 | 工作量 | 负责模块 |
|------|--------|----------|
| ECS 系统补全 (10个系统) | 5天 | server/ecs/systems |
| AOI + 地图系统测试 | 3天 | server/game/map |
| NPC 脚本引擎测试 | 2天 | server/game/npc |
| 客户端游戏逻辑测试 | 4天 | client/game |
| 客户端渲染测试 | 3天 | client/render |

### Phase 3: 客户端与工具 (2周)
**目标**: 补充客户端和工具类测试

| 任务 | 工作量 | 负责模块 |
|------|--------|----------|
| 客户端核心框架测试 | 2天 | client/core |
| UI 状态机测试 | 3天 | client/ui/states |
| 客户端网络测试 | 2天 | client/network |
| 通用工具测试 | 2天 | common, server/common |
| 配置与监控测试 | 2天 | server/config, server/monitor |

### Phase 4: 集成与E2E (1-2周)
**目标**: 构建完整的集成测试体系

| 任务 | 工作量 |
|------|--------|
| 战斗系统集成测试 | 2天 |
| 物品系统集成测试 | 2天 |
| 地图系统集成测试 | 1天 |
| 社交系统集成测试 | 1天 |
| E2E 场景测试 | 3天 |

---

## 第五部分：测试质量提升建议

### 1. 测试基础设施改进

#### 建立测试工具库
```cpp
// tests/utils/test_utils.h
namespace mir2::test {
  // Mock 工厂
  std::unique_ptr<MockEntityManager> CreateMockEntityManager();

  // 测试数据生成器
  CharacterData GenerateTestCharacter(int level = 1);

  // 断言辅助
  void AssertDamageInRange(int actual, int min, int max);
}
```

#### 完善 Mock 基础设施
```cpp
// tests/mocks/
- mock_storage_backend.h       # 存储后端 Mock
- mock_network_manager.h       # 网络管理器 Mock
- mock_ecs_world.h             # ECS World Mock
- mock_map_instance.h          # 地图实例 Mock
```

### 2. 测试覆盖率工具集成

#### 使用 lcov/gcov 生成覆盖率报告
```bash
# CMakeLists.txt 添加
if(ENABLE_COVERAGE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# 生成报告
mkdir build-coverage && cd build-coverage
cmake -DENABLE_COVERAGE=ON ..
make -j8
ctest
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

#### CI/CD 集成
```yaml
# .github/workflows/test-coverage.yml
name: Test Coverage
on: [push, pull_request]
jobs:
  coverage:
    runs-on: ubuntu-latest
    steps:
      - name: Run Tests with Coverage
        run: |
          cmake -DENABLE_COVERAGE=ON -B build
          cmake --build build
          cd build && ctest --output-on-failure
      - name: Upload Coverage
        uses: codecov/codecov-action@v3
        with:
          files: ./build/coverage.info
```

### 3. 测试编写规范

#### 单元测试模板
```cpp
// tests/server/ecs/systems/example_system_test.cc
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "server/ecs/systems/example_system.h"
#include "tests/mocks/mock_ecs_world.h"

namespace mir2::test {

class ExampleSystemTest : public ::testing::Test {
protected:
  void SetUp() override {
    world_ = std::make_unique<MockECSWorld>();
    system_ = std::make_unique<ExampleSystem>(world_.get());
  }

  std::unique_ptr<MockECSWorld> world_;
  std::unique_ptr<ExampleSystem> system_;
};

TEST_F(ExampleSystemTest, BasicFunctionality) {
  // Arrange
  auto entity = world_->CreateEntity();

  // Act
  system_->Process(entity);

  // Assert
  EXPECT_EQ(world_->GetComponent<SomeComponent>(entity).value, 42);
}

TEST_F(ExampleSystemTest, EdgeCaseHandling) {
  // Test edge cases
}

TEST_F(ExampleSystemTest, ErrorHandling) {
  // Test error scenarios
}

} // namespace mir2::test
```

#### 集成测试模板
```cpp
// tests/integration/combat_flow_test.cc
#include <gtest/gtest.h>
#include "tests/integration/test_helpers.h"

namespace mir2::integration {

class CombatFlowTest : public ::testing::Test {
protected:
  void SetUp() override {
    // 启动测试服务器
    server_ = CreateTestServer();
    client_ = CreateTestClient();
    client_->Connect("127.0.0.1", 7000);
    WaitForConnection();
  }

  std::unique_ptr<TestServer> server_;
  std::unique_ptr<TestClient> client_;
};

TEST_F(CombatFlowTest, CompleteAttackFlow) {
  // 1. 创建测试角色
  auto attacker = CreateTestCharacter("Attacker", 10);
  auto target = CreateTestCharacter("Target", 10);

  // 2. 发起攻击
  client_->SendAttackRequest(target.id);

  // 3. 等待并验证结果
  auto response = client_->WaitForResponse<AttackResponse>();
  ASSERT_TRUE(response.success);
  EXPECT_GT(response.damage, 0);

  // 4. 验证服务器状态
  auto target_hp = server_->GetCharacterHP(target.id);
  EXPECT_LT(target_hp, target.max_hp);
}

} // namespace mir2::integration
```

### 4. 测试数据管理

#### 测试数据分离
```
tests/data/
  - test_characters.json     # 测试角色数据
  - test_items.json          # 测试物品数据
  - test_monsters.json       # 测试怪物数据
  - test_maps.json           # 测试地图数据
  - test_skills.json         # 测试技能数据
```

#### 测试数据加载器
```cpp
// tests/utils/test_data_loader.h
namespace mir2::test {
  class TestDataLoader {
  public:
    static CharacterData LoadTestCharacter(const std::string& name);
    static ItemTemplate LoadTestItem(int item_id);
    static MonsterTemplate LoadTestMonster(int monster_id);
  };
}
```

---

## 第六部分：成功指标与验收标准

### 短期目标 (1个月)
- [ ] 单元测试覆盖率达到 70%
- [ ] 补全所有 P0 关键路径测试
- [ ] 修复所有现有测试失败

### 中期目标 (3个月)
- [ ] 单元测试覆盖率达到 80%
- [ ] 集成测试数量翻倍 (36+)
- [ ] E2E 测试覆盖主要业务流程
- [ ] CI/CD 集成覆盖率报告

### 长期目标 (6个月)
- [ ] 单元测试覆盖率达到 85%+
- [ ] 完整的测试金字塔体系
- [ ] 自动化性能回归测试
- [ ] 混沌工程测试引入

### 验收标准
```
✅ 每个 PR 必须包含测试
✅ 新增代码覆盖率 ≥ 80%
✅ 所有测试必须通过才能合并
✅ 关键路径覆盖率 = 100%
✅ 集成测试运行时间 < 5分钟
✅ E2E 测试运行时间 < 15分钟
```

---

## 第七部分：行动检查清单

### 立即行动 (本周)
- [ ] 设置测试覆盖率工具 (lcov/gcov)
- [ ] 创建 `tests/utils/` 测试工具库
- [ ] 补充 `logic_server` 核心测试
- [ ] 补充 `damage_calculator` 测试
- [ ] 补充 `attack_handler` 测试

### 近期行动 (本月)
- [ ] 完成 Phase 1 (P0 测试)
- [ ] 启动 Phase 2 (P1 测试)
- [ ] 建立 Mock 基础设施
- [ ] 编写测试编写规范文档
- [ ] CI/CD 集成测试覆盖率报告

### 持续改进
- [ ] 每周回顾测试覆盖率变化
- [ ] 定期重构测试代码
- [ ] 维护测试文档
- [ ] 团队测试培训

---

## 附录 A: 完整缺失测试清单

### 客户端 (Client)
```
client/audio/audio_engine.cc
client/core/application.cc
client/core/event_dispatcher.cc
client/core/path_utils.cc
client/core/timer.cc
client/game/actor_data.cc
client/game/entity_manager.cc
client/game/game_client.cc
client/game/game_client_input.cc
client/game/map/map_renderer.cc
client/game/map/map_system.cpp
client/game/map/path_finder.cpp
client/game/map/portal_manager.cpp
client/game/monster/monster_template_mapper.cc
client/handlers/effect_handler.cc
client/handlers/skill_handler.cc
client/network/network_client.cc
client/network/udp_transport.cc
client/render/actor_renderer.cc
client/render/effect_player.cc
client/render/ground_item_renderer.cc
client/render/renderer.cc
client/scene/login_scene.cc
client/ui/hud/player_info_panel.cc
client/ui/hud/status_bar.cc
client/ui/login_screen.cc
client/ui/npc_dialog_ui.cpp
client/ui/states/character_create_state.cc
client/ui/states/character_select_state.cc
client/ui/states/connecting_state.cc
client/ui/states/error_state.cc
client/ui/states/login_input_state.cc
client/ui/states/login_state_helpers.cc
client/ui/ui_renderer.cc
```

### 服务器 (Server)
```
server/common/crypto_utils.cc
server/common/internal_message_helper.cc
server/config/config_manager.cc
server/config/skill_config_loader.cc
server/core/application.cc
server/core/timer.cc
server/core/utils.cc
server/ecs/inventory_migration.cc
server/ecs/skill_registry.cc
server/ecs/systems/character_utils.cc
server/ecs/systems/damage_calculator.cc
server/ecs/systems/effect_broadcaster.cc
server/ecs/systems/equipment_bonus_system.cc
server/ecs/systems/luck_system.cc
server/ecs/systems/passive_skill_system.cc
server/ecs/systems/recovery_system.cc
server/ecs/systems/spatial_query.cc
server/ecs/systems/storage_system.cc
server/ecs/systems/summon_system.cc
server/ecs/systems/trade_system.cc
server/game/chat/chat_service.cc
server/game/entity/monster.cc
server/game/event/event_handler.cc
server/game/guild/guild_manager.cc
server/game/map/aoi_manager.cc
server/game/map/chunk_manager.cc
server/game/map/door_manager.cc
server/game/npc/lua_script_engine.cc
server/game/npc/npc_interaction_handler.cc
server/log/logger.cc
server/logic/crash_handler.cc
server/logic/entity_lane_scheduler.cc
server/logic/handlers/attack_handler.cc
server/logic/handlers/character/bonus_point_handler.cc
server/logic/handlers/effect/effect_broadcast_service.cc
server/logic/handlers/skill_handler.cc
server/logic/logic_server.cc
server/logic/prewarm_manager.cc
server/logic/response_sender.cc
server/logic/services/client_registry.cc
server/logic/services/ecs_combat_service.cc
server/logic/services/ecs_inventory_service.cc
server/logic/services/merchant_service.cc
server/logic/services/player_presence_service.cc
server/monitor/metrics.cc
server/monitor/metrics_stub.cc
server/network/tcp_client.cc
server/network/tcp_server.cc
server/security/anti_cheat.cc
server/storage_engine/backends/common/account_storage_codec.cc
server/storage_engine/backends/postgres/pg_connection_pool.cc
server/storage_engine/backends/storage_engine_backend.cc
server/storage_engine/l1/memory_cache.cc
```

### 通用模块 (Common)
```
common/character_data.cpp
common/types.cpp
common/utf8_utils.cc
```

**总计**: 90 个文件缺少测试

---

## 附录 B: 测试工具推荐

### 单元测试
- **GoogleTest**: 主测试框架 ✅ (已使用)
- **GoogleMock**: Mock 框架 ✅ (已使用)
- **RapidCheck**: 属性测试 ✅ (已使用)

### 覆盖率工具
- **lcov/gcov**: Linux C++ 覆盖率 (推荐)
- **gcovr**: 覆盖率报告生成
- **Codecov**: 在线覆盖率服务

### 性能测试
- **Google Benchmark**: C++ 性能测试
- **Criterion**: 统计性能测试

### 集成测试
- **Docker Compose**: 多服务集成测试环境
- **Testcontainers**: 容器化测试依赖

### 测试辅助
- **Faker**: 测试数据生成
- **Sanitizers**: 内存/线程/未定义行为检测
  - AddressSanitizer (ASan)
  - ThreadSanitizer (TSan)
  - UndefinedBehaviorSanitizer (UBSan)

---

## 附录 C: 参考资源

### 测试最佳实践
- [Google Test Best Practices](https://google.github.io/googletest/)
- [Modern C++ Testing](https://www.sandordargo.com/blog/2019/04/24/parameterized-testing-with-gtest)
- [The Art of Unit Testing](https://www.manning.com/books/the-art-of-unit-testing-second-edition)

### C++ 测试框架
- [GoogleTest Documentation](https://github.com/google/googletest)
- [Catch2](https://github.com/catchorg/Catch2)
- [doctest](https://github.com/doctest/doctest)

### 覆盖率工具
- [lcov](https://github.com/linux-test-project/lcov)
- [gcovr](https://gcovr.com/)
- [OpenCppCoverage](https://github.com/OpenCppCoverage/OpenCppCoverage) (Windows)

---

**报告结束**

*生成工具*: Claude Code + Serena
*分析方法*: 静态代码分析 + 模式匹配
*下一步*: 按照实施计划执行测试补充工作
