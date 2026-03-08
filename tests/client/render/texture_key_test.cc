#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "render/i_texture_cache.h"

#ifdef HAS_SDL2
#include "render/renderer.h"
#endif

namespace {

TEST(TextureKeyTest, EqualityAndHashConsistency) {
    mir2::render::TextureKey key_a{1, 2, true};
    mir2::render::TextureKey key_b{1, 2, true};
    mir2::render::TextureKey key_c{1, 3, true};
    mir2::render::TextureKey key_d{1, 2, false};

    EXPECT_TRUE(key_a == key_b);
    EXPECT_FALSE(key_a == key_c);
    EXPECT_FALSE(key_a == key_d);

    mir2::render::TextureKeyHash hasher;
    EXPECT_EQ(hasher(key_a), hasher(key_b));
    EXPECT_NE(hasher(key_a), hasher(key_c));
    EXPECT_NE(hasher(key_a), hasher(key_d));
}

#ifdef HAS_SDL2
TEST(ArchiveRegistryTest, RegistersAndResolvesNames) {
    auto& registry = mir2::render::ArchiveRegistry::instance();
    const std::string name = "archive_registry_basic";

    uint16_t id_first = registry.get_or_register(name);
    uint16_t id_second = registry.get_or_register(name);

    EXPECT_NE(id_first, 0);
    EXPECT_EQ(id_first, id_second);

    const std::string* resolved = registry.get_name(id_first);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(*resolved, name);
}

TEST(ArchiveRegistryTest, HandlesEmptyNames) {
    auto& registry = mir2::render::ArchiveRegistry::instance();
    EXPECT_EQ(registry.get_or_register(""), 0);
    EXPECT_EQ(registry.get_name(0), nullptr);
}

TEST(ArchiveRegistryTest, ThreadSafeRegistration) {
    auto& registry = mir2::render::ArchiveRegistry::instance();
    const std::string name = "archive_registry_threadsafe";

    constexpr int kThreads = 8;
    constexpr int kIterations = 200;

    std::vector<uint16_t> ids(kThreads, 0);
    std::atomic<bool> ok{true};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i]() {
            uint16_t last_id = 0;
            for (int j = 0; j < kIterations; ++j) {
                last_id = registry.get_or_register(name);
                const std::string* resolved = registry.get_name(last_id);
                if (!resolved || *resolved != name) {
                    ok.store(false, std::memory_order_relaxed);
                }
            }
            ids[i] = last_id;
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_TRUE(ok.load());
    for (int i = 1; i < kThreads; ++i) {
        EXPECT_EQ(ids[i], ids[0]);
    }
    EXPECT_NE(ids[0], 0);
}
#else
TEST(ArchiveRegistryTest, RegistersAndResolvesNames) {
    GTEST_SKIP() << "SDL2 not available; skipping ArchiveRegistry tests.";
}

TEST(ArchiveRegistryTest, HandlesEmptyNames) {
    GTEST_SKIP() << "SDL2 not available; skipping ArchiveRegistry tests.";
}

TEST(ArchiveRegistryTest, ThreadSafeRegistration) {
    GTEST_SKIP() << "SDL2 not available; skipping ArchiveRegistry tests.";
}
#endif

}  // namespace
