#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "logic/events/event_arena.h"

namespace mir2::logic::events {
namespace {

TEST(EventArenaTest, StoreLoadReleaseRoundTrip) {
  EventArena::Config config;
  config.max_producers = 4;
  config.total_blocks = 16;
  config.block_size = 256;

  EventArena arena(config);
  std::array<uint8_t, 6> bytes{1, 2, 3, 4, 5, 6};
  VarRef ref{};
  ASSERT_TRUE(arena.Store(bytes.data(), static_cast<uint32_t>(bytes.size()), &ref));

  const uint8_t* data = nullptr;
  uint32_t size = 0;
  ASSERT_TRUE(arena.Load(ref, &data, &size));
  ASSERT_EQ(size, bytes.size());
  for (size_t i = 0; i < bytes.size(); ++i) {
    EXPECT_EQ(data[i], bytes[i]);
  }

  arena.Release(ref);
}

TEST(EventArenaTest, InvalidRefRejected) {
  EventArena::Config config;
  config.max_producers = 2;
  config.total_blocks = 8;
  config.block_size = 128;
  EventArena arena(config);

  const uint8_t* data = nullptr;
  uint32_t size = 0;
  VarRef bad{};
  bad.producer_id = EventArena::kInvalidProducerId;
  bad.block_index = 0;
  bad.offset = 0;
  bad.length = 4;
  bad.generation = 1;
  EXPECT_FALSE(arena.Load(bad, &data, &size));
}

}  // namespace
}  // namespace mir2::logic::events
