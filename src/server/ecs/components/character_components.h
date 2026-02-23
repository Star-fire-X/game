/**
 * @file character_components.h
 * @brief ECS 角色组件定义
 *
 * 合并角色身份、属性与状态组件，便于统一管理。
 */

#ifndef MIR2_SERVER_ECS_CHARACTER_COMPONENTS_H_
#define MIR2_SERVER_ECS_CHARACTER_COMPONENTS_H_

#include "ecs/id_types.h"

#include "common/types.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <climits>
#include <string>
#include <vector>

namespace mir2::ecs {

/**
 * @brief 角色身份组件
 */
struct CharacterIdentityComponent {
    CharacterId id = kInvalidCharacterId; ///< 角色唯一ID
    AccountId account_id = kInvalidAccountId;  ///< 账号ID（稳定数值ID）
    std::string name;                     ///< 角色名称
    mir2::common::CharacterClass char_class = mir2::common::CharacterClass::WARRIOR;  ///< 职业
    mir2::common::Gender gender = mir2::common::Gender::MALE;                         ///< 性别

    void SetAccountIdValue(AccountId value) {
        account_id = value;
    }

    AccountId ResolveAccountIdValue() const {
        return account_id;
    }
};

/**
 * @brief 角色属性组件
 */
struct CharacterAttributesComponent {
    int level = 1;            ///< 等级
    int64_t experience = 0;   ///< 当前经验值（int64支持高等级大经验值）
    int hp = 0;               ///< 当前HP
    int max_hp = 0;           ///< 最大HP
    int mp = 0;               ///< 当前MP
    int max_mp = 0;           ///< 最大MP
    int inc_health = 0;       ///< 渐进恢复累计HP
    int inc_spell = 0;        ///< 渐进恢复累计MP
    bool next_free_curse_item = false; ///< 下次脱装备可解除诅咒
    int attack = 0;           ///< 物理攻击
    int defense = 0;          ///< 物理防御
    int magic_attack = 0;     ///< 魔法攻击
    int magic_defense = 0;    ///< 魔法防御
    int speed = 0;            ///< 移动/攻速
    int gold = 0;             ///< 金币数量
    int luck = 0;             ///< 幸运值（装备幸运）
    int pk_level = 0;         ///< PK等级(>=2 红名)
    int hit_plus = 0;         ///< 力量加成
    uint8_t life_attrib = 0;  ///< 生命属性(0=普通,1=不死)
    int anti_poison = 0;      ///< 抗毒
    int anti_magic = 0;       ///< 反魔法
    int sc = 0;               ///< 精神力（道士）
    int body_luck = 0;        ///< BodyLuck 幸运值（升级+100，每tick衰减）
    int max_weight = 0;       ///< 最大负重
    int max_wear_weight = 0;  ///< 最大穿戴负重
    int max_hand_weight = 0;  ///< 最大腕力（手持物品重量上限）
    int bonus_remaining = 0;  ///< 可用奖励点数（未分配的）
};

/**
 * @brief 角色状态组件
 */
struct CharacterStateComponent {
    uint32_t map_id = 1;                 ///< 当前地图ID
    mir2::common::Position position = {100, 100}; ///< 当前位置
    mir2::common::Direction direction = mir2::common::Direction::DOWN; ///< 朝向
    bool is_in_activity = false;         ///< 是否在活动中
    int64_t created_at = 0;              ///< 创建时间戳
    int64_t last_login = 0;              ///< 最后登录时间戳
    int64_t last_active = 0;             ///< 最近活跃时间戳
    int64_t last_random_scroll_time_ms = 0; ///< 上次随机传送卷使用时间(ms)
    int64_t last_trade_close_time_ms = 0; ///< 交易窗口关闭时间(ms)
    bool is_online = false;              ///< 在线态（登录=true，断线=false）
};

/**
 * @brief 聊天偏好组件
 */
struct ChatPreferenceComponent {
    static constexpr std::size_t kMaxBlockCount = 10;

    bool hear_whisper = true;
    bool hear_cry = true;
    bool hear_guild_msg = true;
    std::vector<uint32_t> whisper_block_list;

    bool IsBlocked(uint32_t character_id) const {
        return std::find(whisper_block_list.begin(),
                         whisper_block_list.end(),
                         character_id) != whisper_block_list.end();
    }

    bool AddBlock(uint32_t character_id) {
        if (character_id == 0 || IsBlocked(character_id) ||
            whisper_block_list.size() >= kMaxBlockCount) {
            return false;
        }
        whisper_block_list.push_back(character_id);
        return true;
    }

    void RemoveBlock(uint32_t character_id) {
        whisper_block_list.erase(
            std::remove(whisper_block_list.begin(),
                        whisper_block_list.end(),
                        character_id),
            whisper_block_list.end());
    }
};

/**
 * @brief 角色脏标记组件
 */
struct DirtyComponent {
    bool identity_dirty = false;    ///< 身份数据变更
    bool attributes_dirty = false;  ///< 属性数据变更
    bool state_dirty = false;       ///< 状态数据变更
    bool items_dirty = false;       ///< 物品数据变更
    bool equipment_dirty = false;   ///< 装备数据变更
    bool skills_dirty = false;      ///< 技能数据变更
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_CHARACTER_COMPONENTS_H_
