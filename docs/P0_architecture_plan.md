# P0 架构方案：资源加载完善、地图渲染与角色显示、移动系统客户端集成

## 1. 现状分析

### 已有接缝（可测试的接口边界）
| 接缝 | 文件 | 用途 |
|------|------|------|
| `IRenderer` | `src/client/render/i_renderer.h` | 渲染抽象，支持 Mock |
| `INetworkManager` | `src/client/network/i_network_manager.h` | 网络抽象，支持 Mock |
| `GameClient(IRenderer, INetworkManager)` | `game_client.h:185` | 构造注入 |
| Handler Callback 结构体 | `handlers/*_handler.h` | 解耦 handler ↔ GameClient |
| `LRUCache<K,V>` | `resource_loader.h` | 纯数据结构，无 SDL 依赖 |
| `PositionInterpolator` / `EntityInterpolator` | `core/position_interpolator.h` | 纯数学，无外部依赖 |

### 缺失接缝（需引入）
| 缺失点 | 问题 | 方案 |
|--------|------|------|
| `ResourceManager` 无接口 | 无法 mock 资源加载 | 引入 `IResourceProvider` |
| `SDLRenderer::texture_cache_` 无策略 | 无法控制 GPU 缓存淘汰 | 引入 `ITextureCache` |
| 异步加载无基础设施 | 主线程阻塞 I/O | 引入 `AsyncLoader` (线程池 + future) |
| `MapRenderer` 硬耦合 `ResourceManager` | 不可独立测试 | 通过 `IResourceProvider` 解耦 |
| `ActorRenderer` 硬耦合 SDL texture | 不可独立测试 | 通过 `ITextureCache` 解耦 |
| 移动系统无 client-side 可达性验证 | 点击不可达位置也发请求 | 引入 `IWalkabilityProvider` |

---

## 2. 架构变更方案

### 2.1 资源加载系统完善

#### 新增接口：`IResourceProvider`
```
目的：将"获取精灵/地图数据"与"WIL文件解析"解耦
```

```cpp
// src/client/resource/i_resource_provider.h (新文件)
class IResourceProvider {
public:
    virtual ~IResourceProvider() = default;
    virtual std::optional<Sprite> get_sprite(const std::string& archive, int index) = 0;
    virtual std::optional<MapData> load_map(const std::string& path) = 0;
    virtual bool is_archive_loaded(const std::string& name) const = 0;
    virtual bool load_archive(const std::string& path) = 0;
};
```
`ResourceManager` 实现此接口，零行为变更。

#### 新增：`ITextureCache`
```
目的：将 GPU 纹理缓存策略从 SDLRenderer 中抽出，使 MapRenderer/ActorRenderer 可用 mock 测试
```

```cpp
// src/client/render/i_texture_cache.h (新文件)
class ITextureCache {
public:
    virtual ~ITextureCache() = default;
    virtual std::shared_ptr<Texture> get_or_create(
        const std::string& key, const Sprite& sprite, bool flip_v) = 0;
    virtual void evict(const std::string& key) = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
};
```
`SDLRenderer` 内部用 `SDLTextureCache` 实现（提取已有 `texture_cache_` 逻辑）。

#### 新增：`AsyncLoader`
```
目的：异步预加载资源，不阻塞渲染帧
策略：单生产者/多消费者线程池，使用 std::future 回调到主线程
```

```cpp
// src/client/resource/async_loader.h (新文件)
class AsyncLoader {
public:
    explicit AsyncLoader(IResourceProvider& provider, size_t thread_count = 2);

    // 异步请求精灵（回调在主线程 poll 时执行）
    void request_sprite(const std::string& archive, int index,
                        std::function<void(std::optional<Sprite>)> callback);

    // 异步请求地图
    void request_map(const std::string& path,
                     std::function<void(std::optional<MapData>)> callback);

    // 主线程每帧调用，派发完成回调（限制每帧最大数量避免卡顿）
    void poll(int max_completions = 8);

    void shutdown();
};
```

### 2.2 地图渲染改造

**现有 `MapRenderer` 改造**（非新增）:
- 构造函数接受 `IResourceProvider&` 和 `ITextureCache&` 而非裸指针
- 在 `render()` 中对缺失纹理使用占位色块（异步加载未完成时）
- 新增 `prefetch_visible_tiles(camera)` 方法，通过 `AsyncLoader` 预取即将进入视口的瓦片

**文件变更**:
- `src/client/game/map/map_renderer.h` — 修改构造函数签名，新增 prefetch
- `src/client/game/map/map_renderer.cc` — 实现异步预取逻辑

### 2.3 角色显示改造

**现有 `ActorRenderer` 改造**:
- 构造函数接受 `ITextureCache&` 替代 `SDLRenderer*` 的纹理缓存
- `draw_actor()` 中纹理缺失时跳过绘制（异步加载中，下帧再试）
- 保持现有 z-order 排序、多层绘制逻辑不变

**现有 `EntityManager` 改造**:
- 新增 `get_entities_in_view(camera, padding)` 方法：视口裁剪 + 排序一次性完成
- 每个 entity 新增 `EntityInterpolator` 成员（已有类，只需绑定）

**文件变更**:
- `src/client/render/actor_renderer.h/.cc` — 注入 `ITextureCache`
- `src/client/game/entity_manager.h/.cc` — 视口查询 + interpolator 绑定

### 2.4 移动系统客户端集成

#### 新增接口：`IWalkabilityProvider`
```cpp
// src/client/game/map/i_walkability_provider.h (新文件)
class IWalkabilityProvider {
public:
    virtual ~IWalkabilityProvider() = default;
    virtual bool is_walkable(int x, int y) const = 0;
    virtual bool is_valid_position(int x, int y) const = 0;
};
```
`MapSystem` 实现此接口。

#### `MovementController` (新增)
```cpp
// src/client/game/movement_controller.h (新文件)
class MovementController {
public:
    MovementController(IWalkabilityProvider& walkability,
                       INetworkManager& network,
                       PositionInterpolator& interpolator);

    // 处理点击移动请求：验证目标可达 → 发送网络请求
    bool request_move(const Position& current, const Position& target);

    // 处理服务器移动响应：更新插值器
    void on_move_response(const Position& confirmed_pos);

    // 处理其他实体移动广播
    void on_entity_move(uint64_t entity_id, const Position& new_pos, uint8_t direction);

    // 每帧更新
    void update(float delta_ms);

private:
    // 仅验证目标格子是否可行走（不做寻路）
    bool validate_click_target(const Position& target) const;
};
```

**设计要点**:
- 点击验证仅检查目标格是否可行走（`is_walkable`），**不做客户端寻路**
- 服务器权威：移动结果以服务器 MoveRsp 为准
- 客户端预测：发送请求后立即开始插值，收到拒绝则回滚

---

## 3. 文件触碰列表

### 新增文件（6个）

| 文件路径 | 用途 | LOC估计 |
|----------|------|---------|
| `src/client/resource/i_resource_provider.h` | 资源提供者接口 | ~30 |
| `src/client/render/i_texture_cache.h` | GPU 纹理缓存接口 | ~25 |
| `src/client/resource/async_loader.h` | 异步加载器声明 | ~50 |
| `src/client/resource/async_loader.cc` | 异步加载器实现 | ~120 |
| `src/client/game/map/i_walkability_provider.h` | 可行走性接口 | ~20 |
| `src/client/game/movement_controller.h` | 移动控制器（声明+内联实现） | ~100 |

### 修改文件（8个）

| 文件路径 | 变更内容 |
|----------|----------|
| `src/client/resource/resource_loader.h` | `ResourceManager` 继承 `IResourceProvider` |
| `src/client/render/renderer.h` | 提取 `texture_cache_` 为 `SDLTextureCache`，实现 `ITextureCache` |
| `src/client/render/renderer.cc` | 纹理缓存实现迁移到 `SDLTextureCache` |
| `src/client/game/map/map_renderer.h` | 构造函数改为接受接口；新增 `prefetch_visible_tiles` |
| `src/client/game/map/map_renderer.cc` | 异步预取实现 |
| `src/client/render/actor_renderer.h` | 注入 `ITextureCache` |
| `src/client/game/entity_manager.h` | 新增 `get_entities_in_view()`；entity 绑定 `EntityInterpolator` |
| `src/client/game/game_client.h/.cc` | 创建并注入 `AsyncLoader`、`MovementController`；连线回调 |

### 新增测试文件（5个）

| 文件路径 | 测试范围 |
|----------|----------|
| `tests/client/resource/async_loader_test.cc` | 异步加载、回调派发、线程安全 |
| `tests/client/render/texture_cache_test.cc` | LRU 淘汰、命中/未命中 |
| `tests/client/game/movement_controller_test.cc` | 点击验证、预测移动、回滚 |
| `tests/client/game/entity_manager_view_test.cc` | 视口裁剪查询 |
| `tests/client/resource/resource_provider_test.cc` | IResourceProvider mock 验证 |

### 修改测试文件（2个）

| 文件路径 | 变更内容 |
|----------|----------|
| `tests/client/movement_handler_test.cpp` | 补充 MovementController 集成场景 |
| `tests/CMakeLists.txt` | 添加新测试文件 |

---

## 4. 构建顺序

```
Phase 1 - 接口定义（无破坏性，纯新增）
  ├── i_resource_provider.h
  ├── i_texture_cache.h
  └── i_walkability_provider.h

Phase 2 - 接口实现（修改已有类实现接口）
  ├── ResourceManager : IResourceProvider
  ├── SDLTextureCache : ITextureCache（从 SDLRenderer 提取）
  └── MapSystem : IWalkabilityProvider

Phase 3 - 新组件实现
  ├── AsyncLoader（依赖 IResourceProvider）
  └── MovementController（依赖 IWalkabilityProvider + INetworkManager）

Phase 4 - 消费者改造
  ├── MapRenderer 改用 IResourceProvider + ITextureCache + AsyncLoader
  ├── ActorRenderer 改用 ITextureCache
  └── EntityManager 增加视口查询 + EntityInterpolator

Phase 5 - 集成
  └── GameClient 组装所有组件，连线回调

Phase 6 - 测试
  └── 所有新增/修改测试
```

每个 Phase 完成后项目应可编译通过。

---

## 5. 测试策略

### 5.1 无 SDL 的单元测试（Mock 驱动）

| 测试目标 | Mock 对象 | 验证点 |
|----------|-----------|--------|
| `AsyncLoader` | `MockResourceProvider` | 回调在 `poll()` 中执行；线程安全；max_completions 限制生效 |
| `MovementController` | `MockWalkability` + `MockNetwork` | 不可行走目标被拒绝；可行走目标触发网络请求；服务器拒绝触发回滚 |
| `EntityManager::get_entities_in_view` | 无（纯数据结构） | 视口外实体被过滤；边界实体含 padding 时包含 |
| `ITextureCache` | `MockTextureCache` | LRU 淘汰正确；缓存命中不重建纹理 |

### 5.2 集成测试（需 SDL，标记 `[integration]`）

| 测试目标 | 验证点 |
|----------|--------|
| `SDLTextureCache` 实际纹理创建 | 从 Sprite 到 SDL_Texture 往返正确 |
| `MapRenderer` + `AsyncLoader` | 异步加载瓦片后正确渲染 |
| 完整移动流：点击 → 验证 → 请求 → 响应 → 插值 | 端到端回路 |

### 5.3 性能验证

| 指标 | 方法 | 目标 |
|------|------|------|
| 帧率 | `FrameTimer` 统计，100 帧平均 | ≥ 60 FPS |
| 纹理缓存命中率 | `ITextureCache::stats()` | ≥ 90% 在正常游玩中 |
| 异步加载延迟 | `AsyncLoader` 内部计时 | 单精灵 < 2ms IO |
| 视口裁剪效率 | `MapRenderer::stats` tiles_culled | 仅渲染可见范围 |

---

## 6. 风险评估

| 风险 | 严重程度 | 缓解措施 |
|------|----------|----------|
| **异步加载竞态**：主线程渲染读取 vs 加载线程写入 | 高 | `AsyncLoader` 使用 lock-free SPSC 队列，完成结果仅在 `poll()` 中消费 |
| **纹理缓存内存溢出**：大量纹理超出 GPU 显存 | 中 | `ITextureCache` 实现 LRU + 内存上限（默认 256MB），超限时淘汰最旧纹理 |
| **移动预测回滚闪烁**：服务器拒绝移动导致角色抖动 | 中 | 回滚时使用 `PositionInterpolator` 平滑返回，而非瞬移 |
| **接口过度抽象**：引入过多接口增加理解成本 | 低 | 仅在测试需要 mock 的边界引入接口（3个），内部实现保持具体类 |
| **WIL 解析线程安全**：`WilArchive` 使用 `mutable ifstream` | 中 | `AsyncLoader` 每线程独立打开文件句柄，不共享 `WilArchive` 实例 |
| **构建顺序依赖**：Phase 4 改造可能引入编译错误 | 低 | 每个 Phase 都是可编译的增量；CI 每阶段验证 |

---

## 7. 架构决策记录

### ADR-1: 为什么不做客户端寻路？
**决策**: 客户端只验证"目标格子是否可行走"，不做 A* 寻路。
**理由**: 服务器已有权威寻路；客户端寻路会增加不一致风险；KISS 原则。

### ADR-2: 为什么用回调而非 Observer/Event Bus？
**决策**: `AsyncLoader` 和 `MovementController` 使用 `std::function` 回调。
**理由**: 现有 Handler 模式已用回调，保持一致；事件总线对当前规模过度设计。

### ADR-3: 为什么提取 `ITextureCache` 而非 Mock 整个 `IRenderer`？
**决策**: 单独抽出纹理缓存接口。
**理由**: `IRenderer` 已有但太宽（20+ 方法）；`MapRenderer`/`ActorRenderer` 只需缓存查询，窄接口更精确。

### ADR-4: 异步加载策略
**决策**: 线程池 + 主线程 poll（每帧限额 8 个完成回调）。
**理由**: 避免 GPU 操作在工作线程（SDL 不安全）；限额避免回调风暴导致帧率尖峰。
