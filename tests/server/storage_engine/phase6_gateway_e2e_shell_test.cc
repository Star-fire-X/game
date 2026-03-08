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

namespace mir2::storage_engine::phase6_gateway_e2e_test {
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
      ("mir2_phase6_gateway_e2e_test_" + std::string(suffix) + "_" +
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

void WriteMockGatewayBinary(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "touch \"$0.invoked\"\n"
      "echo \"GatewayServer initialized\"\n"
      "echo \"Gateway connected to Logic\"\n"
      "echo \"Gateway lifecycle transition flushing -> serving\"\n"
      "trap 'exit 0' TERM INT\n"
      "while true; do sleep 1; done\n");
  SetExecutable(path);
}

void WriteMockLogicBinary(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "touch \"$0.invoked\"\n"
      "echo \"LogicServer initialized\"\n"
      "trap 'exit 0' TERM INT\n"
      "while true; do sleep 1; done\n");
  SetExecutable(path);
}

void WriteMockClientBinary(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "report=\"\"\n"
      "ready_file=\"\"\n"
      "continue_file=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "  case \"$1\" in\n"
      "    --report-file) report=\"$2\"; shift 2 ;;\n"
      "    --pre-move-ready-file) ready_file=\"$2\"; shift 2 ;;\n"
      "    --pre-move-continue-file) continue_file=\"$2\"; shift 2 ;;\n"
      "    *) shift ;;\n"
      "  esac\n"
      "done\n"
      "touch \"$0.invoked\"\n"
      "if [[ -z \"$report\" ]]; then\n"
      "  echo \"missing report file\" >&2\n"
      "  exit 2\n"
      "fi\n"
      "if [[ -n \"$ready_file\" ]]; then\n"
      "  mkdir -p \"$(dirname \"$ready_file\")\"\n"
      "  cat >\"$ready_file\" <<EOF\n"
      "phase6_gateway_mock_client_pre_move_ready player_id=1000 move_target_x=101 move_target_y=100\n"
      "EOF\n"
      "fi\n"
      "if [[ -n \"$continue_file\" ]]; then\n"
      "  for _ in $(seq 1 100); do\n"
      "    [[ -f \"$continue_file\" ]] && break\n"
      "    sleep 0.01\n"
      "  done\n"
      "fi\n"
      "cat >\"$report\" <<EOF\n"
      "phase6_gateway_mock_client_result status=ok login_rsp_code=ERR_OK create_role_rsp_code=ERR_OK select_role_rsp_code=ERR_OK move_rsp_code=ERR_OK disconnect_sent=true player_id=1000 move_target_x=101 move_target_y=100\n"
      "EOF\n");
  SetExecutable(path);
}

void WriteMockFaultDriverBinary(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "scenario=\"\"\n"
      "while [[ $# -gt 0 ]]; do\n"
      "  case \"$1\" in\n"
      "    --scenario) scenario=\"$2\"; shift 2 ;;\n"
      "    *) shift ;;\n"
      "  esac\n"
      "done\n"
      "touch \"$0.invoked\"\n"
      "if [[ \"$scenario\" == \"login_select_move_disconnect_verify\" ]]; then\n"
      "  count_file=\"$0.verify_count\"\n"
      "  count=0\n"
      "  if [[ -f \"$count_file\" ]]; then\n"
      "    count=\"$(cat \"$count_file\")\"\n"
      "  fi\n"
      "  if [[ \"$count\" == \"0\" ]]; then\n"
      "    echo 1 >\"$count_file\"\n"
      "    echo \"phase6_fault_driver_verify_result snapshot_present=true snapshot_version=7 snapshot_hex=baseline_hex\"\n"
      "  else\n"
      "    echo \"phase6_fault_driver_verify_result snapshot_present=true snapshot_version=8 snapshot_hex=updated_hex\"\n"
      "  fi\n"
      "else\n"
      "  echo \"phase6_fault_driver_result status=ok\"\n"
      "fi\n");
  SetExecutable(path);
}

void WriteMockAdminBinary(const std::filesystem::path& path) {
  WriteTextFile(
      path,
      "#!/usr/bin/env bash\n"
      "set -euo pipefail\n"
      "cmd=\"$1\"\n"
      "shift\n"
      "touch \"$0.${cmd}.invoked\"\n"
      "case \"$cmd\" in\n"
      "  validate)\n"
      "    echo \"storage_admin_validate_summary total_corrupted=0\"\n"
      "    ;;\n"
      "  decode-character-snapshot)\n"
      "    expected_x=0\n"
      "    expected_y=0\n"
      "    while [[ $# -gt 0 ]]; do\n"
      "      case \"$1\" in\n"
      "        --expected-x) expected_x=\"$2\"; shift 2 ;;\n"
      "        --expected-y) expected_y=\"$2\"; shift 2 ;;\n"
      "        --hex) shift 2 ;;\n"
      "        *) shift ;;\n"
      "      esac\n"
      "    done\n"
      "    echo \"storage_admin_decode_character_snapshot actual_x=${expected_x} actual_y=${expected_y} position_matches=true\"\n"
      "    ;;\n"
      "  *)\n"
      "    echo \"storage_admin_${cmd}_summary status=ok\"\n"
      "    ;;\n"
      "esac\n");
  SetExecutable(path);
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

TEST(Phase6GatewayE2eGateScriptTest,
     PassesWhenL2VerifyShowsVersionAdvanceAndPositionMatch) {
  const auto temp_dir = MakeTempDir("gateway_e2e_gate");
  const auto client_report = temp_dir / "client.report.txt";
  const auto validate_file = temp_dir / "validate.txt";
  const auto verify_file = temp_dir / "verify.txt";
  const auto logic_log = temp_dir / "logic.log";
  const auto gateway_log = temp_dir / "gateway.log";
  const auto report_file = temp_dir / "gate.report.txt";

  WriteTextFile(
      client_report,
      "phase6_gateway_mock_client_result status=ok login_rsp_code=ERR_OK "
      "create_role_rsp_code=ERR_OK select_role_rsp_code=ERR_OK move_rsp_code=ERR_OK "
      "disconnect_sent=true player_id=1000 move_target_x=101 move_target_y=100\n");
  WriteTextFile(
      validate_file,
      "storage_admin_validate_summary total_corrupted=0\n");
  WriteTextFile(
      verify_file,
      "phase6_gateway_e2e_verify_result verify_result=PASS verify_stage=l2 "
      "snapshot_source=l2 l2_snapshot_present=true postgres_snapshot_present=false "
      "baseline_version=7 snapshot_version=8 version_delta=1 move_target_x=101 "
      "move_target_y=100 actual_x=101 actual_y=100\n");
  WriteTextFile(logic_log, "LogicServer initialized\n");
  WriteTextFile(gateway_log, "GatewayServer initialized\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase6_gateway_e2e_gate.sh",
      {"--scenario",
       "login_select_move_disconnect",
       "--client-report-file",
       client_report.string(),
       "--validate-file",
       validate_file.string(),
       "--verify-file",
       verify_file.string(),
       "--logic-log-file",
       logic_log.string(),
       "--gateway-log-file",
       gateway_log.string(),
       "--report-file",
       report_file.string()});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(ReadTextFile(report_file).find(
                "phase6_gateway_e2e_gate_result status=pass"),
            std::string::npos);
}

TEST(Phase6GatewayE2eGateScriptTest,
     PassesWhenPostgresFallbackAdvancesVersionAndMatchesPosition) {
  const auto temp_dir = MakeTempDir("gateway_e2e_gate_pg");
  const auto client_report = temp_dir / "client.report.txt";
  const auto validate_file = temp_dir / "validate.txt";
  const auto verify_file = temp_dir / "verify.txt";
  const auto logic_log = temp_dir / "logic.log";
  const auto gateway_log = temp_dir / "gateway.log";
  const auto report_file = temp_dir / "gate.report.txt";

  WriteTextFile(
      client_report,
      "phase6_gateway_mock_client_result status=ok login_rsp_code=ERR_OK "
      "create_role_rsp_code=ERR_OK select_role_rsp_code=ERR_OK move_rsp_code=ERR_OK "
      "disconnect_sent=true player_id=1000 move_target_x=101 move_target_y=100\n");
  WriteTextFile(validate_file, "storage_admin_validate_summary total_corrupted=0\n");
  WriteTextFile(
      verify_file,
      "phase6_gateway_e2e_verify_result verify_result=PASS "
      "verify_stage=postgres_fallback snapshot_source=postgres "
      "l2_snapshot_present=false postgres_snapshot_present=true baseline_version=11 "
      "snapshot_version=12 version_delta=1 move_target_x=101 move_target_y=100 "
      "actual_x=101 actual_y=100\n");
  WriteTextFile(logic_log, "LogicServer initialized\n");
  WriteTextFile(gateway_log, "GatewayServer initialized\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase6_gateway_e2e_gate.sh",
      {"--scenario",
       "login_select_move_disconnect",
       "--client-report-file",
       client_report.string(),
       "--validate-file",
       validate_file.string(),
       "--verify-file",
       verify_file.string(),
       "--logic-log-file",
       logic_log.string(),
       "--gateway-log-file",
       gateway_log.string(),
       "--report-file",
       report_file.string()});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_NE(ReadTextFile(report_file).find("verify_stage=postgres_fallback"),
            std::string::npos);
}

TEST(Phase6GatewayE2eGateScriptTest,
     FailsWhenVersionDeltaIsZeroEvenIfPositionMatches) {
  const auto temp_dir = MakeTempDir("gateway_e2e_gate_delta_zero");
  const auto client_report = temp_dir / "client.report.txt";
  const auto validate_file = temp_dir / "validate.txt";
  const auto verify_file = temp_dir / "verify.txt";
  const auto logic_log = temp_dir / "logic.log";
  const auto gateway_log = temp_dir / "gateway.log";
  const auto report_file = temp_dir / "gate.report.txt";

  WriteTextFile(
      client_report,
      "phase6_gateway_mock_client_result status=ok login_rsp_code=ERR_OK "
      "create_role_rsp_code=ERR_OK select_role_rsp_code=ERR_OK move_rsp_code=ERR_OK "
      "disconnect_sent=true player_id=1000 move_target_x=101 move_target_y=100\n");
  WriteTextFile(validate_file, "storage_admin_validate_summary total_corrupted=0\n");
  WriteTextFile(
      verify_file,
      "phase6_gateway_e2e_verify_result verify_result=FAIL verify_stage=l2 "
      "snapshot_source=l2 l2_snapshot_present=true postgres_snapshot_present=false "
      "baseline_version=8 snapshot_version=8 version_delta=0 move_target_x=101 "
      "move_target_y=100 actual_x=101 actual_y=100\n");
  WriteTextFile(logic_log, "LogicServer initialized\n");
  WriteTextFile(gateway_log, "GatewayServer initialized\n");

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase6_gateway_e2e_gate.sh",
      {"--scenario",
       "login_select_move_disconnect",
       "--client-report-file",
       client_report.string(),
       "--validate-file",
       validate_file.string(),
       "--verify-file",
       verify_file.string(),
       "--logic-log-file",
       logic_log.string(),
       "--gateway-log-file",
       gateway_log.string(),
       "--report-file",
       report_file.string()});

  EXPECT_NE(result.exit_code, 0);
  EXPECT_NE(ReadTextFile(report_file).find("version_delta"),
            std::string::npos);
}

TEST(Phase6GatewayE2eDrillScriptTest, RecordsAcceptanceForHealthyRoundTrip) {
  const auto temp_dir = MakeTempDir("gateway_e2e_drill");
  const auto acceptance_csv = temp_dir / "acceptance.csv";
  const auto report_file = temp_dir / "drill.report.txt";
  const auto gateway_bin = temp_dir / "mock_gateway";
  const auto logic_bin = temp_dir / "mock_logic";
  const auto mock_client_bin = temp_dir / "mock_client";
  const auto fault_driver_bin = temp_dir / "mock_fault_driver";
  const auto admin_bin = temp_dir / "mock_admin";

  WriteMockGatewayBinary(gateway_bin);
  WriteMockLogicBinary(logic_bin);
  WriteMockClientBinary(mock_client_bin);
  WriteMockFaultDriverBinary(fault_driver_bin);
  WriteMockAdminBinary(admin_bin);

  const auto result = RunBashScript(
      RepoRoot() / "scripts/run_storage_engine_phase6_gateway_e2e_drill.sh",
      {"--scenario",
       "login_select_move_disconnect",
       "--gateway-bin",
       gateway_bin.string(),
       "--logic-bin",
       logic_bin.string(),
       "--mock-client-bin",
       mock_client_bin.string(),
       "--fault-driver-bin",
       fault_driver_bin.string(),
       "--admin-bin",
       admin_bin.string(),
       "--gateway-e2e-gate-script",
       (RepoRoot() / "scripts/run_storage_engine_phase6_gateway_e2e_gate.sh").string(),
       "--config-template-gateway",
       (RepoRoot() / "config/gateway.yaml").string(),
       "--config-template-logic",
       (RepoRoot() / "config/logic.yaml").string(),
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
       (temp_dir / "db").string(),
       "--backend-state-path",
       (temp_dir / "backend_state.txt").string(),
       "--acceptance-csv",
       acceptance_csv.string(),
       "--report-file",
       report_file.string()});

  EXPECT_EQ(result.exit_code, 0);
  const auto csv = ReadTextFile(acceptance_csv);
  EXPECT_NE(csv.find("\"login_select_move_disconnect\""), std::string::npos);
  EXPECT_NE(csv.find("\"pass\""), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(gateway_bin.string() + ".invoked"));
  EXPECT_TRUE(std::filesystem::exists(logic_bin.string() + ".invoked"));
  EXPECT_TRUE(std::filesystem::exists(mock_client_bin.string() + ".invoked"));
  EXPECT_TRUE(std::filesystem::exists(fault_driver_bin.string() + ".invoked"));
  EXPECT_TRUE(std::filesystem::exists(admin_bin.string() + ".validate.invoked"));
}

}  // namespace
}  // namespace mir2::storage_engine::phase6_gateway_e2e_test
