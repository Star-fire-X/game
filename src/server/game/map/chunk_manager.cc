#include "game/map/chunk_manager.h"

#include <unordered_set>
#include <utility>
#include <vector>

namespace mir2::game::map {
namespace {

static_assert(ChunkManager::kExperimental,
              "ChunkManager is expected to stay experimental in current phase.");

int32_t FloorDiv(int32_t value, int32_t divisor) {
  if (divisor <= 0) {
    return 0;
  }
  int32_t q = value / divisor;
  const int32_t r = value % divisor;
  if (r != 0 && ((r < 0) != (divisor < 0))) {
    --q;
  }
  return q;
}

}  // namespace

void ChunkManager::LoadChunk(int32_t chunk_x, int32_t chunk_y) {
  const ChunkId id{chunk_x, chunk_y};
  auto it = chunks_.find(id);
  if (it == chunks_.end()) {
    Chunk chunk;
    chunk.chunk_x = chunk_x;
    chunk.chunk_y = chunk_y;
    // TODO: load tile data for this chunk from map files/cache.
    chunk.loaded = true;
    chunks_.emplace(id, std::move(chunk));
    return;
  }

  if (!it->second.loaded) {
    // TODO: load tile data for this chunk from map files/cache.
    it->second.tiles.clear();
    it->second.loaded = true;
  }
}

void ChunkManager::UnloadChunk(int32_t chunk_x, int32_t chunk_y) {
  const ChunkId id{chunk_x, chunk_y};
  auto it = chunks_.find(id);
  if (it == chunks_.end()) {
    return;
  }

  // TODO: release cached tile data if needed.
  it->second.tiles.clear();
  chunks_.erase(it);
}

void ChunkManager::UpdateActiveWindow(int32_t entity_x, int32_t entity_y) {
  const ChunkId center{
      FloorDiv(entity_x, kChunkSize),
      FloorDiv(entity_y, kChunkSize),
  };
  const int32_t radius = kActiveWindow / 2;

  std::unordered_set<ChunkId, ChunkIdHash> active;
  active.reserve(static_cast<size_t>(kActiveWindow * kActiveWindow));

  for (int32_t dy = -radius; dy <= radius; ++dy) {
    for (int32_t dx = -radius; dx <= radius; ++dx) {
      const int32_t chunk_x = center.x + dx;
      const int32_t chunk_y = center.y + dy;
      LoadChunk(chunk_x, chunk_y);
      active.insert({chunk_x, chunk_y});
    }
  }

  std::vector<ChunkId> to_unload;
  to_unload.reserve(chunks_.size());
  for (const auto& [id, chunk] : chunks_) {
    if (active.find(id) == active.end()) {
      to_unload.push_back(id);
    }
  }

  for (const auto& id : to_unload) {
    UnloadChunk(id.x, id.y);
  }
}

const ChunkManager::Chunk* ChunkManager::GetChunk(int32_t chunk_x,
                                                  int32_t chunk_y) const {
  const ChunkId id{chunk_x, chunk_y};
  auto it = chunks_.find(id);
  if (it == chunks_.end()) {
    return nullptr;
  }
  return &it->second;
}

}  // namespace mir2::game::map
