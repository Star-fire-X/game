#include <gtest/gtest.h>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/strand.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "client/network/message_dispatcher.h"

namespace {

using legend2::MessageDispatcher;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;

NetworkPacket MakePacket(MsgId msg_id, std::vector<uint8_t> payload = {}) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(msg_id);
    packet.payload = std::move(payload);
    return packet;
}

}  // namespace

TEST(message_dispatcher, RegisterAndDispatchSingleHandler) {
    MessageDispatcher dispatcher;
    const NetworkPacket packet = MakePacket(MsgId::kLoginRsp, {1, 2, 3});

    bool called = false;
    NetworkPacket captured;

    dispatcher.RegisterHandler(MsgId::kLoginRsp,
                               [&called, &captured](const NetworkPacket& incoming) {
                                   called = true;
                                   captured = incoming;
                               });

    dispatcher.Dispatch(packet);

    EXPECT_TRUE(called);
    EXPECT_EQ(captured.msg_id, packet.msg_id);
    EXPECT_EQ(captured.payload, packet.payload);
}

TEST(message_dispatcher, ConcurrentRegistrationDifferentIds) {
    MessageDispatcher dispatcher;
    const std::vector<MsgId> msg_ids = {
        MsgId::kLoginRsp,
        MsgId::kRoleListRsp,
        MsgId::kCreateRoleRsp,
        MsgId::kSelectRoleRsp,
        MsgId::kEnterGameRsp,
        MsgId::kMoveRsp,
        MsgId::kEntityMove,
        MsgId::kEntityEnter
    };

    std::atomic<int> handled{0};
    std::vector<std::thread> threads;
    threads.reserve(msg_ids.size());

    for (const auto msg_id : msg_ids) {
        threads.emplace_back([&dispatcher, &handled, msg_id]() {
            dispatcher.RegisterHandler(msg_id, [&handled, msg_id](const NetworkPacket& packet) {
                if (packet.msg_id == static_cast<uint16_t>(msg_id)) {
                    ++handled;
                }
            });
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    for (const auto msg_id : msg_ids) {
        dispatcher.Dispatch(MakePacket(msg_id));
    }

    EXPECT_EQ(handled.load(), static_cast<int>(msg_ids.size()));
}

TEST(message_dispatcher, DuplicateRegistrationOverrides) {
    MessageDispatcher dispatcher;
    int result = 0;

    dispatcher.RegisterHandler(MsgId::kMoveRsp, [&result](const NetworkPacket&) {
        result = 1;
    });
    dispatcher.RegisterHandler(MsgId::kMoveRsp, [&result](const NetworkPacket&) {
        result = 2;
    });

    dispatcher.Dispatch(MakePacket(MsgId::kMoveRsp));

    EXPECT_EQ(result, 2);
}

TEST(message_dispatcher, UnregisteredMessageUsesDefaultHandler) {
    MessageDispatcher dispatcher;
    std::atomic<int> fallback_calls{0};

    dispatcher.SetDefaultHandler([&fallback_calls](const NetworkPacket& packet) {
        if (packet.msg_id == static_cast<uint16_t>(MsgId::kChatReq)) {
            ++fallback_calls;
        }
    });

    dispatcher.Dispatch(MakePacket(MsgId::kChatReq));

    EXPECT_EQ(fallback_calls.load(), 1);
}

TEST(message_dispatcher, StrandSerializedDispatch) {
    MessageDispatcher dispatcher;
    asio::io_context io_context;
    auto work_guard = asio::make_work_guard(io_context);
    asio::strand<asio::io_context::executor_type> strand(io_context.get_executor());

    const std::vector<MsgId> msg_ids = {
        MsgId::kLoginRsp,
        MsgId::kRoleListRsp,
        MsgId::kCreateRoleRsp,
        MsgId::kSelectRoleRsp,
        MsgId::kEnterGameRsp,
        MsgId::kMoveRsp,
        MsgId::kEntityMove,
        MsgId::kEntityEnter
    };

    std::atomic<int> handled{0};
    for (const auto msg_id : msg_ids) {
        asio::post(strand, [msg_id, &dispatcher, &handled]() {
            dispatcher.RegisterHandler(msg_id, [&handled, msg_id](const NetworkPacket& packet) {
                if (packet.msg_id == static_cast<uint16_t>(msg_id)) {
                    ++handled;
                }
            });
            dispatcher.Dispatch(MakePacket(msg_id));
        });
    }

    work_guard.reset();

    std::vector<std::thread> threads;
    threads.emplace_back([&io_context]() { io_context.run(); });
    threads.emplace_back([&io_context]() { io_context.run(); });

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(handled.load(), static_cast<int>(msg_ids.size()));
}
