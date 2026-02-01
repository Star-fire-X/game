#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <entt/entt.hpp>
#include <sol/sol.hpp>

#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"

#define private public
#include "game/npc/lua_script_engine.h"
#undef private

namespace mir2::game::npc {
void RegisterNpcAPIs(sol::state& lua, ecs::EventBus* event_bus);
}  // namespace mir2::game::npc

namespace {

using mir2::ecs::EventBus;
using mir2::ecs::events::NpcDialogEvent;
using mir2::ecs::events::NpcGiveItemEvent;
using mir2::game::npc::LuaScriptContext;
using mir2::game::npc::LuaScriptEngine;
using mir2::game::npc::RegisterNpcAPIs;

struct MockEventBus {
    MockEventBus() : bus(registry) {
        bus.Subscribe<NpcDialogEvent>([this](const NpcDialogEvent& event) {
            dialog_event = event;
            ++dialog_count;
        });
        bus.Subscribe<NpcGiveItemEvent>([this](const NpcGiveItemEvent& event) {
            give_item_event = event;
            ++give_item_count;
        });
    }

    entt::registry registry;
    EventBus bus;
    NpcDialogEvent dialog_event{};
    NpcGiveItemEvent give_item_event{};
    int dialog_count = 0;
    int give_item_count = 0;
};

std::filesystem::path MakeTempScriptPath(const std::string& suffix) {
    static std::atomic<int> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("lua_bindings_test_" + std::to_string(stamp) + "_" +
            std::to_string(counter.fetch_add(1)) + suffix);
}

struct TempScriptFile {
    explicit TempScriptFile(const std::string& contents)
        : path(MakeTempScriptPath(".lua")) {
        Write(contents);
    }

    ~TempScriptFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    void Write(const std::string& contents) const {
        std::ofstream output(path, std::ios::trunc);
        output << contents;
        output.flush();
    }

    bool AdvanceMtime() const {
        std::error_code ec;
        auto current = std::filesystem::last_write_time(path, ec);
        if (ec) {
            return false;
        }
        std::filesystem::last_write_time(path, current + std::chrono::seconds(2), ec);
        if (ec) {
            return false;
        }
        return true;
    }

    std::filesystem::path path;
};

TEST(LuaBindingsTest, LoadScriptSuccessAndFailure) {
    LuaScriptEngine engine;
    ASSERT_TRUE(engine.Initialize());

    TempScriptFile valid_script("local x = 1\n");
    EXPECT_TRUE(engine.LoadScript(valid_script.path.string()));

    TempScriptFile invalid_script("local = \n");
    EXPECT_FALSE(engine.LoadScript(invalid_script.path.string()));

    EXPECT_FALSE(engine.LoadScript(""));
}

TEST(LuaBindingsTest, ApiCallsPublishEvents) {
    MockEventBus mock_bus;

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);
    RegisterNpcAPIs(lua, &mock_bus.bus);

    const auto result = lua.safe_script(R"lua(
        npc.say({player = 100, npc_id = 42}, "Hello")
        npc.giveItem({player_id = 100, npc_id = 42}, 200, 3)
    )lua");
    EXPECT_TRUE(result.valid());

    const auto player_entity = static_cast<entt::entity>(100);

    EXPECT_EQ(mock_bus.dialog_count, 1);
    EXPECT_EQ(mock_bus.dialog_event.player, player_entity);
    EXPECT_EQ(mock_bus.dialog_event.npc_id, 42u);
    EXPECT_EQ(mock_bus.dialog_event.text, "Hello");

    EXPECT_EQ(mock_bus.give_item_count, 1);
    EXPECT_EQ(mock_bus.give_item_event.player, player_entity);
    EXPECT_EQ(mock_bus.give_item_event.npc_id, 42u);
    EXPECT_EQ(mock_bus.give_item_event.item_id, 200u);
    EXPECT_EQ(mock_bus.give_item_event.count, 3);
}

TEST(LuaBindingsTest, ScriptErrorDoesNotCrash) {
    LuaScriptEngine engine;
    ASSERT_TRUE(engine.Initialize());

    TempScriptFile script("error(\"boom\")\n");
    ASSERT_TRUE(engine.LoadScript(script.path.string()));

    LuaScriptContext context;
    EXPECT_FALSE(engine.ExecuteScript(script.path.string(), context));
}

TEST(LuaBindingsTest, InstructionLimitDetectsInfiniteLoop) {
    LuaScriptEngine engine;
    ASSERT_TRUE(engine.Initialize());
    engine.instruction_limit_ = 10000;

    TempScriptFile script("while true do end\n");
    ASSERT_TRUE(engine.LoadScript(script.path.string()));

    LuaScriptContext context;
    EXPECT_FALSE(engine.ExecuteScript(script.path.string(), context));
}

TEST(LuaBindingsTest, HotReloadUpdatesScriptBehavior) {
    LuaScriptEngine engine;
    ASSERT_TRUE(engine.Initialize());

    TempScriptFile script("hot_value = 1\n");
    ASSERT_TRUE(engine.LoadScript(script.path.string()));

    LuaScriptContext context;
    ASSERT_TRUE(engine.ExecuteScript(script.path.string(), context));

    sol::optional<int> value = (*engine.lua_)["hot_value"];
    ASSERT_TRUE(value);
    EXPECT_EQ(*value, 1);

    script.Write("hot_value = 2\n");
    ASSERT_TRUE(script.AdvanceMtime());

    engine.CheckAndReloadModified();
    ASSERT_TRUE(engine.ExecuteScript(script.path.string(), context));

    value = (*engine.lua_)["hot_value"];
    ASSERT_TRUE(value);
    EXPECT_EQ(*value, 2);
}

}  // namespace
