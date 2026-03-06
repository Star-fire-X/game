#ifndef MIR2_STORAGE_ENGINE_BACKENDS_STORAGE_ENGINE_BACKEND_SQL_H_
#define MIR2_STORAGE_ENGINE_BACKENDS_STORAGE_ENGINE_BACKEND_SQL_H_

namespace mir2::db::storage_engine_sql {

inline constexpr const char* kUpsertSql =
    "INSERT INTO kv_store (key, version, data, is_deleted, updated_at) "
    "VALUES ($1, $2, $3, FALSE, NOW()) "
    "ON CONFLICT (key) DO UPDATE "
    "SET version = EXCLUDED.version, data = EXCLUDED.data, "
    "is_deleted = FALSE, updated_at = NOW() "
    // Keep newest write only; callers must supply monotonic-increasing version.
    "WHERE kv_store.version < EXCLUDED.version";

inline constexpr const char* kHardDeleteSql =
    "DELETE FROM kv_store WHERE key = $1 AND version <= $2";

inline constexpr const char* kSoftDeleteSql =
    "INSERT INTO kv_store (key, version, data, is_deleted, updated_at) "
    "VALUES ($1, $2, decode('', 'hex'), TRUE, NOW()) "
    "ON CONFLICT (key) DO UPDATE "
    "SET version = EXCLUDED.version, data = EXCLUDED.data, is_deleted = TRUE, updated_at = NOW() "
    "WHERE kv_store.version <= EXCLUDED.version";

inline constexpr const char* kLoadSql =
    "SELECT version, data FROM kv_store WHERE key = $1 AND is_deleted = FALSE";

inline constexpr const char* kLoadAllSql =
    "SELECT key, version, data FROM kv_store WHERE is_deleted = FALSE";

}  // namespace mir2::db::storage_engine_sql

#endif  // MIR2_STORAGE_ENGINE_BACKENDS_STORAGE_ENGINE_BACKEND_SQL_H_
