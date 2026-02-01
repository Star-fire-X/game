/**
 * @file lua_script_engine.h
 * @brief LuaJIT script engine definition.
 */

#ifndef LEGEND2_GAME_NPC_LUA_SCRIPT_ENGINE_H
#define LEGEND2_GAME_NPC_LUA_SCRIPT_ENGINE_H

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include <sol/sol.hpp>

namespace mir2::ecs {
class EventBus;
}  // namespace mir2::ecs

namespace mir2::game::npc {

/**
 * @brief Lua script execution context.
 *
 * @note Context data should be created from the same Lua state managed by LuaScriptEngine.
 */
struct LuaScriptContext {
  sol::table data;
};

/**
 * @brief LuaJIT script engine.
 */
class LuaScriptEngine {
 public:
  explicit LuaScriptEngine(ecs::EventBus* event_bus = nullptr);

  /**
   * @brief Initialize LuaJIT VM.
   */
  bool Initialize();

  /**
   * @brief Register C++ bindings for Lua scripts.
   *
   * @note Placeholder, implemented in task5.
   */
  void RegisterAPIs();

  /**
   * @brief Load a single Lua script file.
   */
  bool LoadScript(const std::string& path);

  /**
   * @brief Load all Lua scripts from a directory.
   *
   * @return Number of scripts loaded.
   */
  size_t LoadScriptsFromDirectory(const std::string& dir);

  /**
   * @brief Execute a loaded script by ID.
   */
  bool ExecuteScript(const std::string& script_id, const LuaScriptContext& context);

  /**
   * @brief Reload a single script by ID.
   */
  bool ReloadScript(const std::string& script_id);

  /**
   * @brief Reload all scripts.
   */
  void ReloadAll();

  /**
   * @brief Check and reload scripts modified on disk.
   */
  void CheckAndReloadModified();

 private:
  static void InstructionLimitHook(lua_State* state, lua_Debug* debug);

  bool LoadScriptInternal(const std::string& script_id, const std::string& path);
  std::string NormalizeScriptId(const std::string& script_id) const;

  std::unique_ptr<sol::state> lua_;
  ecs::EventBus* event_bus_ = nullptr;
  std::unordered_map<std::string, sol::protected_function> scripts_;
  std::unordered_map<std::string, std::filesystem::file_time_type> script_mtimes_;
  std::unordered_map<std::string, std::string> script_paths_;
  int instruction_limit_ = 1'000'000;
};

}  // namespace mir2::game::npc

#endif  // LEGEND2_GAME_NPC_LUA_SCRIPT_ENGINE_H
