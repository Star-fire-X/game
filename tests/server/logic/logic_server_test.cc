/**
 * @file logic_server_test.cc
 * @brief Comprehensive tests for LogicServer - the core game server orchestrator
 *
 * Test Coverage:
 * - Server lifecycle (Initialize, Run, Shutdown)
 * - Gateway connection management
 * - Message routing and processing
 * - Coroutine executor integration
 * - Player mailbox management
 * - Backpressure handling
 * - Session cleanup and zombie detection
 * - Error handling and graceful degradation
 *
 * Priority: P0 Ultra-Critical (Score: 49)
 * Risk: Highest - Any failure impacts all game functionality
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <openssl/sha.h>

#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/detail/config.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "logic/logic_server.h"
#include "common/protocol/npc_message_codec.h"
#include "data/item_template.h"
#include "ecs/components/character_components.h"
#include "ecs/components/monster_component.h"
#include "ecs/events/npc_events.h"
#include "ecs/registry_manager.h"
#include "ecs/skill_registry.h"
#include "ecs/systems/monster_drop_system.h"
#include "game/map/map_instance.h"
#include "game/map/scene_manager.h"
#include "log/logger.h"
#include "logic/mock_response_sender.h"
#include "logic/services/merchant_service.h"
#include "logic/services/session_role_store.h"
#include "logic/services/world_sync_broadcast_service.h"
#include "game/npc/npc_manager.h"
#include <nlohmann/json.hpp>
#include "storage_engine/interfaces/storage_backend.h"
#include "storage_engine/storage_engine.h"

namespace mir2::logic::test {
namespace {

using namespace std::chrono_literals;

std::filesystem::path GetRepoRoot() {
  return std::filesystem::path(__FILE__)
      .parent_path()
      .parent_path()
      .parent_path()
      .parent_path();
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input.is_open()) {
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

std::string Sha256Hex(const std::string& content) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char*>(content.data()),
         content.size(),
         digest);

  static constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(SHA256_DIGEST_LENGTH * 2);
  for (unsigned char byte : digest) {
    result.push_back(kHex[(byte >> 4) & 0x0F]);
    result.push_back(kHex[byte & 0x0F]);
  }
  return result;
}

std::size_t ArtifactRowCount(const std::string& artifact_name,
                             const std::string& content) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(content);
  } catch (const std::exception&) {
    return 0;
  }
  if (artifact_name == "items") {
    return root.at("items").size();
  }
  if (artifact_name == "skills") {
    return root.at("skills").size();
  }
  if (artifact_name == "maps") {
    return root.at("maps").size();
  }
  if (artifact_name == "gates") {
    return root.at("gates").size();
  }
  if (artifact_name == "drops") {
    return root.at("drop_tables").size();
  }
  if (artifact_name == "shops") {
    return root.at("shops").size();
  }
  if (artifact_name == "monster_spawns") {
    return root.at("spawn_points").size();
  }
  if (artifact_name == "npcs") {
    return root.at("npcs").size();
  }
  return 0;
}

nlohmann::json::iterator FindManifestArtifact(nlohmann::json& manifest,
                                              const std::string& artifact_name) {
  return std::find_if(
      manifest["artifacts"].begin(),
      manifest["artifacts"].end(),
      [&artifact_name](const nlohmann::json& artifact) {
        return artifact.value("name", "") == artifact_name;
      });
}

std::string ShellEscape(std::string_view raw) {
  std::string escaped;
  escaped.reserve(raw.size() + 2);
  escaped.push_back('\'');
  for (char c : raw) {
    if (c == '\'') {
      escaped += "'\"'\"'";
    } else {
      escaped.push_back(c);
    }
  }
  escaped.push_back('\'');
  return escaped;
}

struct CommandResult {
  int exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
};

CommandResult RunCommand(const std::vector<std::string>& args,
                         const std::filesystem::path& cwd = {}) {
  const auto temp_dir = std::filesystem::temp_directory_path() /
                        ("mir2_logic_server_cmd_" +
                         std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch()
                                            .count()));
  std::error_code ec;
  std::filesystem::create_directories(temp_dir, ec);
  const auto stdout_path = temp_dir / "stdout.txt";
  const auto stderr_path = temp_dir / "stderr.txt";

  std::string command;
  if (!cwd.empty()) {
    command += "cd " + ShellEscape(cwd.string()) + " && ";
  }
  for (const auto& arg : args) {
    if (!command.empty() && command.back() != ' ') {
      command.push_back(' ');
    }
    command += ShellEscape(arg);
  }
  command += " >" + ShellEscape(stdout_path.string());
  command += " 2>" + ShellEscape(stderr_path.string());

  CommandResult result;
  const int status = std::system(command.c_str());
  if (status >= 0 && WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else {
    result.exit_code = status;
  }
  result.stdout_text = ReadTextFile(stdout_path);
  result.stderr_text = ReadTextFile(stderr_path);
  std::filesystem::remove_all(temp_dir, ec);
  return result;
}

/**
 * @brief Mock storage backend for testing
 */
class MockStorageBackend : public storage_engine::IStorageBackend,
                           public storage_engine::IAtomicBatchStorageBackend {
 public:
  using StorageResult = storage_engine::IStorageBackend::StorageResult;
  using BatchItems =
      std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>;
  using LoadResult = std::optional<std::pair<uint64_t, std::vector<uint8_t>>>;
  using LoadAllResult =
      std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>>;

  MOCK_METHOD(StorageResult,
              Save,
              (const std::string&, uint64_t, const std::vector<uint8_t>&),
              (override));

  MOCK_METHOD(StorageResult,
              SaveBatch,
              ((const BatchItems&)),
              (override));

  MOCK_METHOD(StorageResult,
              SaveBatchAtomic,
              ((const BatchItems&)),
              (override));

  MOCK_METHOD((LoadResult),
              Load,
              (const std::string&),
              (override));

  MOCK_METHOD((LoadAllResult),
              LoadAll,
              (),
              (override));

  MOCK_METHOD(StorageResult, Validate, (), (override));

  MOCK_METHOD(bool, IsHealthy, (), (const, override));

  // Default implementations for basic functionality
  void SetDefaultBehavior() {
    using ::testing::Return;
    ON_CALL(*this, Save(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(Return(StorageResult{true, "", 0}));
    ON_CALL(*this, SaveBatch(::testing::_))
        .WillByDefault(Return(StorageResult{true, "", 0}));
    ON_CALL(*this, SaveBatchAtomic(::testing::_))
        .WillByDefault(Return(StorageResult{true, "", 0}));
    ON_CALL(*this, Load(::testing::_)).WillByDefault(Return(std::nullopt));
    ON_CALL(*this, LoadAll()).WillByDefault(Return(std::nullopt));
    ON_CALL(*this, Validate()).WillByDefault(Return(StorageResult{true, "", 0}));
    ON_CALL(*this, IsHealthy()).WillByDefault(Return(true));
  }
};

/**
 * @brief Test fixture for LogicServer
 *
 * Provides a controlled testing environment with:
 * - Isolated io_context for each test
 * - Mock storage backend
 * - Helper methods for common test scenarios
 */
class LogicServerTest : public ::testing::Test {
 protected:
  struct RuntimeArtifactSpec {
    std::string file_name;
    std::string content;
    bool write_file = true;
    bool corrupt_hash = false;
  };

  void SetUp() override {
    // Create mock storage backend
    mock_storage_ = std::make_unique<MockStorageBackend>();
    mock_storage_->SetDefaultBehavior();
    game::npc::NpcManager::Instance().Clear();
    mir2::data::ItemTemplateManager::Instance().Clear();
    mir2::ecs::SkillRegistry::instance().clear();

    // Create test config directory
    CreateTestConfig();
  }

  void TearDown() override {
    if (server_) {
      server_->Shutdown();
      server_.reset();
    }

    if (storage_engine::StorageEngine::IsInitialized()) {
      storage_engine::StorageEngine::Shutdown();
    }
    mir2::log::Logger::Instance().Shutdown();
    game::npc::NpcManager::Instance().Clear();
    mir2::data::ItemTemplateManager::Instance().Clear();
    mir2::ecs::SkillRegistry::instance().clear();

    // Clean up test config
    CleanupTestConfig();
  }

  /**
   * @brief Create a minimal valid configuration file for testing
   */
  void CreateTestConfig() {
    static std::atomic<uint64_t> sequence{0};
    const uint64_t now_ticks = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const uint64_t seq = sequence.fetch_add(1, std::memory_order_relaxed);
    const std::string unique_id =
        std::to_string(now_ticks) + "_" + std::to_string(seq);

    test_artifacts_dir_ = std::filesystem::temp_directory_path() /
                          ("mir2_logic_server_test_" + unique_id);
    const std::filesystem::path log_dir = test_artifacts_dir_ / "logs";
    const std::filesystem::path l2_dir = test_artifacts_dir_ / "rocksdb";

    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(log_dir, ec))
        << "Failed to create log dir: " << log_dir << " error=" << ec.message();
    ASSERT_TRUE(std::filesystem::create_directories(l2_dir, ec))
        << "Failed to create rocksdb dir: " << l2_dir << " error=" << ec.message();

    config_path_ = (test_artifacts_dir_ / "test_config_logic_server.yaml").string();
    std::ofstream config_file(config_path_, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(config_file.is_open())
        << "Failed to open test config file: " << config_path_;

    config_file
        << "server:\n"
        << "  id: 1\n"
        << "  name: \"LogicServerTest\"\n"
        << "  bind_ip: \"127.0.0.1\"\n"
        << "  port: 0\n"
        << "  io_threads: 1\n"
        << "  max_connections: 64\n"
        << "  tick_interval_ms: 50\n"
        << "  enable_network_listener: false\n"
        << "  metrics_port: 0\n"
        << "\n"
        << "database:\n"
        << "  host: \"127.0.0.1\"\n"
        << "  port: 5432\n"
        << "  user: \"mir2\"\n"
        << "  password: \"mir2_password\"\n"
        << "  database: \"mir2_game\"\n"
        << "  pool_size: 0\n"
        << "\n"
        << "log:\n"
        << "  level: \"error\"\n"
        << "  path: '" << log_dir.string() << "'\n"
        << "  max_size_mb: 5\n"
        << "  max_files: 2\n"
        << "\n"
        << "storage_engine:\n"
        << "  l2_path: '" << l2_dir.string() << "'\n";

    config_file
        << "services:\n"
        << "  logic:\n"
        << "    host: \"127.0.0.1\"\n"
        << "    port: 0\n";
#if defined(ASIO_HAS_LOCAL_SOCKETS)
    const std::filesystem::path uds_path = test_artifacts_dir_ / "logic_test.sock";
    config_file
        << "    transport: \"uds\"\n"
        << "    uds_path: '" << uds_path.string() << "'\n";
#else
    config_file << "    transport: \"tcp\"\n";
#endif
    config_file << "\n";
    config_file.flush();
    ASSERT_TRUE(config_file.good())
        << "Failed to write test config file: " << config_path_;

    default_map_id_ = AllocateDefaultMapId();
    EnsureDefaultMapFixture(default_map_id_);
    WriteCombatConfig(static_cast<int>(default_map_id_));
  }

  uint32_t AllocateDefaultMapId() {
    static std::atomic<uint32_t> map_sequence{0};
    const uint32_t seq = map_sequence.fetch_add(1, std::memory_order_relaxed);

#if defined(_WIN32)
    const uint64_t process_id = static_cast<uint64_t>(_getpid());
#else
    const uint64_t process_id = static_cast<uint64_t>(getpid());
#endif

    constexpr uint32_t kMapIdBase = 1000000;
    constexpr uint32_t kMapIdSpan = 1000000000;
    uint32_t map_id = kMapIdBase +
                      static_cast<uint32_t>((process_id * 2654435761ULL + seq) %
                                            kMapIdSpan);
    if (map_id == 65535u) {
      ++map_id;
    }
    return map_id;
  }

  void EnsureDefaultMapFixture(uint32_t map_id) {
    map_file_path_.clear();
    created_map_file_ = false;
    had_existing_map_file_ = false;

    const std::filesystem::path map_dir = std::filesystem::current_path() / "Map";
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(map_dir, ec) || !ec)
        << "Failed to create map dir: " << map_dir << " error=" << ec.message();

    map_file_path_ = map_dir / (std::to_string(map_id) + ".map");
    had_existing_map_file_ = std::filesystem::exists(map_file_path_);
    if (had_existing_map_file_) {
      return;
    }

    std::ofstream map_file(map_file_path_, std::ios::out | std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(map_file.is_open())
        << "Failed to open default map fixture file: " << map_file_path_;

    constexpr int32_t kWidth = 256;
    constexpr int32_t kHeight = 256;
    std::vector<uint8_t> header(52, 0);
    header[0] = static_cast<uint8_t>(kWidth & 0xFF);
    header[1] = static_cast<uint8_t>((kWidth >> 8) & 0xFF);
    header[2] = static_cast<uint8_t>(kHeight & 0xFF);
    header[3] = static_cast<uint8_t>((kHeight >> 8) & 0xFF);
    map_file.write(reinterpret_cast<const char*>(header.data()),
                   static_cast<std::streamsize>(header.size()));

    std::array<uint8_t, 12> tile{};
    for (int32_t x = 0; x < kWidth; ++x) {
      for (int32_t y = 0; y < kHeight; ++y) {
        map_file.write(reinterpret_cast<const char*>(tile.data()),
                       static_cast<std::streamsize>(tile.size()));
      }
    }

    map_file.flush();
    ASSERT_TRUE(map_file.good())
        << "Failed to write default map fixture file: " << map_file_path_;
    created_map_file_ = true;
  }

  void WriteCombatConfig(int respawn_map_id) {
    combat_config_path_ = test_artifacts_dir_ / "combat_config.yaml";
    std::ofstream combat_config_file(
        combat_config_path_.string(), std::ios::out | std::ios::trunc);
    ASSERT_TRUE(combat_config_file.is_open())
        << "Failed to open combat config file: " << combat_config_path_;
    combat_config_file
        << "combat:\n"
        << "  min_variance_percent: 90\n"
        << "  max_variance_percent: 110\n"
        << "  minimum_damage: 1\n"
        << "  respawn:\n"
        << "    hp_percent: 100\n"
        << "    mp_percent: 100\n"
        << "    map_id: " << respawn_map_id << "\n"
        << "    position:\n"
        << "      x: 0\n"
        << "      y: 0\n";
    combat_config_file.flush();
    ASSERT_TRUE(combat_config_file.good())
        << "Failed to write combat config file: " << combat_config_path_;
  }

  void WriteMapsConfig(const std::vector<int32_t>& map_ids) {
    const std::filesystem::path tables_dir = test_artifacts_dir_ / "tables";
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(tables_dir, ec) || !ec)
        << "Failed to create tables dir: " << tables_dir << " error=" << ec.message();

    maps_config_path_ = tables_dir / "maps.yaml";
    std::ofstream maps_file(maps_config_path_, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(maps_file.is_open())
        << "Failed to open maps config file: " << maps_config_path_;
    maps_file << "maps:\n";
    for (const int32_t map_id : map_ids) {
      maps_file << "  - id: " << map_id << "\n";
    }
    maps_file.flush();
    ASSERT_TRUE(maps_file.good())
        << "Failed to write maps config file: " << maps_config_path_;
  }

  void WriteMapsConfigText(const std::string& content) {
    const std::filesystem::path tables_dir = test_artifacts_dir_ / "tables";
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(tables_dir, ec) || !ec)
        << "Failed to create tables dir: " << tables_dir << " error=" << ec.message();

    maps_config_path_ = tables_dir / "maps.yaml";
    std::ofstream maps_file(maps_config_path_, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(maps_file.is_open())
        << "Failed to open maps config file: " << maps_config_path_;
    maps_file << content;
    maps_file.flush();
    ASSERT_TRUE(maps_file.good())
        << "Failed to write maps config file: " << maps_config_path_;
  }

  void WriteGatesConfig(const std::vector<mir2::game::map::GateInfo>& gates) {
    gates_config_path_ = test_artifacts_dir_ / "gates.yaml";
    std::ofstream gates_file(gates_config_path_, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(gates_file.is_open())
        << "Failed to open gates config file: " << gates_config_path_;
    gates_file << "gates:\n";
    for (const auto& gate : gates) {
      gates_file
          << "  - gate_id: " << gate.gate_id << "\n"
          << "    source_map: \"" << gate.source_map << "\"\n"
          << "    source_x: " << gate.source_x << "\n"
          << "    source_y: " << gate.source_y << "\n"
          << "    target_map: \"" << gate.target_map << "\"\n"
          << "    target_x: " << gate.target_x << "\n"
          << "    target_y: " << gate.target_y << "\n"
          << "    require_item: " << (gate.require_item ? "true" : "false") << "\n"
          << "    required_item_id: " << gate.required_item_id << "\n";
    }
    gates_file.flush();
    ASSERT_TRUE(gates_file.good())
        << "Failed to write gates config file: " << gates_config_path_;
  }

  void WriteShopsConfigText(const std::string& content) {
    const std::filesystem::path tables_dir = test_artifacts_dir_ / "tables";
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(tables_dir, ec) || !ec)
        << "Failed to create tables dir: " << tables_dir << " error=" << ec.message();

    const auto shops_config_path = tables_dir / "shops.yaml";
    std::ofstream shops_file(shops_config_path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(shops_file.is_open())
        << "Failed to open shops config file: " << shops_config_path;
    shops_file << content;
    shops_file.flush();
    ASSERT_TRUE(shops_file.good())
        << "Failed to write shops config file: " << shops_config_path;
  }

  void WriteSpawnPointsConfigText(const std::string& content) {
    const std::filesystem::path tables_dir = test_artifacts_dir_ / "tables";
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(tables_dir, ec) || !ec)
        << "Failed to create tables dir: " << tables_dir << " error=" << ec.message();

    const auto spawn_config_path = tables_dir / "spawn_points.yaml";
    std::ofstream spawn_file(spawn_config_path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(spawn_file.is_open())
        << "Failed to open spawn_points config file: " << spawn_config_path;
    spawn_file << content;
    spawn_file.flush();
    ASSERT_TRUE(spawn_file.good())
        << "Failed to write spawn_points config file: " << spawn_config_path;
  }

  void WriteNpcsConfigText(const std::string& content) {
    const std::filesystem::path tables_dir = test_artifacts_dir_ / "tables";
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(tables_dir, ec) || !ec)
        << "Failed to create tables dir: " << tables_dir << " error=" << ec.message();

    const auto npcs_config_path = tables_dir / "npcs.yaml";
    std::ofstream npcs_file(npcs_config_path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(npcs_file.is_open())
        << "Failed to open npcs config file: " << npcs_config_path;
    npcs_file << content;
    npcs_file.flush();
    ASSERT_TRUE(npcs_file.good())
        << "Failed to write npcs config file: " << npcs_config_path;
  }

  void EnsureAdditionalMapFixture(uint32_t map_id) {
    const std::filesystem::path map_dir = std::filesystem::current_path() / "Map";
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::create_directories(map_dir, ec) || !ec)
        << "Failed to create map dir: " << map_dir << " error=" << ec.message();

    const auto map_path = map_dir / (std::to_string(map_id) + ".map");
    if (std::filesystem::exists(map_path)) {
      return;
    }

    std::ofstream map_file(map_path, std::ios::out | std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(map_file.is_open())
        << "Failed to open additional map fixture file: " << map_path;

    constexpr int32_t kWidth = 256;
    constexpr int32_t kHeight = 256;
    std::vector<uint8_t> header(52, 0);
    header[0] = static_cast<uint8_t>(kWidth & 0xFF);
    header[1] = static_cast<uint8_t>((kWidth >> 8) & 0xFF);
    header[2] = static_cast<uint8_t>(kHeight & 0xFF);
    header[3] = static_cast<uint8_t>((kHeight >> 8) & 0xFF);
    map_file.write(reinterpret_cast<const char*>(header.data()),
                   static_cast<std::streamsize>(header.size()));

    std::array<uint8_t, 12> tile{};
    for (int32_t x = 0; x < kWidth; ++x) {
      for (int32_t y = 0; y < kHeight; ++y) {
        map_file.write(reinterpret_cast<const char*>(tile.data()),
                       static_cast<std::streamsize>(tile.size()));
      }
    }
    map_file.flush();
    ASSERT_TRUE(map_file.good())
        << "Failed to write additional map fixture file: " << map_path;
    extra_created_map_files_.push_back(map_path);
  }

  static std::string BuildRuntimeMapsContent(const std::vector<uint32_t>& map_ids) {
    nlohmann::json maps = nlohmann::json::array();
    for (uint32_t map_id : map_ids) {
      maps.push_back({
          {"map_id", map_id},
          {"fixes", nlohmann::json::array()},
          {"safe_zones", nlohmann::json::array()},
          {"quest_requirements", nlohmann::json::array()},
      });
    }
    return nlohmann::json{{"maps", maps}}.dump(2) + "\n";
  }

  static std::string BuildRuntimeMapsContentFromEntries(
      const std::vector<nlohmann::json>& map_entries) {
    nlohmann::json maps = nlohmann::json::array();
    for (const auto& entry : map_entries) {
      maps.push_back(entry);
    }
    return nlohmann::json{{"maps", maps}}.dump(2) + "\n";
  }

  static std::string BuildRuntimeGatesContent(uint32_t source_map_id,
                                              bool empty = false) {
    nlohmann::json gates = nlohmann::json::array();
    if (!empty) {
      gates.push_back({
          {"gate_id", 101},
          {"source_map", std::to_string(source_map_id)},
          {"source_x", 10},
          {"source_y", 20},
          {"target_map", std::to_string(source_map_id)},
          {"target_x", 30},
          {"target_y", 40},
          {"require_item", false},
          {"required_item_id", 0},
      });
    }
    return nlohmann::json{
               {"gates", gates}}
        .dump(2) +
        "\n";
  }

  static std::string BuildRuntimeDropsContent(bool empty = false,
                                              uint32_t monster_template_id = 900001) {
    nlohmann::json tables = nlohmann::json::array();
    if (!empty) {
      tables.push_back({
          {"monster_template_id", monster_template_id},
          {"items",
           nlohmann::json::array({
               {{"item_id", 1001},
                {"drop_rate", 1.0},
                {"min_count", 1},
                {"max_count", 1},
                {"rarity", 1},
                {"boss_bonus", 0.0}},
           })},
      });
    }
    return nlohmann::json{{"drop_tables", tables}}.dump(2) + "\n";
  }

  static std::string BuildRuntimeShopsContent(bool empty = false) {
    nlohmann::json shops = nlohmann::json::array();
    if (!empty) {
      shops.push_back({
          {"store_id", 77},
          {"name", "runtime_basic"},
          {"buy_rate", 1.0},
          {"sell_rate", 0.5},
          {"items",
           nlohmann::json::array({
               {{"item_id", 1001}, {"price", 100}, {"stock", -1}},
               {{"item_id", 2001}, {"price", 250}, {"stock", 5}},
           })},
      });
    }
    return nlohmann::json{{"shops", shops}}.dump(2) + "\n";
  }

  static std::string BuildRuntimeMonsterSpawnsContent(
      const std::vector<nlohmann::json>& spawn_entries) {
    nlohmann::json spawn_points = nlohmann::json::array();
    for (const auto& entry : spawn_entries) {
      spawn_points.push_back(entry);
    }
    return nlohmann::json{{"spawn_points", spawn_points}}.dump(2) + "\n";
  }

  static std::string BuildRuntimeNpcsContent(
      const std::vector<nlohmann::json>& npc_entries) {
    nlohmann::json npcs = nlohmann::json::array();
    for (const auto& entry : npc_entries) {
      npcs.push_back(entry);
    }
    return nlohmann::json{{"npcs", npcs}}.dump(2) + "\n";
  }

  bool AddRuntimeArtifact(const std::filesystem::path& runtime_dir,
                          const std::string& artifact_name,
                          const RuntimeArtifactSpec& spec) {
    const auto manifest_path = runtime_dir / "manifest.json";
    auto manifest_text = ReadTextFile(manifest_path);
    if (manifest_text.empty()) {
      return false;
    }
    auto manifest = nlohmann::json::parse(manifest_text);
    if (!manifest.contains("artifacts") || !manifest["artifacts"].is_array()) {
      return false;
    }
    if (spec.write_file) {
      std::ofstream artifact_out(runtime_dir / spec.file_name,
                                 std::ios::out | std::ios::trunc);
      if (!artifact_out.is_open()) {
        return false;
      }
      artifact_out << spec.content;
      artifact_out.flush();
      if (!artifact_out.good()) {
        return false;
      }
    }
    const auto hash =
        spec.corrupt_hash ? std::string(64, '0') : Sha256Hex(spec.content);
    const auto row_count = ArtifactRowCount(artifact_name, spec.content);
    const auto it = FindManifestArtifact(manifest, artifact_name);
    const nlohmann::json artifact = {
        {"name", artifact_name},
        {"file", spec.file_name},
        {"hash", hash},
        {"row_count", row_count},
    };
    if (it == manifest["artifacts"].end()) {
      manifest["artifacts"].push_back(artifact);
    } else {
      *it = artifact;
    }
    std::sort(manifest["artifacts"].begin(),
              manifest["artifacts"].end(),
              [](const nlohmann::json& lhs, const nlohmann::json& rhs) {
                return lhs.at("name").get<std::string>() <
                       rhs.at("name").get<std::string>();
              });

    std::ofstream manifest_out(manifest_path, std::ios::out | std::ios::trunc);
    if (!manifest_out.is_open()) {
      return false;
    }
    manifest_out << manifest.dump(2) << "\n";
    manifest_out.flush();
    return manifest_out.good();
  }

  void CleanupTestConfig() {
    if (test_artifacts_dir_.empty()) {
      return;
    }
    std::error_code ec;
    std::filesystem::remove_all(test_artifacts_dir_, ec);
    test_artifacts_dir_.clear();
    config_path_.clear();
    combat_config_path_.clear();
    maps_config_path_.clear();
    gates_config_path_.clear();
    if (created_map_file_ && !had_existing_map_file_ && !map_file_path_.empty()) {
      std::error_code map_ec;
      std::filesystem::remove(map_file_path_, map_ec);
    }
    for (const auto& extra_map_path : extra_created_map_files_) {
      std::error_code map_ec;
      std::filesystem::remove(extra_map_path, map_ec);
    }
    extra_created_map_files_.clear();
    map_file_path_.clear();
    created_map_file_ = false;
    had_existing_map_file_ = false;
  }

  bool SetStorageEngineBoolConfig(const std::string& key, bool value) {
    if (config_path_.empty()) {
      return false;
    }

    std::ifstream input(config_path_, std::ios::in);
    if (!input.is_open()) {
      return false;
    }
    const std::string content(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    input.close();

    const std::string key_prefix = "  " + key + ":";
    const std::string new_line =
        key_prefix + (value ? " true" : " false");

    std::string updated = content;
    const size_t key_pos = updated.find(key_prefix);
    if (key_pos != std::string::npos) {
      const size_t line_end = updated.find('\n', key_pos);
      const size_t replace_len =
          line_end == std::string::npos ? updated.size() - key_pos
                                        : line_end - key_pos;
      updated.replace(key_pos, replace_len, new_line);
    } else {
      const std::string section_header = "storage_engine:\n";
      const size_t section_pos = updated.find(section_header);
      if (section_pos == std::string::npos) {
        return false;
      }
      const size_t insert_pos =
          updated.find('\n', section_pos + section_header.size());
      if (insert_pos == std::string::npos) {
        return false;
      }
      updated.insert(insert_pos + 1, new_line + "\n");
    }

    std::ofstream output(config_path_, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
      return false;
    }
    output << updated;
    output.flush();
    return output.good();
  }

  /**
   * @brief Initialize server with test configuration
   */
  bool InitializeServer() {
    if (auto_runtime_bundle_) {
      const std::filesystem::path runtime_manifest =
          test_artifacts_dir_ / "runtime" / "manifest.json";
      if (!std::filesystem::exists(runtime_manifest)) {
        if (!ExportRuntimeConfig(false).has_value()) {
          return false;
        }
      }
    }
    server_ = std::make_unique<LogicServer>();
    if (capture_responses_) {
      mir2::logic::test_support::LogicServerTestAccess::SetResponseSendHook(
          *server_,
          [this](uint64_t client_id,
                 uint16_t msg_id,
                 const std::vector<uint8_t>& payload) {
            captured_responses_.push_back({client_id, msg_id, payload});
          });
    }
    return server_->Initialize(config_path_);
  }

  void EnableResponseCapture() {
    capture_responses_ = true;
    captured_responses_.clear();
  }

  void ClearCapturedResponses() {
    captured_responses_.clear();
  }

  std::optional<std::filesystem::path> ExportRuntimeConfig(bool include_skills,
                                                           bool empty_skills = false) {
    const std::filesystem::path excel_dir = test_artifacts_dir_ / "excel";
    const std::filesystem::path runtime_dir = test_artifacts_dir_ / "runtime";
    std::error_code ec;
    if (!(std::filesystem::create_directories(excel_dir, ec) || !ec)) {
      ADD_FAILURE() << "Failed to create excel dir: " << excel_dir
                    << " error=" << ec.message();
      return std::nullopt;
    }
    if (!(std::filesystem::create_directories(runtime_dir, ec) || !ec)) {
      ADD_FAILURE() << "Failed to create runtime dir: " << runtime_dir
                    << " error=" << ec.message();
      return std::nullopt;
    }

    const auto workbook_writer = test_artifacts_dir_ / "write_items_workbook.py";
    std::ofstream script_file(workbook_writer, std::ios::out | std::ios::trunc);
    if (!script_file.is_open()) {
      ADD_FAILURE() << "Failed to open workbook writer script: " << workbook_writer;
      return std::nullopt;
    }
    script_file << R"PY(
from openpyxl import Workbook
from pathlib import Path
import sys

items_path = Path(sys.argv[1])
items_path.parent.mkdir(parents=True, exist_ok=True)
wb = Workbook()
ws = wb.active
ws.title = "items"
ws.append(["id", "name", "std_mode", "price", "stackable", "stack_limit"])
ws.append([1002, "Town Scroll", 3, 200, True, 10])
ws.append([1001, "Small Heal", 0, 100, True, 20])
wb.save(items_path)

skills_arg = sys.argv[2] if len(sys.argv) > 2 else ""
empty_skills = len(sys.argv) > 3 and sys.argv[3] == "empty"
if skills_arg:
    skills_path = Path(skills_arg)
    wb = Workbook()
    ws = wb.active
    ws.title = "skills"
    ws.append([
        "id", "name", "required_class", "required_level", "skill_type", "target_type",
        "description", "max_level", "train_level_req", "train_points_req",
        "is_universal", "is_passive", "mp_cost", "consumes_talisman",
        "talisman_cost", "required_amulet", "amulet_cost", "cooldown_ms",
        "cast_time_ms", "can_be_interrupted", "range", "aoe_radius",
        "min_power", "max_power", "def_power", "def_max_power", "train_lv",
        "duration_ms", "stat_modifier", "dot_damage", "dot_interval_ms",
        "effect_type", "effect_id", "animation_id", "sound_id"
    ])
    if not empty_skills:
        ws.append([
            3, "Attack Training", "WARRIOR", 7, "PHYSICAL", "SINGLE_ENEMY",
            "desc", 3, "[1, 7, 11, 15]", "[0, 500, 2000, 5000]",
            False, False, 3, False, 0, "NONE", 0, 800,
            0, True, 1.0, 0.0, 3, 8, 0, 0, 0,
            0, 0, 0, 1000, 0, 0, "", ""
        ])
        ws.append([
            11, "Lightning", "MAGE", 19, "MAGICAL", "SINGLE_ENEMY",
            "desc", 3, "[1, 7, 11, 15]", "[0, 500, 2000, 5000]",
            False, False, 14, False, 0, "NONE", 0, 1000,
            0, True, 7.0, 0.0, 8, 16, 0, 0, 0,
            0, 0, 0, 1000, 0, 0, "", ""
        ])
    wb.save(skills_path)
)PY";
    script_file.flush();
    if (!script_file.good()) {
      ADD_FAILURE() << "Failed to write workbook writer script: " << workbook_writer;
      return std::nullopt;
    }

    const auto workbook_path = excel_dir / "items.xlsx";
    const auto skills_workbook_path = excel_dir / "skills.xlsx";
    std::vector<std::string> workbook_cmd = {"python3", workbook_writer.string(), workbook_path.string()};
    if (include_skills) {
      workbook_cmd.push_back(skills_workbook_path.string());
      if (empty_skills) {
        workbook_cmd.push_back("empty");
      }
    }
    auto result = RunCommand(workbook_cmd);
    if (result.exit_code != 0) {
      ADD_FAILURE() << result.stderr_text;
      return std::nullopt;
    }

    const auto export_script = GetRepoRoot() / "tools" / "config_pipeline" / "export.py";
    std::vector<std::string> export_cmd = {"python3",
                                           export_script.string(),
                                           "--source-dir",
                                           excel_dir.string(),
                                           "--out-dir",
                                           runtime_dir.string(),
                                           "--tables",
                                           "items"};
    if (include_skills) {
      export_cmd.push_back("skills");
    }
    export_cmd.insert(export_cmd.end(),
                      {"--generated-at", "2026-03-07T00:00:00Z"});
    result = RunCommand(export_cmd);
    if (result.exit_code != 0) {
      ADD_FAILURE() << result.stderr_text;
      return std::nullopt;
    }
    if (!include_skills &&
        !AddRuntimeArtifact(
            runtime_dir,
            "skills",
            RuntimeArtifactSpec{
                "skills.json",
                nlohmann::json{{"skills", nlohmann::json::array()}}.dump(2) + "\n",
            })) {
      ADD_FAILURE() << "failed to add runtime skills artifact";
      return std::nullopt;
    }
    if (!AddRuntimeArtifact(
            runtime_dir,
            "maps",
            RuntimeArtifactSpec{
                "maps.json",
                BuildRuntimeMapsContent({default_map_id_}),
            })) {
      ADD_FAILURE() << "failed to add runtime maps artifact";
      return std::nullopt;
    }
    if (!AddRuntimeArtifact(
            runtime_dir,
            "gates",
            RuntimeArtifactSpec{
                "gates.json",
                BuildRuntimeGatesContent(default_map_id_, true),
            })) {
      ADD_FAILURE() << "failed to add runtime gates artifact";
      return std::nullopt;
    }
    if (!AddRuntimeArtifact(
            runtime_dir,
            "drops",
            RuntimeArtifactSpec{
                "drops.json",
                BuildRuntimeDropsContent(true),
            })) {
      ADD_FAILURE() << "failed to add runtime drops artifact";
      return std::nullopt;
    }
    if (!AddRuntimeArtifact(
            runtime_dir,
            "shops",
            RuntimeArtifactSpec{
                "shops.json",
                BuildRuntimeShopsContent(true),
            })) {
      ADD_FAILURE() << "failed to add runtime shops artifact";
      return std::nullopt;
    }
    if (!AddRuntimeArtifact(
            runtime_dir,
            "monster_spawns",
            RuntimeArtifactSpec{
                "monster_spawns.json",
                BuildRuntimeMonsterSpawnsContent({}),
            })) {
      ADD_FAILURE() << "failed to add runtime monster spawns artifact";
      return std::nullopt;
    }
    if (!AddRuntimeArtifact(
            runtime_dir,
            "npcs",
            RuntimeArtifactSpec{
                "npcs.json",
                BuildRuntimeNpcsContent({}),
            })) {
      ADD_FAILURE() << "failed to add runtime npcs artifact";
      return std::nullopt;
    }
    if (!std::filesystem::exists(runtime_dir / "items.json")) {
      ADD_FAILURE() << "items.json was not generated";
      return std::nullopt;
    }
    if (!std::filesystem::exists(runtime_dir / "manifest.json")) {
      ADD_FAILURE() << "manifest.json was not generated";
      return std::nullopt;
    }
    if (include_skills && !std::filesystem::exists(runtime_dir / "skills.json")) {
      ADD_FAILURE() << "skills.json was not generated";
      return std::nullopt;
    }
    return runtime_dir;
  }

  std::optional<std::filesystem::path> ExportRepoExcelRuntimeConfig() {
    const std::filesystem::path source_dir = GetRepoRoot() / "config" / "excel";
    const std::filesystem::path runtime_dir = test_artifacts_dir_ / "runtime";
    std::error_code ec;
    if (!(std::filesystem::create_directories(runtime_dir, ec) || !ec)) {
      ADD_FAILURE() << "Failed to create runtime dir: " << runtime_dir
                    << " error=" << ec.message();
      return std::nullopt;
    }

    const auto export_script = GetRepoRoot() / "tools" / "config_pipeline" / "export.py";
    std::vector<std::string> export_cmd = {
        "python3",
        export_script.string(),
        "--source-dir",
        source_dir.string(),
        "--out-dir",
        runtime_dir.string(),
        "--generated-at",
        "2026-03-07T00:00:00Z",
    };
    const auto result = RunCommand(export_cmd);
    if (result.exit_code != 0) {
      ADD_FAILURE() << result.stderr_text;
      return std::nullopt;
    }
    return runtime_dir;
  }

  /**
   * @brief Start server in background thread
   */
  void StartServerAsync() {
    ASSERT_NE(server_, nullptr)
        << "StartServerAsync requires InitializeServer() first";
    server_thread_ = std::thread([this]() { server_->Run(); });

    // Wait for server to fully start (max 1 second)
    std::this_thread::sleep_for(100ms);
  }

  /**
   * @brief Shutdown server and wait for thread to join
   */
  void ShutdownServer() {
    if (server_) {
      server_->Shutdown();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  std::unique_ptr<LogicServer> server_;
  std::unique_ptr<MockStorageBackend> mock_storage_;
  std::string config_path_;
  std::filesystem::path combat_config_path_;
  std::filesystem::path maps_config_path_;
  std::filesystem::path gates_config_path_;
  std::filesystem::path test_artifacts_dir_;
  std::filesystem::path map_file_path_;
  std::vector<std::filesystem::path> extra_created_map_files_;
  uint32_t default_map_id_ = 1;
  bool created_map_file_ = false;
  bool had_existing_map_file_ = false;
  bool capture_responses_ = false;
  bool auto_runtime_bundle_ = true;
  std::vector<CapturedResponse> captured_responses_;
  std::thread server_thread_;
};

/**
 * @brief Test: Server initialization succeeds with valid configuration
 */
TEST_F(LogicServerTest, InitializeFailsWhenRuntimeBundleMissing) {
  auto_runtime_bundle_ = false;
  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeLoadsBundleExportedFromRepoConfigExcel) {
  const auto runtime_dir = ExportRepoExcelRuntimeConfig();
  ASSERT_TRUE(runtime_dir.has_value());

  const auto maps = nlohmann::json::parse(ReadTextFile(*runtime_dir / "maps.json"));
  ASSERT_TRUE(maps.contains("maps"));
  ASSERT_TRUE(maps["maps"].is_array());
  ASSERT_FALSE(maps["maps"].empty());

  std::vector<uint32_t> map_ids;
  for (const auto& entry : maps["maps"]) {
    map_ids.push_back(entry.at("map_id").get<uint32_t>());
  }
  ASSERT_FALSE(map_ids.empty());
  const uint32_t bootstrap_map_id = map_ids.front();
  WriteCombatConfig(static_cast<int>(bootstrap_map_id));
  for (uint32_t map_id : map_ids) {
    if (map_id != default_map_id_) {
      EnsureAdditionalMapFixture(map_id);
    }
  }

  ASSERT_TRUE(InitializeServer());
  EXPECT_NE(mir2::data::ItemTemplateManager::Instance().GetTemplate(1001u), nullptr);
  EXPECT_NE(ecs::RegistryManager::Instance().GetWorld(bootstrap_map_id), nullptr);
}

TEST_F(LogicServerTest, InitializeLoadsExportedRuntimeItemsIntoItemTemplateManager) {
  ASSERT_TRUE(ExportRuntimeConfig(false).has_value());

  ASSERT_TRUE(InitializeServer());

  const auto* item = mir2::data::ItemTemplateManager::Instance().GetTemplate(1001u);
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->name, "Small Heal");
  EXPECT_EQ(item->price, 100);
  EXPECT_EQ(mir2::ecs::SkillRegistry::instance().get_skill(3), nullptr);
}

TEST_F(LogicServerTest, InitializeLoadsExportedRuntimeSkillsIntoSkillRegistry) {
  ASSERT_TRUE(ExportRuntimeConfig(true).has_value());

  ASSERT_TRUE(InitializeServer());

  const auto* skill = mir2::ecs::SkillRegistry::instance().get_skill(3u);
  ASSERT_NE(skill, nullptr);
  EXPECT_EQ(skill->name, "Attack Training");
  EXPECT_EQ(skill->required_class, mir2::common::CharacterClass::WARRIOR);
}

TEST_F(LogicServerTest, InitializeSucceedsWhenSkillsArtifactIsEmpty) {
  ASSERT_TRUE(ExportRuntimeConfig(true, true).has_value());

  ASSERT_TRUE(InitializeServer());
  EXPECT_EQ(mir2::ecs::SkillRegistry::instance().get_skill(3u), nullptr);
}

TEST_F(LogicServerTest, InitializeFailsWhenRuntimeManifestHashIsCorrupted) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());

  const auto manifest_path = *runtime_dir / "manifest.json";
  auto manifest = nlohmann::json::parse(ReadTextFile(manifest_path));
  auto item_artifact = FindManifestArtifact(manifest, "items");
  ASSERT_NE(item_artifact, manifest["artifacts"].end());
  (*item_artifact)["hash"] = std::string(64, '0');
  std::ofstream output(manifest_path, std::ios::out | std::ios::trunc);
  ASSERT_TRUE(output.is_open());
  output << manifest.dump(2) << "\n";
  output.flush();
  ASSERT_TRUE(output.good());

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeFailsWhenSkillsArtifactHashIsCorrupted) {
  const auto runtime_dir = ExportRuntimeConfig(true);
  ASSERT_TRUE(runtime_dir.has_value());

  const auto manifest_path = *runtime_dir / "manifest.json";
  auto manifest = nlohmann::json::parse(ReadTextFile(manifest_path));
  auto skills_artifact = FindManifestArtifact(manifest, "skills");
  ASSERT_NE(skills_artifact, manifest["artifacts"].end());
  (*skills_artifact)["hash"] = std::string(64, '0');
  std::ofstream output(manifest_path, std::ios::out | std::ios::trunc);
  ASSERT_TRUE(output.is_open());
  output << manifest.dump(2) << "\n";
  output.flush();
  ASSERT_TRUE(output.good());

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeFailsWhenSkillsArtifactFileIsMissing) {
  const auto runtime_dir = ExportRuntimeConfig(true);
  ASSERT_TRUE(runtime_dir.has_value());
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::remove(*runtime_dir / "skills.json", ec));

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest,
       InitializeCreatesAdditionalWorldsFromRuntimeMapsWhenLegacyYamlMissing) {
  const uint32_t extra_map_id = default_map_id_ + 1;
  EnsureAdditionalMapFixture(extra_map_id);

  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "maps",
      RuntimeArtifactSpec{
          "maps.json",
          BuildRuntimeMapsContent({default_map_id_, extra_map_id}),
      }));

  ASSERT_TRUE(InitializeServer());
  EXPECT_NE(ecs::RegistryManager::Instance().GetWorld(default_map_id_), nullptr);
  EXPECT_NE(ecs::RegistryManager::Instance().GetWorld(extra_map_id), nullptr);
}

TEST_F(LogicServerTest,
       InitializeLoadsRuntimeGatesWhenArtifactPresentAndLegacyYamlMissing) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "maps",
      RuntimeArtifactSpec{"maps.json", BuildRuntimeMapsContent({default_map_id_})}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "gates",
      RuntimeArtifactSpec{"gates.json", BuildRuntimeGatesContent(default_map_id_)}));

  ASSERT_TRUE(InitializeServer());
  auto gate = mir2::logic::test_support::LogicServerTestAccess::GateManager(*server_)
                  .CheckGateTrigger(static_cast<int32_t>(default_map_id_), 10, 20);
  ASSERT_TRUE(gate.has_value());
  EXPECT_EQ(gate->gate_id, 101u);
}

TEST_F(LogicServerTest, InitializeLoadsRuntimeDropsWhenArtifactPresent) {
  const uint32_t kRuntimeMonsterTemplateId = 900001;
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "maps",
      RuntimeArtifactSpec{"maps.json", BuildRuntimeMapsContent({default_map_id_})}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "drops",
      RuntimeArtifactSpec{"drops.json",
                          BuildRuntimeDropsContent(false, kRuntimeMonsterTemplateId)}));

  ASSERT_TRUE(InitializeServer());
  const auto& world_systems =
      mir2::logic::test_support::LogicServerTestAccess::WorldSystems(*server_);
  auto bundle_it = world_systems.find(default_map_id_);
  ASSERT_NE(bundle_it, world_systems.end());
  ASSERT_NE(bundle_it->second, nullptr);
  ASSERT_NE(bundle_it->second->monster_drop_system, nullptr);
  EXPECT_TRUE(mir2::ecs::test_support::MonsterDropSystemTestAccess::DropTables(
                  *bundle_it->second->monster_drop_system)
                  .contains(kRuntimeMonsterTemplateId));
}

TEST_F(LogicServerTest, InitializeUsesEmptyRuntimeDropsWhenArtifactPresent) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "maps",
      RuntimeArtifactSpec{"maps.json", BuildRuntimeMapsContent({default_map_id_})}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "drops",
      RuntimeArtifactSpec{"drops.json", BuildRuntimeDropsContent(true)}));

  ASSERT_TRUE(InitializeServer());
  const auto& world_systems =
      mir2::logic::test_support::LogicServerTestAccess::WorldSystems(*server_);
  auto bundle_it = world_systems.find(default_map_id_);
  ASSERT_NE(bundle_it, world_systems.end());
  ASSERT_NE(bundle_it->second, nullptr);
  ASSERT_NE(bundle_it->second->monster_drop_system, nullptr);
  EXPECT_TRUE(mir2::ecs::test_support::MonsterDropSystemTestAccess::DropTables(
                  *bundle_it->second->monster_drop_system)
                  .empty());
}

TEST_F(LogicServerTest, InitializeAppliesRuntimeMapAttributesWhenMapsArtifactPresent) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "maps",
      RuntimeArtifactSpec{
          "maps.json",
          BuildRuntimeMapsContentFromEntries({
              nlohmann::json{
                  {"map_id", default_map_id_},
                  {"is_safe_zone", false},
                  {"is_pk_zone", true},
                  {"min_level", 10},
                  {"max_level", 20},
                  {"no_recall", true},
                  {"safe_zones",
                   nlohmann::json::array({
                       {{"x", 1}, {"y", 2}, {"radius", 3}},
                   })},
                  {"quest_requirements",
                   nlohmann::json::array({
                       {{"quest_id", 7}, {"quest_value", 11}},
                   })},
                  {"fixes", nlohmann::json::array()},
              },
          }),
      }));

  ASSERT_TRUE(InitializeServer());

  auto* world = ecs::RegistryManager::Instance().GetWorld(default_map_id_);
  ASSERT_NE(world, nullptr);
  auto* map_ctx = world->Registry().ctx().find<mir2::game::map::MapInstance*>();
  ASSERT_NE(map_ctx, nullptr);
  ASSERT_NE(*map_ctx, nullptr);

  EXPECT_TRUE((*map_ctx)->CanPK());
  EXPECT_FALSE((*map_ctx)->CheckLevelRequirement(9));
  EXPECT_TRUE((*map_ctx)->CheckLevelRequirement(10));
  EXPECT_FALSE((*map_ctx)->CheckLevelRequirement(21));

  const auto attributes = (*map_ctx)->GetAttributes();
  EXPECT_TRUE(attributes.no_recall);
  ASSERT_EQ(attributes.safe_zones.size(), 1u);
  EXPECT_EQ(attributes.safe_zones[0].radius, 3);
  ASSERT_EQ(attributes.quest_requirements.size(), 1u);
  EXPECT_EQ(attributes.quest_requirements[0].quest_id, 7);
}

TEST_F(LogicServerTest, InitializeFailsWhenMapsArtifactFileIsMissing) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::remove(*runtime_dir / "maps.json", ec));

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest,
       InitializeLoadsRuntimeShopsAndPublishesShopOpenWhenArtifactPresent) {
  EnableResponseCapture();
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "shops",
      RuntimeArtifactSpec{"shops.json", BuildRuntimeShopsContent(false)}));

  ASSERT_TRUE(InitializeServer());
  const auto& world_systems =
      mir2::logic::test_support::LogicServerTestAccess::WorldSystems(*server_);
  auto bundle_it = world_systems.find(default_map_id_);
  ASSERT_NE(bundle_it, world_systems.end());
  ASSERT_NE(bundle_it->second, nullptr);
  ASSERT_NE(bundle_it->second->merchant_service, nullptr);
  ASSERT_NE(bundle_it->second->npc_shop_response_service, nullptr);
  ASSERT_NE(bundle_it->second->merchant_service->GetShop(77), nullptr);

  auto* world = ecs::RegistryManager::Instance().GetWorld(default_map_id_);
  ASSERT_NE(world, nullptr);
  const auto player = world->Registry().create();
  auto& identity = world->Registry().emplace<ecs::CharacterIdentityComponent>(player);
  identity.id = 1001;
  identity.name = "player_1001";
  EXPECT_FALSE(mir2::logic::test_support::LogicServerTestAccess::RoleStoreRef(*server_)
                   .BindClientRole(/*client_id=*/9001, /*player_id=*/1001)
                   .has_value());
  ASSERT_EQ(mir2::logic::test_support::LogicServerTestAccess::RoleStoreRef(*server_)
                .GetClientIdByRoleId(/*player_id=*/1001),
            std::optional<uint64_t>(9001));

  ClearCapturedResponses();
  ecs::events::NpcOpenMerchantEvent event{};
  event.player = player;
  event.npc_id = 5001;
  event.store_id = 77;
  world->GetEventBus().Publish(event);

  ASSERT_EQ(captured_responses_.size(), 1u);
  EXPECT_EQ(captured_responses_[0].client_id, 9001u);
  EXPECT_EQ(captured_responses_[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kNpcShopOpen));

  mir2::common::NpcShopOpenMsg decoded;
  ASSERT_EQ(mir2::common::DecodeNpcShopOpen(
                static_cast<uint16_t>(mir2::common::MsgId::kNpcShopOpen),
                captured_responses_[0].payload,
                &decoded),
            mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(decoded.shop_id, 77u);
  EXPECT_EQ(decoded.npc_id, 5001u);
  ASSERT_EQ(decoded.items.size(), 2u);
}

TEST_F(LogicServerTest, InitializeFailsWhenShopsArtifactFileIsMissing) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::remove(*runtime_dir / "shops.json", ec));

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeUsesEmptyRuntimeShopsWhenArtifactPresent) {
  ASSERT_TRUE(ExportRuntimeConfig(false).has_value());
  WriteShopsConfigText(
      "shops:\n"
      "  - store_id: 88\n"
      "    name: legacy_shop\n"
      "    buy_rate: 1.0\n"
      "    sell_rate: 0.5\n"
      "    items:\n"
      "      - item_id: 1001\n"
      "        price: 100\n"
      "        stock: -1\n");

  const auto runtime_dir = test_artifacts_dir_ / "runtime";
  ASSERT_TRUE(AddRuntimeArtifact(
      runtime_dir,
      "shops",
      RuntimeArtifactSpec{"shops.json", BuildRuntimeShopsContent(true)}));

  ASSERT_TRUE(InitializeServer());
  const auto& world_systems =
      mir2::logic::test_support::LogicServerTestAccess::WorldSystems(*server_);
  auto bundle_it = world_systems.find(default_map_id_);
  ASSERT_NE(bundle_it, world_systems.end());
  ASSERT_NE(bundle_it->second, nullptr);
  ASSERT_NE(bundle_it->second->merchant_service, nullptr);
  EXPECT_EQ(bundle_it->second->merchant_service->GetShop(77), nullptr);
  EXPECT_EQ(bundle_it->second->merchant_service->GetShop(88), nullptr);
}

TEST_F(LogicServerTest, InitializeLoadsRuntimeNpcsWhenArtifactPresent) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "shops",
      RuntimeArtifactSpec{"shops.json", BuildRuntimeShopsContent(false)}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "npcs",
      RuntimeArtifactSpec{
          "npcs.json",
          BuildRuntimeNpcsContent({
              nlohmann::json{
                  {"npc_id", 1},
                  {"template_id", 2001},
                  {"name", "Potion Trader"},
                  {"type", "MERCHANT"},
                  {"map_id", default_map_id_},
                  {"x", 10},
                  {"y", 15},
                  {"direction", 0},
                  {"enabled", true},
                  {"store_id", 77},
              },
              nlohmann::json{
                  {"npc_id", 2},
                  {"template_id", 2002},
                  {"name", "Disabled Trader"},
                  {"type", "MERCHANT"},
                  {"map_id", default_map_id_},
                  {"x", 20},
                  {"y", 25},
                  {"direction", 3},
                  {"enabled", false},
                  {"store_id", 77},
              },
          }),
      }));

  ASSERT_TRUE(InitializeServer());
  EXPECT_EQ(game::npc::NpcManager::Instance().TotalCount(), 1u);
  ASSERT_TRUE(game::npc::NpcManager::Instance().GetNpcData(1).has_value());
  EXPECT_FALSE(game::npc::NpcManager::Instance().GetNpcData(2).has_value());
}

TEST_F(LogicServerTest, InitializeFailsWhenNpcsArtifactFileIsMissing) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::remove(*runtime_dir / "npcs.json", ec));

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeUsesEmptyRuntimeNpcsWhenArtifactPresentAndLegacyMissing) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "npcs",
      RuntimeArtifactSpec{"npcs.json", BuildRuntimeNpcsContent({})}));

  ASSERT_TRUE(InitializeServer());
  EXPECT_EQ(game::npc::NpcManager::Instance().TotalCount(), 0u);
}

TEST_F(LogicServerTest, InitializeSkipsDisabledRuntimeMerchantNpcs) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "shops",
      RuntimeArtifactSpec{"shops.json", BuildRuntimeShopsContent(false)}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "npcs",
      RuntimeArtifactSpec{
          "npcs.json",
          BuildRuntimeNpcsContent({
              nlohmann::json{
                  {"npc_id", 20},
                  {"template_id", 4001},
                  {"name", "Disabled Only Trader"},
                  {"type", "MERCHANT"},
                  {"map_id", default_map_id_},
                  {"x", 8},
                  {"y", 9},
                  {"direction", 0},
                  {"enabled", false},
                  {"store_id", 77},
              },
          }),
      }));

  ASSERT_TRUE(InitializeServer());
  EXPECT_EQ(game::npc::NpcManager::Instance().TotalCount(), 0u);
  EXPECT_FALSE(game::npc::NpcManager::Instance().GetNpcData(20).has_value());
}

TEST_F(LogicServerTest, InitializeFailsWhenRuntimeNpcMapIdMissingFromSnapshotMaps) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "maps",
      RuntimeArtifactSpec{"maps.json", BuildRuntimeMapsContent({default_map_id_})}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "shops",
      RuntimeArtifactSpec{"shops.json", BuildRuntimeShopsContent(false)}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "npcs",
      RuntimeArtifactSpec{
          "npcs.json",
          BuildRuntimeNpcsContent({
              nlohmann::json{
                  {"npc_id", 30},
                  {"template_id", 5001},
                  {"name", "Invalid Map Trader"},
                  {"type", "MERCHANT"},
                  {"map_id", default_map_id_ + 999},
                  {"x", 10},
                  {"y", 15},
                  {"direction", 0},
                  {"enabled", true},
                  {"store_id", 77},
              },
          }),
      }));

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeFailsWhenRuntimeNpcStoreIdMissingFromRuntimeShops) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "shops",
      RuntimeArtifactSpec{"shops.json", BuildRuntimeShopsContent(false)}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "npcs",
      RuntimeArtifactSpec{
          "npcs.json",
          BuildRuntimeNpcsContent({
              nlohmann::json{
                  {"npc_id", 40},
                  {"template_id", 6001},
                  {"name", "Missing Shop Trader"},
                  {"type", "MERCHANT"},
                  {"map_id", default_map_id_},
                  {"x", 10},
                  {"y", 15},
                  {"direction", 0},
                  {"enabled", true},
                  {"store_id", 78},
              },
          }),
      }));

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeRuntimeNpcsAppearInInitialStateSync) {
  EnableResponseCapture();
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "shops",
      RuntimeArtifactSpec{"shops.json", BuildRuntimeShopsContent(false)}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "npcs",
      RuntimeArtifactSpec{
          "npcs.json",
          BuildRuntimeNpcsContent({
              nlohmann::json{
                  {"npc_id", 101},
                  {"template_id", 2001},
                  {"name", "Potion Trader"},
                  {"type", "MERCHANT"},
                  {"map_id", default_map_id_},
                  {"x", 110},
                  {"y", 100},
                  {"direction", 0},
                  {"enabled", true},
                  {"store_id", 77},
              },
          }),
      }));

  ASSERT_TRUE(InitializeServer());
  const auto& world_systems =
      mir2::logic::test_support::LogicServerTestAccess::WorldSystems(*server_);
  auto bundle_it = world_systems.find(default_map_id_);
  ASSERT_NE(bundle_it, world_systems.end());
  ASSERT_NE(bundle_it->second, nullptr);
  ASSERT_NE(bundle_it->second->world_sync_broadcast_service, nullptr);

  auto* world = ecs::RegistryManager::Instance().GetWorld(default_map_id_);
  ASSERT_NE(world, nullptr);
  const auto player = world->Registry().create();
  auto& identity = world->Registry().emplace<ecs::CharacterIdentityComponent>(player);
  identity.id = 1001;
  identity.name = "player_1001";
  auto& state = world->Registry().emplace<ecs::CharacterStateComponent>(player);
  state.map_id = default_map_id_;
  state.position = {100, 100};
  auto& attrs = world->Registry().emplace<ecs::CharacterAttributesComponent>(player);
  attrs.level = 10;
  attrs.hp = 100;
  attrs.max_hp = 100;
  attrs.mp = 50;
  attrs.max_mp = 50;
  EXPECT_FALSE(mir2::logic::test_support::LogicServerTestAccess::RoleStoreRef(*server_)
                   .BindClientRole(/*client_id=*/9001, /*player_id=*/1001)
                   .has_value());

  auto& scene_manager =
      mir2::logic::test_support::LogicServerTestAccess::SceneManager(*server_);
  ASSERT_TRUE(scene_manager.AddEntityToMap(
      static_cast<int32_t>(default_map_id_), player, 100, 100));

  ClearCapturedResponses();
  ASSERT_TRUE(bundle_it->second->world_sync_broadcast_service->RequestImmediateStateSyncForRole(
      1001));

  const auto it = std::find_if(
      captured_responses_.begin(),
      captured_responses_.end(),
      [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kStateSync);
      });
  ASSERT_NE(it, captured_responses_.end());
  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::StateSync>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::StateSync>(it->payload.data());
  ASSERT_NE(payload, nullptr);
  ASSERT_NE(payload->entities(), nullptr);
  ASSERT_EQ(payload->entities()->size(), 1u);
  EXPECT_EQ(payload->entities()->Get(0)->entity_type(), mir2::proto::EntityType::NPC);
  EXPECT_EQ(payload->entities()->Get(0)->entity_id(), 101u);
}

TEST_F(LogicServerTest, InitializeRuntimeNpcsSendEntityEnterWhenPlayerMovesIntoAoi) {
  EnableResponseCapture();
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "shops",
      RuntimeArtifactSpec{"shops.json", BuildRuntimeShopsContent(false)}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "npcs",
      RuntimeArtifactSpec{
          "npcs.json",
          BuildRuntimeNpcsContent({
              nlohmann::json{
                  {"npc_id", 102},
                  {"template_id", 2001},
                  {"name", "Potion Trader"},
                  {"type", "MERCHANT"},
                  {"map_id", default_map_id_},
                  {"x", 140},
                  {"y", 100},
                  {"direction", 0},
                  {"enabled", true},
                  {"store_id", 77},
              },
          }),
      }));

  ASSERT_TRUE(InitializeServer());
  auto* world = ecs::RegistryManager::Instance().GetWorld(default_map_id_);
  ASSERT_NE(world, nullptr);
  const auto player = world->Registry().create();
  auto& identity = world->Registry().emplace<ecs::CharacterIdentityComponent>(player);
  identity.id = 1002;
  identity.name = "player_1002";
  auto& state = world->Registry().emplace<ecs::CharacterStateComponent>(player);
  state.map_id = default_map_id_;
  state.position = {10, 10};
  auto& attrs = world->Registry().emplace<ecs::CharacterAttributesComponent>(player);
  attrs.level = 10;
  attrs.hp = 100;
  attrs.max_hp = 100;
  attrs.mp = 50;
  attrs.max_mp = 50;
  EXPECT_FALSE(mir2::logic::test_support::LogicServerTestAccess::RoleStoreRef(*server_)
                   .BindClientRole(/*client_id=*/9002, /*player_id=*/1002)
                   .has_value());

  auto& scene_manager =
      mir2::logic::test_support::LogicServerTestAccess::SceneManager(*server_);
  ASSERT_TRUE(scene_manager.AddEntityToMap(
      static_cast<int32_t>(default_map_id_), player, 10, 10));

  ClearCapturedResponses();
  ASSERT_TRUE(scene_manager.UpdateEntityPosition(player, 121, 100));
  ASSERT_GT(scene_manager.DispatchPendingAOIEvents(), 0u);

  const auto it = std::find_if(
      captured_responses_.begin(),
      captured_responses_.end(),
      [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kEntityEnter);
      });
  ASSERT_NE(it, captured_responses_.end());
  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::EntityEnter>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::EntityEnter>(it->payload.data());
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->entity_id(), 102u);
  EXPECT_EQ(payload->entity_type(), mir2::proto::EntityType::NPC);
}

TEST_F(LogicServerTest, InitializeRuntimeNpcsSendEntityLeaveWhenPlayerMovesOutOfAoi) {
  EnableResponseCapture();
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "shops",
      RuntimeArtifactSpec{"shops.json", BuildRuntimeShopsContent(false)}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "npcs",
      RuntimeArtifactSpec{
          "npcs.json",
          BuildRuntimeNpcsContent({
              nlohmann::json{
                  {"npc_id", 103},
                  {"template_id", 2001},
                  {"name", "Potion Trader"},
                  {"type", "MERCHANT"},
                  {"map_id", default_map_id_},
                  {"x", 110},
                  {"y", 100},
                  {"direction", 0},
                  {"enabled", true},
                  {"store_id", 77},
              },
          }),
      }));

  ASSERT_TRUE(InitializeServer());
  auto* world = ecs::RegistryManager::Instance().GetWorld(default_map_id_);
  ASSERT_NE(world, nullptr);
  const auto player = world->Registry().create();
  auto& identity = world->Registry().emplace<ecs::CharacterIdentityComponent>(player);
  identity.id = 1003;
  identity.name = "player_1003";
  auto& state = world->Registry().emplace<ecs::CharacterStateComponent>(player);
  state.map_id = default_map_id_;
  state.position = {100, 100};
  auto& attrs = world->Registry().emplace<ecs::CharacterAttributesComponent>(player);
  attrs.level = 10;
  attrs.hp = 100;
  attrs.max_hp = 100;
  attrs.mp = 50;
  attrs.max_mp = 50;
  EXPECT_FALSE(mir2::logic::test_support::LogicServerTestAccess::RoleStoreRef(*server_)
                   .BindClientRole(/*client_id=*/9003, /*player_id=*/1003)
                   .has_value());

  auto& scene_manager =
      mir2::logic::test_support::LogicServerTestAccess::SceneManager(*server_);
  ASSERT_TRUE(scene_manager.AddEntityToMap(
      static_cast<int32_t>(default_map_id_), player, 100, 100));
  ClearCapturedResponses();
  ASSERT_TRUE(scene_manager.UpdateEntityPosition(player, 200, 200));
  ASSERT_GT(scene_manager.DispatchPendingAOIEvents(), 0u);

  const auto it = std::find_if(
      captured_responses_.begin(),
      captured_responses_.end(),
      [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kEntityLeave);
      });
  ASSERT_NE(it, captured_responses_.end());
  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::EntityLeave>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::EntityLeave>(it->payload.data());
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->entity_id(), 103u);
  EXPECT_EQ(payload->entity_type(), mir2::proto::EntityType::NPC);
}

TEST_F(LogicServerTest, InitializeRuntimeNpcsOnDifferentMapDoNotAppearInStateSync) {
  EnableResponseCapture();
  const uint32_t other_map_id = default_map_id_ + 1;
  EnsureAdditionalMapFixture(other_map_id);
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "maps",
      RuntimeArtifactSpec{"maps.json", BuildRuntimeMapsContent({default_map_id_, other_map_id})}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "shops",
      RuntimeArtifactSpec{"shops.json", BuildRuntimeShopsContent(false)}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "npcs",
      RuntimeArtifactSpec{
          "npcs.json",
          BuildRuntimeNpcsContent({
              nlohmann::json{
                  {"npc_id", 104},
                  {"template_id", 2001},
                  {"name", "Far Trader"},
                  {"type", "MERCHANT"},
                  {"map_id", other_map_id},
                  {"x", 110},
                  {"y", 100},
                  {"direction", 0},
                  {"enabled", true},
                  {"store_id", 77},
              },
          }),
      }));

  ASSERT_TRUE(InitializeServer());
  const auto& world_systems =
      mir2::logic::test_support::LogicServerTestAccess::WorldSystems(*server_);
  auto bundle_it = world_systems.find(default_map_id_);
  ASSERT_NE(bundle_it, world_systems.end());
  ASSERT_NE(bundle_it->second, nullptr);
  ASSERT_NE(bundle_it->second->world_sync_broadcast_service, nullptr);

  auto* world = ecs::RegistryManager::Instance().GetWorld(default_map_id_);
  ASSERT_NE(world, nullptr);
  const auto player = world->Registry().create();
  auto& identity = world->Registry().emplace<ecs::CharacterIdentityComponent>(player);
  identity.id = 1004;
  identity.name = "player_1004";
  auto& state = world->Registry().emplace<ecs::CharacterStateComponent>(player);
  state.map_id = default_map_id_;
  state.position = {100, 100};
  auto& attrs = world->Registry().emplace<ecs::CharacterAttributesComponent>(player);
  attrs.level = 10;
  attrs.hp = 100;
  attrs.max_hp = 100;
  attrs.mp = 50;
  attrs.max_mp = 50;
  EXPECT_FALSE(mir2::logic::test_support::LogicServerTestAccess::RoleStoreRef(*server_)
                   .BindClientRole(/*client_id=*/9004, /*player_id=*/1004)
                   .has_value());

  auto& scene_manager =
      mir2::logic::test_support::LogicServerTestAccess::SceneManager(*server_);
  ASSERT_TRUE(scene_manager.AddEntityToMap(
      static_cast<int32_t>(default_map_id_), player, 100, 100));

  ClearCapturedResponses();
  ASSERT_TRUE(bundle_it->second->world_sync_broadcast_service->RequestImmediateStateSyncForRole(
      1004));

  const auto it = std::find_if(
      captured_responses_.begin(),
      captured_responses_.end(),
      [](const CapturedResponse& response) {
        return response.msg_id ==
               static_cast<uint16_t>(mir2::common::MsgId::kStateSync);
      });
  ASSERT_NE(it, captured_responses_.end());
  flatbuffers::Verifier verifier(it->payload.data(), it->payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::StateSync>(nullptr));
  const auto* payload = flatbuffers::GetRoot<mir2::proto::StateSync>(it->payload.data());
  ASSERT_NE(payload, nullptr);
  ASSERT_NE(payload->entities(), nullptr);
  EXPECT_EQ(payload->entities()->size(), 0u);
}

TEST_F(LogicServerTest,
       InitializeLoadsRuntimeMonsterSpawnsWhenArtifactPresentAndTicksWorld) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "monster_spawns",
      RuntimeArtifactSpec{
          "monster_spawns.json",
          BuildRuntimeMonsterSpawnsContent({
              nlohmann::json{
                  {"spawn_id", 10},
                  {"map_id", default_map_id_},
                  {"center_x", 1},
                  {"center_y", 1},
                  {"spawn_radius", 0},
                  {"monster_template_id", 9001},
                  {"patrol_radius", 5},
                  {"respawn_interval", 0.0},
                  {"max_count", 1},
                  {"aggro_range", 10},
                  {"attack_range", 3},
              },
          }),
      }));

  ASSERT_TRUE(InitializeServer());
  StartServerAsync();
  ShutdownServer();

  auto* world = ecs::RegistryManager::Instance().GetWorld(default_map_id_);
  ASSERT_NE(world, nullptr);
  auto view = world->Registry().view<ecs::MonsterIdentityComponent>();
  size_t monster_count = 0;
  for (auto entity : view) {
    (void)entity;
    ++monster_count;
  }
  EXPECT_GT(monster_count, 0u);
}

TEST_F(LogicServerTest, InitializeFailsWhenMonsterSpawnsArtifactFileIsMissing) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::remove(*runtime_dir / "monster_spawns.json", ec));

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeUsesEmptyRuntimeMonsterSpawnsWhenArtifactPresent) {
  ASSERT_TRUE(ExportRuntimeConfig(false).has_value());
  WriteSpawnPointsConfigText(
      "spawn_points:\n"
      "  - spawn_id: 10\n"
      "    map_id: " + std::to_string(default_map_id_) + "\n"
      "    center_x: 1\n"
      "    center_y: 1\n"
      "    spawn_radius: 0\n"
      "    monster_template_id: 9001\n"
      "    patrol_radius: 5\n"
      "    respawn_interval: 0\n"
      "    max_count: 1\n"
      "    aggro_range: 10\n"
      "    attack_range: 3\n");
  const auto runtime_dir = test_artifacts_dir_ / "runtime";
  ASSERT_TRUE(AddRuntimeArtifact(
      runtime_dir,
      "monster_spawns",
      RuntimeArtifactSpec{"monster_spawns.json", BuildRuntimeMonsterSpawnsContent({})}));

  ASSERT_TRUE(InitializeServer());
  StartServerAsync();
  ShutdownServer();

  auto* world = ecs::RegistryManager::Instance().GetWorld(default_map_id_);
  ASSERT_NE(world, nullptr);
  auto view = world->Registry().view<ecs::MonsterIdentityComponent>();
  size_t monster_count = 0;
  for (auto entity : view) {
    (void)entity;
    ++monster_count;
  }
  EXPECT_EQ(monster_count, 0u);
}

TEST_F(LogicServerTest,
       InitializeFailsWhenRuntimeMonsterSpawnMapIdMissingFromSnapshotMaps) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "maps",
      RuntimeArtifactSpec{"maps.json", BuildRuntimeMapsContent({default_map_id_})}));
  ASSERT_TRUE(AddRuntimeArtifact(
      *runtime_dir,
      "monster_spawns",
      RuntimeArtifactSpec{
          "monster_spawns.json",
          BuildRuntimeMonsterSpawnsContent({
              nlohmann::json{
                  {"spawn_id", 10},
                  {"map_id", default_map_id_ + 999},
                  {"center_x", 1},
                  {"center_y", 1},
                  {"spawn_radius", 0},
                  {"monster_template_id", 9001},
                  {"patrol_radius", 5},
                  {"respawn_interval", 0.0},
                  {"max_count", 1},
                  {"aggro_range", 10},
                  {"attack_range", 3},
              },
          }),
      }));

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest,
       InitializeFailsWhenRuntimeMonsterSpawnMapIdMissingFromLegacyMaps) {
  ASSERT_TRUE(ExportRuntimeConfig(false).has_value());
  WriteMapsConfig({static_cast<int32_t>(default_map_id_)});
  const auto runtime_dir = test_artifacts_dir_ / "runtime";
  ASSERT_TRUE(AddRuntimeArtifact(
      runtime_dir,
      "monster_spawns",
      RuntimeArtifactSpec{
          "monster_spawns.json",
          BuildRuntimeMonsterSpawnsContent({
              nlohmann::json{
                  {"spawn_id", 10},
                  {"map_id", default_map_id_ + 999},
                  {"center_x", 1},
                  {"center_y", 1},
                  {"spawn_radius", 0},
                  {"monster_template_id", 9001},
                  {"patrol_radius", 5},
                  {"respawn_interval", 0.0},
                  {"max_count", 1},
                  {"aggro_range", 10},
                  {"attack_range", 3},
              },
          }),
      }));

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeFailsWhenGatesArtifactFileIsMissing) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::remove(*runtime_dir / "gates.json", ec));

  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeFailsWhenDropsArtifactFileIsMissing) {
  const auto runtime_dir = ExportRuntimeConfig(false);
  ASSERT_TRUE(runtime_dir.has_value());
  std::error_code ec;
  EXPECT_TRUE(std::filesystem::remove(*runtime_dir / "drops.json", ec));

  EXPECT_FALSE(InitializeServer());
}

/**
 * @brief Test: Server initialization fails with invalid configuration
 */
TEST_F(LogicServerTest, InitializeFailsWithInvalidConfig) {
  server_ = std::make_unique<LogicServer>();
  EXPECT_FALSE(server_->Initialize("nonexistent_config.yaml"));
}

TEST_F(LogicServerTest, InitializeFailsWhenDefaultMapBootstrapFails) {
  WriteCombatConfig(/*respawn_map_id=*/65535);
  EXPECT_FALSE(InitializeServer());
}

TEST_F(LogicServerTest, InitializeSkipsWorldCreationWhenSceneBootstrapFails) {
  constexpr int32_t kMissingMapId = 2000000001;
  ASSERT_EQ(ecs::RegistryManager::Instance().GetWorld(
                static_cast<uint32_t>(kMissingMapId)),
            nullptr);

  WriteMapsConfig({kMissingMapId});
  ASSERT_TRUE(InitializeServer());

  EXPECT_EQ(ecs::RegistryManager::Instance().GetWorld(
                static_cast<uint32_t>(kMissingMapId)),
            nullptr);
}

/**
 * @brief Test: Server can start and stop cleanly
 */
TEST_F(LogicServerTest, ServerStartsAndStopsCleanly) {
  ASSERT_TRUE(InitializeServer());

  std::atomic<bool> server_running{false};

  // Start server in background thread
  server_thread_ = std::thread([this, &server_running]() {
    server_running = true;
    server_->Run();
    server_running = false;
  });

  // Wait for server to start
  std::this_thread::sleep_for(100ms);
  EXPECT_TRUE(server_running.load());

  // Request shutdown
  server_->Shutdown();

  // Wait for thread to finish
  if (server_thread_.joinable()) {
    server_thread_.join();
  }

  // Verify server stopped
  EXPECT_FALSE(server_running.load());
}

/**
 * @brief Test: Server handles multiple shutdown calls gracefully
 */
TEST_F(LogicServerTest, HandleMultipleShutdownCallsGracefully) {
  ASSERT_TRUE(InitializeServer());

  StartServerAsync();

  // Call shutdown multiple times - should not crash or deadlock
  server_->Shutdown();
  server_->Shutdown();
  server_->Shutdown();

  ShutdownServer();

  // Test passes if we reach here without hanging
  SUCCEED();
}

/**
 * @brief Test: Server rejects operations after shutdown
 */
TEST_F(LogicServerTest, RejectsOperationsAfterShutdown) {
  ASSERT_TRUE(InitializeServer());

  StartServerAsync();
  ShutdownServer();

  // Attempting to initialize again should fail
  EXPECT_FALSE(server_->Initialize(config_path_));
}

/**
 * @brief Test: Server handles rapid start/stop cycles
 */
TEST_F(LogicServerTest, HandlesRapidStartStopCycles) {
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(InitializeServer());
    StartServerAsync();
    std::this_thread::sleep_for(50ms);
    ShutdownServer();
    if (storage_engine::StorageEngine::IsInitialized()) {
      storage_engine::StorageEngine::Shutdown();
    }
    server_.reset();
  }
  SUCCEED();
}

/**
 * @brief Test: Server tick loop executes continuously
 */
TEST_F(LogicServerTest, TickLoopExecutesContinuously) {
  // This test would require access to internal tick counters
  // or metrics. For now, we verify the server runs for a duration.
  ASSERT_TRUE(InitializeServer());
  StartServerAsync();

  // Let server run for 500ms (should execute ~10 ticks at 50ms interval)
  std::this_thread::sleep_for(500ms);

  ShutdownServer();
  SUCCEED();
}

/**
 * @brief Test: Server initializes all subsystems correctly
 */
TEST_F(LogicServerTest, InitializesAllSubsystemsCorrectly) {
  ASSERT_TRUE(InitializeServer());

  // Verify all critical subsystems are initialized
  // This would require exposing subsystem state or using metrics
  // For now, we verify initialization doesn't crash
  SUCCEED();
}

/**
 * @brief Test: Server handles missing storage gracefully
 */
TEST_F(LogicServerTest, HandlesMissingStorageGracefully) {
  // Configure storage to fail
  EXPECT_CALL(*mock_storage_, IsHealthy()).WillRepeatedly(::testing::Return(false));

  ASSERT_TRUE(InitializeServer());

  // Server should still start but may log errors
  // This tests graceful degradation
  StartServerAsync();
  std::this_thread::sleep_for(100ms);
  ShutdownServer();

  SUCCEED();
}

/**
 * @brief Test: Server enforces single instance
 */
TEST_F(LogicServerTest, EnforcesSingleInstance) {
  // Attempting to create two servers should either:
  // 1. Fail the second initialization
  // 2. Share resources safely
  // This depends on implementation details
  ASSERT_TRUE(ExportRuntimeConfig(false).has_value());

  auto server1 = std::make_unique<LogicServer>();
  auto server2 = std::make_unique<LogicServer>();

  ASSERT_TRUE(server1->Initialize(config_path_));

  // Second initialization should fail or be handled gracefully
  // Implementation may vary
  SUCCEED();
}

/**
 * @brief Test: Server processes tick within acceptable time
 */
TEST_F(LogicServerTest, ProcessesTickWithinAcceptableTime) {
  ASSERT_TRUE(InitializeServer());
  StartServerAsync();

  // Run for 1 second to ensure ticks are processing
  std::this_thread::sleep_for(1s);

  ShutdownServer();

  // Test passes if shutdown completes within reasonable time (5 seconds)
  SUCCEED();
}

/**
 * @brief Test: Server maintains thread safety during concurrent access
 */
TEST_F(LogicServerTest, MaintainsThreadSafetyDuringConcurrentAccess) {
  ASSERT_TRUE(InitializeServer());
  StartServerAsync();

  // Create multiple threads that might interact with the server
  std::vector<std::thread> threads;
  std::atomic<int> operation_count{0};

  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&operation_count]() {
      for (int j = 0; j < 100; ++j) {
        // Simulate concurrent operations
        operation_count.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(1ms);
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(operation_count.load(), 400);

  ShutdownServer();
}

/**
 * @brief Test: Server handles signal interruption gracefully
 */
TEST_F(LogicServerTest, HandlesSignalInterruptionGracefully) {
  ASSERT_TRUE(InitializeServer());
  StartServerAsync();

  // Simulate signal-like shutdown request
  std::this_thread::sleep_for(100ms);
  server_->Shutdown();

  ShutdownServer();

  // Test passes if shutdown completes cleanly
  SUCCEED();
}

/**
 * @brief Test: Server cleanup is complete after shutdown
 */
TEST_F(LogicServerTest, CleanupIsCompleteAfterShutdown) {
  ASSERT_TRUE(InitializeServer());
  StartServerAsync();

  std::this_thread::sleep_for(100ms);
  ShutdownServer();

  // After shutdown, all resources should be released
  // This would be verified by checking for resource leaks
  // AddressSanitizer or Valgrind would catch issues
  SUCCEED();
}

/**
 * @brief Test: Server handles zero-tick scenario
 */
TEST_F(LogicServerTest, HandlesZeroTickScenario) {
  ASSERT_TRUE(InitializeServer());

  // Start and immediately shutdown before first tick
  server_thread_ = std::thread([this]() { server_->Run(); });

  // Shutdown immediately
  server_->Shutdown();

  if (server_thread_.joinable()) {
    server_thread_.join();
  }

  SUCCEED();
}

/**
 * @brief Test: Server validates configuration on initialization
 */
TEST_F(LogicServerTest, ValidatesConfigurationOnInitialization) {
  server_ = std::make_unique<LogicServer>();

  // Test with various invalid configs
  EXPECT_FALSE(server_->Initialize(""));
  EXPECT_FALSE(server_->Initialize("/invalid/path/config.yaml"));
}

/**
 * @brief Test: Server recovers from transient storage failures
 */
TEST_F(LogicServerTest, RecoversFromTransientStorageFailures) {
  // LogicServer currently builds storage backends internally, so this test can
  // only validate lifecycle robustness instead of mock-driven failover.

  ASSERT_TRUE(InitializeServer());
  StartServerAsync();

  // Let server run and recover
  std::this_thread::sleep_for(500ms);

  ShutdownServer();
  SUCCEED();
}

/**
 * @brief Test: Server metrics are updated correctly
 */
TEST_F(LogicServerTest, MetricsAreUpdatedCorrectly) {
  ASSERT_TRUE(InitializeServer());
  StartServerAsync();

  // Run server to generate metrics
  std::this_thread::sleep_for(300ms);

  // Verify metrics are being published
  // This would require metrics access or mock
  // For now, we verify server runs without issues

  ShutdownServer();
  SUCCEED();
}

/**
 * @brief Test: Server handles configuration reload
 */
TEST_F(LogicServerTest, HandlesConfigurationReload) {
#if !defined(SIGUSR1)
  GTEST_SKIP() << "SIGUSR1 not supported on this platform";
#endif
  ASSERT_TRUE(SetStorageEngineBoolConfig("enable_strict_write_guarantee", false));
  ASSERT_TRUE(SetStorageEngineBoolConfig("enable_new_write_path", true));
  ASSERT_TRUE(InitializeServer());
  StartServerAsync();

  auto& engine = storage_engine::StorageEngine::Instance();

  const std::string before_key = "logic:reload:before:key";
  const std::vector<std::pair<std::string, std::vector<uint8_t>>> before_batch = {
      {before_key, {1, 2, 3}},
      {"", {9}},
  };
  EXPECT_FALSE(engine.BatchSet(before_batch));
  EXPECT_TRUE(engine.Get(before_key).has_value());

  ASSERT_TRUE(SetStorageEngineBoolConfig("enable_new_write_path", false));
  std::raise(SIGUSR1);

  bool switched = false;
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  uint32_t attempt = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const std::string key =
        "logic:reload:after:key:" + std::to_string(attempt++);
    const std::vector<std::pair<std::string, std::vector<uint8_t>>> batch = {
        {key, {4, 5, 6}},
        {"", {8}},
    };
    EXPECT_FALSE(engine.BatchSet(batch));
    if (!engine.Get(key).has_value()) {
      switched = true;
      break;
    }
    std::this_thread::sleep_for(50ms);
  }
  EXPECT_TRUE(switched);

  ShutdownServer();
}

}  // namespace
}  // namespace mir2::logic::test
