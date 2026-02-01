/**
 * @file enums.h
 * @brief 公共枚举定义
 */

#ifndef MIR2_COMMON_ENUMS_H
#define MIR2_COMMON_ENUMS_H

#include <cstdint>

namespace mir2::common {

/**
 * @brief 消息ID
 */
enum class MsgId : uint16_t {
  kNone = 0,

  // ========== 登录模块 (1000-1999) ==========
  kLoginReq = 1001,
  kLoginRsp = 1002,
  kLogout = 1003,
  kCreateRoleReq = 1010,
  kCreateRoleRsp = 1011,
  kSelectRoleReq = 1012,
  kSelectRoleRsp = 1013,
  kRoleListReq = 1020,
  kRoleListRsp = 1021,
  kRoleUpdate = 1022,

  // ========== 游戏模块 (2000-2999) ==========
  kEnterGameRsp = 2001,
  kMoveReq = 2010,
  kMoveRsp = 2011,
  kEntityEnter = 2020,
  kEntityLeave = 2021,
  kEntityMove = 2022,
  kEntityUpdate = 2023,
  kChangeMap = 2030,
  kTeleport = 2031,
  kStateSync = 2040,

  // ========== 战斗模块 (3000-3999) ==========
  kAttackReq = 3001,
  kAttackRsp = 3002,
  kSkillReq = 3010,
  kSkillRsp = 3011,
  kDamage = 3020,
  kDeath = 3021,
  kRespawn = 3022,
  kBuffAdd = 3030,
  kBuffRemove = 3031,
  kSkillEffect = 3040,
  kPlayEffect = 3041,
  kPlaySound = 3042,

  // ========== 物品模块 (4000-4999) ==========
  kInventoryUpdate = 4001,
  kUseItemReq = 4010,
  kUseItemRsp = 4011,
  kDropItemReq = 4020,
  kDropItemRsp = 4021,
  kPickupItemReq = 4030,
  kPickupItemRsp = 4031,
  kEquipReq = 4040,
  kUnequipReq = 4041,
  kEquipRsp = 4042,
  kUnequipRsp = 4043,

  // ========== 社交模块 (5000-5999) ==========
  kChatReq = 5001,
  kChatRsp = 5002,
  kPrivateChat = 5010,
  kGuildChat = 5020,

  // ========== NPC模块 (6000-6999) ==========
  kNpcInteractReq = 6001,   // 玩家点击NPC请求
  kNpcInteractRsp = 6002,   // NPC交互响应
  kNpcDialogShow = 6010,    // 显示NPC对话内容
  kNpcMenuSelect = 6011,    // 玩家选择对话菜单
  kNpcShopOpen = 6020,      // 打开NPC商店
  kNpcShopClose = 6021,     // 关闭NPC商店
  kNpcQuestAccept = 6030,   // 接受任务
  kNpcQuestComplete = 6031, // 完成任务

  // ========== 系统模块 (9000-9999) ==========
  kHeartbeat = 9001,
  kHeartbeatRsp = 9002,
  kKick = 9010,
  kServerNotice = 9020
};

/**
 * @brief 包标记
 */
enum class PacketFlags : uint8_t {
  kNone = 0x00,
  kEncrypted = 0x01,  // bit0: 加密
  kCompressed = 0x02  // bit1: 压缩
};

/**
 * @brief 角色职业类型
 */
enum class CharacterClass : uint8_t {
  WARRIOR = 0,
  MAGE = 1,
  TAOIST = 2
};

/**
 * @brief 角色性别
 */
enum class Gender : uint8_t {
  MALE = 0,
  FEMALE = 1
};

/**
 * @brief 物品类型
 */
enum class ItemType : uint8_t {
  WEAPON = 0,
  ARMOR = 1,
  HELMET = 2,
  BOOTS = 3,
  RING = 4,
  NECKLACE = 5,
  BRACELET = 6,
  BELT = 7,
  CONSUMABLE = 8,
  MATERIAL = 9,
  QUEST = 10
};

/**
 * @brief 装备槽位
 */
enum class EquipSlot : uint8_t {
  WEAPON = 0,
  ARMOR = 1,
  HELMET = 2,
  BOOTS = 3,
  RING_LEFT = 4,
  RING_RIGHT = 5,
  NECKLACE = 6,
  BRACELET_LEFT = 7,
  BRACELET_RIGHT = 8,
  BELT = 9,
  AMULET = 10,      // 护身符槽位 (U_ARMRINGL)
  TALISMAN = 11,    // 符咒槽位 (U_BUJUK)
  CHARM = 12,       // 宝石槽位
  MAX_SLOTS = 13
};

/**
 * @brief 护身符类型
 */
enum class AmuletType : uint8_t {
  NONE = 0,
  HOLY = 1,      // 神圣护身符 (Shape=5) - 用于召唤/治疗技能
  POISON = 2,    // 毒素护身符 (Shape<=2) - 用于施毒技能
  FIRE = 3,      // 火焰护身符 - 用于火系技能
  ICE = 4        // 冰霜护身符 - 用于冰系技能
};

/**
 * @brief 技能类型
 */
enum class SkillType : uint8_t {
  PHYSICAL = 0,
  MAGICAL = 1,
  BUFF = 2,
  DEBUFF = 3,
  HEAL = 4
};

/**
 * @brief 技能目标类型
 */
enum class SkillTarget : uint8_t {
  SELF = 0,
  SINGLE_ENEMY = 1,
  SINGLE_ALLY = 2,
  AOE_ENEMY = 3,
  AOE_ALLY = 4,
  AOE_ALL = 5
};

/**
 * @brief 怪物AI状态
 */
enum class MonsterState : uint8_t {
  IDLE = 0,
  PATROL = 1,
  CHASE = 2,
  ATTACK = 3,
  RETURN = 4,
  DEAD = 5
};

/**
 * @brief 角色/实体朝向（8方向）
 */
enum class Direction : uint8_t {
  UP = 0,
  UP_RIGHT = 1,
  RIGHT = 2,
  DOWN_RIGHT = 3,
  DOWN = 4,
  DOWN_LEFT = 5,
  LEFT = 6,
  UP_LEFT = 7
};

/**
 * @brief 实体类型
 */
enum class EntityType : uint8_t {
  PLAYER = 0,
  MONSTER = 1,
  NPC = 2,
  ITEM = 3,
  PORTAL = 4
};

/**
 * @brief 攻击类型
 */
enum class AttackType : uint8_t {
  kHit = 0,        // CM_HIT - 普通攻击
  kHeavyHit = 1,   // CM_HEAVYHIT - 重击(+50%伤害)
  kBigHit = 2,     // CM_BIGHIT - 强力攻击(使用m_nHitPlus)
  kPowerHit = 3,   // CM_POWERHIT - 蓄力攻击(+100%伤害)
  kLongHit = 4,    // CM_LONGHIT - 远距离近战(2-3格范围)
  kTwnHit = 5,     // CM_TWNHIT - 双倍攻击(攻击两次)
  kWideHit = 6,    // CM_WIDEHIT - 广域攻击(3x3区域)
  kFireHit = 7     // CM_FIREHIT - 火焰攻击(物理+火焰伤害)
};

/**
 * @brief 服务类型
 */
enum class ServiceType : uint8_t {
  kGateway = 1,
  kWorld = 2,
  kGame = 3,
  kDb = 4
};

/**
 * @brief 内部消息ID
 */
enum class InternalMsgId : uint16_t {
  kServiceHello = 60000,
  kServiceHelloAck = 60001,
  kRoutedMessage = 60010
};

}  // namespace mir2::common

#endif  // MIR2_COMMON_ENUMS_H
