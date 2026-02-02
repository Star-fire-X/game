# RocksDB 快速参考指南

基于官方文档的关键配置和最佳实践总结

---

## 1. 核心 API

### 1.1 打开/关闭数据库

```cpp
// 初始化选项
rocksdb::Options options;
options.create_if_missing = true;

// 打开数据库
rocksdb::DB* db;
rocksdb::Status status = rocksdb::DB::Open(options, "/path/to/db", &db);

// 检查状态
if (!status.ok()) {
    std::cerr << "Error: " << status.ToString() << std::endl;
}

// 关闭数据库
delete db;
```

### 1.2 基本 CRUD 操作

```cpp
// Put（写入）
std::string value = "hello world";
rocksdb::WriteOptions write_opts;
db->Put(write_opts, "key1", value);

// Get（读取）
std::string result;
rocksdb::ReadOptions read_opts;
db->Get(read_opts, "key1", &result);

// Delete（删除）
db->Delete(write_opts, "key1");
```

### 1.3 PinnableSlice 零拷贝读取

```cpp
// 使用 PinnableSlice 避免内存拷贝
rocksdb::PinnableSlice pinnable_val;
rocksdb::Status s = db->Get(rocksdb::ReadOptions(),
                            db->DefaultColumnFamily(),
                            "key1",
                            &pinnable_val);

if (s.ok()) {
    // pinnable_val 指向 Block Cache 中的数据
    // 生命周期结束后自动释放引用
    std::string_view data(pinnable_val.data(), pinnable_val.size());
}
```

### 1.4 WriteBatch 批量操作

```cpp
// 构造批次
rocksdb::WriteBatch batch;
batch.Put("key1", "value1");
batch.Put("key2", "value2");
batch.Delete("key3");

// 原子提交（单次 WAL 写入）
rocksdb::WriteOptions write_opts;
write_opts.sync = false;  // 异步 WAL，降低延迟
db->Write(write_opts, &batch);
```

---

## 2. 性能优化配置

### 2.1 BlockBasedTableOptions（关键！）

```cpp
rocksdb::BlockBasedTableOptions table_options;

// Bloom Filter 配置
table_options.filter_policy.reset(
    rocksdb::NewBloomFilterPolicy(
        10.0,    // bits per key (1% 假阳性)
        false    // use new format
    )
);

// Block Cache 配置
table_options.block_cache = rocksdb::NewLRUCache(256 * 1024 * 1024);  // 256MB

// 缓存索引和过滤器到 Block Cache
table_options.cache_index_and_filter_blocks = true;
table_options.pin_l0_filter_and_index_blocks_in_cache = true;

// Block 大小（默认 4KB）
table_options.block_size = 4096;

// 应用配置
rocksdb::Options options;
options.table_factory.reset(
    rocksdb::NewBlockBasedTableFactory(table_options));
```

**为什么重要**：
- Bloom Filter：避免不存在的 key 查询 SSD (~50μs → ~1μs)
- Block Cache：热数据常驻内存
- 索引缓存：加速 key 查找

### 2.2 压缩配置

```cpp
rocksdb::Options options;

// 压缩算法（LZ4 最快，zstd 最高）
options.compression = rocksdb::kLZ4Compression;

// 压缩比例（典型游戏数据 2-3x）
options.compression_per_level = {
    rocksdb::kNoCompression,      // Level 0（最新）
    rocksdb::kLZ4Compression,     // Level 1+
    rocksdb::kLZ4Compression
};
```

### 2.3 Write Buffer 配置

```cpp
rocksdb::Options options;

// MemTable 大小（越大 → 更少的 compaction，更多内存）
options.write_buffer_size = 64 * 1024 * 1024;  // 64MB

// MemTable 数量（当前 MB 数 > 写缓冲大小时触发 flush）
options.max_write_buffer_number = 2;

// 写入速度限制（防止 compaction 跟不上）
options.rate_limiter = rocksdb::NewGenericRateLimiter(
    100 * 1024 * 1024);  // 100MB/s
```

### 2.4 并发配置

```cpp
rocksdb::Options options;

// 增加并发度
options.IncreaseParallelism(num_threads);

// 优化 Level Style Compaction
options.OptimizeLevelStyleCompaction(target_file_size_base);
```

---

## 3. DBWithTTL（自动过期）

### 3.1 TTL 支持

```cpp
#include <rocksdb/db_ttl.h>

// 打开 TTL DB
rocksdb::DBWithTTL* db;
int32_t ttl_seconds = 3600;  // 1 小时
rocksdb::Status s = rocksdb::DBWithTTL::Open(
    options,
    "/path/to/db",
    &db,
    ttl_seconds);

// Put 时自动设置 TTL
db->Put(write_opts, "key1", "value1");  // 1 小时后自动过期

// 关闭时必须 delete（重要！）
delete db;
```

**注意**：
- DBWithTTL 是 C++ 原始指针
- 必须手动 `delete` 释放，否则 LOCK 文件不释放
- TTL 检查发生在读取或 compaction 时

---

## 4. ReadOptions 和 WriteOptions

### 4.1 ReadOptions

```cpp
rocksdb::ReadOptions read_opts;

// 同步读取（确保 Cache 一致性）
read_opts.verify_checksums = true;  // 默认 true

// 填充 Cache（热数据）
read_opts.fill_cache = true;  // 默认 true

// 跳过 Bloom Filter（强制读取数据）
read_opts.read_tier = rocksdb::kReadAllTier;  // 默认值
```

### 4.2 WriteOptions

```cpp
rocksdb::WriteOptions write_opts;

// 同步 WAL（强一致性）
write_opts.sync = false;  // 异步 WAL（性能优先）

// 禁用 WAL（超高性能，风险大）
write_opts.disableWAL = false;  // 建议保持 false
```

---

## 5. 故障恢复与一致性

### 5.1 WAL（Write-Ahead Log）

```cpp
rocksdb::Options options;

// WAL 文件位置
options.wal_dir = "/path/to/wal";

// WAL 过期时间
options.WAL_ttl_seconds = 604800;  // 7 天

// 恢复
options.error_if_exists = false;  // 允许打开已存在的 DB
```

### 5.2 事务支持

```cpp
#include <rocksdb/utilities/transaction_db.h>

rocksdb::TransactionDB* txn_db;
rocksdb::Status s = rocksdb::TransactionDB::Open(
    options,
    txn_opts,
    "/path/to/db",
    &txn_db);

// 事务写入
rocksdb::Transaction* txn = txn_db->BeginTransaction(write_opts);
txn->Put("key1", "value1");
txn->Put("key2", "value2");
txn->Commit();
delete txn;
```

---

## 6. 监控和统计

### 6.1 数据库统计

```cpp
// 获取统计信息
std::string stats;
db->GetProperty("rocksdb.stats", &stats);
std::cout << stats << std::endl;

// 获取特定指标
std::string num_keys;
db->GetProperty("rocksdb.estimate-num-keys", &num_keys);

std::string size;
db->GetProperty("rocksdb.total-sst-files-size", &size);
```

### 6.2 自定义统计

```cpp
rocksdb::Statistics* stats = rocksdb::CreateDBStatistics();
rocksdb::Options options;
options.statistics = stats;

// ... 使用数据库 ...

// 读取统计
uint64_t tickers = stats->getTickerCount(rocksdb::DB_GET);
uint64_t histograms = stats->getHistogramData(rocksdb::DB_GET);
```

---

## 7. 性能调优检查表

| 配置项 | 推荐值 | 效果 |
|--------|--------|------|
| **Block Cache** | 256MB | +95% 热数据命中率 |
| **Bloom Filter** | 10 bits/key | -95% 无效读 |
| **Write Buffer** | 64MB | -80% WAL 开销 |
| **压缩** | LZ4 | -60% 磁盘占用 |
| **并发** | CPU 核数 | +50% 吞吐 |
| **WriteBatch** | 100 条目/批次 | -90% 写入延迟 |

---

## 8. 常见陷阱

### ❌ 错误做法
```cpp
// 循环 Put（多次 WAL 写入）
for (auto& kv : batch) {
    db->Put(write_opts, kv.first, kv.second);  // ❌ 每次都写 WAL
}

// 重复创建 DB 对象
rocksdb::DB* db = nullptr;
rocksdb::DB::Open(options, path, &db);  // 多次调用
```

### ✅ 正确做法
```cpp
// 使用 WriteBatch（单次 WAL 写入）
rocksdb::WriteBatch batch;
for (auto& kv : batch) {
    batch.Put(kv.first, kv.second);
}
db->Write(write_opts, &batch);  // ✅ 一次 WAL

// 单例 DB 对象
static rocksdb::DB* db = nullptr;
if (!db) {
    rocksdb::DB::Open(options, path, &db);
}
```

---

## 9. 案例：游戏服务器配置模板

```cpp
class RocksDBCacheConfig {
public:
    static rocksdb::Options GetGameServerOptions() {
        rocksdb::Options options;
        options.create_if_missing = true;

        // 表示选项
        rocksdb::BlockBasedTableOptions table_options;

        // Bloom Filter：避免无效读
        table_options.filter_policy.reset(
            rocksdb::NewBloomFilterPolicy(10.0, false));

        // Block Cache：256MB
        table_options.block_cache =
            rocksdb::NewLRUCache(256 * 1024 * 1024);
        table_options.cache_index_and_filter_blocks = true;
        table_options.pin_l0_filter_and_index_blocks_in_cache = true;

        options.table_factory.reset(
            rocksdb::NewBlockBasedTableFactory(table_options));

        // 压缩
        options.compression = rocksdb::kLZ4Compression;

        // MemTable
        options.write_buffer_size = 64 * 1024 * 1024;

        // 并发
        options.IncreaseParallelism(std::thread::hardware_concurrency());
        options.OptimizeLevelStyleCompaction(64 * 1024 * 1024);

        return options;
    }
};
```

---

## 参考资源

- [RocksDB PinnableSlice 论文](https://rocksdb.org/blog/2017/08/24/pinnableslice.html)
- [RocksDB 官方 Wiki](https://github.com/facebook/rocksdb/wiki)
- [RocksDB 性能调优指南](https://github.com/facebook/rocksdb/wiki/Setup-Options-and-Basic-Tuning)
- [RocksDB C++ 示例](https://github.com/facebook/rocksdb/blob/main/examples/simple_example.cc)

---

**最后更新**: 2026-02-02
