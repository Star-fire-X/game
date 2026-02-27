#include "storage_engine/backends/postgres/postgres_database.h"

#include <limits>

#include <pqxx/pqxx>

namespace mir2::db {

PostgresDatabase::PostgresDatabase(std::shared_ptr<PgConnectionPool> pool, uint16_t worker_id)
    : pool_(pool)
    , id_generator_(worker_id)
    , initialized_(false) {
}

mir2::common::DbResult<void> PostgresDatabase::initialize() {
    if (!pool_ || !pool_->IsReady()) {
        return mir2::common::DbResult<void>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, "Connection pool not ready");
    }
    initialized_ = true;
    return mir2::common::DbResult<void>::ok();
}

void PostgresDatabase::close() {
    initialized_ = false;
}

bool PostgresDatabase::is_open() const {
    return initialized_ && pool_ && pool_->IsReady();
}

// 事务操作
mir2::common::DbResult<void> PostgresDatabase::begin_transaction() {
  if (!is_open()) {
    return mir2::common::DbResult<void>::error(
        mir2::common::ErrorCode::DATABASE_ERROR, "Database not open");
  }
  // Compatibility shim: current implementation scopes transaction lifetime to
  // each API call via pqxx::work and does not maintain cross-call state.
  return mir2::common::DbResult<void>::ok();
}

mir2::common::DbResult<void> PostgresDatabase::commit() {
  // Compatibility shim: commits happen inside each method when pqxx::work
  // commit() is called.
  return mir2::common::DbResult<void>::ok();
}

mir2::common::DbResult<void> PostgresDatabase::rollback() {
  // Compatibility shim: rollback is scoped to pqxx::work object lifetime.
  return mir2::common::DbResult<void>::ok();
}

// ID生成
mir2::common::DbResult<uint64_t> PostgresDatabase::generate_id(const std::string& seq_name) {
    (void)seq_name;
    try {
        return mir2::common::DbResult<uint64_t>::ok(id_generator_.next_id());
    } catch (const std::exception& ex) {
        return mir2::common::DbResult<uint64_t>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, ex.what());
    }
}

// 账号操作
mir2::common::DbResult<AccountData> PostgresDatabase::load_account(const std::string& username) {
    if (!is_open()) {
        return mir2::common::DbResult<AccountData>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, "Database not open");
    }

    try {
        auto conn = pool_->Acquire();
        if (!conn) {
            return mir2::common::DbResult<AccountData>::error(
                mir2::common::ErrorCode::DATABASE_ERROR, "Failed to acquire connection");
        }
        PgConnectionGuard guard(*pool_, conn);

        pqxx::work txn(*conn);
        pqxx::result result = txn.exec(
            "SELECT id, username, password_hash, email, "
            "EXTRACT(EPOCH FROM created_at)::BIGINT * 1000, "
            "EXTRACT(EPOCH FROM last_login)::BIGINT * 1000, banned "
            "FROM accounts WHERE username = $1",
            pqxx::params{username});

        if (result.empty()) {
            return mir2::common::DbResult<AccountData>::error(
                mir2::common::ErrorCode::ACCOUNT_NOT_FOUND, "Account not found");
        }

        AccountData account;
        const auto& row = result[0];
        account.id = row[0].as<uint64_t>();
        account.username = row[1].as<std::string>();
        account.password_hash = row[2].as<std::string>();
        account.email = row[3].as<std::string>("");
        account.created_at = row[4].as<int64_t>(0);
        account.last_login = row[5].as<int64_t>(0);
        account.banned = row[6].as<bool>(false);

        return mir2::common::DbResult<AccountData>::ok(account);
    } catch (const std::exception& ex) {
        return mir2::common::DbResult<AccountData>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, ex.what());
    }
}

mir2::common::DbResult<void> PostgresDatabase::create_account(const AccountData& account) {
    if (!is_open()) {
        return mir2::common::DbResult<void>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, "Database not open");
    }

    try {
        auto conn = pool_->Acquire();
        if (!conn) {
            return mir2::common::DbResult<void>::error(
                mir2::common::ErrorCode::DATABASE_ERROR, "Failed to acquire connection");
        }
        PgConnectionGuard guard(*pool_, conn);

        pqxx::work txn(*conn);
        txn.exec(
            "INSERT INTO accounts (username, password_hash, email) "
            "VALUES ($1, $2, $3)",
            pqxx::params{account.username, account.password_hash, account.email});
        txn.commit();

        return mir2::common::DbResult<void>::ok();
    } catch (const pqxx::unique_violation&) {
        return mir2::common::DbResult<void>::error(
            mir2::common::ErrorCode::ACCOUNT_ALREADY_EXISTS, "Account already exists");
    } catch (const std::exception& ex) {
        return mir2::common::DbResult<void>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, ex.what());
    }
}

// 批量保存 - 占位实现
mir2::common::DbResult<void> PostgresDatabase::save_equipment(uint32_t char_id,
    const std::vector<EquipmentSlotData>& equip) {
    if (!is_open()) {
        return mir2::common::DbResult<void>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, "Database not open");
    }

    try {
        auto conn = pool_->Acquire();
        if (!conn) {
            return mir2::common::DbResult<void>::error(
                mir2::common::ErrorCode::DATABASE_ERROR, "Failed to acquire connection");
        }
        PgConnectionGuard guard(*pool_, conn);

        pqxx::work txn(*conn);

        // 删除旧装备
        txn.exec(
            "DELETE FROM character_equipment WHERE character_id = $1",
            pqxx::params{char_id});

        // 插入新装备
        for (const auto& item : equip) {
            txn.exec(
                "INSERT INTO character_equipment (character_id, slot, item_template_id, "
                "instance_id, durability, enhancement_level) "
                "VALUES ($1, $2, $3, $4, $5, $6)",
                pqxx::params{char_id, static_cast<int>(item.slot), item.item_template_id,
                             item.instance_id, item.durability,
                             static_cast<int>(item.enhancement_level)});
        }

        txn.commit();
        return mir2::common::DbResult<void>::ok();
    } catch (const std::exception& ex) {
        return mir2::common::DbResult<void>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, ex.what());
    }
}

mir2::common::DbResult<void> PostgresDatabase::save_inventory(uint32_t char_id,
    const std::vector<InventorySlotData>& inv) {
    if (!is_open()) {
        return mir2::common::DbResult<void>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, "Database not open");
    }

    try {
        auto conn = pool_->Acquire();
        if (!conn) {
            return mir2::common::DbResult<void>::error(
                mir2::common::ErrorCode::DATABASE_ERROR, "Failed to acquire connection");
        }
        PgConnectionGuard guard(*pool_, conn);

        pqxx::work txn(*conn);

        // 删除旧背包
        txn.exec(
            "DELETE FROM character_inventory WHERE character_id = $1",
            pqxx::params{char_id});

        // 插入新背包
        for (const auto& item : inv) {
            txn.exec(
                "INSERT INTO character_inventory (character_id, slot, item_template_id, "
                "instance_id, quantity, durability, enhancement_level) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7)",
                pqxx::params{char_id, item.slot, item.item_template_id, item.instance_id,
                             item.quantity, item.durability,
                             static_cast<int>(item.enhancement_level)});
        }

        txn.commit();
        return mir2::common::DbResult<void>::ok();
    } catch (const std::exception& ex) {
        return mir2::common::DbResult<void>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, ex.what());
    }
}

mir2::common::DbResult<void> PostgresDatabase::save_skills(uint32_t char_id,
    const std::vector<CharacterSkillData>& skills) {
    if (!is_open()) {
        return mir2::common::DbResult<void>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, "Database not open");
    }

    try {
        auto conn = pool_->Acquire();
        if (!conn) {
            return mir2::common::DbResult<void>::error(
                mir2::common::ErrorCode::DATABASE_ERROR, "Failed to acquire connection");
        }
        PgConnectionGuard guard(*pool_, conn);

        pqxx::work txn(*conn);

        // 删除旧技能
        txn.exec(
            "DELETE FROM character_skills WHERE character_id = $1",
            pqxx::params{char_id});

        // 插入新技能
        for (const auto& skill : skills) {
            txn.exec(
                "INSERT INTO character_skills (character_id, skill_id, level, experience) "
                "VALUES ($1, $2, $3, $4)",
                pqxx::params{char_id, skill.skill_id, skill.level, skill.experience});
        }

        txn.commit();
        return mir2::common::DbResult<void>::ok();
    } catch (const std::exception& ex) {
        return mir2::common::DbResult<void>::error(
            mir2::common::ErrorCode::DATABASE_ERROR, ex.what());
    }
}

// 角色操作 - 占位实现
mir2::common::DbResult<void> PostgresDatabase::save_character(const mir2::common::CharacterData& data) {
    static_cast<void>(data);
    return mir2::common::DbResult<void>::error(
        mir2::common::ErrorCode::NOT_IMPLEMENTED, "PostgreSQL save_character not yet implemented");
}

mir2::common::DbResult<mir2::common::CharacterData> PostgresDatabase::load_character(uint32_t character_id) {
    static_cast<void>(character_id);
    return mir2::common::DbResult<mir2::common::CharacterData>::error(
        mir2::common::ErrorCode::NOT_IMPLEMENTED, "PostgreSQL load_character not yet implemented");
}

mir2::common::DbResult<mir2::common::CharacterData> PostgresDatabase::load_character_by_name(
    const std::string& name) {
    static_cast<void>(name);
    return mir2::common::DbResult<mir2::common::CharacterData>::error(
        mir2::common::ErrorCode::NOT_IMPLEMENTED, "PostgreSQL load_character_by_name not yet implemented");
}

mir2::common::DbResult<std::vector<mir2::common::CharacterData>> PostgresDatabase::load_characters_by_account(
    uint64_t account_id) {
    static_cast<void>(account_id);
    return mir2::common::DbResult<std::vector<mir2::common::CharacterData>>::error(
        mir2::common::ErrorCode::NOT_IMPLEMENTED, "PostgreSQL load_characters_by_account not yet implemented");
}

mir2::common::DbResult<void> PostgresDatabase::delete_character(uint32_t character_id) {
    static_cast<void>(character_id);
    return mir2::common::DbResult<void>::error(
        mir2::common::ErrorCode::NOT_IMPLEMENTED, "PostgreSQL delete_character not yet implemented");
}

mir2::common::DbResult<bool> PostgresDatabase::character_name_exists(const std::string& name) {
    static_cast<void>(name);
    return mir2::common::DbResult<bool>::error(
        mir2::common::ErrorCode::NOT_IMPLEMENTED, "PostgreSQL character_name_exists not yet implemented");
}

mir2::common::DbResult<uint32_t> PostgresDatabase::get_next_character_id() {
    // Prefer PostgreSQL sequence when database is available so this API remains
    // a usable uint32 allocator under normal runtime.
    if (is_open()) {
        try {
            auto conn = pool_->Acquire();
            if (!conn) {
                return mir2::common::DbResult<uint32_t>::error(
                    mir2::common::ErrorCode::DATABASE_ERROR,
                    "Failed to acquire connection");
            }
            PgConnectionGuard guard(*pool_, conn);
            pqxx::work txn(*conn);
            const pqxx::result seq_result = txn.exec(
                "SELECT nextval(pg_get_serial_sequence('characters', 'id'))");
            txn.commit();

            if (!seq_result.empty()) {
                const uint64_t next_id =
                    static_cast<uint64_t>(seq_result[0][0].as<int64_t>(0));
                if (next_id == 0 ||
                    next_id > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
                    return mir2::common::DbResult<uint32_t>::error(
                        mir2::common::ErrorCode::DATABASE_ERROR,
                        "character id sequence exceeds uint32_t range");
                }
                return mir2::common::DbResult<uint32_t>::ok(
                    static_cast<uint32_t>(next_id));
            }
        } catch (const std::exception&) {
            // Fall through to compatibility generator below.
        }
    }

    // Compatibility fallback for non-open DB paths and tests.
    auto result = generate_id("character");
    if (!result) {
        return mir2::common::DbResult<uint32_t>::error(
            result.error_code, result.error_message);
    }
    uint32_t fallback_id = static_cast<uint32_t>(result.value);
    if (fallback_id == 0) {
        fallback_id = 1;
    }
    return mir2::common::DbResult<uint32_t>::ok(fallback_id);
}

} // namespace mir2::db
