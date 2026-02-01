#ifndef LEGEND2_COMMON_SKILL_IDS_H
#define LEGEND2_COMMON_SKILL_IDS_H

#include <cstdint>

namespace mir2::common {

namespace SkillId {
// 战士技能
/** \brief 基础剑术 */
constexpr uint32_t ONESWORD = 3;
/** \brief 刺杀剑术 */
constexpr uint32_t ILKWANG = 4;
/** \brief 半月弯刀 */
constexpr uint32_t YEDO = 7;
/** \brief 野蛮冲撞 */
constexpr uint32_t ERGUM = 12;
/** \brief 圆月弯刀 */
constexpr uint32_t BANWOL = 25;
/** \brief 烈火剑法 */
constexpr uint32_t FIRESWORD = 26;
/** \brief 狂风斩 */
constexpr uint32_t MOOTEBO = 27;
/** \brief 抱月刀法 */
constexpr uint32_t BAOYUEDAO = 48;
/** \brief 狮子吼 */
constexpr uint32_t LIONROAR = 49;
/** \brief 无极真气 */
constexpr uint32_t WUJIZHENQI = 50;
/** \brief 召唤神兽 - 战士 */
constexpr uint32_t WARRIOR_SUMMON = 51;
/** \brief 嗜血术 */
constexpr uint32_t BLOODLUST = 52;
/** \brief 血祭 */
constexpr uint32_t BLOODSACRIFICE = 53;
/** \brief 护体神功 */
constexpr uint32_t BODYGUARD = 54;

// 法师技能
/** \brief 火球术 */
constexpr uint32_t FIREBALL = 1;
/** \brief 大火球 */
constexpr uint32_t BIGFIREBALL = 5;
/** \brief 抗拒火环 */
constexpr uint32_t FIREWIND = 8;
/** \brief 火墙 */
constexpr uint32_t FIREWALL = 9;
/** \brief 疾光电影 */
constexpr uint32_t LIGHTNINGBEAM = 10;
/** \brief 雷电术 */
constexpr uint32_t LIGHTENING = 11;
/** \brief 地狱火 */
constexpr uint32_t HELLFIRE = 22;
/** \brief 爆裂火焰 */
constexpr uint32_t FIREBOOM = 23;
/** \brief 雷电风暴 */
constexpr uint32_t THUNDERSTORM = 24;
/** \brief 魔法盾 */
constexpr uint32_t SHIELD = 31;
/** \brief 冰咆哮 */
constexpr uint32_t SNOWWIND = 33;
/** \brief 诱惑之光 - 法师 */
constexpr uint32_t MAGE_CHARM = 36;
/** \brief 群体雷电 */
constexpr uint32_t CHAINLIGHTNING = 37;
/** \brief 流星火雨 */
constexpr uint32_t METEORSTRIKE = 44;
/** \brief 冰箭术 */
constexpr uint32_t ICEARROW = 45;
/** \brief 流星雨 */
constexpr uint32_t METEORRAIN = 47;
/** \brief 灭天火 */
constexpr uint32_t FIRESTORM = 55;
/** \brief 寒冰掌 */
constexpr uint32_t ICEPALM = 56;

// 道士技能
/** \brief 治愈术 */
constexpr uint32_t HEALING = 2;
[[deprecated("Use HEALING instead")]] constexpr uint32_t HEALLING = HEALING;
/** \brief 施毒术 */
constexpr uint32_t AMYOUNSUL = 6;
/** \brief 幽灵盾 */
constexpr uint32_t GHOSTSHIELD = 14;
/** \brief 神圣战甲 */
constexpr uint32_t DEJIWONHO = 15;
/** \brief 圣言术 */
constexpr uint32_t HOLYWORD = 16;
/** \brief 召唤骷髅 */
constexpr uint32_t SKELLETON = 17;
/** \brief 隐身术 */
constexpr uint32_t CLOAK = 18;
/** \brief 集体隐身 */
constexpr uint32_t MASSCLOAK = 19;
/** \brief 诱惑之光 - 道士 */
constexpr uint32_t TAOIST_CHARM = 20;
/** \brief 瞬息移动 */
constexpr uint32_t TELEPORT = 21;
/** \brief 心灵启示 */
constexpr uint32_t REVELATION = 28;
/** \brief 群体治愈 */
constexpr uint32_t BIGHEALLING = 29;
/** \brief 召唤神兽 - 道士 */
constexpr uint32_t TAOIST_SUMMON = 30;
/** \brief 圣言术 - 驱散 */
constexpr uint32_t EXORCISM = 32;
/** \brief 净化术 */
constexpr uint32_t PURIFY = 34;
/** \brief 狮子吼 - 道士 */
constexpr uint32_t TAOIST_ROAR = 35;
/** \brief 群体施毒 */
constexpr uint32_t MASSPOISON = 38;
/** \brief 石化术 */
constexpr uint32_t PETRIFY = 39;
/** \brief 气功波 */
constexpr uint32_t ENERGYWAVE = 40;
/** \brief 无极真气 - 道士 */
constexpr uint32_t TAOIST_WUJI = 41;
/** \brief 噬血术 */
constexpr uint32_t VAMPIRICTOUCH = 42;
/** \brief 毒云术 */
constexpr uint32_t POISONCLOUD = 43;
/** \brief 困魔咒 */
constexpr uint32_t TRAPSPELL = 46;
} // namespace SkillId

} // namespace mir2::common

#endif // LEGEND2_COMMON_SKILL_IDS_H
