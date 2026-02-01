# Legend2 C++ 重制版开发指南

**最后更新**: 2026-01-31
**项目状态**: 地图系统服务端补全完成 ✅

---

## 📋 目录

1. [项目概览](#项目概览)
2. [架构说明](#架构说明)
3. [快速开始](#快速开始)
4. [开发工作流](#开发工作流)
5. [代码规范](#代码规范)
6. [测试指南](#测试指南)
7. [最新完成工作](#最新完成工作)
8. [常见问题](#常见问题)

---

## 项目概览

### 简介

Legend2（传奇2）C++ 重制版是一个使用现代 C++20 技术栈重新实现的经典 MMORPG 服务端和客户端。

### 技术栈

| 组件 | 技术 |
|------|------|
| **语言** | C++20 |
| **构建系统** | CMake 3.25+ |
| **依赖管理** | vcpkg |
| **ECS 框架** | EnTT |
| **网络** | Asio (async) |
| **测试** | GoogleTest + RapidCheck |
| **日志** | spdlog |
| **配置** | yaml-cpp |
| **序列化** | FlatBuffers |
| **客户端渲染** | SDL2 (可选) |

### 核心特性

- ✅ 微服务架构（Gateway、Game、World、DB）
- ✅ ECS 实体组件系统
- ✅ 完整地图系统（加载、传送、门、事件）
- ✅ 战斗系统（核心算法）
- ✅ 异步网络通信
- ⏳ 客户端渲染（开发中）

---

## 架构说明

### 目录结构

```
mir2-cpp/
├── src/
│   ├── common/              # 客户端/服务端共享代码
│   │   ├── types/          # 基础类型定义
│   │   ├── protocol/       # 网络协议
│   │   └── character_data.cpp
│   │
│   ├── server/             # 服务端核心
│   │   ├── apps/           # 服务入口点（game_main, gateway_main, etc.）
│   │   ├── combat/         # 战斗系统
│   │   ├── config/         # 配置管理（map_config_loader, skill_config_loader）
│   │   ├── core/           # 核心工具（timer, utils）
│   │   ├── db/             # 数据库接口
│   │   ├── ecs/            # ECS 系统
│   │   │   ├── components/ # 组件定义（character, combat, item, map, npc, skill）
│   │   │   ├── systems/    # 系统实现（combat, inventory, movement, skill, teleport）
│   │   │   ├── events/     # 事件定义（area_events, map_events）
│   │   │   └── world.cc    # ECS 世界管理
│   │   ├── game/           # 游戏逻辑
│   │   │   └── map/        # 地图系统 ⭐ NEW
│   │   │       ├── map_loader.cc           # 地图文件加载（支持加密）
│   │   │       ├── map_instance.cc         # 地图实例管理（AOI, 实体）
│   │   │       ├── scene_manager.cc        # 场景管理器
│   │   │       ├── door_manager.cc         # 门系统
│   │   │       ├── gate_manager.cc         # 传送门管理
│   │   │       ├── scroll_teleport.cc      # 卷轴传送
│   │   │       ├── cross_server_teleport.cc # 跨服传送框架
│   │   │       ├── map_event_manager.cc    # 地图事件（火焰/采矿/圣盾）
│   │   │       ├── chunk_manager.cc        # 分块加载骨架
│   │   │       ├── area_event_processor.cc # 区域效果处理
│   │   │       └── aoi_manager.cc          # 视野管理（AOI）
│   │   ├── handlers/       # 消息处理器
│   │   │   ├── movement/   # 移动处理（movement_validator）
│   │   │   ├── character/  # 角色管理
│   │   │   ├── combat/     # 战斗处理
│   │   │   ├── item/       # 物品处理
│   │   │   └── npc/        # NPC 交互
│   │   ├── network/        # 网络层
│   │   ├── security/       # 安全模块（anti-cheat, rate_limiter）
│   │   └── log/            # 日志系统
│   │
│   └── client/             # 客户端（SDL2）
│       ├── core/
│       ├── network/
│       ├── render/
│       ├── scene/
│       └── ui/
│
├── tests/                  # 单元测试
│   ├── server/
│   │   ├── map/            # 地图系统测试 ⭐ NEW
│   │   │   ├── gate_manager_test.cpp
│   │   │   ├── map_event_manager_test.cpp
│   │   │   └── map_attributes_test.cpp
│   │   ├── map_loader_test.cpp
│   │   ├── map_instance_test.cpp
│   │   ├── combat_core_test.cpp
│   │   └── ...
│   └── client/
│
├── config/                 # 服务端配置 ⭐ NEW
│   ├── gates.yaml         # 传送门配置
│   ├── combat_config.yaml # 战斗参数
│   └── tables/
│       └── maps.yaml      # 地图属性配置
│
├── Data/                   # 游戏数据资源
├── Map/                    # 地图文件（.map）
├── Wav/                    # 音效资源
├── MUSIC/                  # 音乐资源
├── tools/                  # 开发工具
├── docs/                   # 文档
│   ├── Map_用户故事文档.md
│   └── npc_system_design.md
├── benchmarks/             # 性能基准测试
├── migrations/             # 数据库迁移
└── schemas/                # 数据模式定义
```

### 核心模块说明

#### 1. 地图系统 (src/server/game/map/) ⭐ 最新完成

**功能覆盖**:
- 地图加载（支持 XOR 加密地图：LABY01-04, SNAKE）
- 完整 Tile 数据（背景/前景图像、门、光源、区域）
- 门系统（开关门、锁定、20x20/18x20 范围检测）
- 传送系统（传送门触发、回城卷、地牢卷、跨服传送）
- 地图事件（火焰、采矿、圣盾事件，5 分钟过期清理）
- 地图属性（安全区、战斗区、黑暗等级、任务要求、禁止召唤/随机移动）

**关键类**:
- `MapLoader`: 地图文件解析，支持加密解密
- `MapInstance`: 地图实例，管理实体和 AOI
- `SceneManager`: 场景管理，地图重载
- `DoorManager`: 门索引和状态管理
- `GateManager`: 传送门触发检测（O(1) 哈希索引）
- `MapEventManager`: 地图事件生命周期管理

#### 2. ECS 系统 (src/server/ecs/)

**组件 (components/)**:
- `character_components.h`: 角色基础属性
- `combat_component.h`: 战斗属性
- `equipment_component.h`: 装备系统
- `skill_component.h`: 技能系统
- `npc_component.h`: NPC 属性
- `transform_component.h`: 位置/移动

**系统 (systems/)**:
- `combat_system`: 战斗逻辑
- `movement_system`: 移动验证
- `skill_system`: 技能执行
- `teleport_system`: 传送处理
- `inventory_system`: 背包管理

#### 3. 网络架构 (src/server/network/)

**微服务**:
- `Gateway`: 网关服务（路由、负载均衡）
- `Game`: 游戏服务（核心逻辑）
- `World`: 世界服务（全局状态）
- `DB`: 数据库服务（持久化）

**协议**: 基于 FlatBuffers 的二进制协议

---

## 快速开始

### 环境要求

- **编译器**: GCC 13.3+ 或 Clang 16+ (支持 C++20)
- **CMake**: 3.25+
- **vcpkg**: 最新版本（推荐）
- **操作系统**: Linux (WSL2), macOS, Windows
- **WSL2 用户**: vcpkg 推荐安装在 `/home/wsluser/vcpkg` 路径（本项目 CMakePresets 已预配置）

### 1. 克隆仓库

```bash
git clone <repository-url>
cd mir2-cpp
```

### 2. 配置 vcpkg（推荐）

```bash
# 如果尚未安装 vcpkg
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh

# 设置环境变量（可选）
export VCPKG_ROOT=/path/to/vcpkg
```

### 3. 构建项目

#### 方式 A: 使用 vcpkg preset（推荐）

```bash
# 配置（会自动安装依赖）
cmake --preset vcpkg-debug

# 编译
cmake --build --preset vcpkg-debug -j$(nproc)
```

#### 方式 A.1: WSL 环境下使用 vcpkg preset（WSL 用户推荐）

如果在 WSL2 环境中工作且 vcpkg 安装在 `/home/wsluser/vcpkg`，使用专用的 WSL 预设：

```bash
# 配置 Debug 版本
cmake --preset vcpkg-wsl-debug

# 编译 Debug 版本
cmake --build --preset vcpkg-wsl-debug -j$(nproc)

# 或配置 Release 版本
cmake --preset vcpkg-wsl-release
cmake --build --preset vcpkg-wsl-release -j$(nproc)
```

**CMakePresets 中的可用 WSL 预设**:
- `vcpkg-wsl-debug`: WSL Debug 构建
- `vcpkg-wsl-release`: WSL Release 构建

#### 方式 B: 手动配置 + FetchContent

```bash
# 配置（启用 FetchContent 自动下载依赖）
cmake -B build-linux -DCMAKE_BUILD_TYPE=Debug -DLEGEND2_ALLOW_FETCHCONTENT=ON

# 编译
cmake --build build-linux -j$(nproc)
```

**注意事项**:
1. **RapidCheck 编译错误**: 如果遇到 `uint8_t` 未定义错误，需要修补：
   ```bash
   sed -i '6a#include <cstdint>' build-linux/_deps/rapidcheck-src/include/rapidcheck/Maybe.h
   ```

2. **工具目录禁用**: 如果缺少 SDL2，可临时禁用工具构建：
   ```bash
   # 在 CMakeLists.txt 中注释掉 tools 相关行
   # add_subdirectory(tools/wil2png)
   ```

3. **数据库模块可选**: `mir2_db` 需要 pqxx 依赖，如不需要可跳过

### 4. 运行服务

```bash
# 游戏服务器（使用 WSL 构建输出）
./build-wsl/bin/mir2_game

# 网关服务（使用 WSL 构建输出）
./build-wsl/bin/mir2_gateway

# 世界服务（使用 WSL 构建输出）
./build-wsl/bin/mir2_world

# 或使用其他 preset 的构建输出
# ./build-linux/bin/mir2_game
# ./build-linux/bin/mir2_gateway
```

### 5. 运行测试

```bash
# 使用 WSL preset 构建测试（推荐）
cmake --build --preset vcpkg-wsl-debug --target legend2_tests -j$(nproc)

# 或使用其他 preset
cmake --build build-linux --target legend2_tests -j$(nproc)

# 运行所有测试
ctest --test-dir build-wsl --output-on-failure

# 运行特定模块测试
ctest --test-dir build-wsl -R "map_|combat_"
```

---

## 开发工作流

### 分支策略

- `master`: 主分支，稳定版本
- `develop`: 开发分支
- `feature/*`: 功能分支
- `bugfix/*`: 修复分支

### 开发流程

1. **创建功能分支**
   ```bash
   git checkout -b feature/new-feature develop
   ```

2. **编写代码**
   - 遵循代码规范（见下节）
   - 添加单元测试
   - 更新文档

3. **本地测试**
   ```bash
   # WSL 用户推荐使用 WSL preset
   # 配置
   cmake --preset vcpkg-wsl-debug

   # 编译
   cmake --build --preset vcpkg-wsl-debug -j$(nproc)

   # 运行测试
   ctest --test-dir build-wsl --output-on-failure

   # 代码检查（如果有 clang-tidy）
   clang-tidy src/server/**/*.cc -- -Isrc -std=c++20
   ```

4. **提交代码**
   ```bash
   git add .
   git commit -m "feat(map): add gate trigger optimization

   - Implement O(1) hash-based gate lookup
   - Add coordinate index for fast position matching
   - Update GateManager with coord_index_

   Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
   ```

5. **创建 Pull Request**
   - 标题简明扼要
   - 描述包含：目的、关键变更、测试结果
   - 如有 UI 变化，附上截图

### Git Commit 规范

```
<type>(<scope>): <subject>

<body>

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

**类型 (type)**:
- `feat`: 新功能
- `fix`: 修复 Bug
- `refactor`: 重构
- `perf`: 性能优化
- `test`: 测试相关
- `docs`: 文档更新
- `chore`: 构建/工具相关

**范围 (scope)** 示例:
- `map`: 地图系统
- `combat`: 战斗系统
- `ecs`: ECS 框架
- `network`: 网络层
- `gateway`: 网关服务

---

## 代码规范

> **遵循 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)**

### 命名约定

| 类型 | 风格 | 示例 |
|------|------|------|
| 文件名 | snake_case | `map_instance.h`, `gate_manager.cc` |
| 类/结构体 | PascalCase | `MapInstance`, `GateInfo` |
| 函数/方法 | PascalCase | `LoadMap()`, `CheckGateTrigger()` |
| 变量 | snake_case | `map_width`, `door_index` |
| 常量 | kPascalCase | `kDefaultGridSize`, `kMaxTileCount` |
| 枚举值 | kPascalCase | `AreaEffectType::kFire` |
| 命名空间 | snake_case | `mir2::game::map` |
| 私有成员 | trailing `_` | `map_id_`, `tile_data_` |
| 宏定义 | 全大写 + `_` | `MIR2_GAME_MAP_INSTANCE_H_` |

### 代码风格

```cpp
// 1. 头文件防护（Google 风格）
#ifndef MIR2_GAME_MAP_MAP_INSTANCE_H_
#define MIR2_GAME_MAP_MAP_INSTANCE_H_

#include <cstdint>
#include <vector>
#include <mutex>

namespace mir2::game::map {

// 2. 类定义（访问修饰符不缩进，成员 2 空格缩进）
class MapInstance {
 public:
  // 构造函数（函数定义大括号换行）
  MapInstance(int32_t map_id, int32_t width, int32_t height);
  ~MapInstance() = default;

  // 禁用拷贝
  MapInstance(const MapInstance&) = delete;
  MapInstance& operator=(const MapInstance&) = delete;

  // 公共方法（2 空格缩进）
  bool IsWalkable(int32_t x, int32_t y) const;
  bool IsFlyable(int32_t x, int32_t y) const;

 private:
  // 私有成员（trailing underscore，2 空格缩进）
  int32_t map_id_;
  int32_t map_width_;
  int32_t map_height_;
  std::vector<uint8_t> walkability_;
  mutable std::mutex mutex_;
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_MAP_INSTANCE_H_
```

**实现文件示例** (`map_instance.cc`):

```cpp
#include "game/map/map_instance.h"

#include <algorithm>

namespace mir2::game::map {

// 函数定义：大括号换行（Google 风格）
MapInstance::MapInstance(int32_t map_id, int32_t width, int32_t height)
    : map_id_(map_id),
      map_width_(width),
      map_height_(height),
      walkability_(width * height, 1) {  // 初始化列表，2 空格缩进
  // 构造函数体
}

bool MapInstance::IsWalkable(int32_t x, int32_t y) const {
  std::lock_guard<std::mutex> lock(mutex_);

  // 边界检查
  if (!IsValidPosition(x, y)) {
    return false;
  }

  // 查询 walkability 数组
  const size_t index = static_cast<size_t>(y) * map_width_ + x;
  return walkability_[index] != 0;
}

}  // namespace mir2::game::map
```

### 注释规范

遵循 Google 风格，使用简洁的行注释，对外接口使用 Doxygen 风格：

```cpp
// 检查指定坐标是否可行走。
//
// Args:
//   x: X坐标
//   y: Y坐标
//
// Returns:
//   true 如果坐标可行走，false 否则
bool MapInstance::IsWalkable(int32_t x, int32_t y) const {
  std::lock_guard<std::mutex> lock(mutex_);

  // 边界检查
  if (!IsValidPosition(x, y)) {
    return false;
  }

  // 查询 walkability 数组
  const size_t index = static_cast<size_t>(y) * map_width_ + x;
  return walkability_[index] != 0;
}
```

**头文件注释示例**:

```cpp
// 地图实例管理类
//
// 管理单个地图的实体集合和视野同步（AOI）。
// 所有公共方法都是线程安全的。
//
// Example:
//   MapInstance map(0, 100, 100);
//   if (map.IsWalkable(10, 20)) {
//     // 坐标可行走
//   }
class MapInstance {
 public:
  // ...
};
```

### 最佳实践（Google C++ Style）

1. **优先使用 `const` 和引用传递**
   ```cpp
   // Good
   bool IsWalkable(int32_t x, int32_t y) const;
   const MapAttributes& GetAttributes() const;
   void ProcessGates(const std::vector<GateInfo>& gates);

   // Bad
   bool IsWalkable(int32_t x, int32_t y);  // 缺少 const
   MapAttributes GetAttributes() const;     // 不必要的拷贝
   void ProcessGates(std::vector<GateInfo> gates);  // 值传递导致拷贝
   ```

2. **使用 `std::optional` 或 `std::unique_ptr` 处理可能失败的操作**
   ```cpp
   // Good: 使用 std::optional 表示可选返回值
   std::optional<GateInfo> CheckGateTrigger(
       const std::string& map_id, int32_t x, int32_t y) const;

   // Good: 使用 std::unique_ptr 表示所有权转移
   std::unique_ptr<MapInstance> CreateMap(int32_t map_id);

   // Bad: 返回裸指针
   GateInfo* CheckGateTrigger(...);  // 内存管理不清晰
   ```

3. **禁用拷贝和移动（如果不需要）**
   ```cpp
   class MapInstance {
    public:
     MapInstance(int32_t map_id, int32_t width, int32_t height);

     // 禁用拷贝（Google 风格）
     MapInstance(const MapInstance&) = delete;
     MapInstance& operator=(const MapInstance&) = delete;

     // 移动语义（如果需要）
     MapInstance(MapInstance&&) = default;
     MapInstance& operator=(MapInstance&&) = default;
   };
   ```

4. **线程安全：使用 RAII 锁**
   ```cpp
   class MapInstance {
    public:
     bool IsWalkable(int32_t x, int32_t y) const {
       std::lock_guard<std::mutex> lock(mutex_);  // RAII 自动解锁
       // ...
     }

    private:
     mutable std::mutex mutex_;  // mutable 允许 const 方法修改
   };
   ```

5. **初始化列表优于赋值**
   ```cpp
   // Good: 使用初始化列表
   MapInstance::MapInstance(int32_t map_id, int32_t width, int32_t height)
       : map_id_(map_id),
         map_width_(width),
         map_height_(height),
         walkability_(width * height, 1) {
   }

   // Bad: 构造函数体内赋值
   MapInstance::MapInstance(int32_t map_id, int32_t width, int32_t height) {
     map_id_ = map_id;  // 效率低，先默认构造再赋值
     map_width_ = width;
     map_height_ = height;
   }
   ```

6. **避免过度优化，保持代码可读性**
   ```cpp
   // Good: 清晰易懂
   for (const auto& gate : gates_) {
     if (gate.source_x == x && gate.source_y == y) {
       return gate;
     }
   }

   // Bad: 过早优化（除非性能测试证明需要）
   // 使用复杂的位运算或手动内联牺牲可读性
   ```

7. **使用 C++20 现代特性**
   ```cpp
   // Concepts（类型约束）
   template<typename T>
   concept Walkable = requires(T t, int32_t x, int32_t y) {
     { t.IsWalkable(x, y) } -> std::convertible_to<bool>;
   };

   template<Walkable T>
   bool CheckPath(const T& map, int32_t x, int32_t y) {
     return map.IsWalkable(x, y);
   }

   // Ranges（函数式编程风格）
   auto walkable_tiles = tiles
       | std::views::filter([](const auto& tile) {
           return !(tile.fr_img & 0x8000);
         });

   // Designated initializers（结构体初始化）
   GateInfo gate{
       .gate_id = 1,
       .source_map = "3",
       .source_x = 330,
       .source_y = 330,
       .target_map = "0"
   };
   ```

8. **避免使用裸指针，优先智能指针**
   ```cpp
   // Good: 使用智能指针
   std::unique_ptr<MapInstance> map_instance_;
   std::shared_ptr<const Config> config_;

   // Bad: 裸指针（除非与 C API 交互必须使用）
   MapInstance* map_instance_;  // 谁负责释放？
   Config* config_;             // 内存管理不清晰
   ```

---

## 测试指南

### 测试框架

- **GoogleTest**: 单元测试
- **RapidCheck**: 属性测试（Property-based testing）
- **GoogleMock**: Mock 对象

### 测试文件组织

```
tests/
├── server/
│   ├── map/                    # 地图系统测试
│   │   ├── gate_manager_test.cpp
│   │   ├── door_manager_test.cpp
│   │   ├── map_event_manager_test.cpp
│   │   └── map_attributes_test.cpp
│   ├── map_loader_test.cpp
│   ├── map_instance_test.cpp
│   ├── combat_core_test.cpp
│   └── ...
└── client/
    └── ...
```

### 编写测试

```cpp
#include <gtest/gtest.h>
#include "game/map/gate_manager.h"

namespace mir2::game::map {

class GateManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    manager_ = std::make_unique<GateManager>();
  }

  std::unique_ptr<GateManager> manager_;
};

TEST_F(GateManagerTest, AddGateStoresCorrectly) {
  GateInfo gate{
    .gate_id = 1,
    .source_map = "3",
    .source_x = 330,
    .source_y = 330,
    .target_map = "0",
    .target_x = 100,
    .target_y = 100
  };

  manager_->AddGate(gate);

  auto result = manager_->CheckGateTrigger("3", 330, 330);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->gate_id, 1);
  EXPECT_EQ(result->target_map, "0");
}

TEST_F(GateManagerTest, CheckGateTriggerReturnsNulloptForNonExistent) {
  auto result = manager_->CheckGateTrigger("999", 100, 100);
  EXPECT_FALSE(result.has_value());
}

}  // namespace mir2::game::map
```

### 运行测试

```bash
# 编译测试
cmake --build build-linux --target legend2_tests -j$(nproc)

# 运行所有测试
ctest --test-dir build-linux --output-on-failure

# 运行特定测试
ctest --test-dir build-linux -R GateManager

# 详细输出
./build-linux/bin/legend2_tests --gtest_filter=GateManagerTest.*
```

### 测试覆盖率

```bash
# 使用 gcov + lcov（需要编译器支持）
cmake -B build-coverage \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="--coverage" \
  -DLEGEND2_ALLOW_FETCHCONTENT=ON

cmake --build build-coverage -j$(nproc)
ctest --test-dir build-coverage

lcov --capture --directory build-coverage \
     --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

