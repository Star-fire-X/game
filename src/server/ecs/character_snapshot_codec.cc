#include "ecs/character_snapshot_codec.h"

#include <charconv>
#include <cstddef>
#include <flatbuffers/flatbuffers.h>
#include <string_view>
#include <system_error>
#include <utility>

#include "persistence_generated.h"

namespace mir2::ecs {
namespace {

struct LegacyCharacterSnapshot FLATBUFFERS_FINAL_CLASS
    : private ::flatbuffers::Table {
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_ID = 4,
    VT_ACCOUNT_ID = 6,
    VT_NAME = 8,
    VT_CHAR_CLASS = 10,
    VT_GENDER = 12,
    VT_LEVEL = 14,
    VT_HP = 16,
    VT_MAX_HP = 18,
    VT_MP = 20,
    VT_MAX_MP = 22,
    VT_ATTACK = 24,
    VT_DEFENSE = 26,
    VT_MAGIC_ATTACK = 28,
    VT_MAGIC_DEFENSE = 30,
    VT_SPEED = 32,
    VT_EXPERIENCE = 34,
    VT_GOLD = 36,
    VT_MAP_ID = 38,
    VT_POS_X = 40,
    VT_POS_Y = 42,
    VT_EQUIPMENT_DATA = 44,
    VT_INVENTORY_DATA = 46,
    VT_SKILLS_DATA = 48,
    VT_CREATED_AT = 50,
    VT_LAST_LOGIN = 52,
    VT_BODY_LUCK = 54,
    VT_BONUS_REMAINING = 56,
    VT_BONUS_DC = 58,
    VT_BONUS_MC = 60,
    VT_BONUS_SC = 62,
    VT_BONUS_AC = 64,
    VT_BONUS_MAC = 66,
    VT_BONUS_HP = 68,
    VT_BONUS_MP = 70,
    VT_BONUS_HIT = 72,
    VT_BONUS_SPEED = 74,
    VT_MAX_WEIGHT = 76,
    VT_MAX_WEAR_WEIGHT = 78,
    VT_MAX_HAND_WEIGHT = 80
  };

  uint32_t id() const { return GetField<uint32_t>(VT_ID, 0); }
  const ::flatbuffers::String* account_id() const {
    return GetPointer<const ::flatbuffers::String*>(VT_ACCOUNT_ID);
  }
  const ::flatbuffers::String* name() const {
    return GetPointer<const ::flatbuffers::String*>(VT_NAME);
  }
  uint8_t char_class() const { return GetField<uint8_t>(VT_CHAR_CLASS, 0); }
  uint8_t gender() const { return GetField<uint8_t>(VT_GENDER, 0); }
  int32_t level() const { return GetField<int32_t>(VT_LEVEL, 0); }
  int32_t hp() const { return GetField<int32_t>(VT_HP, 0); }
  int32_t max_hp() const { return GetField<int32_t>(VT_MAX_HP, 0); }
  int32_t mp() const { return GetField<int32_t>(VT_MP, 0); }
  int32_t max_mp() const { return GetField<int32_t>(VT_MAX_MP, 0); }
  int32_t attack() const { return GetField<int32_t>(VT_ATTACK, 0); }
  int32_t defense() const { return GetField<int32_t>(VT_DEFENSE, 0); }
  int32_t magic_attack() const { return GetField<int32_t>(VT_MAGIC_ATTACK, 0); }
  int32_t magic_defense() const { return GetField<int32_t>(VT_MAGIC_DEFENSE, 0); }
  int32_t speed() const { return GetField<int32_t>(VT_SPEED, 0); }
  int64_t experience() const { return GetField<int64_t>(VT_EXPERIENCE, 0); }
  int32_t gold() const { return GetField<int32_t>(VT_GOLD, 0); }
  uint32_t map_id() const { return GetField<uint32_t>(VT_MAP_ID, 0); }
  int32_t pos_x() const { return GetField<int32_t>(VT_POS_X, 0); }
  int32_t pos_y() const { return GetField<int32_t>(VT_POS_Y, 0); }
  const ::flatbuffers::Vector<uint8_t>* equipment_data() const {
    return GetPointer<const ::flatbuffers::Vector<uint8_t>*>(VT_EQUIPMENT_DATA);
  }
  const ::flatbuffers::Vector<uint8_t>* inventory_data() const {
    return GetPointer<const ::flatbuffers::Vector<uint8_t>*>(VT_INVENTORY_DATA);
  }
  const ::flatbuffers::Vector<uint8_t>* skills_data() const {
    return GetPointer<const ::flatbuffers::Vector<uint8_t>*>(VT_SKILLS_DATA);
  }
  int64_t created_at() const { return GetField<int64_t>(VT_CREATED_AT, 0); }
  int64_t last_login() const { return GetField<int64_t>(VT_LAST_LOGIN, 0); }
  int32_t body_luck() const { return GetField<int32_t>(VT_BODY_LUCK, 0); }
  int32_t bonus_remaining() const {
    return GetField<int32_t>(VT_BONUS_REMAINING, 0);
  }
  int32_t bonus_dc() const { return GetField<int32_t>(VT_BONUS_DC, 0); }
  int32_t bonus_mc() const { return GetField<int32_t>(VT_BONUS_MC, 0); }
  int32_t bonus_sc() const { return GetField<int32_t>(VT_BONUS_SC, 0); }
  int32_t bonus_ac() const { return GetField<int32_t>(VT_BONUS_AC, 0); }
  int32_t bonus_mac() const { return GetField<int32_t>(VT_BONUS_MAC, 0); }
  int32_t bonus_hp() const { return GetField<int32_t>(VT_BONUS_HP, 0); }
  int32_t bonus_mp() const { return GetField<int32_t>(VT_BONUS_MP, 0); }
  int32_t bonus_hit() const { return GetField<int32_t>(VT_BONUS_HIT, 0); }
  int32_t bonus_speed() const { return GetField<int32_t>(VT_BONUS_SPEED, 0); }
  int32_t max_weight() const { return GetField<int32_t>(VT_MAX_WEIGHT, 0); }
  int32_t max_wear_weight() const {
    return GetField<int32_t>(VT_MAX_WEAR_WEIGHT, 0);
  }
  int32_t max_hand_weight() const {
    return GetField<int32_t>(VT_MAX_HAND_WEIGHT, 0);
  }

  bool Verify(::flatbuffers::Verifier& verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<uint32_t>(verifier, VT_ID, 4) &&
           VerifyOffset(verifier, VT_ACCOUNT_ID) &&
           verifier.VerifyString(account_id()) &&
           VerifyOffset(verifier, VT_NAME) &&
           verifier.VerifyString(name()) &&
           VerifyField<uint8_t>(verifier, VT_CHAR_CLASS, 1) &&
           VerifyField<uint8_t>(verifier, VT_GENDER, 1) &&
           VerifyField<int32_t>(verifier, VT_LEVEL, 4) &&
           VerifyField<int32_t>(verifier, VT_HP, 4) &&
           VerifyField<int32_t>(verifier, VT_MAX_HP, 4) &&
           VerifyField<int32_t>(verifier, VT_MP, 4) &&
           VerifyField<int32_t>(verifier, VT_MAX_MP, 4) &&
           VerifyField<int32_t>(verifier, VT_ATTACK, 4) &&
           VerifyField<int32_t>(verifier, VT_DEFENSE, 4) &&
           VerifyField<int32_t>(verifier, VT_MAGIC_ATTACK, 4) &&
           VerifyField<int32_t>(verifier, VT_MAGIC_DEFENSE, 4) &&
           VerifyField<int32_t>(verifier, VT_SPEED, 4) &&
           VerifyField<int64_t>(verifier, VT_EXPERIENCE, 8) &&
           VerifyField<int32_t>(verifier, VT_GOLD, 4) &&
           VerifyField<uint32_t>(verifier, VT_MAP_ID, 4) &&
           VerifyField<int32_t>(verifier, VT_POS_X, 4) &&
           VerifyField<int32_t>(verifier, VT_POS_Y, 4) &&
           VerifyOffset(verifier, VT_EQUIPMENT_DATA) &&
           verifier.VerifyVector(equipment_data()) &&
           VerifyOffset(verifier, VT_INVENTORY_DATA) &&
           verifier.VerifyVector(inventory_data()) &&
           VerifyOffset(verifier, VT_SKILLS_DATA) &&
           verifier.VerifyVector(skills_data()) &&
           VerifyField<int64_t>(verifier, VT_CREATED_AT, 8) &&
           VerifyField<int64_t>(verifier, VT_LAST_LOGIN, 8) &&
           VerifyField<int32_t>(verifier, VT_BODY_LUCK, 4) &&
           VerifyField<int32_t>(verifier, VT_BONUS_REMAINING, 4) &&
           VerifyField<int32_t>(verifier, VT_BONUS_DC, 4) &&
           VerifyField<int32_t>(verifier, VT_BONUS_MC, 4) &&
           VerifyField<int32_t>(verifier, VT_BONUS_SC, 4) &&
           VerifyField<int32_t>(verifier, VT_BONUS_AC, 4) &&
           VerifyField<int32_t>(verifier, VT_BONUS_MAC, 4) &&
           VerifyField<int32_t>(verifier, VT_BONUS_HP, 4) &&
           VerifyField<int32_t>(verifier, VT_BONUS_MP, 4) &&
           VerifyField<int32_t>(verifier, VT_BONUS_HIT, 4) &&
           VerifyField<int32_t>(verifier, VT_BONUS_SPEED, 4) &&
           VerifyField<int32_t>(verifier, VT_MAX_WEIGHT, 4) &&
           VerifyField<int32_t>(verifier, VT_MAX_WEAR_WEIGHT, 4) &&
           VerifyField<int32_t>(verifier, VT_MAX_HAND_WEIGHT, 4) &&
           verifier.EndTable();
  }
};

bool IsValidGenderValue(uint8_t value) {
  return value <= static_cast<uint8_t>(common::Gender::FEMALE);
}

bool IsSemanticallyValidCharacter(uint8_t char_class, uint8_t gender) {
  return common::is_valid_character_class(char_class) && IsValidGenderValue(gender);
}

uint64_t ParseAccountIdOrZero(const flatbuffers::String* value) {
  if (!value) {
    return 0;
  }
  const std::string_view text(value->c_str(), value->size());
  if (text.empty()) {
    return 0;
  }

  uint64_t parsed = 0;
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (ec != std::errc() || ptr != text.data() + text.size()) {
    return 0;
  }
  return parsed;
}

std::string DecodeBlobAsString(const flatbuffers::Vector<uint8_t>* blob) {
  if (!blob) {
    return {};
  }
  return std::string(reinterpret_cast<const char*>(blob->data()), blob->size());
}

common::CharacterData DecodeCurrentSnapshot(const mir2::persist::CharacterSnapshot& snapshot) {
  common::CharacterData data;
  data.id = snapshot.id();
  data.account_id = snapshot.account_id_u64();
  data.name = snapshot.name() ? snapshot.name()->str() : "";
  data.char_class = static_cast<common::CharacterClass>(snapshot.char_class());
  data.gender = static_cast<common::Gender>(snapshot.gender());

  data.stats.level = snapshot.level();
  data.stats.hp = snapshot.hp();
  data.stats.max_hp = snapshot.max_hp();
  data.stats.mp = snapshot.mp();
  data.stats.max_mp = snapshot.max_mp();
  data.stats.attack = snapshot.attack();
  data.stats.defense = snapshot.defense();
  data.stats.magic_attack = snapshot.magic_attack();
  data.stats.magic_defense = snapshot.magic_defense();
  data.stats.speed = snapshot.speed();
  data.stats.experience = snapshot.experience();
  data.stats.gold = snapshot.gold();
  data.stats.body_luck = snapshot.body_luck();
  data.stats.bonus_remaining = snapshot.bonus_remaining();
  data.stats.bonus_dc = snapshot.bonus_dc();
  data.stats.bonus_mc = snapshot.bonus_mc();
  data.stats.bonus_sc = snapshot.bonus_sc();
  data.stats.bonus_ac = snapshot.bonus_ac();
  data.stats.bonus_mac = snapshot.bonus_mac();
  data.stats.bonus_hp = snapshot.bonus_hp();
  data.stats.bonus_mp = snapshot.bonus_mp();
  data.stats.bonus_hit = snapshot.bonus_hit();
  data.stats.bonus_speed = snapshot.bonus_speed();
  data.stats.max_weight = snapshot.max_weight();
  data.stats.max_wear_weight = snapshot.max_wear_weight();
  data.stats.max_hand_weight = snapshot.max_hand_weight();

  data.map_id = snapshot.map_id();
  data.position.x = snapshot.pos_x();
  data.position.y = snapshot.pos_y();

  data.equipment_json = DecodeBlobAsString(snapshot.equipment_data());
  data.inventory_json = DecodeBlobAsString(snapshot.inventory_data());
  data.skills_json = DecodeBlobAsString(snapshot.skills_data());
  data.created_at = snapshot.created_at();
  data.last_login = snapshot.last_login();
  return data;
}

std::optional<common::CharacterData> TryDecodeLegacySnapshot(const uint8_t* buf, size_t size) {
  flatbuffers::Verifier verifier(buf, size);
  if (!verifier.VerifyBuffer<LegacyCharacterSnapshot>(nullptr)) {
    return std::nullopt;
  }

  const auto* snapshot = flatbuffers::GetRoot<LegacyCharacterSnapshot>(buf);
  if (!snapshot) {
    return std::nullopt;
  }

  if (!IsSemanticallyValidCharacter(snapshot->char_class(), snapshot->gender())) {
    return std::nullopt;
  }

  common::CharacterData data;
  data.id = snapshot->id();
  data.account_id = ParseAccountIdOrZero(snapshot->account_id());
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

  data.equipment_json = DecodeBlobAsString(snapshot->equipment_data());
  data.inventory_json = DecodeBlobAsString(snapshot->inventory_data());
  data.skills_json = DecodeBlobAsString(snapshot->skills_data());
  data.created_at = snapshot->created_at();
  data.last_login = snapshot->last_login();

  return data;
}

}  // namespace

std::vector<uint8_t> SerializeCharacterSnapshot(const common::CharacterData& data) {
  flatbuffers::FlatBufferBuilder builder(1024);

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
  snapshot.add_account_id_u64(data.account_id);
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
  const auto decoded = DeserializeCharacterSnapshotWithMetadata(buf, size);
  if (!decoded.has_value()) {
    return std::nullopt;
  }
  return decoded->data;
}

std::optional<CharacterSnapshotDecodeResult> DeserializeCharacterSnapshotWithMetadata(
    const uint8_t* buf, size_t size) {
  if (!buf || size == 0) {
    return std::nullopt;
  }

  CharacterSnapshotDecodeResult current_result;
  bool current_ok = false;

  flatbuffers::Verifier verifier(buf, size);
  if (!mir2::persist::VerifyCharacterSnapshotBuffer(verifier)) {
    auto legacy = TryDecodeLegacySnapshot(buf, size);
    if (!legacy.has_value()) {
      return std::nullopt;
    }

    CharacterSnapshotDecodeResult migrated;
    migrated.data = std::move(*legacy);
    migrated.migrated_from_legacy = true;
    return migrated;
  }

  const auto* snapshot = mir2::persist::GetCharacterSnapshot(buf);
  if (snapshot) {
    current_result.data = DecodeCurrentSnapshot(*snapshot);
    current_ok = true;

    // For legacy payloads, enum fields are usually garbage under the new layout.
    // Try legacy decode only in that case, but keep current decode as fallback
    // for potential forward enum expansion.
    if (IsSemanticallyValidCharacter(snapshot->char_class(), snapshot->gender())) {
      return current_result;
    }
  }

  auto legacy = TryDecodeLegacySnapshot(buf, size);
  if (legacy.has_value()) {
    CharacterSnapshotDecodeResult migrated;
    migrated.data = std::move(*legacy);
    migrated.migrated_from_legacy = true;
    return migrated;
  }

  if (current_ok) {
    return current_result;
  }
  return std::nullopt;
}

}  // namespace mir2::ecs
