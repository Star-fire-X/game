#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SCENARIO=""
CLIENT_REPORT_FILE=""
VALIDATE_FILE=""
VERIFY_FILE=""
LOGIC_LOG_FILE=""
GATEWAY_LOG_FILE=""
REPORT_FILE=""

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase6_gateway_e2e_gate.sh [options]

Options:
  --scenario <login_select_move_disconnect>  Gateway E2E scenario
  --client-report-file <path>                Mock client report file
  --validate-file <path>                     storage_admin validate output
  --verify-file <path>                       Gateway E2E verify output
  --logic-log-file <path>                    Logic log file
  --gateway-log-file <path>                  Gateway log file
  --report-file <path>                       Gate report output
  -h, --help                                 Show help
EOF
}

log() {
  local level="$1"
  shift
  local msg="$*"
  local ts
  ts="$(date '+%Y-%m-%d %H:%M:%S')"
  echo "${ts} [${level}] ${msg}" | tee -a "${REPORT_FILE}"
}

load_line() {
  local path="$1"
  local pattern="$2"
  if [[ ! -f "${path}" ]]; then
    return 1
  fi
  grep -m1 "^${pattern}" "${path}" || true
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scenario)
      SCENARIO="$2"
      shift 2
      ;;
    --client-report-file)
      CLIENT_REPORT_FILE="$2"
      shift 2
      ;;
    --validate-file)
      VALIDATE_FILE="$2"
      shift 2
      ;;
    --verify-file)
      VERIFY_FILE="$2"
      shift 2
      ;;
    --logic-log-file)
      LOGIC_LOG_FILE="$2"
      shift 2
      ;;
    --gateway-log-file)
      GATEWAY_LOG_FILE="$2"
      shift 2
      ;;
    --report-file)
      REPORT_FILE="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ "${SCENARIO}" != "login_select_move_disconnect" ]]; then
  echo "invalid --scenario: ${SCENARIO}" >&2
  exit 1
fi
if [[ -z "${CLIENT_REPORT_FILE}" || -z "${VALIDATE_FILE}" || -z "${VERIFY_FILE}" ||
      -z "${LOGIC_LOG_FILE}" || -z "${GATEWAY_LOG_FILE}" ]]; then
  echo "--client-report-file, --validate-file, --verify-file, --logic-log-file and --gateway-log-file are required" >&2
  exit 1
fi

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase6_gateway_e2e_gate_$(date +%Y%m%d_%H%M%S).report.txt"
fi
mkdir -p "$(dirname "${REPORT_FILE}")"
touch "${REPORT_FILE}"

client_line="$(load_line "${CLIENT_REPORT_FILE}" "phase6_gateway_mock_client_result")"
if [[ -z "${client_line}" ]]; then
  log "ERROR" "missing phase6_gateway_mock_client_result line"
  exit 1
fi
validate_line="$(load_line "${VALIDATE_FILE}" "storage_admin_validate_summary")"
if [[ -z "${validate_line}" ]]; then
  log "ERROR" "missing storage_admin_validate_summary line"
  exit 1
fi
verify_line="$(load_line "${VERIFY_FILE}" "phase6_gateway_e2e_verify_result")"
if [[ -z "${verify_line}" ]]; then
  log "ERROR" "missing phase6_gateway_e2e_verify_result line"
  exit 1
fi

logic_log="$(cat "${LOGIC_LOG_FILE}")"
gateway_log="$(cat "${GATEWAY_LOG_FILE}")"

declare -A client_kv=()
for token in ${client_line}; do
  if [[ "${token}" == *=* ]]; then
    client_kv["${token%%=*}"]="${token#*=}"
  fi
done

declare -A validate_kv=()
for token in ${validate_line}; do
  if [[ "${token}" == *=* ]]; then
    validate_kv["${token%%=*}"]="${token#*=}"
  fi
done

declare -A verify_kv=()
for token in ${verify_line}; do
  if [[ "${token}" == *=* ]]; then
    verify_kv["${token%%=*}"]="${token#*=}"
  fi
done

fail_reasons=()

if [[ "${client_kv[status]:-}" != "ok" ]]; then
  fail_reasons+=("client_status_not_ok")
fi
if [[ "${client_kv[login_rsp_code]:-}" != "ERR_OK" ]]; then
  fail_reasons+=("login_rsp_not_ok")
fi
if [[ "${client_kv[create_role_rsp_code]:-}" != "ERR_OK" ]]; then
  fail_reasons+=("create_role_rsp_not_ok")
fi
if [[ "${client_kv[select_role_rsp_code]:-}" != "ERR_OK" ]]; then
  fail_reasons+=("select_role_rsp_not_ok")
fi
if [[ "${client_kv[move_rsp_code]:-}" != "ERR_OK" ]]; then
  fail_reasons+=("move_rsp_not_ok")
fi
if [[ "${client_kv[disconnect_sent]:-}" != "true" ]]; then
  fail_reasons+=("disconnect_not_sent")
fi
if [[ "${validate_kv[total_corrupted]:-}" != "0" ]]; then
  fail_reasons+=("validate_total_corrupted!=0")
fi
if [[ "${verify_kv[verify_result]:-}" != "PASS" ]]; then
  fail_reasons+=("verify_result_not_pass")
fi
if [[ "${verify_kv[verify_stage]:-}" != "l2" &&
      "${verify_kv[verify_stage]:-}" != "postgres_fallback" ]]; then
  fail_reasons+=("verify_stage_invalid")
fi
if [[ "${verify_kv[verify_stage]:-}" == "l2" &&
      "${verify_kv[snapshot_source]:-}" != "l2" ]]; then
  fail_reasons+=("snapshot_source_not_l2")
fi
if [[ "${verify_kv[verify_stage]:-}" == "postgres_fallback" &&
      "${verify_kv[snapshot_source]:-}" != "postgres" ]]; then
  fail_reasons+=("snapshot_source_not_postgres")
fi

baseline_version="${verify_kv[baseline_version]:-0}"
snapshot_version="${verify_kv[snapshot_version]:-0}"
version_delta="${verify_kv[version_delta]:-0}"
move_target_x="${verify_kv[move_target_x]:-${client_kv[move_target_x]:-0}}"
move_target_y="${verify_kv[move_target_y]:-${client_kv[move_target_y]:-0}}"
actual_x="${verify_kv[actual_x]:-0}"
actual_y="${verify_kv[actual_y]:-0}"

if (( snapshot_version < baseline_version + 1 )); then
  fail_reasons+=("snapshot_version_not_advanced")
fi
if (( version_delta < 1 )); then
  fail_reasons+=("version_delta_lt_1")
fi
if [[ "${actual_x}" != "${move_target_x}" ]]; then
  fail_reasons+=("actual_x_mismatch")
fi
if [[ "${actual_y}" != "${move_target_y}" ]]; then
  fail_reasons+=("actual_y_mismatch")
fi
if [[ -n "${client_kv[move_target_x]:-}" &&
      "${move_target_x}" != "${client_kv[move_target_x]}" ]]; then
  fail_reasons+=("verify_move_target_x_mismatch")
fi
if [[ -n "${client_kv[move_target_y]:-}" &&
      "${move_target_y}" != "${client_kv[move_target_y]}" ]]; then
  fail_reasons+=("verify_move_target_y_mismatch")
fi
if [[ "${logic_log}" != *"LogicServer initialized"* ]]; then
  fail_reasons+=("logic_initialized_log_missing")
fi
if [[ "${gateway_log}" != *"GatewayServer initialized"* ]]; then
  fail_reasons+=("gateway_initialized_log_missing")
fi

if [[ ${#fail_reasons[@]} -eq 0 ]]; then
  log "INFO" "phase6_gateway_e2e_gate_result status=pass scenario=${SCENARIO} verify_stage=${verify_kv[verify_stage]:-unknown} baseline_version=${baseline_version} snapshot_version=${snapshot_version} version_delta=${version_delta} move_target_x=${move_target_x} move_target_y=${move_target_y} actual_x=${actual_x} actual_y=${actual_y}"
  exit 0
fi

log "ERROR" "phase6_gateway_e2e_gate_result status=fail scenario=${SCENARIO} reasons=$(IFS=,; echo "${fail_reasons[*]}") verify_stage=${verify_kv[verify_stage]:-unknown} baseline_version=${baseline_version} snapshot_version=${snapshot_version} version_delta=${version_delta} move_target_x=${move_target_x} move_target_y=${move_target_y} actual_x=${actual_x} actual_y=${actual_y}"
exit 1
