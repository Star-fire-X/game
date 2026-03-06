#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DRIVER_BIN="${PROJECT_ROOT}/build-wsl/bin/mir2_storage_engine_phase6_fault_driver"
ADMIN_BIN="${PROJECT_ROOT}/build-wsl/bin/mir2_storage_admin"
RELEASE_GATE_SCRIPT="${PROJECT_ROOT}/scripts/run_storage_engine_phase6_release_gate.sh"
SCENARIO=""
KILL_POINT="prepare_ready"
DB_PATH=""
BACKEND_STATE_PATH=""
CHECKPOINT_PATH=""
RESTORE_DB_PATH=""
KEY=""
VALUE_HEX="01020304"
PREPARE_SLEEP_MS=30000
RECOVER_TIMEOUT_MS=5000
REPORT_FILE=""
ACCEPTANCE_CSV=""
READY_FILE=""
RECOVER_READY_FILE=""
PREPARE_LOG=""
RECOVER_LOG=""
HEALTH_FILE=""
VALIDATE_FILE=""
GATE_REPORT_FILE=""
CHECKPOINT_CREATE_FILE=""
CHECKPOINT_RESTORE_FILE=""

ACCEPTANCE_CSV_HEADER="timestamp_utc,scenario,kill_point,status,prepare_killed,recover_exit_code,gate_exit_code,db_path,backend_state_path,checkpoint_path,restore_db_path,report_file,prepare_log,recover_log,health_file,validate_file,gate_report_file,checkpoint_create_file,checkpoint_restore_file"

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase6_single_node_drill.sh [options]

Options:
  --scenario <durable_async|tombstone_gc|startup_validation|checkpoint_restore>  Drill scenario (required)
  --kill-point <prepare_ready|recover_wait> Kill point (default: prepare_ready)
  --driver-bin <path>                      Fault driver binary path
  --admin-bin <path>                       mir2_storage_admin binary path
  --release-gate-script <path>             Phase6 release gate script path
  --db-path <path>                         RocksDB path (required)
  --backend-state-path <path>              Backend state file path (required)
  --checkpoint-path <path>                 Checkpoint output path override
  --restore-db-path <path>                 Restored DB path override
  --key <key>                              Drill key override
  --value-hex <hex>                        Drill value override
  --prepare-sleep-ms <n>                   Prepare-side hold time before kill (default: 30000)
  --recover-timeout-ms <n>                 Recover-side wait timeout (default: 5000)
  --report-file <path>                     Main report path
  --acceptance-csv <path>                  Acceptance CSV output
  --ready-file <path>                      Ready marker override
  --recover-ready-file <path>              Recover-phase ready marker override
  --prepare-log <path>                     Prepare log path
  --recover-log <path>                     Recover log path
  --health-file <path>                     Health output path
  --validate-file <path>                   Validate output path
  --gate-report-file <path>                Gate report path
  --checkpoint-create-file <path>          checkpoint-create summary path
  --checkpoint-restore-file <path>         checkpoint-restore summary path
  -h, --help                               Show help
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

is_uint() {
  [[ "${1}" =~ ^[0-9]+$ ]]
}

csv_escape() {
  local value="$1"
  value="${value//\"/\"\"}"
  printf '"%s"' "${value}"
}

ensure_acceptance_csv_schema() {
  if [[ -z "${ACCEPTANCE_CSV}" ]]; then
    return 0
  fi
  mkdir -p "$(dirname "${ACCEPTANCE_CSV}")"
  if [[ ! -f "${ACCEPTANCE_CSV}" || ! -s "${ACCEPTANCE_CSV}" ]]; then
    printf '%s\n' "${ACCEPTANCE_CSV_HEADER}" >"${ACCEPTANCE_CSV}"
    return 0
  fi
  local current_header
  current_header="$(head -n 1 "${ACCEPTANCE_CSV}" | tr -d '\r')"
  if [[ "${current_header}" != "${ACCEPTANCE_CSV_HEADER}" ]]; then
    echo "acceptance csv header mismatch: ${ACCEPTANCE_CSV}" >&2
    return 1
  fi
  return 0
}

record_acceptance() {
  local timestamp_utc="$1"
  local status="$2"
  local prepare_killed="$3"
  local recover_exit_code="$4"
  local gate_exit_code="$5"

  if [[ -z "${ACCEPTANCE_CSV}" ]]; then
    return 0
  fi

  {
    csv_escape "${timestamp_utc}"; printf ','
    csv_escape "${SCENARIO}"; printf ','
    csv_escape "${KILL_POINT}"; printf ','
    csv_escape "${status}"; printf ','
    csv_escape "${prepare_killed}"; printf ','
    csv_escape "${recover_exit_code}"; printf ','
    csv_escape "${gate_exit_code}"; printf ','
    csv_escape "${DB_PATH}"; printf ','
    csv_escape "${BACKEND_STATE_PATH}"; printf ','
    csv_escape "${CHECKPOINT_PATH}"; printf ','
    csv_escape "${RESTORE_DB_PATH}"; printf ','
    csv_escape "${REPORT_FILE}"; printf ','
    csv_escape "${PREPARE_LOG}"; printf ','
    csv_escape "${RECOVER_LOG}"; printf ','
    csv_escape "${HEALTH_FILE}"; printf ','
    csv_escape "${VALIDATE_FILE}"; printf ','
    csv_escape "${GATE_REPORT_FILE}"; printf ','
    csv_escape "${CHECKPOINT_CREATE_FILE}"; printf ','
    csv_escape "${CHECKPOINT_RESTORE_FILE}"; printf '\n'
  } >>"${ACCEPTANCE_CSV}"
}

wait_for_file() {
  local path="$1"
  local timeout_ms="$2"
  local deadline=$(( $(date +%s%3N) + timeout_ms ))
  while [[ $(date +%s%3N) -lt ${deadline} ]]; do
    if [[ -f "${path}" ]]; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scenario)
      SCENARIO="$2"
      shift 2
      ;;
    --kill-point)
      KILL_POINT="$2"
      shift 2
      ;;
    --driver-bin)
      DRIVER_BIN="$2"
      shift 2
      ;;
    --admin-bin)
      ADMIN_BIN="$2"
      shift 2
      ;;
    --release-gate-script)
      RELEASE_GATE_SCRIPT="$2"
      shift 2
      ;;
    --db-path)
      DB_PATH="$2"
      shift 2
      ;;
    --backend-state-path)
      BACKEND_STATE_PATH="$2"
      shift 2
      ;;
    --checkpoint-path)
      CHECKPOINT_PATH="$2"
      shift 2
      ;;
    --restore-db-path)
      RESTORE_DB_PATH="$2"
      shift 2
      ;;
    --key)
      KEY="$2"
      shift 2
      ;;
    --value-hex)
      VALUE_HEX="$2"
      shift 2
      ;;
    --prepare-sleep-ms)
      PREPARE_SLEEP_MS="$2"
      shift 2
      ;;
    --recover-timeout-ms)
      RECOVER_TIMEOUT_MS="$2"
      shift 2
      ;;
    --report-file)
      REPORT_FILE="$2"
      shift 2
      ;;
    --acceptance-csv)
      ACCEPTANCE_CSV="$2"
      shift 2
      ;;
    --ready-file)
      READY_FILE="$2"
      shift 2
      ;;
    --recover-ready-file)
      RECOVER_READY_FILE="$2"
      shift 2
      ;;
    --prepare-log)
      PREPARE_LOG="$2"
      shift 2
      ;;
    --recover-log)
      RECOVER_LOG="$2"
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
    --gate-report-file)
      GATE_REPORT_FILE="$2"
      shift 2
      ;;
    --checkpoint-create-file)
      CHECKPOINT_CREATE_FILE="$2"
      shift 2
      ;;
    --checkpoint-restore-file)
      CHECKPOINT_RESTORE_FILE="$2"
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

if [[ "${SCENARIO}" != "durable_async" &&
      "${SCENARIO}" != "tombstone_gc" &&
      "${SCENARIO}" != "startup_validation" &&
      "${SCENARIO}" != "checkpoint_restore" ]]; then
  echo "invalid --scenario: ${SCENARIO}" >&2
  exit 1
fi
if [[ "${KILL_POINT}" != "prepare_ready" && "${KILL_POINT}" != "recover_wait" ]]; then
  echo "invalid --kill-point: ${KILL_POINT}" >&2
  exit 1
fi
if [[ -z "${DB_PATH}" || -z "${BACKEND_STATE_PATH}" ]]; then
  echo "--db-path and --backend-state-path are required" >&2
  exit 1
fi
if ! is_uint "${PREPARE_SLEEP_MS}" || ! is_uint "${RECOVER_TIMEOUT_MS}"; then
  echo "prepare/recover timeouts must be unsigned integers" >&2
  exit 1
fi
if [[ ! -x "${DRIVER_BIN}" ]]; then
  echo "driver bin not executable: ${DRIVER_BIN}" >&2
  exit 1
fi
if [[ ! -x "${ADMIN_BIN}" ]]; then
  echo "admin bin not executable: ${ADMIN_BIN}" >&2
  exit 1
fi
if [[ ! -x "${RELEASE_GATE_SCRIPT}" ]]; then
  echo "release gate script not executable: ${RELEASE_GATE_SCRIPT}" >&2
  exit 1
fi

if [[ -z "${KEY}" ]]; then
  if [[ "${SCENARIO}" == "durable_async" ]]; then
    KEY="phase6:durable_async:key"
  elif [[ "${SCENARIO}" == "tombstone_gc" ]]; then
    KEY="phase6:tombstone_gc:key"
  elif [[ "${SCENARIO}" == "checkpoint_restore" ]]; then
    KEY="phase6:checkpoint_restore:key"
  else
    KEY="phase6:startup_validation:key"
  fi
fi

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase6_${SCENARIO}_drill_$(date +%Y%m%d_%H%M%S).report.txt"
fi
if [[ -z "${READY_FILE}" ]]; then
  READY_FILE="${PROJECT_ROOT}/logs/phase6_${SCENARIO}.ready"
fi
if [[ -z "${PREPARE_LOG}" ]]; then
  PREPARE_LOG="${PROJECT_ROOT}/logs/phase6_${SCENARIO}.prepare.log"
fi
if [[ -z "${RECOVER_READY_FILE}" ]]; then
  RECOVER_READY_FILE="${READY_FILE}.recover"
fi
if [[ -z "${RECOVER_LOG}" ]]; then
  RECOVER_LOG="${PROJECT_ROOT}/logs/phase6_${SCENARIO}.recover.log"
fi
if [[ -z "${HEALTH_FILE}" ]]; then
  HEALTH_FILE="${PROJECT_ROOT}/logs/phase6_${SCENARIO}.health.txt"
fi
if [[ -z "${VALIDATE_FILE}" ]]; then
  VALIDATE_FILE="${PROJECT_ROOT}/logs/phase6_${SCENARIO}.validate.txt"
fi
if [[ -z "${GATE_REPORT_FILE}" ]]; then
  GATE_REPORT_FILE="${PROJECT_ROOT}/logs/phase6_${SCENARIO}.gate.report.txt"
fi
if [[ -z "${CHECKPOINT_PATH}" ]]; then
  CHECKPOINT_PATH="${PROJECT_ROOT}/logs/phase6_${SCENARIO}.checkpoint"
fi
if [[ -z "${RESTORE_DB_PATH}" ]]; then
  RESTORE_DB_PATH="${PROJECT_ROOT}/logs/phase6_${SCENARIO}.restore_db"
fi
if [[ -z "${CHECKPOINT_CREATE_FILE}" ]]; then
  CHECKPOINT_CREATE_FILE="${PROJECT_ROOT}/logs/phase6_${SCENARIO}.checkpoint_create.txt"
fi
if [[ -z "${CHECKPOINT_RESTORE_FILE}" ]]; then
  CHECKPOINT_RESTORE_FILE="${PROJECT_ROOT}/logs/phase6_${SCENARIO}.checkpoint_restore.txt"
fi

mkdir -p "$(dirname "${REPORT_FILE}")" "$(dirname "${READY_FILE}")" \
  "$(dirname "${RECOVER_READY_FILE}")" \
  "$(dirname "${PREPARE_LOG}")" "$(dirname "${RECOVER_LOG}")" \
  "$(dirname "${HEALTH_FILE}")" "$(dirname "${VALIDATE_FILE}")" \
  "$(dirname "${GATE_REPORT_FILE}")" "$(dirname "${DB_PATH}")" \
  "$(dirname "${CHECKPOINT_PATH}")" "$(dirname "${RESTORE_DB_PATH}")" \
  "$(dirname "${CHECKPOINT_CREATE_FILE}")" "$(dirname "${CHECKPOINT_RESTORE_FILE}")" \
  "$(dirname "${BACKEND_STATE_PATH}")"
touch "${REPORT_FILE}"
rm -f "${READY_FILE}"
rm -f "${RECOVER_READY_FILE}" "${RECOVER_READY_FILE}.killed"

if ! ensure_acceptance_csv_schema; then
  exit 1
fi

timestamp_utc="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
status="fail"
prepare_killed="false"
recover_exit_code=1
gate_exit_code=1
health_exit_code=1
validate_exit_code=1

if [[ "${SCENARIO}" == "durable_async" ]]; then
  PREPARE_SCENARIO="durable_async_prepare"
  RECOVER_SCENARIO="durable_async_recover"
elif [[ "${SCENARIO}" == "tombstone_gc" ]]; then
  PREPARE_SCENARIO="tombstone_gc_prepare"
  RECOVER_SCENARIO="tombstone_gc_recover"
elif [[ "${SCENARIO}" == "checkpoint_restore" ]]; then
  PREPARE_SCENARIO="checkpoint_restore_prepare"
  RECOVER_SCENARIO="checkpoint_restore_recover"
else
  PREPARE_SCENARIO="startup_validation_prepare"
  RECOVER_SCENARIO="startup_validation_recover"
fi

log "INFO" "phase6_single_node_drill_start scenario=${SCENARIO} db_path=${DB_PATH}"

"${DRIVER_BIN}" \
  --scenario "${PREPARE_SCENARIO}" \
  --kill-point "${KILL_POINT}" \
  --db-path "${DB_PATH}" \
  --backend-state-path "${BACKEND_STATE_PATH}" \
  --key "${KEY}" \
  --value-hex "${VALUE_HEX}" \
  --ready-file "${READY_FILE}" \
  --sleep-ms "${PREPARE_SLEEP_MS}" \
  >"${PREPARE_LOG}" 2>&1 &
prepare_pid=$!

if ! wait_for_file "${READY_FILE}" 5000; then
  log "ERROR" "prepare phase did not become ready"
  kill -9 "${prepare_pid}" >/dev/null 2>&1 || true
  wait "${prepare_pid}" 2>/dev/null || true
  record_acceptance "${timestamp_utc}" "${status}" "${prepare_killed}" \
      "${recover_exit_code}" "${gate_exit_code}"
  exit 1
fi

kill -9 "${prepare_pid}" >/dev/null 2>&1 || true
wait "${prepare_pid}" 2>/dev/null || true
prepare_killed="true"

ACTIVE_DB_PATH="${DB_PATH}"
if [[ "${SCENARIO}" == "checkpoint_restore" ]]; then
  set +e
  "${ADMIN_BIN}" checkpoint-create \
    --db-path "${DB_PATH}" \
    --output-path "${CHECKPOINT_PATH}" \
    --overwrite >"${CHECKPOINT_CREATE_FILE}"
  checkpoint_create_exit=$?
  "${ADMIN_BIN}" checkpoint-restore \
    --checkpoint-path "${CHECKPOINT_PATH}" \
    --restore-db-path "${RESTORE_DB_PATH}" \
    --overwrite >"${CHECKPOINT_RESTORE_FILE}"
  checkpoint_restore_exit=$?
  set -e
  if [[ "${checkpoint_create_exit}" != "0" || "${checkpoint_restore_exit}" != "0" ]]; then
    log "ERROR" "checkpoint create/restore failed checkpoint_create_exit=${checkpoint_create_exit} checkpoint_restore_exit=${checkpoint_restore_exit}"
    record_acceptance "${timestamp_utc}" "${status}" "${prepare_killed}" \
        "${recover_exit_code}" "${gate_exit_code}"
    exit 1
  fi
  ACTIVE_DB_PATH="${RESTORE_DB_PATH}"
fi

if [[ "${KILL_POINT}" == "recover_wait" ]]; then
  "${DRIVER_BIN}" \
    --scenario "${RECOVER_SCENARIO}" \
    --kill-point "${KILL_POINT}" \
    --db-path "${ACTIVE_DB_PATH}" \
    --backend-state-path "${BACKEND_STATE_PATH}" \
    --key "${KEY}" \
    --value-hex "${VALUE_HEX}" \
    --ready-file "${RECOVER_READY_FILE}" \
    --timeout-ms "${RECOVER_TIMEOUT_MS}" \
    >"${RECOVER_LOG}" 2>&1 &
  recover_pid=$!
  if ! wait_for_file "${RECOVER_READY_FILE}" 5000; then
    log "ERROR" "recover phase did not become ready for kill-point=${KILL_POINT}"
    kill -9 "${recover_pid}" >/dev/null 2>&1 || true
    wait "${recover_pid}" 2>/dev/null || true
    record_acceptance "${timestamp_utc}" "${status}" "${prepare_killed}" \
        "${recover_exit_code}" "${gate_exit_code}"
    exit 1
  fi
  kill -9 "${recover_pid}" >/dev/null 2>&1 || true
  wait "${recover_pid}" 2>/dev/null || true
  touch "${RECOVER_READY_FILE}.killed"
fi

set +e
"${DRIVER_BIN}" \
  --scenario "${RECOVER_SCENARIO}" \
  --kill-point "${KILL_POINT}" \
  --db-path "${ACTIVE_DB_PATH}" \
  --backend-state-path "${BACKEND_STATE_PATH}" \
  --key "${KEY}" \
  --value-hex "${VALUE_HEX}" \
  --ready-file "${RECOVER_READY_FILE}" \
  --timeout-ms "${RECOVER_TIMEOUT_MS}" \
  >"${RECOVER_LOG}" 2>&1
recover_exit_code=$?
set -e

set +e
"${ADMIN_BIN}" health --db-path "${ACTIVE_DB_PATH}" >"${HEALTH_FILE}"
health_exit_code=$?
"${ADMIN_BIN}" validate --db-path "${ACTIVE_DB_PATH}" >"${VALIDATE_FILE}"
validate_exit_code=$?
set -e

set +e
bash "${RELEASE_GATE_SCRIPT}" \
  --scenario "${SCENARIO}" \
  --kill-point "${KILL_POINT}" \
  --summary-file "${RECOVER_LOG}" \
  --checkpoint-create-file "${CHECKPOINT_CREATE_FILE}" \
  --checkpoint-restore-file "${CHECKPOINT_RESTORE_FILE}" \
  --health-file "${HEALTH_FILE}" \
  --validate-file "${VALIDATE_FILE}" \
  --report-file "${GATE_REPORT_FILE}"
gate_exit_code=$?
set -e

if [[ "${recover_exit_code}" == "0" && "${gate_exit_code}" == "0" ]]; then
  status="pass"
  log "INFO" "phase6_single_node_drill_result status=pass scenario=${SCENARIO}"
else
  status="fail"
  log "ERROR" "phase6_single_node_drill_result status=fail scenario=${SCENARIO} recover_exit_code=${recover_exit_code} gate_exit_code=${gate_exit_code}"
fi

record_acceptance "${timestamp_utc}" "${status}" "${prepare_killed}" \
    "${recover_exit_code}" "${gate_exit_code}"

if [[ "${status}" == "pass" ]]; then
  exit 0
fi
exit 1
