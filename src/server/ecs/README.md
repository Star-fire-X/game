# ECS 系统使用指南

## 快速开始

### 基本概念

- **Entity（实体）**：游戏对象的唯一标识符
- **Component（组件）**：纯数据结构，描述实体的属性
- **System（系统）**：处理具有特定组件的实体的逻辑
- **World（世界）**：单个地图/场景的 ECS 容器
- **RegistryManager**：全局 World 管理器（按 map_id 管理多个 World）

### 创建实体

```cpp
// 获取 World（每个地图一个 World）
auto& registry_manager = RegistryManager::Instance();
World* world = registry_manager.CreateWorld(/*map_id=*/1);
auto& registry = world->Registry();

// 创建实体
entt::entity entity = registry.create();

// 添加组件
registry.emplace<CharacterIdentityComponent>(entity,
    1001, "account_123", "玩家名", CharacterClass::WARRIOR, Gender::MALE);
registry.emplace<CharacterAttributesComponent>(entity);
```

### 查询实体

```cpp
// 单组件查询
auto view = registry.view<CharacterAttributesComponent>();
for (auto entity : view) {
    auto& attr = view.get<CharacterAttributesComponent>(entity);
    // 处理属性
}

// 多组件查询
auto view = registry.view<CharacterAttributesComponent, CombatComponent>();
for (auto [entity, attr, combat] : view.each()) {
    // 同时访问多个组件
}
```

### 使用系统

```cpp
// 创建系统（World 自动管理生命周期）
auto* combat_system = world->CreateSystem<CombatSystem>();
auto* level_up_system = world->CreateSystem<LevelUpSystem>();

// 主循环
while (running) {
    world->Update(delta_time);  // 按优先级执行所有系统
}

// 系统会在 World 析构时自动销毁
```

### 多地图 World 管理

```cpp
auto& registry_manager = RegistryManager::Instance();

// 创建多张地图的 World
registry_manager.CreateWorld(1);
registry_manager.CreateWorld(2);
registry_manager.CreateWorld(3);

// 每帧更新所有 World
registry_manager.UpdateAll(delta_time);
registry_manager.GetCharacterManager().Update(delta_time);
```

## 核心组件

### 角色组件

```cpp
// 身份信息
CharacterIdentityComponent identity;
identity.id = 1001;
identity.name = "玩家名";
identity.char_class = CharacterClass::WARRIOR;

// 属性
CharacterAttributesComponent attr;
attr.level = 10;
attr.hp = 500;
attr.max_hp = 500;
attr.attack = 50;

// 状态
CharacterStateComponent state;
state.map_id = 1;
state.position = {100, 100};
state.direction = Direction::DOWN;
```

### 战斗组件

```cpp
CombatComponent combat;
combat.critical_chance = 0.15f;  // 15% 暴击率
combat.evasion_chance = 0.10f;   // 10% 闪避率
combat.attack_range = 3;         // 攻击范围
```

## 核心系统

### CombatSystem - 战斗系统

```cpp
// 造成伤害
int damage = CombatSystem::TakeDamage(registry, target, 100);

// 计算并应用伤害
auto result = CombatSystem::TakeDamageWithCalc(
    registry, attacker, target, config, &event_bus);

// 治疗
int healed = CombatSystem::Heal(registry, entity, 50);

// 执行攻击（包含范围检测）
auto result = CombatSystem::ExecuteAttack(
    registry, attacker, target, config, &event_bus);
```

### LevelUpSystem - 升级系统

```cpp
// 获得经验
bool leveled_up = LevelUpSystem::GainExperience(
    registry, entity, 1000, &event_bus);

// 系统会自动处理升级和属性成长
```

### MovementSystem - 移动系统

```cpp
// 设置位置
MovementSystem::SetPosition(registry, entity, 150, 200);

// 设置地图
MovementSystem::SetMapId(registry, entity, 2);

// 设置朝向
MovementSystem::SetDirection(registry, entity, Direction::UP);
```

## 事件系统

EventBus 封装了 EnTT 的原生事件机制：组件构造/销毁事件仍然使用 `entt::registry` 的信号，
自定义事件使用 `entt::dispatcher`。dispatcher 在编译期绑定事件类型，避免类型擦除与 RTTI 依赖，
事件订阅函数的签名也会在编译期校验，类型更安全。

### 订阅事件

```cpp
auto& event_bus = world.GetEventBus();

// 订阅升级事件
event_bus.Subscribe<LevelUpEvent>([](const LevelUpEvent& event) {
    SYSLOG_INFO("角色 {} 升到 {} 级",
        entt::to_integral(event.entity), event.new_level);
});

// 订阅伤害事件
event_bus.Subscribe<DamageDealtEvent>([](const DamageDealtEvent& event) {
    if (event.is_critical) {
        SYSLOG_INFO("暴击！造成 {} 点伤害", event.damage);
    }
});
```

### 发布事件

```cpp
// 发布自定义事件
events::CharacterLoginEvent event;
event.entity = entity;
event.character_id = 1001;
event_bus.Publish(event);
```

## 角色管理

### CharacterEntityManager

```cpp
auto& registry_manager = RegistryManager::Instance();
auto& manager = registry_manager.GetCharacterManager();

// 获取或创建角色（指定地图）
entt::entity entity = manager.GetOrCreate(character_id, map_id);

// 玩家登录
manager.OnLogin(character_id, &event_bus);

// 玩家断线
manager.OnDisconnect(character_id, &event_bus);

// 定期更新（保存和清理）
manager.Update(delta_time);

// 保存角色
auto data = manager.Save(character_id);

// 跨地图移动
manager.MoveToMap(character_id, /*new_map_id=*/2, /*x=*/120, /*y=*/80);
```

## 脏标记系统

```cpp
#include "ecs/dirty_tracker.h"

// 标记属性变更
dirty_tracker::mark_attributes_dirty(registry, entity);

// 标记状态变更
dirty_tracker::mark_state_dirty(registry, entity);

// 检查是否有变更
if (dirty_tracker::is_dirty(registry, entity)) {
    // 保存数据
}

// 清除脏标记
dirty_tracker::clear_dirty(registry, entity);
```

## 线程安全

⚠️ **重要：ECS 系统是单线程设计**

```cpp
// ✅ 正确
void GameLoop() {
    world.Update(delta_time);
}

// ❌ 错误 - 不要在其他线程访问
void NetworkThread() {
    auto entity = manager.GetOrCreate(id);  // 崩溃风险！
}
```

详见：[THREADING.md](./THREADING.md)

## 性能优化建议

### 1. 使用视图而非逐个查询

```cpp
// ❌ 慢
for (uint32_t id : player_ids) {
    auto entity = manager.TryGet(id);
    auto* attr = registry.try_get<AttributesComponent>(*entity);
}

// ✅ 快
auto view = registry.view<CharacterIdentityComponent, AttributesComponent>();
for (auto [entity, identity, attr] : view.each()) {
    // 批量处理
}
```

### 2. 避免频繁创建/销毁实体

```cpp
// ✅ 使用对象池或标记为"死亡"
registry.emplace<DeadTag>(entity);
```

### 3. 组件尽量小

```cpp
// ❌ 大组件
struct HugeComponent {
    int data[1000];  // 4KB
};

// ✅ 小组件
struct SmallComponent {
    int value;  // 4 bytes
};
```

## 调试技巧

### 1. 查看实体组件

```cpp
void DebugEntity(entt::registry& registry, entt::entity entity) {
    SYSLOG_DEBUG("Entity {}", entt::to_integral(entity));

    if (auto* id = registry.try_get<CharacterIdentityComponent>(entity)) {
        SYSLOG_DEBUG("  Name: {}", id->name);
    }
    if (auto* attr = registry.try_get<CharacterAttributesComponent>(entity)) {
        SYSLOG_DEBUG("  Level: {}, HP: {}/{}",
            attr->level, attr->hp, attr->max_hp);
    }
}
```

### 2. 统计实体数量

```cpp
size_t total = registry.size();
size_t alive = registry.alive();
SYSLOG_DEBUG("实体总数: {}, 存活: {}", total, alive);
```

### 3. 遍历所有实体

```cpp
registry.each([&](auto entity) {
    DebugEntity(registry, entity);
});
```

## 常见问题

### Q: 如何添加新组件？

1. 在 `src/server/ecs/components/` 创建头文件
2. 定义 POD 结构体（只包含数据）
3. 如需持久化，在 `DirtyComponent` 添加对应标记

### Q: 如何添加新系统？

1. 继承 `System` 基类
2. 实现 `Update()` 方法
3. 使用 `World::CreateSystem<T>()` 创建并注册

### Q: 组件应该包含逻辑吗？

❌ 不应该。组件只包含数据，逻辑放在 System 中。

### Q: 如何在系统间共享数据？

通过事件系统或共享组件。

## 参考资料

- [EnTT 官方文档](https://github.com/skypjack/entt)
- [ECS 架构模式](https://github.com/SanderMertens/ecs-faq)
- [线程模型文档](./THREADING.md)

---

**最后更新：** 2026-01-28
