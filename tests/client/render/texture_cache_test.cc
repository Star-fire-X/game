#include <gtest/gtest.h>

#include "client/resource/resource_loader.h"

#ifdef HAS_SDL2
#include <atomic>
#include <thread>
#include <vector>

#include "render/renderer.h"
#endif

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

#ifdef HAS_SDL2
namespace {
mir2::client::Sprite MakeSprite(int width, int height) {
    mir2::client::Sprite sprite;
    sprite.width = width;
    sprite.height = height;
    sprite.pixels.assign(static_cast<size_t>(width * height), 0xFF00FF00u);
    return sprite;
}

std::shared_ptr<mir2::render::Texture> MakeDummyTexture(const mir2::client::Sprite& sprite,
                                                        bool /*flip_vertical*/) {
    return std::make_shared<mir2::render::Texture>(nullptr, sprite.width, sprite.height);
}

void WaitForStart(const std::atomic<bool>& start_flag) {
    while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}
}  // namespace

TEST(TextureCacheTest, SdlTextureCacheConcurrentGetsAreSafe) {
    mir2::render::SDLTextureCache cache(MakeDummyTexture, 0);
    const auto sprite = MakeSprite(8, 8);

    constexpr int kEntries = 32;
    for (int i = 0; i < kEntries; ++i) {
        mir2::render::TextureKey key{1, static_cast<uint32_t>(i), false};
        auto texture = cache.get_or_create(key, sprite);
        ASSERT_NE(texture, nullptr);
    }

    constexpr int kThreads = 8;
    constexpr int kReadsPerThread = 500;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            WaitForStart(start);
            for (int i = 0; i < kReadsPerThread; ++i) {
                mir2::render::TextureKey key{1, static_cast<uint32_t>(i % kEntries), false};
                auto texture = cache.get(key);
                EXPECT_NE(texture, nullptr);
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < kThreads) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(cache.size(), static_cast<size_t>(kEntries));
}

TEST(TextureCacheTest, SdlTextureCacheConcurrentGetOrCreateAreSafe) {
    mir2::render::SDLTextureCache cache(MakeDummyTexture, 0);
    const auto sprite = MakeSprite(8, 8);

    constexpr int kThreads = 8;
    constexpr int kWritesPerThread = 64;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            WaitForStart(start);
            for (int i = 0; i < kWritesPerThread; ++i) {
                const std::string key = "cache_key_" + std::to_string(t) + "_" + std::to_string(i);
                auto texture = cache.get_or_create(key, sprite, (i % 2) == 0);
                EXPECT_NE(texture, nullptr);
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < kThreads) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(cache.size(), static_cast<size_t>(kThreads * kWritesPerThread));
}

TEST(TextureCacheTest, SdlTextureCacheConcurrentReadWriteAreSafe) {
    mir2::render::SDLTextureCache cache(MakeDummyTexture, 0);
    const auto sprite = MakeSprite(8, 8);

    constexpr int kInitialEntries = 16;
    for (int i = 0; i < kInitialEntries; ++i) {
        mir2::render::TextureKey key{2, static_cast<uint32_t>(i), false};
        ASSERT_NE(cache.get_or_create(key, sprite), nullptr);
        const std::string str_key = "base_key_" + std::to_string(i);
        ASSERT_NE(cache.get_or_create(str_key, sprite, false), nullptr);
    }

    constexpr int kReaderThreads = 6;
    constexpr int kWriterThreads = 3;
    constexpr int kReadOps = 300;
    constexpr int kWriteOps = 120;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    threads.reserve(kReaderThreads + kWriterThreads);

    for (int t = 0; t < kReaderThreads; ++t) {
        threads.emplace_back([&]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            WaitForStart(start);
            for (int i = 0; i < kReadOps; ++i) {
                mir2::render::TextureKey key{2, static_cast<uint32_t>(i % kInitialEntries), false};
                auto texture = cache.get(key);
                EXPECT_NE(texture, nullptr);
                const std::string str_key = "base_key_" + std::to_string(i % kInitialEntries);
                auto str_texture = cache.get(str_key, false);
                EXPECT_NE(str_texture, nullptr);
            }
        });
    }

    for (int t = 0; t < kWriterThreads; ++t) {
        threads.emplace_back([&, t]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            WaitForStart(start);
            for (int i = 0; i < kWriteOps; ++i) {
                mir2::render::TextureKey key{3, static_cast<uint32_t>(t * kWriteOps + i), (i % 2) == 0};
                auto texture = cache.get_or_create(key, sprite);
                EXPECT_NE(texture, nullptr);
                const std::string str_key = "dynamic_key_" + std::to_string(t) + "_" + std::to_string(i);
                auto str_texture = cache.get_or_create(str_key, sprite, (i % 2) != 0);
                EXPECT_NE(str_texture, nullptr);
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < (kReaderThreads + kWriterThreads)) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    for (int i = 0; i < kInitialEntries; ++i) {
        mir2::render::TextureKey key{2, static_cast<uint32_t>(i), false};
        EXPECT_NE(cache.get(key), nullptr);
        const std::string str_key = "base_key_" + std::to_string(i);
        EXPECT_NE(cache.get(str_key, false), nullptr);
    }
}

TEST(TextureCacheTest, SdlTextureCacheConcurrentEvictAreSafe) {
    mir2::render::SDLTextureCache cache(MakeDummyTexture, 0);
    const auto sprite = MakeSprite(8, 8);

    constexpr int kTextureKeys = 32;
    constexpr int kStringKeys = 32;
    for (int i = 0; i < kTextureKeys; ++i) {
        mir2::render::TextureKey key{5, static_cast<uint32_t>(i), (i % 2) == 0};
        ASSERT_NE(cache.get_or_create(key, sprite), nullptr);
    }
    for (int i = 0; i < kStringKeys; ++i) {
        const std::string key = "evict_key_" + std::to_string(i);
        ASSERT_NE(cache.get_or_create(key, sprite, (i % 2) != 0), nullptr);
    }

    constexpr int kTextureEvictThreads = 3;
    constexpr int kStringEvictThreads = 3;
    constexpr int kCreateThreads = 4;
    constexpr int kEvictOps = 300;
    constexpr int kCreateOps = 240;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    threads.reserve(kTextureEvictThreads + kStringEvictThreads + kCreateThreads);

    for (int t = 0; t < kTextureEvictThreads; ++t) {
        threads.emplace_back([&, t]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            WaitForStart(start);
            for (int i = 0; i < kEvictOps; ++i) {
                mir2::render::TextureKey key{5, static_cast<uint32_t>((t + i) % kTextureKeys), (i % 2) == 0};
                cache.evict(key);
            }
        });
    }

    for (int t = 0; t < kStringEvictThreads; ++t) {
        threads.emplace_back([&, t]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            WaitForStart(start);
            for (int i = 0; i < kEvictOps; ++i) {
                const std::string key = "evict_key_" + std::to_string((t + i) % kStringKeys);
                cache.evict(key);
            }
        });
    }

    for (int t = 0; t < kCreateThreads; ++t) {
        threads.emplace_back([&, t]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            WaitForStart(start);
            for (int i = 0; i < kCreateOps; ++i) {
                if ((i % 2) == 0) {
                    mir2::render::TextureKey key{5, static_cast<uint32_t>((t + i) % kTextureKeys), (i % 2) == 0};
                    auto texture = cache.get_or_create(key, sprite);
                    EXPECT_NE(texture, nullptr);
                } else {
                    const std::string key = "evict_key_" + std::to_string((t + i) % kStringKeys);
                    auto texture = cache.get_or_create(key, sprite, (i % 3) == 0);
                    EXPECT_NE(texture, nullptr);
                }
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < (kTextureEvictThreads + kStringEvictThreads + kCreateThreads)) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }
}

TEST(TextureCacheTest, SdlTextureCacheConcurrentClearAreSafe) {
    mir2::render::SDLTextureCache cache(MakeDummyTexture, 0);
    const auto sprite = MakeSprite(8, 8);

    constexpr int kWarmupEntries = 12;
    for (int i = 0; i < kWarmupEntries; ++i) {
        mir2::render::TextureKey key{6, static_cast<uint32_t>(i), false};
        ASSERT_NE(cache.get_or_create(key, sprite), nullptr);
    }

    constexpr int kWriterThreads = 6;
    constexpr int kClearThreads = 3;
    constexpr int kWriteOps = 200;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<int> writers_done{0};
    std::vector<std::thread> threads;
    threads.reserve(kWriterThreads + kClearThreads);

    for (int t = 0; t < kWriterThreads; ++t) {
        threads.emplace_back([&, t]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            WaitForStart(start);
            for (int i = 0; i < kWriteOps; ++i) {
                if ((i % 2) == 0) {
                    mir2::render::TextureKey key{6, static_cast<uint32_t>(t * kWriteOps + i), (i % 3) == 0};
                    auto texture = cache.get_or_create(key, sprite);
                    EXPECT_NE(texture, nullptr);
                } else {
                    const std::string key = "clear_key_" + std::to_string(t) + "_" + std::to_string(i);
                    auto texture = cache.get_or_create(key, sprite, (i % 4) == 0);
                    EXPECT_NE(texture, nullptr);
                }
            }
            writers_done.fetch_add(1, std::memory_order_acq_rel);
        });
    }

    for (int t = 0; t < kClearThreads; ++t) {
        threads.emplace_back([&]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            WaitForStart(start);
            while (writers_done.load(std::memory_order_acquire) < kWriterThreads) {
                cache.clear();
                std::this_thread::yield();
            }
            cache.clear();
        });
    }

    while (ready.load(std::memory_order_acquire) < (kWriterThreads + kClearThreads)) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(cache.size(), 0u);
}

TEST(TextureCacheTest, SdlTextureCacheStressHonorsCapacity) {
    const int width = 16;
    const int height = 16;
    const size_t bytes_per_texture =
        static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(uint32_t);
    const size_t max_entries = 32;
    const size_t max_bytes = bytes_per_texture * max_entries;

    mir2::render::SDLTextureCache cache(MakeDummyTexture, max_bytes);
    const auto sprite = MakeSprite(width, height);

    constexpr int kThreads = 12;
    constexpr int kOpsPerThread = 200;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            ready.fetch_add(1, std::memory_order_acq_rel);
            WaitForStart(start);
            for (int i = 0; i < kOpsPerThread; ++i) {
                if ((i % 3) == 0) {
                    mir2::render::TextureKey key{4, static_cast<uint32_t>((t + i) % 64), (i % 2) == 0};
                    cache.get(key);
                } else {
                    mir2::render::TextureKey key{static_cast<uint16_t>(t + 1),
                                                 static_cast<uint32_t>(t * kOpsPerThread + i),
                                                 (i % 2) != 0};
                    auto texture = cache.get_or_create(key, sprite);
                    EXPECT_NE(texture, nullptr);
                }
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < kThreads) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_LE(cache.current_bytes(), max_bytes);
    EXPECT_LE(cache.size(), max_entries);
}

TEST(TextureCacheTest, SdlTextureCacheTextureKeyGetOrCreate) {
    int factory_calls = 0;
    mir2::render::SDLTextureCache cache(
        [&](const mir2::client::Sprite& sprite, bool /*flip_vertical*/) {
            ++factory_calls;
            return std::make_shared<mir2::render::Texture>(nullptr, sprite.width, sprite.height);
        },
        1024);

    const auto sprite = MakeSprite(2, 2);
    const mir2::render::TextureKey key{7, 42, true};

    EXPECT_EQ(cache.get(key), nullptr);

    auto created = cache.get_or_create(key, sprite);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(cache.get(key), created);
    EXPECT_EQ(factory_calls, 1);

    auto cached = cache.get_or_create(key, sprite);
    EXPECT_EQ(cached, created);
    EXPECT_EQ(factory_calls, 1);
}

TEST(TextureCacheTest, SdlTextureCacheMixedKeyTypesCoexist) {
    mir2::render::SDLTextureCache cache(MakeDummyTexture, 1024);
    const auto sprite = MakeSprite(1, 1);

    auto string_texture = cache.get_or_create("ui:button", sprite, false);
    auto key_texture = cache.get_or_create(mir2::render::TextureKey{5, 9, false}, sprite);

    ASSERT_NE(string_texture, nullptr);
    ASSERT_NE(key_texture, nullptr);
    EXPECT_NE(string_texture, key_texture);
    EXPECT_EQ(cache.size(), 2u);
}

TEST(TextureCacheTest, SdlTextureCacheLruEvictionHandlesMixedKeys) {
    const auto sprite = MakeSprite(1, 1);  // 4 bytes per texture
    mir2::render::SDLTextureCache cache(MakeDummyTexture, 8);  // allow 2 entries

    const std::string string_key = "lru_string";
    const mir2::render::TextureKey key_a{1, 1, false};
    const mir2::render::TextureKey key_b{1, 2, false};

    ASSERT_NE(cache.get_or_create(string_key, sprite, false), nullptr);
    ASSERT_NE(cache.get_or_create(key_a, sprite), nullptr);
    EXPECT_EQ(cache.size(), 2u);

    ASSERT_NE(cache.get(string_key, false), nullptr);  // touch string key
    ASSERT_NE(cache.get_or_create(key_b, sprite), nullptr);  // should evict key_a

    EXPECT_EQ(cache.get(key_a), nullptr);
    EXPECT_NE(cache.get(string_key, false), nullptr);
    EXPECT_NE(cache.get(key_b), nullptr);
    EXPECT_EQ(cache.size(), 2u);
}
#else
TEST(TextureCacheTest, SdlTextureCacheConcurrentGetsAreSafe) {
    GTEST_SKIP() << "SDL2 not available; skipping SDLTextureCache tests.";
}

TEST(TextureCacheTest, SdlTextureCacheConcurrentGetOrCreateAreSafe) {
    GTEST_SKIP() << "SDL2 not available; skipping SDLTextureCache tests.";
}

TEST(TextureCacheTest, SdlTextureCacheConcurrentReadWriteAreSafe) {
    GTEST_SKIP() << "SDL2 not available; skipping SDLTextureCache tests.";
}

TEST(TextureCacheTest, SdlTextureCacheStressHonorsCapacity) {
    GTEST_SKIP() << "SDL2 not available; skipping SDLTextureCache tests.";
}

TEST(TextureCacheTest, SdlTextureCacheConcurrentEvictAreSafe) {
    GTEST_SKIP() << "SDL2 not available; skipping SDLTextureCache tests.";
}

TEST(TextureCacheTest, SdlTextureCacheConcurrentClearAreSafe) {
    GTEST_SKIP() << "SDL2 not available; skipping SDLTextureCache tests.";
}
#endif
