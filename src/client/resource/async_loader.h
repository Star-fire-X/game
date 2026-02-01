// =============================================================================
// Legend2 异步资源加载器 (Async Loader)
// =============================================================================

#ifndef LEGEND2_CLIENT_RESOURCE_ASYNC_LOADER_H
#define LEGEND2_CLIENT_RESOURCE_ASYNC_LOADER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "client/resource/resource_loader.h"
#include "client/resource/i_resource_provider.h"

namespace mir2::client {

/// 异步资源加载器
/// 使用线程池加载资源，回调在主线程 poll 时执行
class AsyncLoader {
public:
    explicit AsyncLoader(IResourceProvider& provider, size_t thread_count = 2);
    ~AsyncLoader();

    // Non-copyable
    AsyncLoader(const AsyncLoader&) = delete;
    AsyncLoader& operator=(const AsyncLoader&) = delete;

    /// 异步请求精灵
    void request_sprite(const std::string& archive, int index,
                        std::function<void(std::optional<Sprite>)> callback);

    /// 异步请求地图
    void request_map(const std::string& path,
                     std::function<void(std::optional<MapData>)> callback);

    /// 主线程每帧调用，派发完成回调
    void poll(int max_completions = 8);

    /// 关闭线程池
    void shutdown();

private:
    void worker_loop();

    IResourceProvider& provider_;
    std::vector<std::thread> workers_;

    std::queue<std::function<void()>> tasks_;
    std::mutex tasks_mutex_;
    std::condition_variable tasks_cv_;

    std::queue<std::function<void()>> completions_;
    std::mutex completions_mutex_;

    std::atomic<bool> stop_{false};
};

} // namespace mir2::client

#endif // LEGEND2_CLIENT_RESOURCE_ASYNC_LOADER_H
