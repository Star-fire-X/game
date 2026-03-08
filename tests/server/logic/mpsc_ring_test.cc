#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "core/concurrency/mpsc_ring.h"

namespace mir2::core::concurrency {
namespace {

TEST(MpscRingQueueTest, SingleThreadRoundTrip) {
  MpscRingQueue<uint32_t, 1024> queue;

  EXPECT_TRUE(queue.TryEnqueue(42U));
  EXPECT_EQ(queue.ApproxSize(), 1U);

  uint32_t value = 0;
  EXPECT_TRUE(queue.TryDequeue(&value));
  EXPECT_EQ(value, 42U);
  EXPECT_EQ(queue.ApproxSize(), 0U);
}

TEST(MpscRingQueueTest, MultiProducerSingleConsumer) {
  constexpr uint32_t kProducerCount = 4;
  constexpr uint32_t kPerProducer = 4000;
  constexpr uint32_t kTotal = kProducerCount * kPerProducer;

  MpscRingQueue<uint32_t, 1024> queue;
  std::atomic<uint32_t> consumed{0};
  std::vector<bool> seen(kTotal, false);

  std::thread consumer([&]() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (consumed.load(std::memory_order_relaxed) < kTotal &&
           std::chrono::steady_clock::now() < deadline) {
      uint32_t value = 0;
      if (!queue.TryDequeue(&value)) {
        std::this_thread::yield();
        continue;
      }
      if (value < kTotal) {
        seen[value] = true;
      }
      consumed.fetch_add(1, std::memory_order_relaxed);
    }
  });

  std::vector<std::thread> producers;
  producers.reserve(kProducerCount);
  for (uint32_t p = 0; p < kProducerCount; ++p) {
    producers.emplace_back([&, p]() {
      for (uint32_t i = 0; i < kPerProducer; ++i) {
        const uint32_t value = p * kPerProducer + i;
        while (!queue.TryEnqueue(value)) {
          std::this_thread::yield();
        }
      }
    });
  }

  for (auto& producer : producers) {
    producer.join();
  }
  consumer.join();

  EXPECT_EQ(consumed.load(std::memory_order_relaxed), kTotal);
  for (bool mark : seen) {
    EXPECT_TRUE(mark);
  }
}

}  // namespace
}  // namespace mir2::core::concurrency
