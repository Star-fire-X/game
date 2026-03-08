# EnTT版本兼容性分析报告

## 当前配置

**EnTT版本**: 3.13.2
**安装方式**: vcpkg
**C++标准**: C++20

## 兼容性检查结果 ✅

### 1. 已弃用API检查

经过全代码扫描，项目**未使用**以下EnTT 3.13已弃用的API：

#### Registry相关（已弃用）
- ❌ `registry.size()` → 使用 `.storage<Entity>().size()`
- ❌ `registry.alive()` → 使用 `.storage<Entity>().in_use()`
- ❌ `registry.reserve()` → 使用 `.storage<Entity>().reserve()`
- ❌ `registry.capacity()` → 使用 `.storage<Entity>().capacity()`
- ❌ `registry.empty()` → 使用 `.storage<Entity>().empty()`
- ❌ `registry.data()` → 使用 `.storage<Entity>().data()`
- ❌ `registry.released()` → 使用 `.storage<Entity>().in_use()` 和 `.size()`
- ❌ `registry.release()` → 使用 `.storage<Entity>().erase()`
- ❌ `registry.assign()` → 使用 `.storage<Entity>().push()`

#### Storage/Group相关（已弃用）
- ❌ `sort_as()` → 使用基于迭代器的重载版本
- ❌ `pack()` → 使用基于迭代器的 `sort_as()`
- ❌ `basic_sparse_set<>::at()` → 使用 `operator[]`
- ❌ `basic_view::operator[]` 用于size类型

**结论**: 项目未使用任何已弃用API ✅

### 2. 当前API使用情况

项目正确使用了EnTT 3.13推荐的API：

#### Registry操作
```cpp
// src/server/legacy/character_factory.cc:19
entt::entity entity = registry.create();
auto& identity = registry.emplace<CharacterIdentityComponent>(entity);
auto* comp = registry.try_get<Component>(entity);
registry.destroy(entity);
```

#### View迭代（两种风格）
```cpp
// 回调风格 - src/server/ecs/systems/combat_system.cc:173
combat_group_.each([&](entt::entity entity,
                       CombatComponent& combat,
                       CharacterAttributesComponent& attributes) {
    // ...
});

// 结构化绑定风格 - src/server/ecs/systems/monster_drop_system.cc:189
for (auto [entity, party] : party_view.each()) {
    // ...
}
```

#### 事件/信号系统
```cpp
// src/server/ecs/event_bus.h:26
dispatcher_.trigger(event);
dispatcher_.sink<Event>().connect<&Handler::Handle>(handler_ptr);
```

## 未来升级考虑事项

### 1. 如果升级到EnTT 3.14+

留意以下可能的变化：
- `view.each()` 可能进一步优化或调整API
- `group`的内部组织可能变化
- 信号系统可能有性能改进

### 2. 性能优化建议

#### 当前代码优化点

**src/server/ecs/systems/combat_system.cc:173**
```cpp
// 当前使用（有效但可优化）
combat_group_.each([&](entt::entity entity,
                       CombatComponent& combat,
                       CharacterAttributesComponent& attributes,
                       CharacterStateComponent& state) {
    (void)combat;  // 未使用的参数
    (void)state;   // 未使用的参数
    // ...
});

// 优化建议：如果不使用某些组件，考虑使用view而非group
auto view = registry.view<CharacterAttributesComponent>();
for (auto entity : view) {
    auto& attributes = view.get<CharacterAttributesComponent>(entity);
    if (attributes.hp <= 0) continue;
    // ...
}
```

**src/server/ecs/systems/monster_drop_system.cc:189**
```cpp
// 当前使用（推荐风格）
for (auto [entity, party] : party_view.each()) {
    if (party.party_id == party_member->party_id) {
        loot_mode = party.loot_mode;
        loot_range = party.loot_range;
        break;
    }
}
```

## 版本升级测试清单

如需升级EnTT版本，建议测试：

- [ ] 所有ECS系统的Update()方法正常运行
- [ ] Entity创建/销毁无内存泄漏
- [ ] View/Group迭代性能无退化
- [ ] 事件系统触发和订阅正常
- [ ] 多线程访问（如使用）无竞态条件

## 相关资源

- [EnTT官方文档](https://skypjack.github.io/entt/)
- [EnTT GitHub Releases](https://github.com/skypjack/entt/releases)
- [EnTT v3.13 Breaking Changes](https://github.com/skypjack/entt/discussions/619)
- [EnTT Issues - Migration Guide](https://github.com/skypjack/entt/issues/214)

## 总结

✅ **项目EnTT使用符合3.13.2标准**
✅ **无需immediate修复工作**
⚠️ **建议定期检查新版本发布说明**
💡 **可考虑性能优化建议**

---

*生成时间: 2026-02-03*
*EnTT版本: 3.13.2*
*检查范围: src/server/ecs, src/server/game, src/server/handlers*
