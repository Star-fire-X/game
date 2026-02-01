#include <gtest/gtest.h>

#include <optional>
#include <unordered_map>

#include "common/database.h"
#include "db/character_repository.h"
#include "db/redis_manager.h"
#include "server/config/config_manager.h"
#include "server/db/redis_test_utils.h"

using mir2::common::CharacterClass;
using mir2::common::CharacterData;
using mir2::common::DbResult;
using mir2::common::ErrorCode;
using mir2::common::IDatabase;
using namespace mir2::db;
using namespace mir2::db::test_utils;

namespace {

CharacterData make_character(uint32_t id, const std::string& name) {
    CharacterData data;
    data.id = id;
    data.account_id = "test_account";
    data.name = name;
    data.char_class = CharacterClass::WARRIOR;
    data.stats.level = 10;
    data.stats.hp = 120;
    data.stats.max_hp = 120;
    data.stats.mp = 60;
    data.stats.max_mp = 60;
    data.stats.attack = 20;
    data.stats.defense = 15;
    data.stats.magic_attack = 8;
    data.stats.magic_defense = 6;
    data.stats.speed = 12;
    data.stats.gold = 500;
    return data;
}

std::shared_ptr<RedisManager> make_manager(uint16_t port) {
    auto manager = std::make_shared<RedisManager>();
    mir2::config::RedisConfig config;
    config.host = "127.0.0.1";
    config.port = port;
    config.password = "";
    config.db = 0;
    if (!manager->Initialize(config)) {
        return nullptr;
    }
    return manager;
}

class InMemoryDatabase : public IDatabase {
public:
    DbResult<void> initialize() override {
        open_ = true;
        return DbResult<void>::ok();
    }

    void close() override { open_ = false; }
    bool is_open() const override { return open_; }

    DbResult<void> save_character(const CharacterData& data) override {
        if (!open_) {
            return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        characters_[data.id] = data;
        return DbResult<void>::ok();
    }

    DbResult<CharacterData> load_character(uint32_t character_id) override {
        if (!open_) {
            return DbResult<CharacterData>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        auto it = characters_.find(character_id);
        if (it == characters_.end()) {
            return DbResult<CharacterData>::error(ErrorCode::CHARACTER_NOT_FOUND, "Not found");
        }
        return DbResult<CharacterData>::ok(it->second);
    }

    DbResult<CharacterData> load_character_by_name(const std::string& name) override {
        if (!open_) {
            return DbResult<CharacterData>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        for (const auto& [id, data] : characters_) {
            if (data.name == name) {
                return DbResult<CharacterData>::ok(data);
            }
        }
        return DbResult<CharacterData>::error(ErrorCode::CHARACTER_NOT_FOUND, "Not found");
    }

    DbResult<std::vector<CharacterData>> load_characters_by_account(
        const std::string& account_id) override {
        if (!open_) {
            return DbResult<std::vector<CharacterData>>::error(
                ErrorCode::DATABASE_ERROR, "Database not open");
        }
        std::vector<CharacterData> result;
        for (const auto& [id, data] : characters_) {
            if (data.account_id == account_id) {
                result.push_back(data);
            }
        }
        return DbResult<std::vector<CharacterData>>::ok(result);
    }

    DbResult<void> delete_character(uint32_t character_id) override {
        if (!open_) {
            return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        characters_.erase(character_id);
        return DbResult<void>::ok();
    }

    DbResult<bool> character_name_exists(const std::string& name) override {
        if (!open_) {
            return DbResult<bool>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        for (const auto& [id, data] : characters_) {
            if (data.name == name) {
                return DbResult<bool>::ok(true);
            }
        }
        return DbResult<bool>::ok(false);
    }

    DbResult<uint32_t> get_next_character_id() override {
        if (!open_) {
            return DbResult<uint32_t>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        uint32_t max_id = 0;
        for (const auto& [id, data] : characters_) {
            max_id = std::max(max_id, id);
        }
        return DbResult<uint32_t>::ok(max_id + 1);
    }

    DbResult<void> begin_transaction() override {
        if (!open_) {
            return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        if (in_transaction_) {
            return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Transaction already open");
        }
        backup_ = Backup{characters_, equipment_, inventory_, skills_};
        in_transaction_ = true;
        return DbResult<void>::ok();
    }

    DbResult<void> commit() override {
        if (!open_) {
            return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        backup_.reset();
        in_transaction_ = false;
        return DbResult<void>::ok();
    }

    DbResult<void> rollback() override {
        if (!open_) {
            return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        if (backup_) {
            characters_ = backup_->characters;
            equipment_ = backup_->equipment;
            inventory_ = backup_->inventory;
            skills_ = backup_->skills;
        }
        backup_.reset();
        in_transaction_ = false;
        return DbResult<void>::ok();
    }

    DbResult<void> save_equipment(uint32_t char_id,
                                  const std::vector<EquipmentSlotData>& equip) override {
        if (!open_) {
            return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        equipment_[char_id] = equip;
        return DbResult<void>::ok();
    }

    DbResult<void> save_inventory(uint32_t char_id,
                                  const std::vector<InventorySlotData>& inv) override {
        if (!open_) {
            return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        inventory_[char_id] = inv;
        return DbResult<void>::ok();
    }

    DbResult<void> save_skills(uint32_t char_id,
                               const std::vector<CharacterSkillData>& skills) override {
        if (!open_) {
            return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        skills_[char_id] = skills;
        return DbResult<void>::ok();
    }

    DbResult<AccountData> load_account(const std::string& username) override {
        if (!open_) {
            return DbResult<AccountData>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        auto it = accounts_.find(username);
        if (it == accounts_.end()) {
            return DbResult<AccountData>::error(ErrorCode::ACCOUNT_NOT_FOUND, "Not found");
        }
        return DbResult<AccountData>::ok(it->second);
    }

    DbResult<void> create_account(const AccountData& account) override {
        if (!open_) {
            return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        if (accounts_.count(account.username)) {
            return DbResult<void>::error(ErrorCode::ACCOUNT_ALREADY_EXISTS, "Exists");
        }
        accounts_[account.username] = account;
        return DbResult<void>::ok();
    }

    DbResult<uint64_t> generate_id(const std::string& seq_name) override {
        static_cast<void>(seq_name);
        if (!open_) {
            return DbResult<uint64_t>::error(ErrorCode::DATABASE_ERROR, "Database not open");
        }
        return DbResult<uint64_t>::ok(next_id_++);
    }

private:
    struct Backup {
        std::unordered_map<uint32_t, CharacterData> characters;
        std::unordered_map<uint32_t, std::vector<EquipmentSlotData>> equipment;
        std::unordered_map<uint32_t, std::vector<InventorySlotData>> inventory;
        std::unordered_map<uint32_t, std::vector<CharacterSkillData>> skills;
    };

    bool open_ = false;
    bool in_transaction_ = false;
    uint64_t next_id_ = 1;
    std::optional<Backup> backup_;
    std::unordered_map<uint32_t, CharacterData> characters_;
    std::unordered_map<uint32_t, std::vector<EquipmentSlotData>> equipment_;
    std::unordered_map<uint32_t, std::vector<InventorySlotData>> inventory_;
    std::unordered_map<uint32_t, std::vector<CharacterSkillData>> skills_;
    std::unordered_map<std::string, AccountData> accounts_;
};

class FaultyDatabase : public InMemoryDatabase {
public:
    DbResult<void> save_equipment(uint32_t char_id,
                                  const std::vector<EquipmentSlotData>& equip) override {
        static_cast<void>(char_id);
        static_cast<void>(equip);
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Equipment failure");
    }
};

}  // namespace

TEST(DbIntegrationTest, RepositoryCacheDatabaseFlow) {
    auto server = std::make_unique<FakeRedisServer>();
    ASSERT_TRUE(server->is_ready());

    auto manager = make_manager(server->port());
    ASSERT_TRUE(manager);
    auto cache = std::make_shared<RedisCache>(manager);

    auto db = std::make_shared<InMemoryDatabase>();
    ASSERT_TRUE(db->initialize());

    CharacterRepository repo(db, cache);

    auto data = make_character(1, "Integration");
    std::vector<EquipmentSlotData> equip;
    std::vector<InventorySlotData> inv;

    auto result = repo.save_character_full(data, equip, inv);
    ASSERT_TRUE(result);

    auto db_loaded = db->load_character(1);
    ASSERT_TRUE(db_loaded);
    EXPECT_EQ(db_loaded.value.name, "Integration");

    CharacterData cached;
    EXPECT_TRUE(cache->get_character(1, cached));
    EXPECT_EQ(cached.name, "Integration");

    manager->Shutdown();
}

TEST(DbIntegrationTest, TransactionRollbackOnFailure) {
    auto server = std::make_unique<FakeRedisServer>();
    ASSERT_TRUE(server->is_ready());

    auto manager = make_manager(server->port());
    ASSERT_TRUE(manager);
    auto cache = std::make_shared<RedisCache>(manager);

    auto db = std::make_shared<FaultyDatabase>();
    ASSERT_TRUE(db->initialize());

    CharacterRepository repo(db, cache);

    auto data = make_character(2, "Rollback");
    std::vector<EquipmentSlotData> equip;
    std::vector<InventorySlotData> inv;

    auto result = repo.save_character_full(data, equip, inv);
    EXPECT_FALSE(result);

    auto db_loaded = db->load_character(2);
    EXPECT_FALSE(db_loaded);

    manager->Shutdown();
}
