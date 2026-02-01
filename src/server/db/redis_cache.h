#ifndef MIR2_DB_REDIS_CACHE_H
#define MIR2_DB_REDIS_CACHE_H

#include "db/redis_manager.h"
#include "common/character_data.h"
#include "common/types/database_types.h"
#include <memory>
#include <string>
#include <vector>

namespace mir2::db {

/**
 * @brief Redis缓存层
 *
 * 缓存策略：
 * - session:{account_id} - HASH, TTL 30min
 * - char:{id} - HASH, 无TTL（在线角色）
 * - char:{id}:equip - HASH, 无TTL
 * - char:{id}:inv - HASH, 无TTL
 * - map:{id}:players - SET, 无TTL
 */
class RedisCache {
public:
    explicit RedisCache(std::shared_ptr<RedisManager> redis);
    ~RedisCache() = default;

    // 会话管理
    bool set_session(const std::string& account_id, const std::string& session_token,
        int ttl_seconds = 1800);
    bool get_session(const std::string& account_id, std::string& session_token);
    bool delete_session(const std::string& account_id);

    // 角色缓存
    bool cache_character(const mir2::common::CharacterData& data);
    bool get_character(uint32_t char_id, mir2::common::CharacterData& data);
    bool delete_character(uint32_t char_id);

    // 装备缓存
    bool cache_equipment(uint32_t char_id, const std::vector<EquipmentSlotData>& equip);
    bool get_equipment(uint32_t char_id, std::vector<EquipmentSlotData>& equip);

    // 背包缓存
    bool cache_inventory(uint32_t char_id, const std::vector<InventorySlotData>& inv);
    bool get_inventory(uint32_t char_id, std::vector<InventorySlotData>& inv);

    // 地图玩家列表
    bool add_player_to_map(uint32_t map_id, uint32_t char_id);
    bool remove_player_from_map(uint32_t map_id, uint32_t char_id);
    std::vector<uint32_t> get_map_players(uint32_t map_id);

    // 工具方法
    bool is_ready() const { return redis_ && redis_->IsReady(); }

private:
    std::shared_ptr<RedisManager> redis_;

    // 序列化辅助方法
    std::string serialize_character(const mir2::common::CharacterData& data);
    mir2::common::CharacterData deserialize_character(const std::string& json);
    std::string serialize_equipment(const std::vector<EquipmentSlotData>& equip);
    std::vector<EquipmentSlotData> deserialize_equipment(const std::string& json);
    std::string serialize_inventory(const std::vector<InventorySlotData>& inv);
    std::vector<InventorySlotData> deserialize_inventory(const std::string& json);
};

} // namespace mir2::db

#endif
