#include <gtest/gtest.h>

#include "client/resource/resource_loader.h"

TEST(TextureCacheTest, LruEvictionWorks) {
    mir2::client::LRUCache<std::string, int> cache(2);

    cache.put("a", 1);
    cache.put("b", 2);
    EXPECT_TRUE(cache.contains("a"));
    EXPECT_TRUE(cache.contains("b"));

    // Touch "a" so "b" becomes least recently used.
    auto value = cache.get("a");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 1);

    cache.put("c", 3);  // should evict "b"

    EXPECT_TRUE(cache.contains("a"));
    EXPECT_FALSE(cache.contains("b"));
    EXPECT_TRUE(cache.contains("c"));
}

TEST(TextureCacheTest, CapacityShrinksAndEvicts) {
    mir2::client::LRUCache<std::string, int> cache(3);
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);

    cache.set_capacity(1);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_TRUE(cache.contains("c"));
}
