// =============================================================================
// Legend2 异步资源加载器实现 (Async Loader Implementation)
// =============================================================================

#include "client/resource/async_loader.h"

#include <algorithm>

namespace mir2::client {

AsyncLoader::AsyncLoader(IResourceProvider& provider, size_t thread_count)
    : provider_(provider) {
    const size_t actual_threads = std::max<size_t>(1, thread_count);
    workers_.reserve(actual_threads);
    for (size_t i = 0; i < actual_threads; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
}

AsyncLoader::~AsyncLoader() {
    shutdown();
}

void AsyncLoader::request_sprite(const std::string& archive, int index,
                                 std::function<void(std::optional<Sprite>)> callback) {
    if (stop_.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_.push([this, archive, index, callback = std::move(callback)]() mutable {
            auto result = provider_.get_sprite(archive, index);
            std::lock_guard<std::mutex> completion_lock(completions_mutex_);
            completions_.push([callback = std::move(callback), result = std::move(result)]() mutable {
                if (callback) {
                    callback(std::move(result));
                }
            });
        });
    }
    tasks_cv_.notify_one();
}

void AsyncLoader::request_map(const std::string& path,
                              std::function<void(std::optional<MapData>)> callback) {
    if (stop_.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_.push([this, path, callback = std::move(callback)]() mutable {
            auto result = provider_.load_map(path);
            std::lock_guard<std::mutex> completion_lock(completions_mutex_);
            completions_.push([callback = std::move(callback), result = std::move(result)]() mutable {
                if (callback) {
                    callback(std::move(result));
                }
            });
        });
    }
    tasks_cv_.notify_one();
}

void AsyncLoader::poll(int max_completions) {
    for (int i = 0; i < max_completions; ++i) {
        std::function<void()> completion;
        {
            std::lock_guard<std::mutex> lock(completions_mutex_);
            if (completions_.empty()) {
                break;
            }
            completion = std::move(completions_.front());
            completions_.pop();
        }
        if (completion) {
            completion();
        }
    }
}

void AsyncLoader::shutdown() {
    if (stop_.exchange(true)) {
        return;
    }
    tasks_cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        while (!tasks_.empty()) {
            tasks_.pop();
        }
    }
    {
        std::lock_guard<std::mutex> lock(completions_mutex_);
        while (!completions_.empty()) {
            completions_.pop();
        }
    }
}

void AsyncLoader::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_cv_.wait(lock, [this]() { return stop_.load() || !tasks_.empty(); });
            if (stop_.load() && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        if (task) {
            task();
        }
    }
}

} // namespace mir2::client
