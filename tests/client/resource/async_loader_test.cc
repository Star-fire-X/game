#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>
#include <atomic>
#include <chrono>

#include "client/resource/async_loader.h"

namespace {

class FakeResourceProvider : public mir2::client::IResourceProvider {
public:
    std::optional<mir2::client::Sprite> sprite_result;
    std::optional<mir2::client::MapData> map_result;

    std::atomic<int> sprite_calls{0};
    std::atomic<int> map_calls{0};

    std::mutex mutex;
    std::condition_variable cv;

    std::optional<mir2::client::Sprite> get_sprite(const std::string& /*archive*/, int /*index*/) override {
        sprite_calls.fetch_add(1);
        cv.notify_all();
        return sprite_result;
    }

    std::optional<mir2::client::MapData> load_map(const std::string& /*path*/) override {
        map_calls.fetch_add(1);
        cv.notify_all();
        return map_result;
    }

    bool is_archive_loaded(const std::string& /*name*/) const override { return true; }
    bool load_archive(const std::string& /*path*/) override { return true; }
};

mir2::client::Sprite MakeSprite(int width, int height) {
    mir2::client::Sprite sprite;
    sprite.width = width;
    sprite.height = height;
    sprite.offset_x = 0;
    sprite.offset_y = 0;
    sprite.pixels.resize(static_cast<size_t>(width * height), 0xFFFFFFFF);
    return sprite;
}

} // namespace

TEST(AsyncLoaderTest, CallbackRunsOnPoll) {
    FakeResourceProvider provider;
    provider.sprite_result = MakeSprite(2, 2);

    mir2::client::AsyncLoader loader(provider, 1);

    std::atomic<bool> callback_called{false};
    loader.request_sprite("Hum", 1, [&](std::optional<mir2::client::Sprite> sprite) {
        callback_called.store(true);
        ASSERT_TRUE(sprite.has_value());
        EXPECT_EQ(sprite->width, 2);
    });

    // Wait until worker thread has processed the request.
    {
        std::unique_lock<std::mutex> lock(provider.mutex);
        provider.cv.wait_for(lock, std::chrono::milliseconds(200), [&] {
            return provider.sprite_calls.load() > 0;
        });
    }

    EXPECT_FALSE(callback_called.load());
    loader.poll(1);
    EXPECT_TRUE(callback_called.load());

    loader.shutdown();
}

TEST(AsyncLoaderTest, PollHonorsMaxCompletions) {
    FakeResourceProvider provider;
    provider.sprite_result = MakeSprite(1, 1);

    mir2::client::AsyncLoader loader(provider, 1);

    std::atomic<int> callback_count{0};
    loader.request_sprite("Hum", 1, [&](std::optional<mir2::client::Sprite>) { callback_count.fetch_add(1); });
    loader.request_sprite("Hum", 2, [&](std::optional<mir2::client::Sprite>) { callback_count.fetch_add(1); });

    // Wait for both tasks to finish.
    {
        std::unique_lock<std::mutex> lock(provider.mutex);
        provider.cv.wait_for(lock, std::chrono::milliseconds(200), [&] {
            return provider.sprite_calls.load() >= 2;
        });
    }

    loader.poll(1);
    EXPECT_EQ(callback_count.load(), 1);

    loader.poll(1);
    EXPECT_EQ(callback_count.load(), 2);

    loader.shutdown();
}
