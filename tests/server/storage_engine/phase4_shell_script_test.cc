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

namespace mir2::storage_engine::phase4_script_test {
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
      ("mir2_phase4_script_test_" + std::string(suffix) + "_" +
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

std::filesystem::path CreatePhase4ProjectRoot(std::string_view suffix) {
  const auto root = MakeTempDir(std::string(suffix));
  std::filesystem::create_directories(root / "scripts");
  std::filesystem::create_directories(root / "config");
  std::filesystem::create_directories(root / "logs");
  return root;
}

void WritePhase4Config(const std::filesystem::path& path,
                       std::string_view active_key_id,
                       std::string_view env_name) {
  WriteTextFile(
      path,
      "storage_engine:\n"
      "  enable_data_encryption: true\n"
      "  encryption_active_key_id: \"" +
          std::string(active_key_id) + "\"\n"
      "  encryption_key_env: " + std::string(env_name) + "\n");
}

void WriteMockRotationScript(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "mode=\"\"\n"
      "config=\"\"\n"
      "target=\"\"\n"
      "report=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "  case \"$1\" in\n"
      "    --apply) mode=\"apply\"; shift ;;\n"
      "    --rollback) mode=\"rollback\"; shift ;;\n"
      "    --config) config=\"$2\"; shift 2 ;;\n"
      "    --target-key-id) target=\"$2\"; shift 2 ;;\n"
      "    --report-file) report=\"$2\"; shift 2 ;;\n"
      "    --dry-run|--skip-reload) shift ;;\n"
      "    --env-name|--pid) shift 2 ;;\n"
      "    *) shift ;;\n"
      "  esac\n"
      "done\n"
      "mkdir -p \"$(dirname \"$report\")\"\n"
      ": >\"$report\"\n"
      "if [[ -n \"$config\" && -n \"$target\" ]]; then\n"
      "  sed -i -E \"s|^  encryption_active_key_id:.*$|  encryption_active_key_id: \\\"${target}\\\"|\" \"$config\"\n"
      "fi\n"
      "echo \"mock_rotation mode=${mode} target=${target}\" >>\"$report\"\n");
  SetExecutable(path);
}

void WriteMockGateScript(const std::filesystem::path& path,
                         bool should_pass,
                         std::string_view reasons = "") {
  std::string line;
  if (should_pass) {
    line =
        "phase4_health_gate_result status=pass decrypt_failures=0 "
        "decode_errors=0 encrypted_decode_reads=1 "
        "runtime_config_audit_key_enable_data_encryption_total=0 "
        "runtime_config_audit_key_encryption_key_env_total=0";
  } else {
    line =
        "phase4_health_gate_result status=fail reasons=" + std::string(reasons) +
        " decrypt_failures=0 decode_errors=1 encrypted_decode_reads=1 "
        "runtime_config_audit_key_enable_data_encryption_total=0 "
        "runtime_config_audit_key_encryption_key_env_total=0";
  }

  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "report=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "  case \"$1\" in\n"
      "    --report-file) report=\"$2\"; shift 2 ;;\n"
      "    *) shift ;;\n"
      "  esac\n"
      "done\n"
      "mkdir -p \"$(dirname \"$report\")\"\n"
      ": >\"$report\"\n"
      "echo \"" + line + "\" >>\"$report\"\n" +
      (should_pass ? "exit 0\n" : "exit 1\n"));
  SetExecutable(path);
}

void WriteBrokenGateScript(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "report=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "  case \"$1\" in\n"
      "    --report-file) report=\"$2\"; shift 2 ;;\n"
      "    *) shift ;;\n"
      "  esac\n"
      "done\n"
      "mkdir -p \"$(dirname \"$report\")\"\n"
      ": >\"$report\"\n"
      "echo \"gate crashed before writing result\" >&2\n"
      "exit 1\n");
  SetExecutable(path);
}

TEST(Phase4HealthGateScriptTest, PassesHealthyInputFileAndWritesPassReport) {
  const auto temp_dir = MakeTempDir("health_gate_pass");
  const auto input_path = temp_dir / "health.txt";
  const auto report_path = temp_dir / "health.report.txt";

  WriteTextFile(
      input_path,
      "storage_admin_health_summary "
      "enable_data_encryption=true "
      "enable_v2_encode=true "
      "decode_errors=0 "
      "encrypted_decode_reads=1 "
      "decrypt_failures=0 "
      "runtime_config_audit_key_enable_data_encryption_total=0 "
      "runtime_config_audit_key_encryption_key_env_total=0\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase4_health_gate.sh",
      {"--input-file",
       input_path.string(),
       "--min-encrypted-decode-reads",
       "1",
       "--report-file",
       report_path.string()});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.empty());
  EXPECT_NE(ReadTextFile(report_path).find("phase4_health_gate_result status=pass"),
            std::string::npos);
}

TEST(Phase4HealthGateScriptTest, RejectsMissingRuntimeAuditField) {
  const auto temp_dir = MakeTempDir("health_gate_missing_field");
  const auto input_path = temp_dir / "health.txt";
  const auto report_path = temp_dir / "health.report.txt";

  WriteTextFile(
      input_path,
      "storage_admin_health_summary "
      "enable_data_encryption=true "
      "enable_v2_encode=true "
      "decode_errors=0 "
      "encrypted_decode_reads=1 "
      "decrypt_failures=0 "
      "runtime_config_audit_key_enable_data_encryption_total=0\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase4_health_gate.sh",
      {"--input-file", input_path.string(), "--report-file", report_path.string()});

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_NE(ReadTextFile(report_path).find(
                "missing field in health output: "
                "runtime_config_audit_key_encryption_key_env_total"),
            std::string::npos);
}

TEST(Phase4HealthGateScriptTest,
     RejectsInvalidRequireEncryptionEnabledBooleanValue) {
  const auto temp_dir = MakeTempDir("health_gate_invalid_bool");
  const auto input_path = temp_dir / "health.txt";

  WriteTextFile(
      input_path,
      "storage_admin_health_summary "
      "enable_data_encryption=true "
      "enable_v2_encode=true "
      "decode_errors=0 "
      "encrypted_decode_reads=1 "
      "decrypt_failures=0 "
      "runtime_config_audit_key_enable_data_encryption_total=0 "
      "runtime_config_audit_key_encryption_key_env_total=0\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase4_health_gate.sh",
      {"--input-file",
       input_path.string(),
       "--require-encryption-enabled",
       "maybe"});

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_NE(result.stderr_text.find(
                "invalid --require-encryption-enabled: maybe"),
            std::string::npos);
}

TEST(Phase4HealthGateScriptTest, RejectsNonNumericCountersInHealthOutput) {
  const auto temp_dir = MakeTempDir("health_gate_non_numeric");
  const auto input_path = temp_dir / "health.txt";
  const auto report_path = temp_dir / "health.report.txt";

  WriteTextFile(
      input_path,
      "storage_admin_health_summary "
      "enable_data_encryption=true "
      "enable_v2_encode=true "
      "decode_errors=oops "
      "encrypted_decode_reads=1 "
      "decrypt_failures=0 "
      "runtime_config_audit_key_enable_data_encryption_total=0 "
      "runtime_config_audit_key_encryption_key_env_total=0\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase4_health_gate.sh",
      {"--input-file", input_path.string(), "--report-file", report_path.string()});

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_NE(ReadTextFile(report_path).find("non-numeric counters in health output"),
            std::string::npos);
}

TEST(Phase4KeyRotationScriptTest, RejectsMutuallyExclusiveModes) {
  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase4_key_rotation.sh",
      {"--precheck",
       "--apply",
       "--config",
       "/tmp/nonexistent_logic.yaml",
       "--target-key-id",
       "k_new"});

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_NE(result.stderr_text.find(
                "Only one of --precheck/--apply/--rollback may be specified."),
            std::string::npos);
}

TEST(Phase4KeyRotationScriptTest, PrecheckPassesWithValidConfigAndEnv) {
  const auto temp_dir = MakeTempDir("key_rotation_precheck");
  const auto config_path = temp_dir / "logic.yaml";
  const auto report_path = temp_dir / "precheck.report.txt";
  constexpr char kEnvName[] = "MIR2_PHASE4_KEY_ROTATION_TEST_KEYS";

  WritePhase4Config(config_path, "k_old", kEnvName);

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase4_key_rotation.sh",
      {"--precheck",
       "--config",
       config_path.string(),
       "--target-key-id",
       "k_new",
       "--report-file",
       report_path.string()},
      {{kEnvName,
        "k_old=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff,"
        "k_new=ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100"}});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(ReadTextFile(report_path).find("Precheck passed."),
            std::string::npos);
}

TEST(Phase4KeyRotationScriptTest,
     PrecheckFailsWhenTargetKeyMissingFromKeyring) {
  const auto temp_dir = MakeTempDir("key_rotation_missing_target");
  const auto config_path = temp_dir / "logic.yaml";
  const auto report_path = temp_dir / "precheck.report.txt";
  constexpr char kEnvName[] = "MIR2_PHASE4_KEY_ROTATION_TEST_KEYS";

  WritePhase4Config(config_path, "k_old", kEnvName);

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase4_key_rotation.sh",
      {"--precheck",
       "--config",
       config_path.string(),
       "--target-key-id",
       "k_new",
       "--report-file",
       report_path.string()},
      {{kEnvName,
        "k_old=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"}});

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_NE(ReadTextFile(report_path).find(
                "does not contain target key id=k_new"),
            std::string::npos);
}

TEST(Phase4KeyRotationScriptTest, PrecheckFailsWhenKeyringEnvironmentMissing) {
  const auto temp_dir = MakeTempDir("key_rotation_missing_env");
  const auto config_path = temp_dir / "logic.yaml";
  const auto report_path = temp_dir / "precheck.report.txt";
  constexpr char kEnvName[] = "MIR2_PHASE4_KEY_ROTATION_MISSING_ENV";

  unsetenv(kEnvName);
  WritePhase4Config(config_path, "k_old", kEnvName);

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase4_key_rotation.sh",
      {"--precheck",
       "--config",
       config_path.string(),
       "--report-file",
       report_path.string()});

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_NE(ReadTextFile(report_path).find(
                "Environment variable " + std::string(kEnvName) +
                " is empty or missing."),
            std::string::npos);
}

TEST(Phase4GrayBatchScriptTest,
     SkipGateDoesNotRequireRollbackKeyIdAndRecordsSkippedAcceptance) {
  const auto project_root = CreatePhase4ProjectRoot("gray_skip_gate");
  const auto gray_script = project_root / "scripts/run_storage_engine_phase4_gray_batch.sh";
  const auto rotation_script = project_root / "scripts/run_storage_engine_phase4_key_rotation.sh";
  const auto gate_script = project_root / "scripts/run_storage_engine_phase4_health_gate.sh";
  const auto config_path = project_root / "config/logic.yaml";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase4_gray_batch.sh",
      gray_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockRotationScript(rotation_script);
  WriteMockGateScript(gate_script, true);
  WritePhase4Config(config_path, "k_old", "UNUSED_ENV");

  const auto result = RunBashScript(
      gray_script,
      {"--batch",
       "5",
       "--config",
       config_path.string(),
       "--target-key-id",
       "k_new",
       "--skip-gate",
       "--auto-rollback-on-gate-fail",
       "--acceptance-csv",
       acceptance_csv.string()},
      {},
      project_root);

  EXPECT_EQ(result.exit_code, 0);
  const auto csv = ReadTextFile(acceptance_csv);
  EXPECT_NE(csv.find("\"skipped\""), std::string::npos);
  EXPECT_NE(csv.find("\"skip_gate\""), std::string::npos);
}

TEST(Phase4GrayBatchScriptTest,
     GateFailureTriggersRollbackAndRecordsAcceptance) {
  const auto project_root = CreatePhase4ProjectRoot("gray_gate_fail");
  const auto gray_script = project_root / "scripts/run_storage_engine_phase4_gray_batch.sh";
  const auto rotation_script = project_root / "scripts/run_storage_engine_phase4_key_rotation.sh";
  const auto gate_script = project_root / "scripts/run_storage_engine_phase4_health_gate.sh";
  const auto config_path = project_root / "config/logic.yaml";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase4_gray_batch.sh",
      gray_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockRotationScript(rotation_script);
  WriteMockGateScript(gate_script, false, "decode_errors>0");
  WritePhase4Config(config_path, "k_old", "UNUSED_ENV");

  const auto result = RunBashScript(
      gray_script,
      {"--batch",
       "25",
       "--config",
       config_path.string(),
       "--target-key-id",
       "k_new",
       "--rollback-key-id",
       "k_old",
       "--auto-rollback-on-gate-fail",
       "--db-path",
       "/tmp/unused-db",
       "--acceptance-csv",
       acceptance_csv.string()},
      {},
      project_root);

  EXPECT_EQ(result.exit_code, 1);
  const auto csv = ReadTextFile(acceptance_csv);
  EXPECT_NE(csv.find("\"blocked_rolled_back\""), std::string::npos);
  EXPECT_NE(csv.find("\"decode_errors>0\""), std::string::npos);
  EXPECT_NE(csv.find("\"true\",\"true\""), std::string::npos);
  EXPECT_NE(ReadTextFile(config_path).find("encryption_active_key_id: \"k_old\""),
            std::string::npos);
}

TEST(Phase4GrayBatchScriptTest,
     GateFailureWithoutResultMarksGateObservationError) {
  const auto project_root = CreatePhase4ProjectRoot("gray_gate_broken_report");
  const auto gray_script = project_root / "scripts/run_storage_engine_phase4_gray_batch.sh";
  const auto rotation_script =
      project_root / "scripts/run_storage_engine_phase4_key_rotation.sh";
  const auto gate_script =
      project_root / "scripts/run_storage_engine_phase4_health_gate.sh";
  const auto config_path = project_root / "config/logic.yaml";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase4_gray_batch.sh",
      gray_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockRotationScript(rotation_script);
  WriteBrokenGateScript(gate_script);
  WritePhase4Config(config_path, "k_old", "UNUSED_ENV");

  const auto result = RunBashScript(
      gray_script,
      {"--batch",
       "25",
       "--config",
       config_path.string(),
       "--target-key-id",
       "k_new",
       "--db-path",
       "/tmp/unused-db",
       "--acceptance-csv",
       acceptance_csv.string()},
      {},
      project_root);

  EXPECT_EQ(result.exit_code, 1);
  const auto csv = ReadTextFile(acceptance_csv);
  EXPECT_NE(csv.find("\"blocked\""), std::string::npos);
  EXPECT_NE(csv.find("\"error\""), std::string::npos);
  EXPECT_NE(csv.find("\"gate_report_missing\""), std::string::npos);
}

TEST(Phase4GrayBatchScriptTest, RejectsAcceptanceCsvHeaderMismatch) {
  const auto project_root = CreatePhase4ProjectRoot("gray_bad_csv_header");
  const auto gray_script =
      project_root / "scripts/run_storage_engine_phase4_gray_batch.sh";
  const auto rotation_script =
      project_root / "scripts/run_storage_engine_phase4_key_rotation.sh";
  const auto gate_script =
      project_root / "scripts/run_storage_engine_phase4_health_gate.sh";
  const auto config_path = project_root / "config/logic.yaml";
  const auto acceptance_csv = project_root / "logs/acceptance.csv";

  std::filesystem::copy_file(
      RepoRoot() / "scripts/run_storage_engine_phase4_gray_batch.sh",
      gray_script,
      std::filesystem::copy_options::overwrite_existing);
  WriteMockRotationScript(rotation_script);
  WriteMockGateScript(gate_script, true);
  WritePhase4Config(config_path, "k_old", "UNUSED_ENV");
  WriteTextFile(acceptance_csv, "old_header_only\n");

  const auto result = RunBashScript(
      gray_script,
      {"--batch",
       "5",
       "--config",
       config_path.string(),
       "--target-key-id",
       "k_new",
       "--skip-gate",
       "--acceptance-csv",
       acceptance_csv.string()},
      {},
      project_root);

  EXPECT_EQ(result.exit_code, 1);
  EXPECT_NE(result.stderr_text.find("acceptance csv header mismatch"),
            std::string::npos);
}

TEST(Phase5CapacityGateScriptTest, PassesHealthyInputFileAndWritesPassReport) {
  const auto temp_dir = MakeTempDir("phase5_capacity_gate_pass");
  const auto input_path = temp_dir / "health.txt";
  const auto report_path = temp_dir / "capacity.report.txt";

  WriteTextFile(
      input_path,
      "storage_admin_health_summary "
      "approx_size_bytes=1048576 "
      "l2_soft_limit_write_total=0 "
      "l2_hard_limit_reject_total=0 "
      "l2_hard_limit_bypass_total=0\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase5_capacity_gate.sh",
      {"--input-file",
       input_path.string(),
       "--l2-max-size-mb",
       "512",
       "--report-file",
       report_path.string()});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.empty());
  EXPECT_NE(ReadTextFile(report_path).find("phase5_capacity_gate_result status=pass"),
            std::string::npos);
}

TEST(Phase5CapacityGateScriptTest,
     RejectsActiveHardLimitAndRejectCountersOverThreshold) {
  const auto temp_dir = MakeTempDir("phase5_capacity_gate_fail");
  const auto input_path = temp_dir / "health.txt";
  const auto report_path = temp_dir / "capacity.report.txt";

  WriteTextFile(
      input_path,
      "storage_admin_health_summary "
      "approx_size_bytes=524288000 "
      "l2_soft_limit_write_total=9 "
      "l2_hard_limit_reject_total=2 "
      "l2_hard_limit_bypass_total=1\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase5_capacity_gate.sh",
      {"--input-file",
       input_path.string(),
       "--l2-max-size-mb",
       "512",
       "--max-hard-limit-rejects",
       "0",
       "--max-hard-limit-bypasses",
       "0",
       "--report-file",
       report_path.string()});

  EXPECT_EQ(result.exit_code, 1);
  const auto report = ReadTextFile(report_path);
  EXPECT_NE(report.find("phase5_capacity_gate_result status=fail"),
            std::string::npos);
  EXPECT_NE(report.find("hard_limit_active"), std::string::npos);
  EXPECT_NE(report.find("l2_hard_limit_reject_total>0"), std::string::npos);
}

TEST(Phase5TombstoneGcGateScriptTest,
     PassesHealthyInputFileAndWritesPassReport) {
  const auto temp_dir = MakeTempDir("phase5_tombstone_gc_gate_pass");
  const auto input_path = temp_dir / "health.txt";
  const auto report_path = temp_dir / "tombstone_gc.report.txt";

  WriteTextFile(
      input_path,
      "storage_admin_health_summary "
      "tombstone_gc_pending=0 "
      "tombstone_gc_reclaimed_total=1 "
      "tombstone_gc_failed_total=0\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase5_tombstone_gc_gate.sh",
      {"--input-file",
       input_path.string(),
       "--max-tombstone-gc-pending",
       "0",
       "--min-tombstone-gc-reclaimed",
       "1",
       "--max-tombstone-gc-failed",
       "0",
       "--report-file",
       report_path.string()});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.empty());
  EXPECT_NE(ReadTextFile(report_path).find(
                "phase5_tombstone_gc_gate_result status=pass"),
            std::string::npos);
}

TEST(Phase5TombstoneGcGateScriptTest,
     RejectsPendingAndFailedTombstoneGcCounters) {
  const auto temp_dir = MakeTempDir("phase5_tombstone_gc_gate_fail");
  const auto input_path = temp_dir / "health.txt";
  const auto report_path = temp_dir / "tombstone_gc.report.txt";

  WriteTextFile(
      input_path,
      "storage_admin_health_summary "
      "tombstone_gc_pending=2 "
      "tombstone_gc_reclaimed_total=0 "
      "tombstone_gc_failed_total=1\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase5_tombstone_gc_gate.sh",
      {"--input-file",
       input_path.string(),
       "--max-tombstone-gc-pending",
       "0",
       "--min-tombstone-gc-reclaimed",
       "1",
       "--max-tombstone-gc-failed",
       "0",
       "--report-file",
       report_path.string()});

  EXPECT_EQ(result.exit_code, 1);
  const auto report = ReadTextFile(report_path);
  EXPECT_NE(report.find("phase5_tombstone_gc_gate_result status=fail"),
            std::string::npos);
  EXPECT_NE(report.find("tombstone_gc_pending>0"), std::string::npos);
  EXPECT_NE(report.find("tombstone_gc_reclaimed_total<1"), std::string::npos);
  EXPECT_NE(report.find("tombstone_gc_failed_total>0"), std::string::npos);
}

}  // namespace
}  // namespace mir2::storage_engine::phase4_script_test
