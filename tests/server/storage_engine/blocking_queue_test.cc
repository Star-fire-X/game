/**
 * @file blocking_queue_test.cc
 * @brief BlockingQueue depth/monitoring behavior tests.
 */

#include "storage_engine/persistence/blocking_queue.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

namespace mir2::storage_engine::persistence {
namespace {

TEST(BlockingQueueMetricsTest, SizeAndFlagsReflectQueueDepth) {
  BlockingQueue<int> queue(2);

  EXPECT_EQ(queue.Size(), 0u);
  EXPECT_TRUE(queue.IsEmpty());
  EXPECT_FALSE(queue.IsFull());

  EXPECT_TRUE(queue.Enqueue(1));
  EXPECT_EQ(queue.Size(), 1u);
  EXPECT_FALSE(queue.IsEmpty());
  EXPECT_FALSE(queue.IsFull());

  EXPECT_TRUE(queue.Enqueue(2));
  EXPECT_EQ(queue.Size(), 2u);
  EXPECT_FALSE(queue.IsEmpty());
  EXPECT_TRUE(queue.IsFull());

  EXPECT_EQ(queue.Pop(), 1);
  EXPECT_EQ(queue.Size(), 1u);
  EXPECT_FALSE(queue.IsEmpty());
  EXPECT_FALSE(queue.IsFull());

  queue.Clear();
  EXPECT_EQ(queue.Size(), 0u);
  EXPECT_TRUE(queue.IsEmpty());
  EXPECT_FALSE(queue.IsFull());
}

TEST(BlockingQueueMetricsTest, MonitoringMethodsRemainStableUnderConcurrency) {
  constexpr size_t kCapacity = 1024;
  constexpr int kItemCount = 4000;
  BlockingQueue<int> queue(kCapacity);

  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  std::atomic<bool> producer_done{false};
  std::atomic<bool> observer_violation{false};

  std::thread producer([&]() {
    for (int i = 0; i < kItemCount; ++i) {
      while (!queue.Enqueue(i)) {
        std::this_thread::yield();
      }
      produced.fetch_add(1, std::memory_order_relaxed);
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::thread consumer([&]() {
    while (consumed.load(std::memory_order_acquire) < kItemCount) {
      auto value = queue.Pop(1);
      if (!value.has_value()) {
        continue;
      }
      consumed.fetch_add(1, std::memory_order_relaxed);
    }
  });

  std::thread observer([&]() {
    while (!producer_done.load(std::memory_order_acquire) ||
           consumed.load(std::memory_order_acquire) < kItemCount) {
      const size_t depth = queue.Size();
      if (depth > kCapacity) {
        observer_violation.store(true, std::memory_order_release);
        break;
      }

      const bool empty = queue.IsEmpty();
      const bool full = queue.IsFull();
      if ((depth == 0 && !empty) || (depth == kCapacity && !full)) {
        observer_violation.store(true, std::memory_order_release);
        break;
      }

      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  });

  producer.join();
  consumer.join();
  observer.join();

  EXPECT_EQ(produced.load(std::memory_order_relaxed), kItemCount);
  EXPECT_EQ(consumed.load(std::memory_order_relaxed), kItemCount);
  EXPECT_FALSE(observer_violation.load(std::memory_order_acquire));
  EXPECT_EQ(queue.Size(), 0u);
  EXPECT_TRUE(queue.IsEmpty());
}

}  // namespace
}  // namespace mir2::storage_engine::persistence
