#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SCENARIO=""
LOGIC_LOG_FILE=""
HEALTH_FILE=""
VALIDATE_FILE=""
REPORT_FILE=""

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase6_logic_process_gate.sh [options]

Options:
  --scenario <startup_validation|checkpoint_restore>  Logic-process drill scenario
  --logic-log-file <path>                             Logic stdout/stderr log
  --health-file <path>                                storage_admin health output
  --validate-file <path>                              storage_admin validate output
  --report-file <path>                                Report output path
  -h, --help                                          Show help
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
    --logic-log-file)
      LOGIC_LOG_FILE="$2"
      shift 2
      ;;
    --health-file)
      HEALTH_FILE="$2"
      shift 2
      ;;
    --validate-file)
      VALIDATE_FILE="$2"
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

if [[ "${SCENARIO}" != "startup_validation" &&
      "${SCENARIO}" != "checkpoint_restore" ]]; then
  echo "invalid --scenario: ${SCENARIO}" >&2
  exit 1
fi
if [[ -z "${LOGIC_LOG_FILE}" || -z "${HEALTH_FILE}" || -z "${VALIDATE_FILE}" ]]; then
  echo "--logic-log-file, --health-file and --validate-file are required" >&2
  exit 1
fi

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase6_logic_process_gate_$(date +%Y%m%d_%H%M%S).report.txt"
fi
mkdir -p "$(dirname "${REPORT_FILE}")"
touch "${REPORT_FILE}"

health_line="$(load_line "${HEALTH_FILE}" "storage_admin_health_summary")"
if [[ -z "${health_line}" ]]; then
  log "ERROR" "missing storage_admin_health_summary line"
  exit 1
fi
validate_line="$(load_line "${VALIDATE_FILE}" "storage_admin_validate_summary")"
if [[ -z "${validate_line}" ]]; then
  log "ERROR" "missing storage_admin_validate_summary line"
  exit 1
fi

logic_log="$(cat "${LOGIC_LOG_FILE}")"

declare -A validate_kv=()
for token in ${validate_line}; do
  if [[ "${token}" == *=* ]]; then
    validate_kv["${token%%=*}"]="${token#*=}"
  fi
done

fail_reasons=()
if [[ "${SCENARIO}" == "startup_validation" ]]; then
  if [[ "${logic_log}" != *"LogicServer init failed"* ]]; then
    fail_reasons+=("logic_init_failed_log_missing")
  fi
  if [[ -z "${validate_kv[total_corrupted]:-}" ||
        "${validate_kv[total_corrupted]}" == "0" ]]; then
    fail_reasons+=("validate_total_corrupted<=0")
  fi
else
  if [[ "${logic_log}" != *"LogicServer initialized"* ]]; then
    fail_reasons+=("logic_initialized_log_missing")
  fi
  if [[ "${validate_kv[total_corrupted]:-}" != "0" ]]; then
    fail_reasons+=("validate_total_corrupted!=0")
  fi
fi

if [[ ${#fail_reasons[@]} -eq 0 ]]; then
  log "INFO" "phase6_logic_process_gate_result status=pass scenario=${SCENARIO}"
  exit 0
fi

log "ERROR" "phase6_logic_process_gate_result status=fail scenario=${SCENARIO} reasons=$(IFS=,; echo "${fail_reasons[*]}")"
exit 1
