#ifndef MIR2_DB_POSTGRES_DATABASE_H
#define MIR2_DB_POSTGRES_DATABASE_H

#include "common/database.h"
#include "common/types/database_types.h"
#include "server/common/snowflake_id.h"
#include "db/pg_connection_pool.h"
#include <memory>

namespace mir2::db {

/**
 * @brief PostgreSQL数据库实现
 */
class PostgresDatabase : public mir2::common::IDatabase {
public:
    explicit PostgresDatabase(std::shared_ptr<PgConnectionPool> pool, uint16_t worker_id = 0);
    ~PostgresDatabase() override = default;

    // IDatabase基础接口
    mir2::common::DbResult<void> initialize() override;
    void close() override;
    bool is_open() const override;

    // 角色操作
    mir2::common::DbResult<void> save_character(const mir2::common::CharacterData& data) override;
    mir2::common::DbResult<mir2::common::CharacterData> load_character(uint32_t character_id) override;
    mir2::common::DbResult<mir2::common::CharacterData> load_character_by_name(const std::string& name) override;
    mir2::common::DbResult<std::vector<mir2::common::CharacterData>> load_characters_by_account(
        const std::string& account_id) override;
    mir2::common::DbResult<void> delete_character(uint32_t character_id) override;
    mir2::common::DbResult<bool> character_name_exists(const std::string& name) override;
    mir2::common::DbResult<uint32_t> get_next_character_id() override;

    // 事务操作
    mir2::common::DbResult<void> begin_transaction() override;
    mir2::common::DbResult<void> commit() override;
    mir2::common::DbResult<void> rollback() override;

    // 批量保存
    mir2::common::DbResult<void> save_equipment(uint32_t char_id,
        const std::vector<EquipmentSlotData>& equip) override;
    mir2::common::DbResult<void> save_inventory(uint32_t char_id,
        const std::vector<InventorySlotData>& inv) override;
    mir2::common::DbResult<void> save_skills(uint32_t char_id,
        const std::vector<CharacterSkillData>& skills) override;

    // 账号操作
    mir2::common::DbResult<AccountData> load_account(const std::string& username) override;
    mir2::common::DbResult<void> create_account(const AccountData& account) override;

    // ID生成
    mir2::common::DbResult<uint64_t> generate_id(const std::string& seq_name) override;

private:
    std::shared_ptr<PgConnectionPool> pool_;
    common::SnowflakeIdGenerator id_generator_;
    bool initialized_;
};

} // namespace mir2::db

#endif
