#ifndef MIR2_DB_CHARACTER_REPOSITORY_H
#define MIR2_DB_CHARACTER_REPOSITORY_H

#include "common/database.h"
#include "common/character_data.h"
#include "common/types/database_types.h"
#include "db/redis_cache.h"
#include <chrono>
#include <memory>
#include <unordered_set>

namespace mir2::db {

/**
 * @brief 角色仓储层
 *
 * 封装缓存+数据库的业务逻辑：
 * - Cache-First读取策略
 * - Write-Behind写入策略（30秒批量写DB）
 * - 事务性保存
 */
class CharacterRepository {
public:
    CharacterRepository(std::shared_ptr<mir2::common::IDatabase> db,
                        std::shared_ptr<RedisCache> cache);
    ~CharacterRepository() = default;

    // 角色操作（带缓存）
    mir2::common::DbResult<mir2::common::CharacterData> load_character(uint32_t char_id);
    mir2::common::DbResult<void> save_character(const mir2::common::CharacterData& data);
    mir2::common::DbResult<void> delete_character(uint32_t char_id);

    // 装备操作
    mir2::common::DbResult<std::vector<EquipmentSlotData>> load_equipment(uint32_t char_id);
    mir2::common::DbResult<void> save_equipment(uint32_t char_id,
                                           const std::vector<EquipmentSlotData>& equip);

    // 背包操作
    mir2::common::DbResult<std::vector<InventorySlotData>> load_inventory(uint32_t char_id);
    mir2::common::DbResult<void> save_inventory(uint32_t char_id,
                                           const std::vector<InventorySlotData>& inv);

    // 批量保存（事务性）
    mir2::common::DbResult<void> save_character_full(const mir2::common::CharacterData& data,
                                                const std::vector<EquipmentSlotData>& equip,
                                                const std::vector<InventorySlotData>& inv);

    // 缓存管理
    void mark_dirty(uint32_t char_id);
    void flush_dirty_characters();
    void set_flush_interval(std::chrono::seconds interval) { flush_interval_ = interval; }

private:
    std::shared_ptr<mir2::common::IDatabase> db_;
    std::shared_ptr<RedisCache> cache_;
    std::unordered_set<uint32_t> dirty_characters_;
    std::chrono::seconds flush_interval_{30};
    std::chrono::steady_clock::time_point last_flush_;
};

} // namespace mir2::db

#endif
