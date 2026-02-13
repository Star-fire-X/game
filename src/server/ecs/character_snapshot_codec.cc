#include "ecs/character_snapshot_codec.h"

#include <flatbuffers/flatbuffers.h>

#include "persistence_generated.h"

namespace mir2::ecs {

std::vector<uint8_t> SerializeCharacterSnapshot(const common::CharacterData& data) {
  flatbuffers::FlatBufferBuilder builder(1024);

  auto account_id = builder.CreateString(data.account_id);
  auto name = builder.CreateString(data.name);

  // Store JSON strings as raw byte vectors.
  std::vector<uint8_t> equip_bytes(data.equipment_json.begin(),
                                   data.equipment_json.end());
  std::vector<uint8_t> inv_bytes(data.inventory_json.begin(),
                                 data.inventory_json.end());
  std::vector<uint8_t> skills_bytes(data.skills_json.begin(),
                                    data.skills_json.end());

  auto equip_vec = builder.CreateVector(equip_bytes);
  auto inv_vec = builder.CreateVector(inv_bytes);
  auto skills_vec = builder.CreateVector(skills_bytes);

  mir2::persist::CharacterSnapshotBuilder snapshot(builder);
  snapshot.add_id(data.id);
  snapshot.add_account_id(account_id);
  snapshot.add_name(name);
  snapshot.add_char_class(static_cast<uint8_t>(data.char_class));
  snapshot.add_gender(static_cast<uint8_t>(data.gender));
  snapshot.add_level(data.stats.level);
  snapshot.add_hp(data.stats.hp);
  snapshot.add_max_hp(data.stats.max_hp);
  snapshot.add_mp(data.stats.mp);
  snapshot.add_max_mp(data.stats.max_mp);
  snapshot.add_attack(data.stats.attack);
  snapshot.add_defense(data.stats.defense);
  snapshot.add_magic_attack(data.stats.magic_attack);
  snapshot.add_magic_defense(data.stats.magic_defense);
  snapshot.add_speed(data.stats.speed);
  snapshot.add_experience(data.stats.experience);
  snapshot.add_gold(data.stats.gold);
  snapshot.add_map_id(data.map_id);
  snapshot.add_pos_x(data.position.x);
  snapshot.add_pos_y(data.position.y);
  snapshot.add_equipment_data(equip_vec);
  snapshot.add_inventory_data(inv_vec);
  snapshot.add_skills_data(skills_vec);
  snapshot.add_created_at(data.created_at);
  snapshot.add_last_login(data.last_login);
  // Growth system fields
  snapshot.add_body_luck(data.stats.body_luck);
  snapshot.add_bonus_remaining(data.stats.bonus_remaining);
  snapshot.add_bonus_dc(data.stats.bonus_dc);
  snapshot.add_bonus_mc(data.stats.bonus_mc);
  snapshot.add_bonus_sc(data.stats.bonus_sc);
  snapshot.add_bonus_ac(data.stats.bonus_ac);
  snapshot.add_bonus_mac(data.stats.bonus_mac);
  snapshot.add_bonus_hp(data.stats.bonus_hp);
  snapshot.add_bonus_mp(data.stats.bonus_mp);
  snapshot.add_bonus_hit(data.stats.bonus_hit);
  snapshot.add_bonus_speed(data.stats.bonus_speed);
  snapshot.add_max_weight(data.stats.max_weight);
  snapshot.add_max_wear_weight(data.stats.max_wear_weight);
  snapshot.add_max_hand_weight(data.stats.max_hand_weight);

  auto offset = snapshot.Finish();
  builder.Finish(offset);

  const uint8_t* buf = builder.GetBufferPointer();
  size_t size = builder.GetSize();
  return std::vector<uint8_t>(buf, buf + size);
}

std::optional<common::CharacterData> DeserializeCharacterSnapshot(
    const uint8_t* buf, size_t size) {
  if (!buf || size == 0) {
    return std::nullopt;
  }

  flatbuffers::Verifier verifier(buf, size);
  if (!mir2::persist::VerifyCharacterSnapshotBuffer(verifier)) {
    return std::nullopt;
  }

  const auto* snapshot = mir2::persist::GetCharacterSnapshot(buf);
  if (!snapshot) {
    return std::nullopt;
  }

  common::CharacterData data;
  data.id = snapshot->id();
  data.account_id = snapshot->account_id() ? snapshot->account_id()->str() : "";
  data.name = snapshot->name() ? snapshot->name()->str() : "";
  data.char_class = static_cast<common::CharacterClass>(snapshot->char_class());
  data.gender = static_cast<common::Gender>(snapshot->gender());

  data.stats.level = snapshot->level();
  data.stats.hp = snapshot->hp();
  data.stats.max_hp = snapshot->max_hp();
  data.stats.mp = snapshot->mp();
  data.stats.max_mp = snapshot->max_mp();
  data.stats.attack = snapshot->attack();
  data.stats.defense = snapshot->defense();
  data.stats.magic_attack = snapshot->magic_attack();
  data.stats.magic_defense = snapshot->magic_defense();
  data.stats.speed = snapshot->speed();
  data.stats.experience = snapshot->experience();
  data.stats.gold = snapshot->gold();
  // Growth system fields (backward compatible — defaults to 0 for old data)
  data.stats.body_luck = snapshot->body_luck();
  data.stats.bonus_remaining = snapshot->bonus_remaining();
  data.stats.bonus_dc = snapshot->bonus_dc();
  data.stats.bonus_mc = snapshot->bonus_mc();
  data.stats.bonus_sc = snapshot->bonus_sc();
  data.stats.bonus_ac = snapshot->bonus_ac();
  data.stats.bonus_mac = snapshot->bonus_mac();
  data.stats.bonus_hp = snapshot->bonus_hp();
  data.stats.bonus_mp = snapshot->bonus_mp();
  data.stats.bonus_hit = snapshot->bonus_hit();
  data.stats.bonus_speed = snapshot->bonus_speed();
  data.stats.max_weight = snapshot->max_weight();
  data.stats.max_wear_weight = snapshot->max_wear_weight();
  data.stats.max_hand_weight = snapshot->max_hand_weight();

  data.map_id = snapshot->map_id();
  data.position.x = snapshot->pos_x();
  data.position.y = snapshot->pos_y();

  if (auto* eq = snapshot->equipment_data()) {
    data.equipment_json.assign(
        reinterpret_cast<const char*>(eq->data()), eq->size());
  }
  if (auto* inv = snapshot->inventory_data()) {
    data.inventory_json.assign(
        reinterpret_cast<const char*>(inv->data()), inv->size());
  }
  if (auto* sk = snapshot->skills_data()) {
    data.skills_json.assign(
        reinterpret_cast<const char*>(sk->data()), sk->size());
  }

  data.created_at = snapshot->created_at();
  data.last_login = snapshot->last_login();

  return data;
}

}  // namespace mir2::ecs
