#include "logic/thread_affinity.h"

#include <future>
#include <thread>

#include <gtest/gtest.h>

namespace {

using mir2::logic::AssertNotOnLogicThread;
using mir2::logic::AssertOnLogicThread;
using mir2::logic::BindLogicThread;
using mir2::logic::ClearLogicThread;

}  // namespace

TEST(ThreadAffinityTest, AssertOnLogicThreadWhenBound) {
  ClearLogicThread();
  BindLogicThread(std::this_thread::get_id());
  EXPECT_TRUE(AssertOnLogicThread("unit_test_main"));

  std::promise<bool> promise;
  auto future = promise.get_future();
  std::thread worker([&promise]() {
    promise.set_value(AssertOnLogicThread("unit_test_worker"));
  });
  worker.join();
  EXPECT_FALSE(future.get());

  ClearLogicThread();
}

TEST(ThreadAffinityTest, AssertNotOnLogicThreadWhenBound) {
  ClearLogicThread();
  BindLogicThread(std::this_thread::get_id());
  EXPECT_FALSE(AssertNotOnLogicThread("unit_test_main"));

  std::promise<bool> promise;
  auto future = promise.get_future();
  std::thread worker([&promise]() {
    promise.set_value(AssertNotOnLogicThread("unit_test_worker"));
  });
  worker.join();
  EXPECT_TRUE(future.get());

  ClearLogicThread();
}
