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

namespace mir2::storage_engine::phase6_script_test {
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
      ("mir2_phase6_script_test_" + std::string(suffix) + "_" +
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

CommandResult RunBashScript(const std::filesystem::path& script_path,
                            const std::vector<std::string>& args,
                            const std::map<std::string, std::string>& env = {},
                            const std::filesystem::path& cwd = {}) {
  const auto temp_dir = MakeTempDir("command");
  const auto stdout_path = temp_dir / "stdout.txt";
  const auto stderr_path = temp_dir / "stderr.txt";

  std::string command;
  if (!cwd.empty()) {
    command += "cd " + ShellEscape(cwd.string()) + " && ";
  }
  for (const auto& [key, value] : env) {
    command += key + "=" + ShellEscape(value) + " ";
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

std::filesystem::path CreatePhase6ProjectRoot(std::string_view suffix) {
  const auto root = MakeTempDir(std::string(suffix));
  std::filesystem::create_directories(root / "scripts");
  std::filesystem::create_directories(root / "logs");
  std::filesystem::create_directories(root / "artifacts");
  return root;
}

void WriteMockPhase6DriverScript(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "scenario=\"\"\n"
      "kill_point=\"\"\n"
      "ready=\"\"\n"
      "backend_state=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "  case \"$1\" in\n"
      "    --scenario) scenario=\"$2\"; shift 2 ;;\n"
      "    --kill-point) kill_point=\"$2\"; shift 2 ;;\n"
      "    --ready-file) ready=\"$2\"; shift 2 ;;\n"
      "    --backend-state-path) backend_state=\"$2\"; shift 2 ;;\n"
      "    --db-path|--key|--value-hex|--sleep-ms|--timeout-ms) shift 2 ;;\n"
      "    *) shift ;;\n"
      "  esac\n"
      "done\n"
      "if [[ \"$scenario\" == *\"prepare\" ]]; then\n"
      "  if [[ -n \"$ready\" ]]; then touch \"$ready\"; fi\n"
      "  echo \"phase6_fault_driver_result scenario=${scenario} kill_point=${kill_point} status=ready\"\n"
      "  sleep 30\n"
      "fi\n"
      "if [[ \"$scenario\" == \"durable_async_recover\" ]]; then\n"
      "  if [[ \"$kill_point\" == \"recover_wait\" ]]; then\n"
      "    if [[ -z \"$ready\" ]]; then echo \"recover_wait requires ready-file\" >&2; exit 2; fi\n"
      "    if [[ ! -f \"${ready}.killed\" ]]; then touch \"$ready\"; sleep 30; fi\n"
      "  fi\n"
      "  echo \"phase6_fault_driver_result scenario=durable_async_recover kill_point=${kill_point} status=ok backend_key_present=true outbox_depth=0\"\n"
      "  exit 0\n"
      "fi\n"
      "if [[ \"$scenario\" == \"tombstone_gc_recover\" ]]; then\n"
      "  if [[ \"$kill_point\" == \"recover_wait\" ]]; then\n"
      "    if [[ -z \"$ready\" ]]; then echo \"recover_wait requires ready-file\" >&2; exit 2; fi\n"
      "    if [[ ! -f \"${ready}.killed\" ]]; then touch \"$ready\"; sleep 30; fi\n"
      "  fi\n"
      "  echo \"phase6_fault_driver_result scenario=tombstone_gc_recover kill_point=${kill_point} status=ok tombstone_gc_pending=0 tombstone_gc_reclaimed_total=1 tombstone_gc_failed_total=0\"\n"
      "  exit 0\n"
      "fi\n"
      "if [[ \"$scenario\" == \"startup_validation_prepare\" ]]; then\n"
      "  if [[ -n \"$ready\" ]]; then touch \"$ready\"; fi\n"
      "  echo \"phase6_fault_driver_result scenario=startup_validation_prepare kill_point=${kill_point} status=ready\"\n"
      "  sleep 30\n"
      "fi\n"
      "if [[ \"$scenario\" == \"startup_validation_recover\" ]]; then\n"
      "  echo \"phase6_fault_driver_result scenario=startup_validation_recover kill_point=${kill_point} status=init_failed corruption_detected=true\"\n"
      "  exit 0\n"
      "fi\n"
      "if [[ \"$scenario\" == \"checkpoint_restore_prepare\" ]]; then\n"
      "  if [[ -n \"$ready\" ]]; then touch \"$ready\"; fi\n"
      "  echo \"phase6_fault_driver_result scenario=checkpoint_restore_prepare kill_point=${kill_point} status=ready\"\n"
      "  sleep 30\n"
      "fi\n"
      "if [[ \"$scenario\" == \"checkpoint_restore_recover\" ]]; then\n"
      "  echo \"phase6_fault_driver_result scenario=checkpoint_restore_recover kill_point=${kill_point} status=ok restored_key_present=true\"\n"
      "  exit 0\n"
      "fi\n"
      "echo \"unknown scenario=${scenario}\" >&2\n"
      "exit 1\n");
  SetExecutable(path);
}

void WriteMockStorageAdminScript(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "cmd=\"$1\"\n"
      "shift\n"
      "db_path=\"\"\n"
      "checkpoint_path=\"\"\n"
      "restore_db_path=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "  case \"$1\" in\n"
      "    --db-path) db_path=\"$2\"; shift 2 ;;\n"
      "    --output-path) checkpoint_path=\"$2\"; shift 2 ;;\n"
      "    --checkpoint-path) checkpoint_path=\"$2\"; shift 2 ;;\n"
      "    --restore-db-path) restore_db_path=\"$2\"; shift 2 ;;\n"
      "    --overwrite) shift ;;\n"
      "    *) shift ;;\n"
      "  esac\n"
      "done\n"
      "case \"$cmd\" in\n"
      "  health)\n"
      "    echo \"storage_admin_health_summary approx_size_bytes=1048576 outbox_depth=0 dead_letter_depth=0 tombstone_gc_pending=0 tombstone_gc_reclaimed_total=1 tombstone_gc_failed_total=0\"\n"
      "    ;;\n"
      "  validate)\n"
      "    if [[ \"$db_path\" == *\"startup_validation\"* ]]; then\n"
      "      echo \"storage_admin_validate_summary total_corrupted=1 tombstone_gc_pending=0 tombstone_gc_reclaimed_total=1 tombstone_gc_failed_total=0\"\n"
      "      exit 1\n"
      "    else\n"
      "      echo \"storage_admin_validate_summary total_corrupted=0 tombstone_gc_pending=0 tombstone_gc_reclaimed_total=1 tombstone_gc_failed_total=0\"\n"
      "    fi\n"
      "    ;;\n"
      "  checkpoint-create)\n"
      "    mkdir -p \"$checkpoint_path\"\n"
      "    echo \"storage_admin_checkpoint_create_summary db_path=${db_path} output_path=${checkpoint_path} status=ok\"\n"
      "    ;;\n"
      "  checkpoint-restore)\n"
      "    mkdir -p \"$restore_db_path\"\n"
      "    echo \"storage_admin_checkpoint_restore_summary checkpoint_path=${checkpoint_path} restore_db_path=${restore_db_path} status=ok\"\n"
      "    ;;\n"
      "  *)\n"
      "    echo \"unsupported cmd=$cmd\" >&2\n"
      "    exit 1\n"
      "    ;;\n"
      "esac\n");
  SetExecutable(path);
}

TEST(Phase6ReleaseGateScriptTest, PassesDurableAsyncHealthySummaries) {
  const auto temp_dir = MakeTempDir("phase6_release_gate_pass");
  const auto summary_path = temp_dir / "summary.txt";
  const auto health_path = temp_dir / "health.txt";
  const auto validate_path = temp_dir / "validate.txt";
  const auto report_path = temp_dir / "release_gate.report.txt";

  WriteTextFile(
      summary_path,
      "phase6_fault_driver_result scenario=durable_async_recover status=ok backend_key_present=true outbox_depth=0\n");
  WriteTextFile(
      health_path,
      "storage_admin_health_summary approx_size_bytes=1024 outbox_depth=0 tombstone_gc_pending=0 tombstone_gc_reclaimed_total=1 tombstone_gc_failed_total=0\n");
  WriteTextFile(
      validate_path,
      "storage_admin_validate_summary total_corrupted=0\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase6_release_gate.sh",
      {"--scenario",
       "durable_async",
       "--kill-point",
       "recover_wait",
       "--summary-file",
       summary_path.string(),
       "--health-file",
       health_path.string(),
       "--validate-file",
       validate_path.string(),
       "--report-file",
       report_path.string()});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(ReadTextFile(report_path).find(
                "phase6_release_gate_result status=pass scenario=durable_async kill_point=recover_wait"),
            std::string::npos);
}

TEST(Phase6ReleaseGateScriptTest, RejectsTombstoneGcFailures) {
  const auto temp_dir = MakeTempDir("phase6_release_gate_fail");
  const auto summary_path = temp_dir / "summary.txt";
  const auto health_path = temp_dir / "health.txt";
  const auto validate_path = temp_dir / "validate.txt";
  const auto report_path = temp_dir / "release_gate.report.txt";

  WriteTextFile(
      summary_path,
      "phase6_fault_driver_result scenario=tombstone_gc_recover status=ok tombstone_gc_pending=2 tombstone_gc_reclaimed_total=0 tombstone_gc_failed_total=1\n");
  WriteTextFile(
      health_path,
      "storage_admin_health_summary tombstone_gc_pending=2 tombstone_gc_reclaimed_total=0 tombstone_gc_failed_total=1\n");
  WriteTextFile(
      validate_path,
      "storage_admin_validate_summary total_corrupted=0\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase6_release_gate.sh",
      {"--scenario",
       "tombstone_gc",
       "--kill-point",
       "recover_wait",
       "--summary-file",
       summary_path.string(),
       "--health-file",
       health_path.string(),
       "--validate-file",
       validate_path.string(),
       "--report-file",
       report_path.string()});

  EXPECT_EQ(result.exit_code, 1);
  const auto report = ReadTextFile(report_path);
  EXPECT_NE(report.find("phase6_release_gate_result status=fail"),
            std::string::npos);
  EXPECT_NE(report.find("tombstone_gc_pending>0"), std::string::npos);
  EXPECT_NE(report.find("tombstone_gc_failed_total>0"), std::string::npos);
}

TEST(Phase6ReleaseGateScriptTest,
     PassesStartupValidationFailClosedOnCorruption) {
  const auto temp_dir = MakeTempDir("phase6_release_gate_startup_validation");
  const auto summary_path = temp_dir / "summary.txt";
  const auto health_path = temp_dir / "health.txt";
  const auto validate_path = temp_dir / "validate.txt";
  const auto report_path = temp_dir / "release_gate.report.txt";

  WriteTextFile(
      summary_path,
      "phase6_fault_driver_result scenario=startup_validation_recover status=init_failed corruption_detected=true\n");
  WriteTextFile(
      health_path,
      "storage_admin_health_summary approx_size_bytes=0 outbox_depth=0 tombstone_gc_pending=0 tombstone_gc_reclaimed_total=0 tombstone_gc_failed_total=0\n");
  WriteTextFile(
      validate_path,
      "storage_admin_validate_summary total_corrupted=1\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase6_release_gate.sh",
      {"--scenario",
       "startup_validation",
       "--kill-point",
       "prepare_ready",
       "--summary-file",
       summary_path.string(),
       "--health-file",
       health_path.string(),
       "--validate-file",
       validate_path.string(),
       "--report-file",
       report_path.string()});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(ReadTextFile(report_path).find(
                "phase6_release_gate_result status=pass scenario=startup_validation"),
            std::string::npos);
}

TEST(Phase6ReleaseGateScriptTest, PassesCheckpointRestoreHealthySummaries) {
  const auto temp_dir = MakeTempDir("phase6_release_gate_checkpoint_restore");
  const auto summary_path = temp_dir / "summary.txt";
  const auto checkpoint_create_path = temp_dir / "checkpoint_create.txt";
  const auto checkpoint_restore_path = temp_dir / "checkpoint_restore.txt";
  const auto health_path = temp_dir / "health.txt";
  const auto validate_path = temp_dir / "validate.txt";
  const auto report_path = temp_dir / "release_gate.report.txt";

  WriteTextFile(
      summary_path,
      "phase6_fault_driver_result scenario=checkpoint_restore_recover status=ok restored_key_present=true\n");
  WriteTextFile(
      checkpoint_create_path,
      "storage_admin_checkpoint_create_summary db_path=/tmp/src_db output_path=/tmp/cp status=ok\n");
  WriteTextFile(
      checkpoint_restore_path,
      "storage_admin_checkpoint_restore_summary checkpoint_path=/tmp/cp restore_db_path=/tmp/restore_db status=ok\n");
  WriteTextFile(
      health_path,
      "storage_admin_health_summary approx_size_bytes=1024 outbox_depth=0 tombstone_gc_pending=0 tombstone_gc_reclaimed_total=0 tombstone_gc_failed_total=0\n");
  WriteTextFile(
      validate_path,
      "storage_admin_validate_summary total_corrupted=0\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase6_release_gate.sh",
      {"--scenario",
       "checkpoint_restore",
       "--summary-file",
       summary_path.string(),
       "--checkpoint-create-file",
       checkpoint_create_path.string(),
       "--checkpoint-restore-file",
       checkpoint_restore_path.string(),
       "--health-file",
       health_path.string(),
       "--validate-file",
       validate_path.string(),
       "--report-file",
       report_path.string()});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(ReadTextFile(report_path).find(
                "phase6_release_gate_result status=pass scenario=checkpoint_restore"),
            std::string::npos);
}

TEST(Phase6SingleNodeDrillScriptTest,
     DurableAsyncDrillKillsPrepareAndRecordsAcceptance) {
  const auto project_root = CreatePhase6ProjectRoot("phase6_drill_durable_async");
  const auto drill_script =
      project_root / "scripts/run_storage_engine_phase6_single_node_drill.sh";
  const auto release_gate_script =
      project_root / "scripts/run_storage_engine_phase6_release_gate.sh";
  const auto driver_script =
      project_root / "scripts/mir2_storage_engine_phase6_fault_driver";
  const auto admin_script =
      project_root / "scripts/mir2_storage_admin";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";
  const auto report_path = project_root / "logs/drill.report.txt";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_single_node_drill.sh",
      drill_script,
      std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_release_gate.sh",
      release_gate_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockPhase6DriverScript(driver_script);
  WriteMockStorageAdminScript(admin_script);

  const auto result = RunBashScript(
      drill_script,
      {"--scenario",
       "durable_async",
       "--driver-bin",
       driver_script.string(),
       "--admin-bin",
       admin_script.string(),
       "--db-path",
       (project_root / "artifacts/db").string(),
       "--backend-state-path",
       (project_root / "artifacts/backend_state.txt").string(),
       "--acceptance-csv",
       acceptance_csv.string(),
       "--report-file",
       report_path.string()},
      {},
      project_root);

  EXPECT_EQ(result.exit_code, 0);
  const auto csv = ReadTextFile(acceptance_csv);
  EXPECT_NE(csv.find("\"durable_async\""), std::string::npos);
  EXPECT_NE(csv.find("\"prepare_ready\""), std::string::npos);
  EXPECT_NE(csv.find("\"pass\""), std::string::npos);
  EXPECT_NE(ReadTextFile(report_path).find("phase6_single_node_drill_result status=pass"),
            std::string::npos);
}

TEST(Phase6SingleNodeDrillScriptTest,
     DurableAsyncRecoverWaitKillPointRestartsRecoveryAndRecordsAcceptance) {
  const auto project_root =
      CreatePhase6ProjectRoot("phase6_drill_durable_async_recover_wait");
  const auto drill_script =
      project_root / "scripts/run_storage_engine_phase6_single_node_drill.sh";
  const auto release_gate_script =
      project_root / "scripts/run_storage_engine_phase6_release_gate.sh";
  const auto driver_script =
      project_root / "scripts/mir2_storage_engine_phase6_fault_driver";
  const auto admin_script =
      project_root / "scripts/mir2_storage_admin";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";
  const auto report_path = project_root / "logs/drill.report.txt";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_single_node_drill.sh",
      drill_script,
      std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_release_gate.sh",
      release_gate_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockPhase6DriverScript(driver_script);
  WriteMockStorageAdminScript(admin_script);

  const auto result = RunBashScript(
      drill_script,
      {"--scenario",
       "durable_async",
       "--kill-point",
       "recover_wait",
       "--driver-bin",
       driver_script.string(),
       "--admin-bin",
       admin_script.string(),
       "--db-path",
       (project_root / "artifacts/db").string(),
       "--backend-state-path",
       (project_root / "artifacts/backend_state.txt").string(),
       "--acceptance-csv",
       acceptance_csv.string(),
       "--report-file",
       report_path.string()},
      {},
      project_root);

  EXPECT_EQ(result.exit_code, 0);
  const auto recover_log =
      ReadTextFile(project_root / "logs/phase6_durable_async.recover.log");
  EXPECT_NE(recover_log.find("kill_point=recover_wait"), std::string::npos);
  const auto csv = ReadTextFile(acceptance_csv);
  EXPECT_NE(csv.find("\"recover_wait\""), std::string::npos);
  EXPECT_NE(csv.find("\"pass\""), std::string::npos);
}

TEST(Phase6SingleNodeDrillScriptTest,
     StartupValidationDrillRecordsFailClosedAcceptance) {
  const auto project_root =
      CreatePhase6ProjectRoot("phase6_drill_startup_validation");
  const auto drill_script =
      project_root / "scripts/run_storage_engine_phase6_single_node_drill.sh";
  const auto release_gate_script =
      project_root / "scripts/run_storage_engine_phase6_release_gate.sh";
  const auto driver_script =
      project_root / "scripts/mir2_storage_engine_phase6_fault_driver";
  const auto admin_script =
      project_root / "scripts/mir2_storage_admin";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";
  const auto report_path = project_root / "logs/drill.report.txt";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_single_node_drill.sh",
      drill_script,
      std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_release_gate.sh",
      release_gate_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockPhase6DriverScript(driver_script);
  WriteMockStorageAdminScript(admin_script);

  const auto result = RunBashScript(
      drill_script,
      {"--scenario",
       "startup_validation",
       "--driver-bin",
       driver_script.string(),
       "--admin-bin",
       admin_script.string(),
       "--db-path",
       (project_root / "artifacts/db").string(),
       "--backend-state-path",
       (project_root / "artifacts/backend_state.txt").string(),
       "--acceptance-csv",
       acceptance_csv.string(),
       "--report-file",
       report_path.string()},
      {},
      project_root);

  EXPECT_EQ(result.exit_code, 0);
  const auto csv = ReadTextFile(acceptance_csv);
  EXPECT_NE(csv.find("\"startup_validation\""), std::string::npos);
  EXPECT_NE(csv.find("\"pass\""), std::string::npos);
}

TEST(Phase6SingleNodeDrillScriptTest,
     CheckpointRestoreDrillRecordsAcceptance) {
  const auto project_root =
      CreatePhase6ProjectRoot("phase6_drill_checkpoint_restore");
  const auto drill_script =
      project_root / "scripts/run_storage_engine_phase6_single_node_drill.sh";
  const auto release_gate_script =
      project_root / "scripts/run_storage_engine_phase6_release_gate.sh";
  const auto driver_script =
      project_root / "scripts/mir2_storage_engine_phase6_fault_driver";
  const auto admin_script =
      project_root / "scripts/mir2_storage_admin";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";
  const auto report_path = project_root / "logs/drill.report.txt";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_single_node_drill.sh",
      drill_script,
      std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase6_release_gate.sh",
      release_gate_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockPhase6DriverScript(driver_script);
  WriteMockStorageAdminScript(admin_script);

  const auto result = RunBashScript(
      drill_script,
      {"--scenario",
       "checkpoint_restore",
       "--driver-bin",
       driver_script.string(),
       "--admin-bin",
       admin_script.string(),
       "--db-path",
       (project_root / "artifacts/db").string(),
       "--backend-state-path",
       (project_root / "artifacts/backend_state.txt").string(),
       "--acceptance-csv",
       acceptance_csv.string(),
       "--report-file",
       report_path.string()},
      {},
      project_root);

  EXPECT_EQ(result.exit_code, 0);
  const auto csv = ReadTextFile(acceptance_csv);
  EXPECT_NE(csv.find("\"checkpoint_restore\""), std::string::npos);
  EXPECT_NE(csv.find("\"pass\""), std::string::npos);
}

}  // namespace
}  // namespace mir2::storage_engine::phase6_script_test
