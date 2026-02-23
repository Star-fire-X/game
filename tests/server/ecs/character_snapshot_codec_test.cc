#include "ecs/character_snapshot_codec.h"

#include <flatbuffers/flatbuffers.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

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
    VT_LAST_LOGIN = 52
  };
};

struct LegacyCharacterSnapshotBuilder {
  explicit LegacyCharacterSnapshotBuilder(flatbuffers::FlatBufferBuilder& fbb)
      : fbb_(fbb), start_(fbb_.StartTable()) {}

  void add_id(uint32_t id) {
    fbb_.AddElement<uint32_t>(LegacyCharacterSnapshot::VT_ID, id, 0);
  }
  void add_account_id(flatbuffers::Offset<flatbuffers::String> account_id) {
    fbb_.AddOffset(LegacyCharacterSnapshot::VT_ACCOUNT_ID, account_id);
  }
  void add_name(flatbuffers::Offset<flatbuffers::String> name) {
    fbb_.AddOffset(LegacyCharacterSnapshot::VT_NAME, name);
  }
  void add_char_class(uint8_t char_class) {
    fbb_.AddElement<uint8_t>(LegacyCharacterSnapshot::VT_CHAR_CLASS, char_class, 0);
  }
  void add_gender(uint8_t gender) {
    fbb_.AddElement<uint8_t>(LegacyCharacterSnapshot::VT_GENDER, gender, 0);
  }
  void add_level(int32_t level) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_LEVEL, level, 0);
  }
  void add_hp(int32_t hp) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_HP, hp, 0);
  }
  void add_max_hp(int32_t max_hp) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_MAX_HP, max_hp, 0);
  }
  void add_mp(int32_t mp) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_MP, mp, 0);
  }
  void add_max_mp(int32_t max_mp) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_MAX_MP, max_mp, 0);
  }
  void add_attack(int32_t attack) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_ATTACK, attack, 0);
  }
  void add_defense(int32_t defense) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_DEFENSE, defense, 0);
  }
  void add_magic_attack(int32_t magic_attack) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_MAGIC_ATTACK, magic_attack, 0);
  }
  void add_magic_defense(int32_t magic_defense) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_MAGIC_DEFENSE, magic_defense, 0);
  }
  void add_speed(int32_t speed) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_SPEED, speed, 0);
  }
  void add_experience(int64_t experience) {
    fbb_.AddElement<int64_t>(LegacyCharacterSnapshot::VT_EXPERIENCE, experience, 0);
  }
  void add_gold(int32_t gold) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_GOLD, gold, 0);
  }
  void add_map_id(uint32_t map_id) {
    fbb_.AddElement<uint32_t>(LegacyCharacterSnapshot::VT_MAP_ID, map_id, 0);
  }
  void add_pos_x(int32_t pos_x) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_POS_X, pos_x, 0);
  }
  void add_pos_y(int32_t pos_y) {
    fbb_.AddElement<int32_t>(LegacyCharacterSnapshot::VT_POS_Y, pos_y, 0);
  }
  void add_equipment_data(flatbuffers::Offset<flatbuffers::Vector<uint8_t>> data) {
    fbb_.AddOffset(LegacyCharacterSnapshot::VT_EQUIPMENT_DATA, data);
  }
  void add_inventory_data(flatbuffers::Offset<flatbuffers::Vector<uint8_t>> data) {
    fbb_.AddOffset(LegacyCharacterSnapshot::VT_INVENTORY_DATA, data);
  }
  void add_skills_data(flatbuffers::Offset<flatbuffers::Vector<uint8_t>> data) {
    fbb_.AddOffset(LegacyCharacterSnapshot::VT_SKILLS_DATA, data);
  }
  void add_created_at(int64_t created_at) {
    fbb_.AddElement<int64_t>(LegacyCharacterSnapshot::VT_CREATED_AT, created_at, 0);
  }
  void add_last_login(int64_t last_login) {
    fbb_.AddElement<int64_t>(LegacyCharacterSnapshot::VT_LAST_LOGIN, last_login, 0);
  }

  flatbuffers::Offset<LegacyCharacterSnapshot> Finish() {
    const auto offset = fbb_.EndTable(start_);
    return flatbuffers::Offset<LegacyCharacterSnapshot>(offset);
  }

 private:
  flatbuffers::FlatBufferBuilder& fbb_;
  flatbuffers::uoffset_t start_;
};

std::vector<uint8_t> BuildLegacySnapshotBytes(const std::string& account_id_text) {
  flatbuffers::FlatBufferBuilder builder(512);
  const auto account_id = builder.CreateString(account_id_text);
  const auto name = builder.CreateString("LegacyHero");

  const std::string equipment_json = R"([{"slot":0}])";
  const std::string inventory_json = R"([{"slot":1}])";
  const std::string skills_json = R"([{"id":7001,"level":3}])";
  const auto equipment = builder.CreateVector(
      reinterpret_cast<const uint8_t*>(equipment_json.data()),
      equipment_json.size());
  const auto inventory = builder.CreateVector(
      reinterpret_cast<const uint8_t*>(inventory_json.data()),
      inventory_json.size());
  const auto skills = builder.CreateVector(
      reinterpret_cast<const uint8_t*>(skills_json.data()),
      skills_json.size());

  LegacyCharacterSnapshotBuilder snapshot(builder);
  snapshot.add_id(42);
  snapshot.add_account_id(account_id);
  snapshot.add_name(name);
  snapshot.add_char_class(1);
  snapshot.add_gender(1);
  snapshot.add_level(20);
  snapshot.add_hp(300);
  snapshot.add_max_hp(500);
  snapshot.add_mp(150);
  snapshot.add_max_mp(250);
  snapshot.add_attack(77);
  snapshot.add_defense(33);
  snapshot.add_magic_attack(55);
  snapshot.add_magic_defense(44);
  snapshot.add_speed(12);
  snapshot.add_experience(9876543210LL);
  snapshot.add_gold(54321);
  snapshot.add_map_id(3);
  snapshot.add_pos_x(17);
  snapshot.add_pos_y(29);
  snapshot.add_equipment_data(equipment);
  snapshot.add_inventory_data(inventory);
  snapshot.add_skills_data(skills);
  snapshot.add_created_at(1700000001);
  snapshot.add_last_login(1700000042);
  builder.Finish(snapshot.Finish());

  const uint8_t* ptr = builder.GetBufferPointer();
  return std::vector<uint8_t>(ptr, ptr + builder.GetSize());
}

}  // namespace

TEST(CharacterSnapshotCodecTest, RoundTripCurrentSnapshot) {
  mir2::common::CharacterData input;
  input.id = 100;
  input.account_id = 90001;
  input.name = "CurrentHero";
  input.char_class = mir2::common::CharacterClass::TAOIST;
  input.gender = mir2::common::Gender::MALE;
  input.stats.level = 35;
  input.stats.hp = 420;
  input.stats.max_hp = 520;
  input.stats.mp = 210;
  input.stats.max_mp = 260;
  input.stats.attack = 88;
  input.stats.defense = 66;
  input.stats.magic_attack = 93;
  input.stats.magic_defense = 70;
  input.stats.speed = 15;
  input.stats.experience = 123456789;
  input.stats.gold = 9999;
  input.stats.body_luck = 12;
  input.stats.bonus_remaining = 7;
  input.stats.max_weight = 100;
  input.map_id = 2;
  input.position = {9, 11};
  input.equipment_json = R"([{"slot":0}])";
  input.inventory_json = R"([{"slot":2}])";
  input.skills_json = R"([{"id":1001,"level":2}])";
  input.created_at = 1700001000;
  input.last_login = 1700002000;

  const auto bytes = mir2::ecs::SerializeCharacterSnapshot(input);
  const auto decoded =
      mir2::ecs::DeserializeCharacterSnapshotWithMetadata(bytes.data(), bytes.size());

  ASSERT_TRUE(decoded.has_value());
  EXPECT_FALSE(decoded->migrated_from_legacy);
  EXPECT_EQ(decoded->data.id, input.id);
  EXPECT_EQ(decoded->data.account_id, input.account_id);
  EXPECT_EQ(decoded->data.name, input.name);
  EXPECT_EQ(decoded->data.map_id, input.map_id);
  EXPECT_EQ(decoded->data.position.x, input.position.x);
  EXPECT_EQ(decoded->data.position.y, input.position.y);
  EXPECT_EQ(decoded->data.inventory_json, input.inventory_json);
}

TEST(CharacterSnapshotCodecTest, LegacySnapshotMigratesToUint64AccountId) {
  const auto bytes = BuildLegacySnapshotBytes("90001");
  const auto decoded =
      mir2::ecs::DeserializeCharacterSnapshotWithMetadata(bytes.data(), bytes.size());

  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->migrated_from_legacy);
  EXPECT_EQ(decoded->data.id, 42u);
  EXPECT_EQ(decoded->data.account_id, 90001u);
  EXPECT_EQ(decoded->data.name, "LegacyHero");
  EXPECT_EQ(decoded->data.stats.level, 20);
  EXPECT_EQ(decoded->data.stats.experience, 9876543210LL);
  EXPECT_EQ(decoded->data.map_id, 3u);
  EXPECT_EQ(decoded->data.position.x, 17);
  EXPECT_EQ(decoded->data.position.y, 29);
  EXPECT_EQ(decoded->data.equipment_json, R"([{"slot":0}])");
}

TEST(CharacterSnapshotCodecTest, LegacySnapshotInvalidAccountIdFallsBackToZero) {
  const auto bytes = BuildLegacySnapshotBytes("acc_90001");
  const auto decoded =
      mir2::ecs::DeserializeCharacterSnapshotWithMetadata(bytes.data(), bytes.size());

  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->migrated_from_legacy);
  EXPECT_EQ(decoded->data.account_id, 0u);
}

TEST(CharacterSnapshotCodecTest, InvalidSnapshotReturnsNullopt) {
  const std::vector<uint8_t> invalid = {0x01, 0x02, 0x03, 0x04};
  const auto decoded =
      mir2::ecs::DeserializeCharacterSnapshotWithMetadata(invalid.data(), invalid.size());
  EXPECT_FALSE(decoded.has_value());
}
