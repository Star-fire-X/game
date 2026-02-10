#ifndef MIR2_STORAGE_ENGINE_PERSISTENCE_BLOCKING_QUEUE_H_
#define MIR2_STORAGE_ENGINE_PERSISTENCE_BLOCKING_QUEUE_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

namespace mir2::storage_engine::persistence {

// Thread-safe blocking queue.
template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(size_t capacity = 10000)
        : capacity_(capacity), queue_size_(0) {}
    ~BlockingQueue() = default;

    bool Enqueue(T item) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (queue_.size() >= capacity_) {
            return false;
        }

        queue_.push_back(std::move(item));
        queue_size_.fetch_add(1, std::memory_order_release);
        lock.unlock();
        cv_.notify_one();

        return true;
    }

    T Pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });

        T item = std::move(queue_.front());
        queue_.pop_front();
        queue_size_.fetch_sub(1, std::memory_order_release);
        return item;
    }

    std::optional<T> Pop(uint32_t timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);

        bool notified = cv_.wait_for(
            lock, std::chrono::milliseconds(timeout_ms),
            [this] { return !queue_.empty(); });

        if (!notified || queue_.empty()) {
            return std::nullopt;
        }

        T item = std::move(queue_.front());
        queue_.pop_front();
        queue_size_.fetch_sub(1, std::memory_order_release);
        return item;
    }

    size_t Size() const {
        return queue_size_.load(std::memory_order_acquire);
    }

    void Clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.clear();
        queue_size_.store(0, std::memory_order_release);
    }

    bool IsEmpty() const {
        return queue_size_.load(std::memory_order_acquire) == 0;
    }

    bool IsFull() const {
        return queue_size_.load(std::memory_order_acquire) >= capacity_;
    }

    void NotifyAll() { cv_.notify_all(); }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    const size_t capacity_;
    std::atomic<size_t> queue_size_;
};

}  // namespace mir2::storage_engine::persistence

#endif  // MIR2_STORAGE_ENGINE_PERSISTENCE_BLOCKING_QUEUE_H_
