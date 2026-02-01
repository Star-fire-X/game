#include "db/character_repository.h"

#include <spdlog/spdlog.h>
#include <utility>

namespace mir2::db {

CharacterRepository::CharacterRepository(std::shared_ptr<mir2::common::IDatabase> db,
                                         std::shared_ptr<RedisCache> cache)
    : db_(std::move(db))
    , cache_(std::move(cache))
    , last_flush_(std::chrono::steady_clock::now()) {
}

// 角色操作（Cache-First）
mir2::common::DbResult<mir2::common::CharacterData> CharacterRepository::load_character(uint32_t char_id) {
    // 1. 尝试从缓存加载
    if (cache_ && cache_->is_ready()) {
        mir2::common::CharacterData data;
        if (cache_->get_character(char_id, data)) {
            spdlog::debug("Character {} loaded from cache", char_id);
            return mir2::common::DbResult<mir2::common::CharacterData>::ok(data);
        }
    }

    // 2. 缓存未命中，从数据库加载
    auto result = db_->load_character(char_id);
    if (!result) {
        return result;
    }

    // 3. 回填缓存
    if (cache_ && cache_->is_ready()) {
        cache_->cache_character(result.value);
    }

    spdlog::debug("Character {} loaded from database", char_id);
    return result;
}

mir2::common::DbResult<void> CharacterRepository::save_character(const mir2::common::CharacterData& data) {
    if (cache_ && cache_->is_ready()) {
        if (!cache_->cache_character(data)) {
            spdlog::warn("Failed to cache character {}", data.id);
        }
        mark_dirty(data.id);
        return mir2::common::DbResult<void>::ok();
    }

    auto result = db_->save_character(data);
    if (!result) {
        spdlog::error("Failed to save character {}: {}", data.id, result.error_message);
    }
    return result;
}

mir2::common::DbResult<void> CharacterRepository::delete_character(uint32_t char_id) {
    if (cache_ && cache_->is_ready()) {
        cache_->delete_character(char_id);
    }
    dirty_characters_.erase(char_id);
    return db_->delete_character(char_id);
}

// 装备操作
mir2::common::DbResult<std::vector<EquipmentSlotData>> CharacterRepository::load_equipment(uint32_t char_id) {
    // 1. 尝试从缓存加载
    if (cache_ && cache_->is_ready()) {
        std::vector<EquipmentSlotData> equip;
        if (cache_->get_equipment(char_id, equip)) {
            return mir2::common::DbResult<std::vector<EquipmentSlotData>>::ok(equip);
        }
    }

    // 2. 从数据库加载（占位）
    return mir2::common::DbResult<std::vector<EquipmentSlotData>>::error(
        mir2::common::ErrorCode::NOT_IMPLEMENTED, "Load equipment from DB not implemented");
}

mir2::common::DbResult<void> CharacterRepository::save_equipment(uint32_t char_id,
                                                            const std::vector<EquipmentSlotData>& equip) {
    if (cache_ && cache_->is_ready()) {
        if (!cache_->cache_equipment(char_id, equip)) {
            spdlog::warn("Failed to cache equipment for character {}", char_id);
        }
        mark_dirty(char_id);
        return mir2::common::DbResult<void>::ok();
    }

    return db_->save_equipment(char_id, equip);
}

// 背包操作
mir2::common::DbResult<std::vector<InventorySlotData>> CharacterRepository::load_inventory(uint32_t char_id) {
    // 1. 尝试从缓存加载
    if (cache_ && cache_->is_ready()) {
        std::vector<InventorySlotData> inv;
        if (cache_->get_inventory(char_id, inv)) {
            return mir2::common::DbResult<std::vector<InventorySlotData>>::ok(inv);
        }
    }

    // 2. 从数据库加载（占位）
    return mir2::common::DbResult<std::vector<InventorySlotData>>::error(
        mir2::common::ErrorCode::NOT_IMPLEMENTED, "Load inventory from DB not implemented");
}

mir2::common::DbResult<void> CharacterRepository::save_inventory(uint32_t char_id,
                                                            const std::vector<InventorySlotData>& inv) {
    if (cache_ && cache_->is_ready()) {
        if (!cache_->cache_inventory(char_id, inv)) {
            spdlog::warn("Failed to cache inventory for character {}", char_id);
        }
        mark_dirty(char_id);
        return mir2::common::DbResult<void>::ok();
    }

    return db_->save_inventory(char_id, inv);
}

// 批量保存（事务性）
mir2::common::DbResult<void> CharacterRepository::save_character_full(
    const mir2::common::CharacterData& data,
    const std::vector<EquipmentSlotData>& equip,
    const std::vector<InventorySlotData>& inv) {

    // 1. 开始事务
    auto begin_result = db_->begin_transaction();
    if (!begin_result) {
        return begin_result;
    }

    // 2. 保存角色数据
    auto char_result = db_->save_character(data);
    if (!char_result) {
        db_->rollback();
        return char_result;
    }

    // 3. 保存装备
    auto equip_result = db_->save_equipment(data.id, equip);
    if (!equip_result) {
        db_->rollback();
        return equip_result;
    }

    // 4. 保存背包
    auto inv_result = db_->save_inventory(data.id, inv);
    if (!inv_result) {
        db_->rollback();
        return inv_result;
    }

    // 5. 提交事务
    auto commit_result = db_->commit();
    if (!commit_result) {
        db_->rollback();
        return commit_result;
    }

    // 6. 更新缓存
    if (cache_ && cache_->is_ready()) {
        cache_->cache_character(data);
        cache_->cache_equipment(data.id, equip);
        cache_->cache_inventory(data.id, inv);
    }

    spdlog::info("Character {} saved with transaction", data.id);
    return mir2::common::DbResult<void>::ok();
}

// 缓存管理
void CharacterRepository::mark_dirty(uint32_t char_id) {
    dirty_characters_.insert(char_id);
}

void CharacterRepository::flush_dirty_characters() {
    if (dirty_characters_.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - last_flush_ < flush_interval_) {
        return;
    }
    last_flush_ = now;

    if (!cache_ || !cache_->is_ready()) {
        spdlog::warn("Redis cache not ready, skip dirty flush");
        return;
    }

    for (auto it = dirty_characters_.begin(); it != dirty_characters_.end(); ) {
        uint32_t char_id = *it;
        mir2::common::CharacterData data;
        std::vector<EquipmentSlotData> equip;
        std::vector<InventorySlotData> inv;

        if (!cache_->get_character(char_id, data) ||
            !cache_->get_equipment(char_id, equip) ||
            !cache_->get_inventory(char_id, inv)) {
            spdlog::warn("Cache miss while flushing character {}", char_id);
            ++it;
            continue;
        }

        auto result = save_character_full(data, equip, inv);
        if (!result) {
            spdlog::error("Failed to flush character {}: {}", char_id, result.error_message);
            ++it;
            continue;
        }

        it = dirty_characters_.erase(it);
    }
}

} // namespace mir2::db
