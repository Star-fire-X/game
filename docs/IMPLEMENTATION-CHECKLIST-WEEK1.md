# 多层缓存实施清单

**状态**: 准备进入第 1 周 (基础层实现)
**日期**: 2026-02-02

---

## 第 1 周：基础层实现

### 任务 1.1: GlobalHybridClock 实现

**文件**: `src/server/cache/global_hybrid_clock.h` + `.cc`

**实现清单**:
- [ ] 设计 HLC 编码格式 (48 bits 物理时间 + 16 bits 逻辑计数器)
- [ ] 实现 `Next()` 无锁 CAS 操作
- [ ] 实现 `RecoverFromPersistence()` 崩溃恢复
- [ ] 实现 `ScanMaxVersionInDB()` 最大版本扫描
- [ ] 单元测试：正常递增、时钟回退、逻辑计数器溢出

**验收标准**:
```cpp
auto clock = GlobalHybridClock();
uint64_t v1 = clock.Next();  // 0x3E8_0000 (假设 Time=1000ms, Seq=0)
uint64_t v2 = clock.Next();  // 0x3E8_0001 (递增)
assert(v2 > v1);  // 版本严格递增
```

---

### 任务 1.2: LocalLRUCache 实现

**文件**: `src/server/cache/local_lru_cache.h` + `.cc`

**实现清单**:
- [ ] 添加 tsl-ordered-map 到 vcpkg.json
- [ ] 定义 `VersionedData` 结构
- [ ] 实现 `Get()` 方法 (shared_lock)
- [ ] 实现 `Set()` 方法 (unique_lock)
- [ ] 实现统计信息 (hits/misses)
- [ ] 单元测试：LRU 淘汰、并发读写

**验收标准**:
```cpp
LocalLRUCache cache(1000);
cache.Set("player:123", versioned_data);

auto result = cache.Get("player:123");
assert(result.has_value());
assert(result->version == versioned_data.version);
```

---

### 任务 1.3: RocksDBCache 实现

**文件**: `src/server/cache/rocksdb_cache.h` + `.cc`

**实现清单**:
- [ ] 设计 `VersionedData` 序列化格式
- [ ] 配置 BlockBasedTableOptions (Bloom Filter + Block Cache)
- [ ] 实现 `Initialize()` (DBWithTTL 打开)
- [ ] 实现 `Get()` (PinnableSlice 零拷贝)
- [ ] 实现 `Set()` (版本检查 + 写入)
- [ ] 实现 `BatchSet()` (WriteBatch)
- [ ] 实现 `UpdateMaxVersion()` (元数据维护)
- [ ] 实现析构函数 (正确释放资源)
- [ ] 单元测试：读写性能、Bloom Filter 效果

**验收标准**:
```cpp
RocksDBCache l2(config);
l2.Set("player:123", versioned_data);

auto result = l2.Get("player:123");
assert(result.has_value());

// 性能测试
auto start = std::chrono::high_resolution_clock::now();
l2.Get("player:123");
auto elapsed = std::chrono::high_resolution_clock::now() - start;
assert(elapsed < 50us);  // <50 微秒
```

---

### 任务 1.4: VersionedData 和辅助结构

**文件**: `src/server/cache/cache_types.h`

**实现清单**:
- [ ] `VersionedData` 结构体定义
- [ ] `Priority` 枚举 (HIGH/NORMAL)
- [ ] `VersionedData::Encode()` / `Decode()`
- [ ] 序列化/反序列化模板函数
- [ ] `CacheStats` 结构体

**代码框架**:
```cpp
namespace mir2::cache {

// HLC 编码版本号
struct VersionedData {
    uint64_t version;  // HLC 编码
    std::shared_ptr<const std::vector<uint8_t>> data;
    uint64_t timestamp_ms;
};

enum class Priority {
    HIGH,      // 立即提交 (<100ms)
    NORMAL     // 批量提交 (30s)
};

// 统计
struct CacheStats {
    uint64_t hits;
    uint64_t misses;
    uint64_t writes;

    double GetHitRatio() const {
        uint64_t total = hits + misses;
        return total > 0 ? (double)hits / total : 0.0;
    }
};

}
```

---

## 第 1 周验证清单

### 单元测试覆盖

```bash
# 编译
cmake --preset vcpkg-debug
cmake --build --preset vcpkg-debug

# 运行测试
ctest --test-dir build-debug -V -R "cache"
```

**预期输出**:
```
Test project /path/to/build-debug
  GlobalHybridClockTest: PASS
  LocalLRUCacheTest: PASS
  RocksDBCacheTest: PASS

  100% tests passed
```

### 性能基准

```cpp
// 性能目标
GlobalHybridClock clock;
auto start = std::chrono::high_resolution_clock::now();
for (int i = 0; i < 1000000; i++) {
    clock.Next();  // 目标：<1μs
}
auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::high_resolution_clock::now() - start).count();
double ops_per_second = 1000000.0 / (elapsed / 1e9);
assert(ops_per_second > 1000000);  // >1M ops/s
```

---

## 文件结构

```
src/server/cache/
├── CMakeLists.txt              # 新增
├── cache_types.h               # 新增
├── global_hybrid_clock.h        # 新增
├── global_hybrid_clock.cc       # 新增
├── local_lru_cache.h            # 新增
├── local_lru_cache.cc           # 新增
├── rocksdb_cache.h              # 新增
└── rocksdb_cache.cc             # 新增

tests/server/
├── cache/                       # 新增文件夹
│   ├── global_hybrid_clock_test.cc
│   ├── local_lru_cache_test.cc
│   └── rocksdb_cache_test.cc
```

---

## CMakeLists.txt 配置

```cmake
# src/server/cache/CMakeLists.txt

add_library(mir2_cache
    global_hybrid_clock.cc
    local_lru_cache.cc
    rocksdb_cache.cc
)

target_include_directories(mir2_cache
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${ROCKSDB_INCLUDE_DIRS}
)

target_link_libraries(mir2_cache
    PUBLIC mir2_common
    PRIVATE rocksdb
)

target_compile_options(mir2_cache
    PRIVATE -Wall -Wextra -O3
)
```

---

## 依赖项检查

### vcpkg.json

```json
{
  "dependencies": [
    {
      "name": "rocksdb",
      "version": "8.0.0"
    },
    {
      "name": "tsl-ordered-map",
      "version": "1.0.0"
    },
    {
      "name": "nlohmann-json",
      "version": "3.11.2"
    }
  ]
}
```

### 验证依赖

```bash
# 检查 RocksDB 是否可用
vcpkg list | grep rocksdb
vcpkg list | grep tsl

# 编译检查
#include <rocksdb/db.h>
#include <rocksdb/db_ttl.h>
#include <tsl/ordered_map.h>
```

---

## 开发建议

### 代码风格
- 使用 `std::atomic` 进行无锁操作
- 使用 `shared_ptr` 实现零拷贝
- 使用 `shared_mutex` 支持读写锁
- 避免异常，返回 `std::optional` 或 bool

### 日志输出
```cpp
#include <spdlog/spdlog.h>

auto logger = spdlog::get("mir2");
logger->info("HLC initialized: {}", version);
logger->debug("L1 cache hit: {}", key);
logger->error("L2 write failed: {}", status.ToString());
```

### 错误处理
```cpp
// RocksDB 状态检查
rocksdb::Status s = db->Get(...);
if (!s.ok()) {
    if (s.IsNotFound()) {
        // key 不存在
    } else {
        // 其他错误
        logger->error("DB error: {}", s.ToString());
    }
}
```

---

## 交付物

**第 1 周结束**：
- ✅ 4 个核心组件完整实现
- ✅ 所有单元测试通过
- ✅ 性能基准达标
- ✅ 代码审查通过

**git 提交**:
```bash
git add src/server/cache tests/server/cache
git commit -m "feat: implement tiered cache L1/L2 foundation

- GlobalHybridClock: 无锁 HLC 时钟
- LocalLRUCache: 基于 tsl::lru_map
- RocksDBCache: DBWithTTL + Bloom Filter + Block Cache
- 100% 单元测试覆盖
- 性能达标：HLC >1M ops/s, L2 reads <50μs"
```

---

**下一步**: 第 2 周进行 TieredCache 协调层实现

准备好开始了吗？
