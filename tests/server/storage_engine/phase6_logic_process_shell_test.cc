#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <vector>

namespace mir2::storage_engine::phase6_logic_process_test {
namespace {

struct CommandResult {
  int exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
};

std::filesystem::path RepoRoot() {
  auto path = std::filesystem::path(__FILE__);
  return path.parent_path().parent_path().parent_path().parent_path();
}

std::filesystem::path MakeTempDir(std::string_view suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path =
      std::filesystem::path("/tmp") /
      ("mir2_phase6_logic_process_test_" + std::string(suffix) + "_" +
       std::to_string(stamp));
  std::filesystem::create_directories(path);
  return path;
}

void WriteTextFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  ASSERT_TRUE(out.is_open());
  out << content;
}

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return "";
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
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

void SetExecutable(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::permissions(
      path,
      std::filesystem::perms::owner_read |
          std::filesystem::perms::owner_write |
          std::filesystem::perms::owner_exec |
          std::filesystem::perms::group_read |
          std::filesystem::perms::group_exec |
          std::filesystem::perms::others_read |
          std::filesystem::perms::others_exec,
      std::filesystem::perm_options::replace,
      ec);
  ASSERT_FALSE(ec);
}

CommandResult RunBashScript(const std::filesystem::path& script_path,
                            const std::vector<std::string>& args,
                            const std::filesystem::path& cwd = {}) {
  const auto temp_dir = MakeTempDir("command");
  const auto stdout_path = temp_dir / "stdout.txt";
  const auto stderr_path = temp_dir / "stderr.txt";

  std::string command;
  if (!cwd.empty()) {
    command += "cd " + ShellEscape(cwd.string()) + " && ";
  }
  command += "bash " + ShellEscape(script_path.string());
  for (const auto& arg : args) {
    command += " " + ShellEscape(arg);
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

  std::error_code ec;
  std::filesystem::remove_all(temp_dir, ec);
  return result;
}

std::filesystem::path CreateProjectRoot(std::string_view suffix) {
  const auto root = MakeTempDir(std::string(suffix));
  std::filesystem::create_directories(root / "scripts");
  std::filesystem::create_directories(root / "logs");
  std::filesystem::create_directories(root / "artifacts");
  return root;
}

void WriteMockLogicBinary(const std::filesystem::path& path,
                          bool startup_success) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "if [[ \"" + std::string(startup_success ? "true" : "false") +
          "\" == \"true\" ]]; then\n"
      "  echo \"LogicServer initialized\"\n"
      "  sleep 1\n"
      "  exit 0\n"
      "fi\n"
      "echo \"LogicServer init failed\" >&2\n"
      "exit 1\n");
  SetExecutable(path);
}

void WriteConfigValidatingLogicBinary(const std::filesystem::path& path,
                                      std::string_view expected_password,
                                      std::string_view forbidden_password) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "config=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "  case \"$1\" in\n"
      "    --config) config=\"$2\"; shift 2 ;;\n"
      "    *) shift ;;\n"
      "  esac\n"
      "done\n"
      "if [[ -z \"$config\" || ! -f \"$config\" ]]; then\n"
      "  echo \"config_missing\" >&2\n"
      "  exit 2\n"
      "fi\n"
      "database_count=$(grep -c '^database:' \"$config\")\n"
      "storage_count=$(grep -c '^storage_engine:' \"$config\")\n"
      "if [[ \"$database_count\" != \"1\" || \"$storage_count\" != \"1\" ]]; then\n"
      "  echo \"duplicate_sections database=$database_count storage_engine=$storage_count\" >&2\n"
      "  exit 3\n"
      "fi\n"
      "if ! grep -q 'password: \"" + std::string(expected_password) + "\"' \"$config\"; then\n"
      "  echo \"expected_password_missing\" >&2\n"
      "  exit 4\n"
      "fi\n"
      "if grep -q 'password: \"" + std::string(forbidden_password) + "\"' \"$config\"; then\n"
      "  echo \"forbidden_password_present\" >&2\n"
      "  exit 5\n"
      "fi\n"
      "echo \"LogicServer initialized\"\n"
      "sleep 1\n"
      "exit 0\n");
  SetExecutable(path);
}

void WriteMockFaultDriver(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "scenario=\"\"\n"
      "ready=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "  case \"$1\" in\n"
      "    --scenario) scenario=\"$2\"; shift 2 ;;\n"
      "    --ready-file) ready=\"$2\"; shift 2 ;;\n"
      "    --db-path|--backend-state-path|--key|--value-hex|--sleep-ms) shift 2 ;;\n"
      "    *) shift ;;\n"
      "  esac\n"
      "done\n"
      "if [[ -n \"$ready\" ]]; then touch \"$ready\"; fi\n"
      "echo \"phase6_fault_driver_result scenario=${scenario} status=ready\"\n"
      "sleep 30\n");
  SetExecutable(path);
}

void WriteMockStorageAdmin(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "cmd=\"$1\"\n"
      "shift\n"
      "case \"$cmd\" in\n"
      "  health)\n"
      "    echo \"storage_admin_health_summary approx_size_bytes=1024 outbox_depth=0 tombstone_gc_pending=0\"\n"
      "    ;;\n"
      "  validate)\n"
      "    echo \"storage_admin_validate_summary total_corrupted=0\"\n"
      "    ;;\n"
      "  checkpoint-create)\n"
      "    echo \"storage_admin_checkpoint_create_summary status=ok\"\n"
      "    ;;\n"
      "  checkpoint-restore)\n"
      "    echo \"storage_admin_checkpoint_restore_summary status=ok\"\n"
      "    ;;\n"
      "  *)\n"
      "    exit 1\n"
      "    ;;\n"
      "esac\n");
  SetExecutable(path);
}

void WriteMockStorageAdmin(const std::filesystem::path& path,
                           int total_corrupted) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "cmd=\"$1\"\n"
      "shift\n"
      "case \"$cmd\" in\n"
      "  health)\n"
      "    echo \"storage_admin_health_summary approx_size_bytes=1024 outbox_depth=0 tombstone_gc_pending=0\"\n"
      "    ;;\n"
      "  validate)\n"
      "    echo \"storage_admin_validate_summary total_corrupted=" +
          std::to_string(total_corrupted) + "\"\n"
      "    ;;\n"
      "  checkpoint-create)\n"
      "    echo \"storage_admin_checkpoint_create_summary status=ok\"\n"
      "    ;;\n"
      "  checkpoint-restore)\n"
      "    echo \"storage_admin_checkpoint_restore_summary status=ok\"\n"
      "    ;;\n"
      "  *)\n"
      "    exit 1\n"
      "    ;;\n"
      "esac\n");
  SetExecutable(path);
}

void WriteStartupValidationConfigCheckingLogicBinary(
    const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "config=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "  case \"$1\" in\n"
      "    --config) config=\"$2\"; shift 2 ;;\n"
      "    *) shift ;;\n"
      "  esac\n"
      "done\n"
      "if [[ -z \"$config\" || ! -f \"$config\" ]]; then\n"
      "  echo \"config_missing\" >&2\n"
      "  exit 2\n"
      "fi\n"
      "if ! grep -q 'startup_fail_on_validation_error: true' \"$config\"; then\n"
      "  echo \"startup_fail_gate_missing\" >&2\n"
      "  exit 3\n"
      "fi\n"
      "if ! grep -q 'enable_v2_encode: true' \"$config\"; then\n"
      "  echo \"enable_v2_encode_missing\" >&2\n"
      "  exit 4\n"
      "fi\n"
      "echo \"LogicServer init failed\" >&2\n"
      "exit 1\n");
  SetExecutable(path);
}

TEST(Phase6LogicProcessGateScriptTest,
     PassesStartupValidationFailClosedSummary) {
  const auto temp_dir = MakeTempDir("logic_gate_startup_validation");
  const auto logic_log = temp_dir / "logic.log";
  const auto health_file = temp_dir / "health.txt";
  const auto validate_file = temp_dir / "validate.txt";
  const auto report_file = temp_dir / "gate.report.txt";

  WriteTextFile(logic_log, "LogicServer init failed\n");
  WriteTextFile(health_file, "storage_admin_health_summary approx_size_bytes=0\n");
  WriteTextFile(validate_file, "storage_admin_validate_summary total_corrupted=1\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase6_logic_process_gate.sh",
      {"--scenario",
       "startup_validation",
       "--logic-log-file",
       logic_log.string(),
       "--health-file",
       health_file.string(),
       "--validate-file",
       validate_file.string(),
       "--report-file",
       report_file.string()});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(ReadTextFile(report_file).find(
                "phase6_logic_process_gate_result status=pass"),
            std::string::npos);
}

TEST(Phase6LogicProcessDrillScriptTest,
     CheckpointRestoreDrillRecordsAcceptance) {
  const auto project_root = CreateProjectRoot("logic_drill_checkpoint_restore");
  const auto drill_script =
      project_root / "scripts/run_storage_engine_phase6_logic_process_drill.sh";
  const auto gate_script =
      project_root / "scripts/run_storage_engine_phase6_logic_process_gate.sh";
  const auto fault_driver_bin =
      project_root / "scripts/mir2_storage_engine_phase6_fault_driver";
  const auto logic_bin = project_root / "scripts/mir2_logic";
  const auto admin_bin = project_root / "scripts/mir2_storage_admin";
  const auto config_template = project_root / "artifacts/logic.yaml";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";
  const auto report_path = project_root / "logs/drill.report.txt";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_logic_process_drill.sh",
      drill_script,
      std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_logic_process_gate.sh",
      gate_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockFaultDriver(fault_driver_bin);
  WriteMockLogicBinary(logic_bin, true);
  WriteMockStorageAdmin(admin_bin);
  WriteTextFile(
      config_template,
      "server:\n"
      "  metrics_port: 0\n"
      "database:\n"
      "  host: \"127.0.0.1\"\n"
      "  port: 5432\n"
      "  user: \"mir2\"\n"
      "  password: \"mir2_password\"\n"
      "  database: \"mir2_game\"\n"
      "storage_engine:\n"
      "  l2_path: \"/tmp/mir2_logic_test\"\n");

  const auto result = RunBashScript(
      drill_script,
      {"--scenario",
       "checkpoint_restore",
       "--logic-bin",
       logic_bin.string(),
       "--fault-driver-bin",
       fault_driver_bin.string(),
       "--admin-bin",
       admin_bin.string(),
       "--config-template",
       config_template.string(),
       "--db-host",
       "127.0.0.1",
       "--db-port",
       "5432",
       "--db-user",
       "mir2",
       "--db-password",
       "mir2_password",
       "--db-name",
       "mir2_game",
       "--db-path",
       (project_root / "artifacts/db").string(),
       "--backend-state-path",
       (project_root / "artifacts/backend_state.txt").string(),
       "--acceptance-csv",
       acceptance_csv.string(),
       "--report-file",
       report_path.string()},
      project_root);

  EXPECT_EQ(result.exit_code, 0);
  const auto csv = ReadTextFile(acceptance_csv);
  EXPECT_NE(csv.find("\"checkpoint_restore\""), std::string::npos);
  EXPECT_NE(csv.find("\"pass\""), std::string::npos);
}

TEST(Phase6LogicProcessDrillScriptTest,
     GeneratedTempConfigOverridesTemplateWithoutDuplicateSections) {
  const auto project_root = CreateProjectRoot("logic_drill_config_override");
  const auto drill_script =
      project_root / "scripts/run_storage_engine_phase6_logic_process_drill.sh";
  const auto gate_script =
      project_root / "scripts/run_storage_engine_phase6_logic_process_gate.sh";
  const auto fault_driver_bin =
      project_root / "scripts/mir2_storage_engine_phase6_fault_driver";
  const auto logic_bin = project_root / "scripts/mir2_logic";
  const auto admin_bin = project_root / "scripts/mir2_storage_admin";
  const auto config_template = project_root / "artifacts/logic.yaml";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";
  const auto report_path = project_root / "logs/drill.report.txt";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_logic_process_drill.sh",
      drill_script,
      std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_logic_process_gate.sh",
      gate_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockFaultDriver(fault_driver_bin);
  WriteConfigValidatingLogicBinary(logic_bin, "mir2_password", "your_password_here");
  WriteMockStorageAdmin(admin_bin);
  WriteTextFile(
      config_template,
      "server:\n"
      "  metrics_port: 9091\n"
      "database:\n"
      "  host: \"127.0.0.1\"\n"
      "  port: 5432\n"
      "  user: \"mir2\"\n"
      "  password: \"your_password_here\"\n"
      "  database: \"mir2_game\"\n"
      "storage_engine:\n"
      "  l2_path: \"/tmp/mir2_logic_bad_template\"\n");

  const auto result = RunBashScript(
      drill_script,
      {"--scenario",
       "checkpoint_restore",
       "--logic-bin",
       logic_bin.string(),
       "--fault-driver-bin",
       fault_driver_bin.string(),
       "--admin-bin",
       admin_bin.string(),
       "--config-template",
       config_template.string(),
       "--db-host",
       "127.0.0.1",
       "--db-port",
       "5432",
       "--db-user",
       "mir2",
       "--db-password",
       "mir2_password",
       "--db-name",
       "mir2_game",
       "--db-path",
       (project_root / "artifacts/db").string(),
       "--backend-state-path",
       (project_root / "artifacts/backend_state.txt").string(),
       "--acceptance-csv",
       acceptance_csv.string(),
       "--report-file",
       report_path.string()},
      project_root);

  EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
  EXPECT_NE(ReadTextFile(report_path).find(
                "phase6_logic_process_drill_result status=pass"),
            std::string::npos);
}

TEST(Phase6LogicProcessDrillScriptTest,
     StartupValidationDrillGeneratesFailClosedV2Config) {
  const auto project_root = CreateProjectRoot("logic_drill_startup_validation");
  const auto drill_script =
      project_root / "scripts/run_storage_engine_phase6_logic_process_drill.sh";
  const auto gate_script =
      project_root / "scripts/run_storage_engine_phase6_logic_process_gate.sh";
  const auto fault_driver_bin =
      project_root / "scripts/mir2_storage_engine_phase6_fault_driver";
  const auto logic_bin = project_root / "scripts/mir2_logic";
  const auto admin_bin = project_root / "scripts/mir2_storage_admin";
  const auto config_template = project_root / "artifacts/logic.yaml";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";
  const auto report_path = project_root / "logs/drill.report.txt";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_logic_process_drill.sh",
      drill_script,
      std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_logic_process_gate.sh",
      gate_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockFaultDriver(fault_driver_bin);
  WriteStartupValidationConfigCheckingLogicBinary(logic_bin);
  WriteMockStorageAdmin(admin_bin, 1);
  WriteTextFile(
      config_template,
      "server:\n"
      "  metrics_port: 9091\n"
      "database:\n"
      "  password: \"your_password_here\"\n"
      "storage_engine:\n"
      "  l2_path: \"/tmp/mir2_logic_bad_template\"\n");

  const auto result = RunBashScript(
      drill_script,
      {"--scenario",
       "startup_validation",
       "--logic-bin",
       logic_bin.string(),
       "--fault-driver-bin",
       fault_driver_bin.string(),
       "--admin-bin",
       admin_bin.string(),
       "--config-template",
       config_template.string(),
       "--db-host",
       "127.0.0.1",
       "--db-port",
       "5432",
       "--db-user",
       "mir2",
       "--db-password",
       "mir2_password",
       "--db-name",
       "mir2_game",
       "--db-path",
       (project_root / "artifacts/db").string(),
       "--backend-state-path",
       (project_root / "artifacts/backend_state.txt").string(),
       "--acceptance-csv",
       acceptance_csv.string(),
       "--report-file",
       report_path.string()},
      project_root);

  EXPECT_EQ(result.exit_code, 0) << result.stderr_text;
  EXPECT_NE(ReadTextFile(report_path).find(
                "phase6_logic_process_drill_result status=pass"),
            std::string::npos);
}

}  // namespace
}  // namespace mir2::storage_engine::phase6_logic_process_test
