#include <gtest/gtest.h>

#include <string>

#include "storage_engine/backends/storage_engine_backend_sql.h"

namespace mir2::db {
namespace {

TEST(StorageEngineBackendSqlTest, UpsertClearsDeletedFlagOnWrite) {
  const std::string sql(storage_engine_sql::kUpsertSql);
  EXPECT_NE(sql.find("is_deleted"), std::string::npos);
  EXPECT_NE(sql.find("is_deleted = FALSE"), std::string::npos);
}

TEST(StorageEngineBackendSqlTest, SoftDeleteUsesLogicalDeleteStatement) {
  const std::string sql(storage_engine_sql::kSoftDeleteSql);
  EXPECT_NE(sql.find("decode('', 'hex')"), std::string::npos);
  EXPECT_NE(sql.find("is_deleted = TRUE"), std::string::npos);
}

TEST(StorageEngineBackendSqlTest, SoftDeleteUpsertsTombstoneForMissingKey) {
  const std::string sql(storage_engine_sql::kSoftDeleteSql);
  EXPECT_NE(sql.find("INSERT INTO kv_store"), std::string::npos);
  EXPECT_NE(sql.find("ON CONFLICT (key) DO UPDATE"), std::string::npos);
}

TEST(StorageEngineBackendSqlTest, LoadQueriesFilterDeletedRows) {
  const std::string load_sql(storage_engine_sql::kLoadSql);
  const std::string load_all_sql(storage_engine_sql::kLoadAllSql);
  EXPECT_NE(load_sql.find("is_deleted = FALSE"), std::string::npos);
  EXPECT_NE(load_all_sql.find("is_deleted = FALSE"), std::string::npos);
}

}  // namespace
}  // namespace mir2::db
