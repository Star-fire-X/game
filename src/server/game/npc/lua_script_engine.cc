/**
 * @file lua_script_engine.cc
 * @brief LuaJIT script engine implementation.
 */

#include "game/npc/lua_script_engine.h"

#include <system_error>
#include <vector>

#include <lua.hpp>
#include <spdlog/spdlog.h>

namespace mir2::game::npc {

namespace {

constexpr int kDefaultInstructionLimit = 1'000'000;
constexpr const char* kInstructionLimitError = "Lua instruction limit exceeded";

bool is_lua_script(const std::filesystem::path& path) {
  return path.has_extension() && path.extension() == ".lua";
}

}  // namespace

void RegisterNpcAPIs(sol::state& lua, ecs::EventBus* event_bus);

LuaScriptEngine::LuaScriptEngine(ecs::EventBus* event_bus)
    : event_bus_(event_bus) {}

bool LuaScriptEngine::Initialize() {
  lua_ = std::make_unique<sol::state>();
  lua_->open_libraries(sol::lib::base, sol::lib::package, sol::lib::math,
                       sol::lib::table, sol::lib::string, sol::lib::coroutine);
  instruction_limit_ = kDefaultInstructionLimit;
  RegisterAPIs();
  return true;
}

void LuaScriptEngine::RegisterAPIs() {
  if (!lua_) {
    return;
  }

  RegisterNpcAPIs(*lua_, event_bus_);
}

bool LuaScriptEngine::LoadScript(const std::string& path) {
  if (!lua_) {
    spdlog::error("LuaScriptEngine: Lua state not initialized");
    return false;
  }
  if (path.empty()) {
    spdlog::error("LuaScriptEngine: script path is empty");
    return false;
  }
  const std::string script_id = NormalizeScriptId(path);
  return LoadScriptInternal(script_id, path);
}

size_t LuaScriptEngine::LoadScriptsFromDirectory(const std::string& dir) {
  if (!lua_) {
    spdlog::error("LuaScriptEngine: Lua state not initialized");
    return 0;
  }

  std::error_code ec;
  const std::filesystem::path root(dir);
  if (!std::filesystem::exists(root, ec)) {
    spdlog::warn("LuaScriptEngine: scripts directory not found: {}", dir);
    return 0;
  }
  if (!std::filesystem::is_directory(root, ec)) {
    spdlog::warn("LuaScriptEngine: scripts path is not a directory: {}", dir);
    return 0;
  }

  size_t loaded = 0;
  std::filesystem::recursive_directory_iterator it(root, ec);
  const std::filesystem::recursive_directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      spdlog::warn("LuaScriptEngine: directory iteration error: {}", ec.message());
      ec.clear();
      continue;
    }

    if (!it->is_regular_file(ec)) {
      if (ec) {
        spdlog::warn("LuaScriptEngine: failed to inspect entry: {}", ec.message());
        ec.clear();
      }
      continue;
    }

    if (!is_lua_script(it->path())) {
      continue;
    }

    if (LoadScript(it->path().string())) {
      ++loaded;
    }
  }

  return loaded;
}

bool LuaScriptEngine::ExecuteScript(const std::string& script_id, const LuaScriptContext& context) {
  if (!lua_) {
    spdlog::error("LuaScriptEngine: Lua state not initialized");
    return false;
  }

  const std::string normalized_id = NormalizeScriptId(script_id);
  auto it = scripts_.find(normalized_id);
  if (it == scripts_.end()) {
    spdlog::warn("LuaScriptEngine: script not loaded: {}", script_id);
    return false;
  }

  sol::protected_function script = it->second;
  if (!script.valid()) {
    spdlog::error("LuaScriptEngine: invalid script function for {}", script_id);
    return false;
  }

  if (instruction_limit_ > 0) {
    lua_sethook(lua_->lua_state(), &LuaScriptEngine::InstructionLimitHook,
                LUA_MASKCOUNT, instruction_limit_);
  }

  sol::protected_function_result result;
  if (context.data.valid()) {
    result = script(context.data);
  } else {
    result = script();
  }

  if (instruction_limit_ > 0) {
    lua_sethook(lua_->lua_state(), nullptr, 0, 0);
  }

  if (!result.valid()) {
    sol::error err = result;
    spdlog::error("LuaScriptEngine: script {} execution failed: {}",
                  script_id, err.what());
    return false;
  }

  return true;
}

bool LuaScriptEngine::ReloadScript(const std::string& script_id) {
  const std::string normalized_id = NormalizeScriptId(script_id);
  auto it = script_paths_.find(normalized_id);
  if (it == script_paths_.end()) {
    spdlog::warn("LuaScriptEngine: script not found for reload: {}", script_id);
    return false;
  }

  return LoadScriptInternal(normalized_id, it->second);
}

void LuaScriptEngine::ReloadAll() {
  std::vector<std::string> script_ids;
  script_ids.reserve(script_paths_.size());
  for (const auto& [script_id, path] : script_paths_) {
    (void)path;
    script_ids.push_back(script_id);
  }

  for (const auto& script_id : script_ids) {
    ReloadScript(script_id);
  }
}

void LuaScriptEngine::CheckAndReloadModified() {
  if (!lua_) {
    return;
  }

  std::vector<std::string> script_ids;
  script_ids.reserve(script_paths_.size());
  for (const auto& [script_id, path] : script_paths_) {
    (void)path;
    script_ids.push_back(script_id);
  }

  for (const auto& script_id : script_ids) {
    auto path_it = script_paths_.find(script_id);
    if (path_it == script_paths_.end()) {
      continue;
    }

    std::error_code ec;
    const auto current_time = std::filesystem::last_write_time(path_it->second, ec);
    if (ec) {
      spdlog::warn("LuaScriptEngine: failed to stat script {}: {}",
                  path_it->second, ec.message());
      continue;
    }

    auto time_it = script_mtimes_.find(script_id);
    if (time_it == script_mtimes_.end() || current_time != time_it->second) {
      spdlog::info("LuaScriptEngine: script modified, reloading: {}", script_id);
      ReloadScript(script_id);
    }
  }

  // TODO: consider inotify/kqueue for efficient file watching on supported platforms.
}

void LuaScriptEngine::InstructionLimitHook(lua_State* state, lua_Debug* /*debug*/) {
  luaL_error(state, kInstructionLimitError);
}

bool LuaScriptEngine::LoadScriptInternal(const std::string& script_id, const std::string& path) {
  if (!lua_) {
    spdlog::error("LuaScriptEngine: Lua state not initialized");
    return false;
  }

  std::error_code ec;
  const std::filesystem::path file_path(path);
  if (!std::filesystem::exists(file_path, ec) || !std::filesystem::is_regular_file(file_path, ec)) {
    spdlog::warn("LuaScriptEngine: script path invalid: {}", path);
    return false;
  }

  const std::string normalized_path = file_path.lexically_normal().string();
  sol::load_result load_result = lua_->load_file(normalized_path);
  if (!load_result.valid()) {
    sol::error err = load_result;
    spdlog::error("LuaScriptEngine: failed to load script {}: {}",
                  normalized_path, err.what());
    return false;
  }

  sol::protected_function script = load_result;
  scripts_[script_id] = std::move(script);
  script_paths_[script_id] = normalized_path;

  auto mtime = std::filesystem::last_write_time(file_path, ec);
  if (ec) {
    spdlog::warn("LuaScriptEngine: failed to read mtime for {}: {}",
                normalized_path, ec.message());
    mtime = std::filesystem::file_time_type::min();
  }
  script_mtimes_[script_id] = mtime;

  spdlog::info("LuaScriptEngine: loaded script {}", script_id);
  return true;
}

std::string LuaScriptEngine::NormalizeScriptId(const std::string& script_id) const {
  if (script_id.empty()) {
    return script_id;
  }
  return std::filesystem::path(script_id).lexically_normal().generic_string();
}

}  // namespace mir2::game::npc
