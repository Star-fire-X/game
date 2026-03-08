# 已废弃的旧模块

## 说明
这些模块已于 2026-02 迁移到 `src/server/storage_engine/`。

## 迁移映射

### persistence/ → storage_engine/persistence/
- persistence_manager.* → storage_engine.* (核心引擎)
- postgres_backend.* → persistence/postgres_backend.* (待迁移)
- json_serializer.* → (待迁移)
- component_registry.* → (待迁移)

### cache/ → storage_engine/
- local_lru_cache.* → l1/memory_cache.*
- rocksdb_cache.* → l2/rocksdb_cache.*
- circuit_breaker.* → utils/circuit_breaker.*
- global_hybrid_clock.* → utils/global_hybrid_clock.*
- async_persistence_queue.* → persistence/async_persistence_queue.*
- tiered_cache.* → storage_engine.* (核心引擎)

## 保留原因
保留旧代码供参考，确保迁移完整性。
确认无问题后可删除。
