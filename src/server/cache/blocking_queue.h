#ifndef MIR2_SERVER_CACHE_BLOCKING_QUEUE_H_
#define MIR2_SERVER_CACHE_BLOCKING_QUEUE_H_

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <cstdint>

namespace mir2::cache {

// 线程安全的阻塞队列
// 支持生产者-消费者模式
// - Enqueue(): 非阻塞入队
// - Pop(): 阻塞式出队
// - Pop(timeout_ms): 带超时的阻塞式出队
template <typename T>
class BlockingQueue {
public:
    // 构造函数，指定队列容量
    explicit BlockingQueue(size_t capacity = 10000) : capacity_(capacity) {}

    // 析构函数
    ~BlockingQueue() = default;

    // 入队（非阻塞）
    // 返回 true 表示成功，false 表示队列已满
    bool Enqueue(T item) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (queue_.size() >= capacity_) {
            return false;
        }

        queue_.push_back(std::move(item));
        lock.unlock();

        // 通知一个等待的消费者
        cv_.notify_one();

        return true;
    }

    // 出队（阻塞，直到有数据）
    T Pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        // 等待直到队列非空
        cv_.wait(lock, [this] { return !queue_.empty(); });

        T item = std::move(queue_.front());
        queue_.pop_front();

        return item;
    }

    // 出队（带超时，毫秒）
    // 返回 std::nullopt 表示超时或队列为空
    std::optional<T> Pop(uint32_t timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);

        // 等待直到队列非空或超时
        bool notified = cv_.wait_for(
            lock, std::chrono::milliseconds(timeout_ms),
            [this] { return !queue_.empty(); });

        if (!notified || queue_.empty()) {
            return std::nullopt;
        }

        T item = std::move(queue_.front());
        queue_.pop_front();

        return item;
    }

    // 获取当前队列大小
    size_t Size() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }

    // 清空队列
    void Clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.clear();
    }

    // 检查队列是否为空
    bool IsEmpty() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // 检查队列是否已满
    bool IsFull() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size() >= capacity_;
    }

private:
    // 互斥锁
    mutable std::mutex mutex_;

    // 条件变量
    std::condition_variable cv_;

    // 双端队列
    std::deque<T> queue_;

    // 容量限制
    size_t capacity_;
};

}  // namespace mir2::cache

#endif  // MIR2_SERVER_CACHE_BLOCKING_QUEUE_H_
