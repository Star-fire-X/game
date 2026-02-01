/**
 * @file player.cc
 * @brief 玩家实体类实现
 */

#include "game/entity/player.h"

#include <algorithm>

namespace mir2::game::entity {

Player::Player(entt::entity entity, uint64_t id)
    : entity_(entity), data_{}, attributes_{} {
  data_.id = id;
}

void Player::SetPosition(int32_t x, int32_t y) {
  data_.x = x;
  data_.y = y;
}

void Player::SetMap(uint32_t map_id, int32_t x, int32_t y) {
  data_.map_id = map_id;
  data_.x = x;
  data_.y = y;
}

void Player::SetDirection(uint8_t direction) {
  // 方向值范围：0-7
  data_.direction = direction % 8;
}

int32_t Player::TakeDamage(int32_t damage) {
  if (damage <= 0 || IsDead()) {
    return 0;
  }

  int32_t actual_damage = std::min(damage, data_.hp);
  data_.hp -= actual_damage;
  return actual_damage;
}

int32_t Player::Heal(int32_t amount) {
  if (amount <= 0 || IsDead()) {
    return 0;
  }

  int32_t actual_heal = std::min(amount, data_.max_hp - data_.hp);
  data_.hp += actual_heal;
  return actual_heal;
}

void Player::AddExp(uint64_t exp) {
  data_.exp += exp;
  // TODO: 检查升级逻辑
}

void Player::RecalculateAttributes() {
  // 基础属性计算（根据职业和等级）
  // 这里使用简化的公式，实际项目应从配置表读取
  switch (data_.profession) {
    case common::CharacterClass::WARRIOR:
      // 战士：高物攻、高物防
      attributes_.attack = 10 + data_.level * 3;
      attributes_.defense = 5 + data_.level * 2;
      attributes_.magic_attack = 5 + data_.level;
      attributes_.magic_defense = 3 + data_.level;
      break;
    case common::CharacterClass::MAGE:
      // 法师：高魔攻、低物防
      attributes_.attack = 5 + data_.level;
      attributes_.defense = 3 + data_.level;
      attributes_.magic_attack = 10 + data_.level * 3;
      attributes_.magic_defense = 5 + data_.level * 2;
      break;
    case common::CharacterClass::TAOIST:
      // 道士：均衡属性
      attributes_.attack = 8 + data_.level * 2;
      attributes_.defense = 4 + data_.level;
      attributes_.magic_attack = 8 + data_.level * 2;
      attributes_.magic_defense = 4 + data_.level;
      break;
  }

  // TODO: 加上装备加成
}

}  // namespace mir2::game::entity
