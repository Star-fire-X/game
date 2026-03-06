#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include "logic/coroutine_executor.h"
#include "logic/handler_registry.h"

namespace mir2::logic {
namespace {

TEST(HandlerRegistryTest, DispatchMessageReturnsInvalidTaskWhenHandlerMissing) {
  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  HandlerRegistry registry(executor);

  HandlerContext context;
  context.client_id = 1001;

  const SpawnResult result = registry.DispatchMessage(
      context,
      static_cast<uint16_t>(12345),
      nullptr,
      0);

  EXPECT_EQ(result, SpawnResult::kInvalidTask);
}

}  // namespace
}  // namespace mir2::logic
