# MIR2-CPP 测试补充快速行动计划

**生成时间**: 2026-02-15
**目标**: 2个月内将测试覆盖率从 55.2% 提升至 75%+

---

## 立即行动 (本周内)

### Step 1: 设置测试基础设施 (Day 1)
```bash
# 1. 启用代码覆盖率
cd /mnt/e/mir2-cpp
mkdir -p build-coverage
cd build-coverage

# 2. 配置 CMake with coverage
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="--coverage -g -O0" \
      -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
      ..

# 3. 构建并运行现有测试
make -j$(nproc)
ctest --output-on-failure

# 4. 生成覆盖率报告
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/ext/*' '*/tests/*' --output-file coverage.info
genhtml coverage.info --output-directory coverage_report

# 5. 查看报告
xdg-open coverage_report/index.html
```

### Step 2: 创建测试工具库 (Day 1-2)
```bash
# 创建目录结构
mkdir -p tests/utils
mkdir -p tests/fixtures
mkdir -p tests/mocks/enhanced

# 创建核心工具文件
touch tests/utils/test_data_loader.h
touch tests/utils/test_helpers.h
touch tests/utils/mock_factory.h
touch tests/fixtures/test_characters.json
touch tests/fixtures/test_items.json
```

创建 `tests/utils/test_helpers.h`:
```cpp
#pragma once
#include <gtest/gtest.h>
#include <memory>
#include "server/ecs/world.h"
#include "server/ecs/character_entity_manager.h"

namespace mir2::test {

// 测试环境设置
class TestEnvironment {
public:
  static void SetUp();
  static void TearDown();
  static entt::registry& GetRegistry();
  static CharacterEntityManager& GetEntityManager();
};

// 测试数据生成
CharacterData CreateTestCharacter(int level = 1, const std::string& name = "TestPlayer");
ItemTemplate CreateTestItem(int item_id, ItemType type = ItemType::Weapon);
MonsterTemplate CreateTestMonster(int monster_id, int level = 1);

// 断言辅助
void AssertInRange(int value, int min, int max, const std::string& msg = "");
void AssertCharacterState(const CharacterData& character, /* expected state */);

} // namespace mir2::test
```

### Step 3: 补充 Ultra-Critical 测试 (Day 3-7)

#### 3.1 logic_server_test.cc (Day 3-4)
```cpp
// tests/server/logic/logic_server_test.cc
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "server/logic/logic_server.h"
#include "tests/mocks/mock_storage_backend.h"

namespace mir2::test {

class LogicServerTest : public ::testing::Test {
protected:
  void SetUp() override {
    config_ = LoadTestConfig();
    storage_ = std::make_unique<MockStorageBackend>();
    server_ = std::make_unique<LogicServer>(config_, storage_.get());
  }

  void TearDown() override {
    server_->Shutdown();
  }

  ServerConfig config_;
  std::unique_ptr<MockStorageBackend> storage_;
  std::unique_ptr<LogicServer> server_;
};

TEST_F(LogicServerTest, ServerStartsSuccessfully) {
  EXPECT_TRUE(server_->Initialize());
  EXPECT_TRUE(server_->Start());
  EXPECT_TRUE(server_->IsRunning());
}

TEST_F(LogicServerTest, ServerHandlesGatewayConnection) {
  server_->Start();

  // Simulate gateway connection
  auto connection_id = SimulateGatewayConnect("127.0.0.1", 7000);
  EXPECT_NE(connection_id, 0);

  // Verify connection registered
  EXPECT_TRUE(server_->HasGatewayConnection(connection_id));
}

TEST_F(LogicServerTest, ServerProcessesPlayerMessage) {
  server_->Start();

  // Create test player session
  auto session_id = CreateTestSession();

  // Send movement message
  MovementMessage msg;
  msg.set_target_x(100);
  msg.set_target_y(100);

  auto response = server_->ProcessMessage(session_id, msg);
  EXPECT_TRUE(response.success);
}

TEST_F(LogicServerTest, ServerHandlesGracefulShutdown) {
  server_->Start();

  // Add active sessions
  auto session1 = CreateTestSession();
  auto session2 = CreateTestSession();

  // Shutdown
  EXPECT_TRUE(server_->Shutdown(std::chrono::seconds(5)));

  // Verify cleanup
  EXPECT_FALSE(server_->IsRunning());
  EXPECT_EQ(server_->GetActiveSessionCount(), 0);
}

TEST_F(LogicServerTest, ServerHandlesStorageFailure) {
  EXPECT_CALL(*storage_, LoadCharacter(_))
      .WillOnce(testing::Return(std::nullopt));

  server_->Start();

  // Attempt to load character
  auto result = server_->LoadPlayerCharacter(12345);
  EXPECT_FALSE(result.has_value());
}

} // namespace mir2::test
```

#### 3.2 damage_calculator_test.cc (Day 4-5)
```cpp
// tests/server/ecs/systems/damage_calculator_test.cc
#include <gtest/gtest.h>
#include "server/ecs/systems/damage_calculator.h"
#include "tests/utils/test_helpers.h"

namespace mir2::test {

class DamageCalculatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    attacker_ = CreateTestCharacter(10);
    attacker_.base_attack = 100;
    attacker_.max_attack = 120;

    defender_ = CreateTestCharacter(10);
    defender_.base_defense = 50;
    defender_.max_defense = 60;
  }

  CharacterData attacker_;
  CharacterData defender_;
  DamageCalculator calculator_;
};

TEST_F(DamageCalculatorTest, CalculatesPhysicalDamage) {
  auto damage = calculator_.CalculatePhysicalDamage(attacker_, defender_);

  // Damage should be in reasonable range
  EXPECT_GT(damage, 0);
  EXPECT_LT(damage, attacker_.max_attack);
}

TEST_F(DamageCalculatorTest, DefenseReducesDamage) {
  auto damage_high_defense = calculator_.CalculatePhysicalDamage(attacker_, defender_);

  // Lower defender's defense
  defender_.base_defense = 10;
  defender_.max_defense = 20;

  auto damage_low_defense = calculator_.CalculatePhysicalDamage(attacker_, defender_);

  EXPECT_GT(damage_low_defense, damage_high_defense);
}

TEST_F(DamageCalculatorTest, CriticalHitDoublesDamage) {
  // Force critical hit
  attacker_.luck = 100; // High luck for guaranteed crit

  auto damage_crit = calculator_.CalculatePhysicalDamage(attacker_, defender_);

  // Reset luck
  attacker_.luck = 0;
  auto damage_normal = calculator_.CalculatePhysicalDamage(attacker_, defender_);

  EXPECT_NEAR(damage_crit, damage_normal * 2.0, damage_normal * 0.1);
}

TEST_F(DamageCalculatorTest, HandlesMagicDamage) {
  attacker_.base_magic = 80;
  attacker_.max_magic = 100;
  defender_.magic_defense = 30;

  auto damage = calculator_.CalculateMagicDamage(attacker_, defender_);

  EXPECT_GT(damage, 0);
  EXPECT_LT(damage, attacker_.max_magic);
}

TEST_F(DamageCalculatorTest, MinimumDamageIsOne) {
  // Max defense vs weak attack
  attacker_.base_attack = 10;
  attacker_.max_attack = 15;
  defender_.base_defense = 1000;
  defender_.max_defense = 1000;

  auto damage = calculator_.CalculatePhysicalDamage(attacker_, defender_);

  EXPECT_GE(damage, 1); // Always at least 1 damage
}

TEST_F(DamageCalculatorTest, ElementalDamageMultipliers) {
  attacker_.element_type = ElementType::Fire;
  defender_.element_resistance[ElementType::Fire] = 50; // 50% resistance

  auto damage_with_resistance = calculator_.CalculateElementalDamage(
      attacker_, defender_, ElementType::Fire, 100);

  EXPECT_NEAR(damage_with_resistance, 50, 5); // ~50 damage after 50% resist
}

} // namespace mir2::test
```

#### 3.3 attack_handler_test.cc (Day 5-6)
```cpp
// tests/server/logic/handlers/attack_handler_test.cc
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "server/logic/handlers/attack_handler.h"
#include "tests/mocks/mock_ecs_world.h"
#include "tests/utils/test_helpers.h"

namespace mir2::test {

class AttackHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    registry_ = std::make_unique<entt::registry>();
    entity_manager_ = std::make_unique<CharacterEntityManager>(registry_.get());
    handler_ = std::make_unique<AttackHandler>(entity_manager_.get());

    // Create attacker and target
    attacker_id_ = entity_manager_->CreateCharacter(CreateTestCharacter(10, "Attacker"));
    target_id_ = entity_manager_->CreateCharacter(CreateTestCharacter(10, "Target"));
  }

  std::unique_ptr<entt::registry> registry_;
  std::unique_ptr<CharacterEntityManager> entity_manager_;
  std::unique_ptr<AttackHandler> handler_;
  EntityId attacker_id_;
  EntityId target_id_;
};

TEST_F(AttackHandlerTest, ProcessValidAttack) {
  AttackRequest request;
  request.set_attacker_id(attacker_id_);
  request.set_target_id(target_id_);

  auto response = handler_->HandleAttack(request);

  EXPECT_TRUE(response.success());
  EXPECT_GT(response.damage(), 0);
  EXPECT_EQ(response.attacker_id(), attacker_id_);
  EXPECT_EQ(response.target_id(), target_id_);
}

TEST_F(AttackHandlerTest, RejectInvalidTarget) {
  AttackRequest request;
  request.set_attacker_id(attacker_id_);
  request.set_target_id(99999); // Invalid target

  auto response = handler_->HandleAttack(request);

  EXPECT_FALSE(response.success());
  EXPECT_EQ(response.error_code(), ErrorCode::InvalidTarget);
}

TEST_F(AttackHandlerTest, RejectOutOfRangeAttack) {
  // Move target far away
  auto& target_transform = registry_->get<TransformComponent>(target_id_);
  target_transform.position = {1000, 1000}; // Far from attacker

  AttackRequest request;
  request.set_attacker_id(attacker_id_);
  request.set_target_id(target_id_);

  auto response = handler_->HandleAttack(request);

  EXPECT_FALSE(response.success());
  EXPECT_EQ(response.error_code(), ErrorCode::OutOfRange);
}

TEST_F(AttackHandlerTest, UpdatesTargetHealth) {
  auto& target_attr = registry_->get<AttributeComponent>(target_id_);
  int initial_hp = target_attr.current_hp;

  AttackRequest request;
  request.set_attacker_id(attacker_id_);
  request.set_target_id(target_id_);

  auto response = handler_->HandleAttack(request);

  EXPECT_TRUE(response.success());
  EXPECT_LT(target_attr.current_hp, initial_hp);
  EXPECT_EQ(target_attr.current_hp, initial_hp - response.damage());
}

TEST_F(AttackHandlerTest, TargetDiesAtZeroHealth) {
  // Set target to low health
  auto& target_attr = registry_->get<AttributeComponent>(target_id_);
  target_attr.current_hp = 1;

  AttackRequest request;
  request.set_attacker_id(attacker_id_);
  request.set_target_id(target_id_);

  auto response = handler_->HandleAttack(request);

  EXPECT_TRUE(response.target_died());
  EXPECT_EQ(target_attr.current_hp, 0);
}

TEST_F(AttackHandlerTest, BroadcastsAttackEvent) {
  MockEventBus event_bus;
  EXPECT_CALL(event_bus, Publish(testing::_))
      .Times(testing::AtLeast(1));

  handler_->SetEventBus(&event_bus);

  AttackRequest request;
  request.set_attacker_id(attacker_id_);
  request.set_target_id(target_id_);

  handler_->HandleAttack(request);
}

} // namespace mir2::test
```

---

## 第一周工作总结

### 预期产出
- ✅ 测试覆盖率工具配置完成
- ✅ 测试工具库建立
- ✅ 3个 Ultra-Critical 组件测试完成
  - logic_server_test.cc
  - damage_calculator_test.cc
  - attack_handler_test.cc

### 下周计划
**Week 2**: 继续 Ultra-Critical 测试
- spatial_query_test.cc (AOI系统)
- crash_handler_test.cc
- skill_handler_test.cc

---

## 资源需求

### 人力
- 主力开发者: 1人 (全职)
- 代码审查者: 1人 (兼职)
- QA: 1人 (兼职)

### 时间
- Phase 1 (P0): 6周
- Phase 2 (P1): 4周
- Phase 3 (P2): 3周
- Phase 4 (集成): 2周
- **总计**: 15周 (~4个月)

### 工具
- GoogleTest/GMock (已有)
- lcov/gcov (需安装)
- Codecov (可选,在线服务)
- Valgrind (内存检测)
- AddressSanitizer (已有)

---

## 成功指标

### Week 1
- [ ] 覆盖率工具配置完成
- [ ] 3个测试文件编写完成
- [ ] 所有现有测试通过

### Month 1
- [ ] P0 Ultra-Critical 完成 (8个文件)
- [ ] 覆盖率 > 60%

### Month 2
- [ ] P0 全部完成 (20个文件)
- [ ] P1 启动 (10个文件)
- [ ] 覆盖率 > 70%

### Month 3
- [ ] P1 完成 (30个文件)
- [ ] 覆盖率 > 80%

---

## 启动命令

```bash
# 立即开始!
cd /mnt/e/mir2-cpp

# 1. 创建工作分支
git checkout -b test/coverage-improvement

# 2. 设置覆盖率构建
./scripts/setup_coverage.sh

# 3. 创建测试工具目录
mkdir -p tests/utils tests/fixtures tests/mocks/enhanced

# 4. 开始编写第一个测试
vim tests/server/logic/logic_server_test.cc

# 5. 运行测试
cd build-coverage
make legend2_tests -j8
./tests/legend2_tests --gtest_filter="LogicServerTest.*"

# 6. 查看覆盖率
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

---

**行动起来！** 🚀

*下一次更新*: 2026-02-22 (一周后)
