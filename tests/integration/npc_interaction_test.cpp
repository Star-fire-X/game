#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "ecs/components/npc_component.h"
#include "ecs/components/transform_component.h"
#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "ecs/systems/npc_ai_system.h"

#define private public
#include "game/npc/lua_script_engine.h"
#undef private

#include "game/npc/npc_script_engine.h"
#include "game/npc/npc_types.h"

namespace {

using mir2::ecs::EventBus;
using mir2::ecs::NpcAIComponent;
using mir2::ecs::NpcScriptComponent;
using mir2::ecs::NpcStateComponent;
using mir2::ecs::TransformComponent;
using mir2::ecs::events::NpcDialogEvent;
using mir2::ecs::events::NpcOpenMerchantEvent;
using mir2::game::npc::LuaScriptContext;
using mir2::game::npc::LuaScriptEngine;
using mir2::game::npc::NpcScriptContext;
using mir2::game::npc::NpcState;
using mir2::game::npc::NpcType;
using mir2::game::npc::NpcAISystem;

struct TempLuaScriptFile {
    explicit TempLuaScriptFile(const std::string& contents) {
        path = MakeTempScriptPath();
        std::ofstream output(path, std::ios::trunc);
        output << contents;
        output.flush();
    }

    ~TempLuaScriptFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    static std::filesystem::path MakeTempScriptPath() {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() /
               ("npc_interaction_test_" + std::to_string(stamp) + "_" +
                std::to_string(counter.fetch_add(1)) + ".lua");
    }

    std::filesystem::path path;
};

entt::entity CreateNpc(entt::registry& registry,
                       uint32_t map_id,
                       int32_t x,
                       int32_t y,
                       const std::string& script_id) {
    const auto entity = registry.create();

    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.map_id = map_id;
    transform.position.x = x;
    transform.position.y = y;

    auto& state = registry.emplace<NpcStateComponent>(entity);
    state.current_state = NpcState::Idle;
    state.previous_state = NpcState::Idle;

    registry.emplace<NpcAIComponent>(entity);

    auto& script = registry.emplace<NpcScriptComponent>(entity);
    script.script_id = script_id;

    return entity;
}

}  // namespace

TEST(NpcInteractionIntegrationTest, ClickInteractLuaFlowAndReturnIdle) {
    entt::registry registry;
    EventBus event_bus(registry);

    const auto player = registry.create();
    const std::string script_id = "npc_interaction.lua";
    const auto npc = CreateNpc(registry, 1, 10, 12, script_id);

    NpcAISystem npc_ai;

    npc_ai.OnPlayerInteract(npc, player);
    npc_ai.Update(registry, 0.1f);

    auto& state = registry.get<NpcStateComponent>(npc);
    EXPECT_EQ(state.current_state, NpcState::Interact);
    EXPECT_EQ(state.previous_state, NpcState::Idle);
    EXPECT_EQ(state.interacting_player, player);

    LuaScriptEngine lua_engine;
    ASSERT_TRUE(lua_engine.Initialize());

    TempLuaScriptFile script(R"lua(
local payload = ...
if payload == nil then
  return
end
npc.say(payload.ctx, "Welcome, traveler")
npc.openShop(payload.ctx, 701)
)lua");

    ASSERT_TRUE(lua_engine.LoadScript(script.path.string()));

    NpcDialogEvent dialog_event{};
    NpcOpenMerchantEvent open_shop_event{};
    int dialog_count = 0;
    int open_shop_count = 0;

    event_bus.Subscribe<NpcDialogEvent>([&](const NpcDialogEvent& event) {
        dialog_event = event;
        ++dialog_count;
    });
    event_bus.Subscribe<NpcOpenMerchantEvent>([&](const NpcOpenMerchantEvent& event) {
        open_shop_event = event;
        ++open_shop_count;
    });

    NpcScriptContext npc_context;
    npc_context.player = player;
    npc_context.npc_id = static_cast<uint64_t>(entt::to_integral(npc));
    npc_context.npc_type = NpcType::kMerchant;
    npc_context.npc_name = "Shopkeeper";
    npc_context.event_bus = &event_bus;

    LuaScriptContext lua_context;
    lua_context.data = lua_engine.lua_->create_table();
    lua_context.data["ctx"] = &npc_context;

    EXPECT_TRUE(lua_engine.ExecuteScript(script.path.string(), lua_context));

    EXPECT_EQ(dialog_count, 1);
    EXPECT_EQ(dialog_event.player, player);
    EXPECT_EQ(dialog_event.npc_id, npc_context.npc_id);
    EXPECT_EQ(dialog_event.text, "Welcome, traveler");

    EXPECT_EQ(open_shop_count, 1);
    EXPECT_EQ(open_shop_event.player, player);
    EXPECT_EQ(open_shop_event.npc_id, npc_context.npc_id);
    EXPECT_EQ(open_shop_event.store_id, 701u);

    state.current_state = NpcState::Idle;
    npc_ai.Update(registry, 0.1f);

    EXPECT_EQ(state.current_state, NpcState::Idle);
    EXPECT_EQ(state.interacting_player, entt::null);
}
