#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/protocol/npc_message_codec.h"

namespace {
using json = nlohmann::json;

std::string MakeString(size_t length, char fill = 'a') {
    return std::string(length, fill);
}

std::vector<uint8_t> DumpJson(const json& j) {
    const auto dumped = j.dump();
    return std::vector<uint8_t>(dumped.begin(), dumped.end());
}

json ParsePayload(const std::vector<uint8_t>& payload) {
    return json::parse(payload.begin(), payload.end());
}

std::vector<std::string> MakeOptions(size_t count, size_t length) {
    std::vector<std::string> options;
    options.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        options.emplace_back(MakeString(length, static_cast<char>('a' + (i % 26))));
    }
    return options;
}

}  // namespace

TEST(NpcMessageCodecTest, NpcInteractReqEncodeRoundTrip) {
    mir2::common::NpcInteractReq request;
    request.npc_id = 123;
    request.player_id = 456;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeNpcInteractReq(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    const auto j = ParsePayload(payload);
    EXPECT_EQ(j.at("version").get<uint32_t>(), mir2::common::kNpcCodecVersion);
    EXPECT_EQ(j.at("npc_id").get<uint64_t>(), request.npc_id);
    EXPECT_EQ(j.at("player_id").get<uint64_t>(), request.player_id);
}

TEST(NpcMessageCodecTest, NpcInteractReqRejectsMissingFields) {
    mir2::common::NpcInteractReq request;
    request.npc_id = 0;
    request.player_id = 12;

    EXPECT_EQ(mir2::common::ValidateNpcInteractReq(request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeNpcInteractReq(request, &status);
    EXPECT_TRUE(payload.empty());
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kMissingField);

    request.npc_id = 9;
    request.player_id = 0;
    EXPECT_EQ(mir2::common::ValidateNpcInteractReq(request),
              mir2::common::MessageCodecStatus::kMissingField);
}

TEST(NpcMessageCodecTest, NpcMenuSelectReqEncodeRoundTrip) {
    mir2::common::NpcMenuSelectReq request;
    request.npc_id = 999;
    request.option_index = std::numeric_limits<uint8_t>::max();

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeNpcMenuSelect(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    const auto j = ParsePayload(payload);
    EXPECT_EQ(j.at("version").get<uint32_t>(), mir2::common::kNpcCodecVersion);
    EXPECT_EQ(j.at("npc_id").get<uint64_t>(), request.npc_id);
    EXPECT_EQ(j.at("option_index").get<uint64_t>(), request.option_index);
}

TEST(NpcMessageCodecTest, NpcMenuSelectReqRejectsMissingNpcId) {
    mir2::common::NpcMenuSelectReq request;
    request.npc_id = 0;
    request.option_index = 1;

    EXPECT_EQ(mir2::common::ValidateNpcMenuSelectReq(request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeNpcMenuSelect(request, &status);
    EXPECT_TRUE(payload.empty());
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kMissingField);
}

TEST(NpcMessageCodecTest, NpcDialogShowRoundTrip) {
    mir2::common::NpcDialogShowMsg msg;
    msg.npc_id = 77;
    msg.text = "Welcome, hero!";
    msg.options = {"Shop", "Leave"};

    json j;
    j["version"] = mir2::common::kNpcCodecVersion;
    j["npc_id"] = msg.npc_id;
    j["text"] = msg.text;
    j["options"] = msg.options;

    auto payload = DumpJson(j);
    mir2::common::NpcDialogShowMsg decoded;
    auto status = mir2::common::DecodeNpcDialogShow(mir2::common::kNpcDialogShowMsgId,
                                                    payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.npc_id, msg.npc_id);
    EXPECT_EQ(decoded.text, msg.text);
    EXPECT_EQ(decoded.options, msg.options);
}

TEST(NpcMessageCodecTest, NpcDialogShowBoundaryValues) {
    mir2::common::NpcDialogShowMsg msg;
    msg.npc_id = 10;
    msg.text = MakeString(mir2::common::kMaxNpcDialogTextLength, 't');
    msg.options = MakeOptions(mir2::common::kMaxNpcDialogOptions,
                              mir2::common::kMaxNpcDialogOptionLength);

    json j;
    j["version"] = mir2::common::kNpcCodecVersion;
    j["npc_id"] = msg.npc_id;
    j["text"] = msg.text;
    j["options"] = msg.options;

    auto payload = DumpJson(j);
    mir2::common::NpcDialogShowMsg decoded;
    auto status = mir2::common::DecodeNpcDialogShow(mir2::common::kNpcDialogShowMsgId,
                                                    payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.text.size(), msg.text.size());
    EXPECT_EQ(decoded.options.size(), msg.options.size());
    EXPECT_EQ(decoded.options.front().size(), msg.options.front().size());
    EXPECT_EQ(decoded.options.back().size(), msg.options.back().size());
}

TEST(NpcMessageCodecTest, NpcDialogShowRejectsMissingText) {
    json j;
    j["version"] = mir2::common::kNpcCodecVersion;
    j["npc_id"] = 55;

    auto payload = DumpJson(j);
    mir2::common::NpcDialogShowMsg decoded;
    auto status = mir2::common::DecodeNpcDialogShow(mir2::common::kNpcDialogShowMsgId,
                                                    payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kMissingField);
}

TEST(NpcMessageCodecTest, NpcShopOpenRoundTrip) {
    mir2::common::NpcShopOpenMsg msg;
    msg.shop_id = 500;
    msg.npc_id = 1234;
    msg.items = {
        mir2::common::NpcShopItem{1001, 25, 3},
        mir2::common::NpcShopItem{2002, 99, 0}
    };

    json items = json::array();
    items.push_back({{"item_id", msg.items[0].item_id},
                     {"price", msg.items[0].price},
                     {"stock", msg.items[0].stock}});
    items.push_back({{"item_id", msg.items[1].item_id},
                     {"price", msg.items[1].price}});

    json j;
    j["version"] = mir2::common::kNpcCodecVersion;
    j["shop_id"] = msg.shop_id;
    j["npc_id"] = msg.npc_id;
    j["items"] = items;

    auto payload = DumpJson(j);
    mir2::common::NpcShopOpenMsg decoded;
    auto status = mir2::common::DecodeNpcShopOpen(mir2::common::kNpcShopOpenMsgId,
                                                  payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.shop_id, msg.shop_id);
    EXPECT_EQ(decoded.npc_id, msg.npc_id);
    ASSERT_EQ(decoded.items.size(), msg.items.size());
    EXPECT_EQ(decoded.items[0].item_id, msg.items[0].item_id);
    EXPECT_EQ(decoded.items[0].price, msg.items[0].price);
    EXPECT_EQ(decoded.items[0].stock, msg.items[0].stock);
    EXPECT_EQ(decoded.items[1].item_id, msg.items[1].item_id);
    EXPECT_EQ(decoded.items[1].price, msg.items[1].price);
    EXPECT_EQ(decoded.items[1].stock, 0);
}

TEST(NpcMessageCodecTest, NpcShopOpenLegacyStoreId) {
    json j;
    j["version"] = mir2::common::kNpcCodecVersion;
    j["store_id"] = 777;
    j["items"] = json::array();

    auto payload = DumpJson(j);
    mir2::common::NpcShopOpenMsg decoded;
    auto status = mir2::common::DecodeNpcShopOpen(mir2::common::kNpcShopOpenMsgId,
                                                  payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.shop_id, 777u);
    EXPECT_EQ(decoded.npc_id, 0u);
    EXPECT_TRUE(decoded.items.empty());
}

TEST(NpcMessageCodecTest, NpcQuestMsgAcceptDefaultStatus) {
    json j;
    j["quest_id"] = 42;

    auto payload = DumpJson(j);
    mir2::common::NpcQuestMsg decoded;
    auto status = mir2::common::DecodeNpcQuestMsg(mir2::common::kNpcQuestAcceptMsgId,
                                                  payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.quest_id, 42u);
    EXPECT_EQ(decoded.status, mir2::common::NpcQuestStatus::kAccepted);
    EXPECT_EQ(decoded.npc_id, 0u);
}

TEST(NpcMessageCodecTest, NpcQuestMsgRejectsInvalidStatus) {
    json j;
    j["version"] = mir2::common::kNpcCodecVersion;
    j["quest_id"] = 1;
    j["status"] = 99;

    auto payload = DumpJson(j);
    mir2::common::NpcQuestMsg decoded;
    auto status = mir2::common::DecodeNpcQuestMsg(mir2::common::kNpcQuestAcceptMsgId,
                                                  payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kValueOutOfRange);
}

TEST(NpcMessageCodecTest, DecodeRejectsInvalidJsonPayload) {
    std::vector<uint8_t> payload = {'{'};
    mir2::common::NpcDialogShowMsg decoded;

    auto status = mir2::common::DecodeNpcDialogShow(mir2::common::kNpcDialogShowMsgId,
                                                    payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kInvalidPayload);
}

TEST(NpcMessageCodecTest, DecodeRejectsFutureVersion) {
    json j;
    j["version"] = mir2::common::kNpcCodecVersion + 1;
    j["npc_id"] = 12;
    j["text"] = "Hi";

    auto payload = DumpJson(j);
    mir2::common::NpcDialogShowMsg decoded;
    auto status = mir2::common::DecodeNpcDialogShow(mir2::common::kNpcDialogShowMsgId,
                                                    payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kValueOutOfRange);
}
