#include "db/redis_cache.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace mir2::db {

RedisCache::RedisCache(std::shared_ptr<RedisManager> redis)
    : redis_(redis) {
}

// 会话管理
bool RedisCache::set_session(const std::string& account_id,
                             const std::string& session_token,
                             int ttl_seconds) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "session:" + account_id;
    std::string cmd = "SETEX " + key + " " + std::to_string(ttl_seconds) + " " + session_token;

    auto reply = redis_->Execute(cmd);
    return reply && reply->type == REDIS_REPLY_STATUS;
}

bool RedisCache::get_session(const std::string& account_id, std::string& session_token) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "session:" + account_id;
    std::string cmd = "GET " + key;

    auto reply = redis_->Execute(cmd);
    if (reply && reply->type == REDIS_REPLY_STRING) {
        session_token = reply->str;
        return true;
    }
    return false;
}

bool RedisCache::delete_session(const std::string& account_id) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "session:" + account_id;
    std::string cmd = "DEL " + key;

    auto reply = redis_->Execute(cmd);
    return reply && reply->type == REDIS_REPLY_INTEGER;
}

// 角色缓存
bool RedisCache::cache_character(const mir2::common::CharacterData& data) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "char:" + std::to_string(data.id);
    std::string value = serialize_character(data);
    std::string cmd = "SET " + key + " " + value;

    auto reply = redis_->Execute(cmd);
    return reply && reply->type == REDIS_REPLY_STATUS;
}

bool RedisCache::get_character(uint32_t char_id, mir2::common::CharacterData& data) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "char:" + std::to_string(char_id);
    std::string cmd = "GET " + key;

    auto reply = redis_->Execute(cmd);
    if (reply && reply->type == REDIS_REPLY_STRING) {
        try {
            data = deserialize_character(reply->str);
            return true;
        } catch (const std::exception& ex) {
            spdlog::error("Failed to deserialize character: {}", ex.what());
        }
    }
    return false;
}

bool RedisCache::delete_character(uint32_t char_id) {
    if (!is_ready()) {
        return false;
    }

    std::string char_key = "char:" + std::to_string(char_id);
    std::string equip_key = "char:" + std::to_string(char_id) + ":equip";
    std::string inv_key = "char:" + std::to_string(char_id) + ":inv";

    std::string cmd = "DEL " + char_key + " " + equip_key + " " + inv_key;
    auto reply = redis_->Execute(cmd);
    return reply && reply->type == REDIS_REPLY_INTEGER;
}

// 装备缓存
bool RedisCache::cache_equipment(uint32_t char_id, const std::vector<EquipmentSlotData>& equip) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "char:" + std::to_string(char_id) + ":equip";
    std::string value = serialize_equipment(equip);
    std::string cmd = "SET " + key + " " + value;

    auto reply = redis_->Execute(cmd);
    return reply && reply->type == REDIS_REPLY_STATUS;
}

bool RedisCache::get_equipment(uint32_t char_id, std::vector<EquipmentSlotData>& equip) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "char:" + std::to_string(char_id) + ":equip";
    std::string cmd = "GET " + key;

    auto reply = redis_->Execute(cmd);
    if (reply && reply->type == REDIS_REPLY_STRING) {
        try {
            equip = deserialize_equipment(reply->str);
            return true;
        } catch (const std::exception& ex) {
            spdlog::error("Failed to deserialize equipment: {}", ex.what());
        }
    }
    return false;
}

// 背包缓存
bool RedisCache::cache_inventory(uint32_t char_id, const std::vector<InventorySlotData>& inv) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "char:" + std::to_string(char_id) + ":inv";
    std::string value = serialize_inventory(inv);
    std::string cmd = "SET " + key + " " + value;

    auto reply = redis_->Execute(cmd);
    return reply && reply->type == REDIS_REPLY_STATUS;
}

bool RedisCache::get_inventory(uint32_t char_id, std::vector<InventorySlotData>& inv) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "char:" + std::to_string(char_id) + ":inv";
    std::string cmd = "GET " + key;

    auto reply = redis_->Execute(cmd);
    if (reply && reply->type == REDIS_REPLY_STRING) {
        try {
            inv = deserialize_inventory(reply->str);
            return true;
        } catch (const std::exception& ex) {
            spdlog::error("Failed to deserialize inventory: {}", ex.what());
        }
    }
    return false;
}

// 地图玩家列表
bool RedisCache::add_player_to_map(uint32_t map_id, uint32_t char_id) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "map:" + std::to_string(map_id) + ":players";
    std::string cmd = "SADD " + key + " " + std::to_string(char_id);

    auto reply = redis_->Execute(cmd);
    return reply && reply->type == REDIS_REPLY_INTEGER;
}

bool RedisCache::remove_player_from_map(uint32_t map_id, uint32_t char_id) {
    if (!is_ready()) {
        return false;
    }

    std::string key = "map:" + std::to_string(map_id) + ":players";
    std::string cmd = "SREM " + key + " " + std::to_string(char_id);

    auto reply = redis_->Execute(cmd);
    return reply && reply->type == REDIS_REPLY_INTEGER;
}

std::vector<uint32_t> RedisCache::get_map_players(uint32_t map_id) {
    std::vector<uint32_t> players;
    if (!is_ready()) {
        return players;
    }

    std::string key = "map:" + std::to_string(map_id) + ":players";
    std::string cmd = "SMEMBERS " + key;

    auto reply = redis_->Execute(cmd);
    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i]->type == REDIS_REPLY_STRING) {
                players.push_back(std::stoul(reply->element[i]->str));
            }
        }
    }
    return players;
}

// 序列化辅助方法
std::string RedisCache::serialize_character(const mir2::common::CharacterData& data) {
    return data.serialize();
}

mir2::common::CharacterData RedisCache::deserialize_character(const std::string& json_str) {
    return mir2::common::CharacterData::deserialize(json_str);
}

std::string RedisCache::serialize_equipment(const std::vector<EquipmentSlotData>& equip) {
    json j = json::array();
    for (const auto& item : equip) {
        j.push_back({
            {"slot", item.slot},
            {"item_template_id", item.item_template_id},
            {"instance_id", item.instance_id},
            {"durability", item.durability},
            {"enhancement_level", item.enhancement_level}
        });
    }
    return j.dump();
}

std::vector<EquipmentSlotData> RedisCache::deserialize_equipment(const std::string& json_str) {
    std::vector<EquipmentSlotData> equip;
    json j = json::parse(json_str);
    for (const auto& item : j) {
        EquipmentSlotData data;
        data.slot = item["slot"];
        data.item_template_id = item["item_template_id"];
        data.instance_id = item["instance_id"];
        data.durability = item["durability"];
        data.enhancement_level = item["enhancement_level"];
        equip.push_back(data);
    }
    return equip;
}

std::string RedisCache::serialize_inventory(const std::vector<InventorySlotData>& inv) {
    json j = json::array();
    for (const auto& item : inv) {
        j.push_back({
            {"slot", item.slot},
            {"item_template_id", item.item_template_id},
            {"instance_id", item.instance_id},
            {"quantity", item.quantity},
            {"durability", item.durability},
            {"enhancement_level", item.enhancement_level}
        });
    }
    return j.dump();
}

std::vector<InventorySlotData> RedisCache::deserialize_inventory(const std::string& json_str) {
    std::vector<InventorySlotData> inv;
    json j = json::parse(json_str);
    for (const auto& item : j) {
        InventorySlotData data;
        data.slot = item["slot"];
        data.item_template_id = item["item_template_id"];
        data.instance_id = item["instance_id"];
        data.quantity = item["quantity"];
        data.durability = item["durability"];
        data.enhancement_level = item["enhancement_level"];
        inv.push_back(data);
    }
    return inv;
}

} // namespace mir2::db
