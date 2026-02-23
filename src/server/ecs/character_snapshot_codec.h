#ifndef MIR2_ECS_CHARACTER_SNAPSHOT_CODEC_H_
#define MIR2_ECS_CHARACTER_SNAPSHOT_CODEC_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "common/character_data.h"

namespace mir2::ecs {

struct CharacterSnapshotDecodeResult {
  common::CharacterData data;
  bool migrated_from_legacy = false;
};

/// Serialize CharacterData to FlatBuffers bytes (detached buffer copy).
std::vector<uint8_t> SerializeCharacterSnapshot(const common::CharacterData& data);

/// Deserialize FlatBuffers bytes back to CharacterData.
/// Returns nullopt if the buffer is invalid.
std::optional<common::CharacterData> DeserializeCharacterSnapshot(
    const uint8_t* buf, size_t size);

/// Deserialize with migration metadata.
/// `migrated_from_legacy` is true when the payload used the historical
/// `account_id:string` snapshot layout.
std::optional<CharacterSnapshotDecodeResult> DeserializeCharacterSnapshotWithMetadata(
    const uint8_t* buf, size_t size);

}  // namespace mir2::ecs

#endif  // MIR2_ECS_CHARACTER_SNAPSHOT_CODEC_H_
