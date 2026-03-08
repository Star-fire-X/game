/**
 * @file item_constants.h
 * @brief Item type constants (StdMode/Shape) from mir2 protocol.
 */

#ifndef LEGEND2_COMMON_ITEM_CONSTANTS_H
#define LEGEND2_COMMON_ITEM_CONSTANTS_H

#include <cstdint>

namespace mir2::common {

/// Item StdMode values (物品类型)
namespace item_std_mode {
constexpr uint8_t kDrug = 0;           ///< 药品
constexpr uint8_t kMeat = 1;           ///< 肉类
constexpr uint8_t kFood = 2;           ///< 食物
constexpr uint8_t kScroll = 3;         ///< 卷轴
constexpr uint8_t kSkillBook = 4;      ///< 技能书
constexpr uint8_t kAmulet = 25;        ///< 符纸
constexpr uint8_t kLight = 30;         ///< 照明(蜡烛/灯笼)
constexpr uint8_t kBundledDrug = 31;   ///< 捆绑药品
constexpr uint8_t kSpecialMeat = 40;   ///< 特殊肉类
constexpr uint8_t kHerb = 42;          ///< 药材
constexpr uint8_t kSpecialItem = 43;   ///< 特殊物品
constexpr uint8_t kDice = 45;          ///< 骰子/彩票
constexpr uint8_t kCoupon = 50;        ///< 商品券
}  // namespace item_std_mode

/// Item Shape values for Drug (StdMode=0)
namespace drug_shape {
constexpr uint8_t kNormal = 0;         ///< 普通药品(渐进恢复)
constexpr uint8_t kFastFill = 1;       ///< 仙水(瞬间恢复)
constexpr uint8_t kFreeCurse = 2;      ///< 解咒药水
}  // namespace drug_shape

/// Item Shape values for Scroll (StdMode=3)
namespace scroll_shape {
constexpr uint8_t kRandomTeleport = 1;   ///< 随机传送卷(传送到出生点)
constexpr uint8_t kDungeonTeleport = 2;  ///< 地牢传送卷(当前地图随机)
constexpr uint8_t kHomeScroll = 3;       ///< 回城卷
constexpr uint8_t kBlessOil = 4;         ///< 祝福油
constexpr uint8_t kCastleScroll = 5;     ///< 回城传书(行会城堡)
constexpr uint8_t kRepairOil = 9;        ///< 修复油(普通)
constexpr uint8_t kPerfectRepairOil = 10;///< 武神油(特殊修复)
constexpr uint8_t kLottery = 11;         ///< 彩票
constexpr uint8_t kAbilityDrug = 12;     ///< 能力提升药
constexpr uint8_t kExpDrug = 13;         ///< 经验药水
}  // namespace scroll_shape

/// Item Shape values for Amulet (StdMode=25)
namespace amulet_shape {
constexpr uint8_t kTalisman = 5;       ///< 符纸(道士施法消耗)
}  // namespace amulet_shape

/// Durability constants
namespace durability {
constexpr int kAmuletConsumePerCast = 100;  ///< 每次施法消耗符纸耐久
constexpr int kMinSellDurability = 4000;    ///< 最低可出售耐久度
constexpr int kMeatDropDecay = 2000;        ///< 肉类掉落耐久衰减
}  // namespace durability

/// Recovery constants
namespace recovery {
constexpr int kMaxIncHealthSpell = 500;     ///< 渐进恢复累加上限
}  // namespace recovery

/// Pickup ownership constants
namespace pickup {
constexpr int64_t kOwnershipDurationMs = 30000;   ///< 拾取权持续时间(30秒)
constexpr int64_t kItemExpireDurationMs = 300000; ///< 物品消失时间(5分钟)
}  // namespace pickup

/// Trade constants
namespace trade {
constexpr float kCastleTaxRate = 0.05f;     ///< 城堡商店税率(5%)
constexpr int kDropCooldownMs = 3000;       ///< 交易后丢弃冷却(3秒)
}  // namespace trade

}  // namespace mir2::common

#endif  // LEGEND2_COMMON_ITEM_CONSTANTS_H
