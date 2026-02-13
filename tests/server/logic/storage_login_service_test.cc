#include <gtest/gtest.h>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include <chrono>
#include <future>
#include <thread>

#include "common/types/error_codes.h"
#include "logic/coroutine_executor.h"
#include "logic/services/storage_login_service.h"

namespace mir2::logic::test {
namespace {

using namespace std::chrono_literals;

Task<void> HoldExecutorSlot(CoroutineExecutor* executor,
                            std::shared_future<void> release_signal) {
  co_await executor->Async([release_signal]() mutable {
    release_signal.wait();
  });
  co_return;
}

class StorageLoginServiceTest : public ::testing::Test {
 protected:
  using WorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;
};

TEST_F(StorageLoginServiceTest, LoginReturnsOverloadedWhenExecutorIsAtLimit) {
  asio::io_context io_context;
  auto guard = asio::make_work_guard(io_context);
  std::thread io_thread([&]() { io_context.run(); });

  CoroutineExecutor executor(io_context, 1, 1);
  StorageLoginService service(executor, nullptr);

  std::promise<void> hold_promise;
  auto hold_signal = hold_promise.get_future().share();
  ASSERT_TRUE(executor.Spawn(HoldExecutorSlot(&executor, hold_signal)));

  std::promise<LoginResult> result_promise;
  auto result_future = result_promise.get_future();
  service.Login("user", "pass",
                [&result_promise](const LoginResult& result) {
                  result_promise.set_value(result);
                });

  ASSERT_EQ(result_future.wait_for(2s), std::future_status::ready);
  const LoginResult result = result_future.get();
  EXPECT_EQ(result.code, mir2::common::ErrorCode::SERVER_OVERLOADED);

  hold_promise.set_value();
  EXPECT_TRUE(executor.DrainAndJoin(2s));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(StorageLoginServiceTest, LoginReturnsMaintenanceWhenExecutorStopsAccepting) {
  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1, 1);
  StorageLoginService service(executor, nullptr);

  executor.StopAccepting();

  std::promise<LoginResult> result_promise;
  auto result_future = result_promise.get_future();
  service.Login("user", "pass",
                [&result_promise](const LoginResult& result) {
                  result_promise.set_value(result);
                });

  ASSERT_EQ(result_future.wait_for(1s), std::future_status::ready);
  const LoginResult result = result_future.get();
  EXPECT_EQ(result.code, mir2::common::ErrorCode::SERVER_MAINTENANCE);
}

}  // namespace
}  // namespace mir2::logic::test
