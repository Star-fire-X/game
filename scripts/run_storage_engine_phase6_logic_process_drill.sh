#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

LOGIC_BIN="${PROJECT_ROOT}/build-wsl/bin/mir2_logic"
FAULT_DRIVER_BIN="${PROJECT_ROOT}/build-wsl/bin/mir2_storage_engine_phase6_fault_driver"
ADMIN_BIN="${PROJECT_ROOT}/build-wsl/bin/mir2_storage_admin"
LOGIC_PROCESS_GATE_SCRIPT="${PROJECT_ROOT}/scripts/run_storage_engine_phase6_logic_process_gate.sh"
SCENARIO=""
CONFIG_TEMPLATE=""
DB_HOST=""
DB_PORT=""
DB_USER=""
DB_PASSWORD=""
DB_NAME=""
DB_PATH=""
BACKEND_STATE_PATH=""
KEY=""
VALUE_HEX="01020304"
REPORT_FILE=""
ACCEPTANCE_CSV=""
LOGIC_LOG_FILE=""
HEALTH_FILE=""
VALIDATE_FILE=""
GATE_REPORT_FILE=""
TEMP_CONFIG_PATH=""
READY_FILE=""
PREPARE_LOG=""
CHECKPOINT_PATH=""
RESTORE_DB_PATH=""
CHECKPOINT_CREATE_FILE=""
CHECKPOINT_RESTORE_FILE=""

ACCEPTANCE_CSV_HEADER="timestamp_utc,scenario,status,logic_exit_code,gate_exit_code,db_path,backend_state_path,checkpoint_path,restore_db_path,logic_log_file,prepare_log,health_file,validate_file,gate_report_file,checkpoint_create_file,checkpoint_restore_file,temp_config_path,report_file"

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase6_logic_process_drill.sh [options]

Options:
  --scenario <startup_validation|checkpoint_restore>  Logic-process drill scenario
  --logic-bin <path>                                   mir2_logic binary
  --fault-driver-bin <path>                            Phase6 fault driver binary
  --admin-bin <path>                                   mir2_storage_admin binary
  --logic-process-gate-script <path>                   Logic-process gate script
  --config-template <path>                             Base logic.yaml template
  --db-host <host>                                     PostgreSQL host
  --db-port <port>                                     PostgreSQL port
  --db-user <user>                                     PostgreSQL user
  --db-password <password>                             PostgreSQL password
  --db-name <name>                                     PostgreSQL database name
  --db-path <path>                                     L2 RocksDB path
  --backend-state-path <path>                          Fault driver backend state path
  --checkpoint-path <path>                             Checkpoint output path
  --restore-db-path <path>                             Restored DB path
  --key <key>                                          Drill key override
  --value-hex <hex>                                    Drill value override
  --report-file <path>                                 Report output path
  --acceptance-csv <path>                              Acceptance CSV path
  --logic-log-file <path>                              Logic stdout/stderr log
  --prepare-log <path>                                 Fault driver prepare log
  --ready-file <path>                                  Fault driver ready file
  --health-file <path>                                 storage_admin health output
  --validate-file <path>                               storage_admin validate output
  --gate-report-file <path>                            Gate report output
  --checkpoint-create-file <path>                      checkpoint-create summary output
  --checkpoint-restore-file <path>                     checkpoint-restore summary output
  --temp-config-path <path>                            Generated temp logic config
  -h, --help                                           Show help
EOF
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

write_temp_logic_config() {
  local output_path="$1"
  local active_db_path="$2"
  local log_dir
  log_dir="$(dirname "${LOGIC_LOG_FILE}")"
  mkdir -p "${log_dir}"
  cat >"${output_path}" <<EOF
server:
  id: 5
  name: "Mir2-Logic-Phase6"
  bind_ip: "127.0.0.1"
  port: 8002
  udp_port: 0
  metrics_port: 0
  enable_network_listener: false

database:
  host: "${DB_HOST}"
  port: ${DB_PORT}
  user: "${DB_USER}"
  password: "${DB_PASSWORD}"
  database: "${DB_NAME}"
  pool_size: 2

log:
  level: "info"
  path: "${log_dir}"
  max_size_mb: 20
  max_files: 3

services:
  logic:
    host: "127.0.0.1"
    port: 8002
    transport: "tcp"
    uds_path: ""

storage_engine:
  l2_path: "${active_db_path}"
  enable_metrics: false
  enable_v2_encode: true
  enable_v2_read_fallback: true
  startup_fail_on_validation_error: true
EOF
}

record_acceptance() {
  local timestamp_utc="$1"
  local status="$2"
  local logic_exit_code="$3"
  local gate_exit_code="$4"

  if [[ -z "${ACCEPTANCE_CSV}" ]]; then
    return 0
  fi

  {
    csv_escape "${timestamp_utc}"; printf ','
    csv_escape "${SCENARIO}"; printf ','
    csv_escape "${status}"; printf ','
    csv_escape "${logic_exit_code}"; printf ','
    csv_escape "${gate_exit_code}"; printf ','
    csv_escape "${DB_PATH}"; printf ','
    csv_escape "${BACKEND_STATE_PATH}"; printf ','
    csv_escape "${CHECKPOINT_PATH}"; printf ','
    csv_escape "${RESTORE_DB_PATH}"; printf ','
    csv_escape "${LOGIC_LOG_FILE}"; printf ','
    csv_escape "${PREPARE_LOG}"; printf ','
    csv_escape "${HEALTH_FILE}"; printf ','
    csv_escape "${VALIDATE_FILE}"; printf ','
    csv_escape "${GATE_REPORT_FILE}"; printf ','
    csv_escape "${CHECKPOINT_CREATE_FILE}"; printf ','
    csv_escape "${CHECKPOINT_RESTORE_FILE}"; printf ','
    csv_escape "${TEMP_CONFIG_PATH}"; printf ','
    csv_escape "${REPORT_FILE}"; printf '\n'
  } >>"${ACCEPTANCE_CSV}"
}

log() {
  local level="$1"
  shift
  local msg="$*"
  local ts
  ts="$(date '+%Y-%m-%d %H:%M:%S')"
  echo "${ts} [${level}] ${msg}" | tee -a "${REPORT_FILE}"
}

wait_for_log_signal() {
  local file="$1"
  local pattern="$2"
  local timeout_ms="$3"
  local deadline=$(( $(date +%s%3N) + timeout_ms ))
  while [[ $(date +%s%3N) -lt ${deadline} ]]; do
    if [[ -f "${file}" ]] && grep -q "${pattern}" "${file}"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

wait_for_file() {
  local file="$1"
  local timeout_ms="$2"
  local deadline=$(( $(date +%s%3N) + timeout_ms ))
  while [[ $(date +%s%3N) -lt ${deadline} ]]; do
    if [[ -f "${file}" ]]; then
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
    --logic-bin)
      LOGIC_BIN="$2"
      shift 2
      ;;
    --fault-driver-bin)
      FAULT_DRIVER_BIN="$2"
      shift 2
      ;;
    --admin-bin)
      ADMIN_BIN="$2"
      shift 2
      ;;
    --logic-process-gate-script)
      LOGIC_PROCESS_GATE_SCRIPT="$2"
      shift 2
      ;;
    --config-template)
      CONFIG_TEMPLATE="$2"
      shift 2
      ;;
    --db-host)
      DB_HOST="$2"
      shift 2
      ;;
    --db-port)
      DB_PORT="$2"
      shift 2
      ;;
    --db-user)
      DB_USER="$2"
      shift 2
      ;;
    --db-password)
      DB_PASSWORD="$2"
      shift 2
      ;;
    --db-name)
      DB_NAME="$2"
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
    --report-file)
      REPORT_FILE="$2"
      shift 2
      ;;
    --acceptance-csv)
      ACCEPTANCE_CSV="$2"
      shift 2
      ;;
    --logic-log-file)
      LOGIC_LOG_FILE="$2"
      shift 2
      ;;
    --prepare-log)
      PREPARE_LOG="$2"
      shift 2
      ;;
    --ready-file)
      READY_FILE="$2"
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
    --temp-config-path)
      TEMP_CONFIG_PATH="$2"
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
if [[ -z "${CONFIG_TEMPLATE}" || -z "${DB_PATH}" || -z "${BACKEND_STATE_PATH}" ]]; then
  echo "--config-template, --db-path and --backend-state-path are required" >&2
  exit 1
fi
if [[ ! -f "${CONFIG_TEMPLATE}" ]]; then
  echo "config template not found: ${CONFIG_TEMPLATE}" >&2
  exit 1
fi
if [[ -z "${KEY}" ]]; then
  if [[ "${SCENARIO}" == "checkpoint_restore" ]]; then
    KEY="phase6:checkpoint_restore:key"
  else
    KEY="phase6:startup_validation:key"
  fi
fi
if [[ ! -x "${LOGIC_BIN}" || ! -x "${FAULT_DRIVER_BIN}" || ! -x "${ADMIN_BIN}" || ! -x "${LOGIC_PROCESS_GATE_SCRIPT}" ]]; then
  echo "logic/fault_driver/admin/gate binary missing or not executable" >&2
  exit 1
fi

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.report.txt"
fi
if [[ -z "${LOGIC_LOG_FILE}" ]]; then
  LOGIC_LOG_FILE="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.logic.log"
fi
if [[ -z "${PREPARE_LOG}" ]]; then
  PREPARE_LOG="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.prepare.log"
fi
if [[ -z "${READY_FILE}" ]]; then
  READY_FILE="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.ready"
fi
if [[ -z "${HEALTH_FILE}" ]]; then
  HEALTH_FILE="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.health.txt"
fi
if [[ -z "${VALIDATE_FILE}" ]]; then
  VALIDATE_FILE="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.validate.txt"
fi
if [[ -z "${GATE_REPORT_FILE}" ]]; then
  GATE_REPORT_FILE="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.gate.report.txt"
fi
if [[ -z "${TEMP_CONFIG_PATH}" ]]; then
  TEMP_CONFIG_PATH="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.logic.yaml"
fi
if [[ -z "${CHECKPOINT_PATH}" ]]; then
  CHECKPOINT_PATH="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.checkpoint"
fi
if [[ -z "${RESTORE_DB_PATH}" ]]; then
  RESTORE_DB_PATH="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.restore_db"
fi
if [[ -z "${CHECKPOINT_CREATE_FILE}" ]]; then
  CHECKPOINT_CREATE_FILE="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.checkpoint_create.txt"
fi
if [[ -z "${CHECKPOINT_RESTORE_FILE}" ]]; then
  CHECKPOINT_RESTORE_FILE="${PROJECT_ROOT}/logs/phase6_logic_process_${SCENARIO}.checkpoint_restore.txt"
fi

mkdir -p "$(dirname "${REPORT_FILE}")" "$(dirname "${LOGIC_LOG_FILE}")" \
  "$(dirname "${PREPARE_LOG}")" "$(dirname "${READY_FILE}")" \
  "$(dirname "${HEALTH_FILE}")" "$(dirname "${VALIDATE_FILE}")" \
  "$(dirname "${GATE_REPORT_FILE}")" "$(dirname "${TEMP_CONFIG_PATH}")" \
  "$(dirname "${DB_PATH}")" "$(dirname "${BACKEND_STATE_PATH}")" \
  "$(dirname "${CHECKPOINT_PATH}")" "$(dirname "${RESTORE_DB_PATH}")" \
  "$(dirname "${CHECKPOINT_CREATE_FILE}")" "$(dirname "${CHECKPOINT_RESTORE_FILE}")"
touch "${REPORT_FILE}"

if ! ensure_acceptance_csv_schema; then
  exit 1
fi

timestamp_utc="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
status="fail"
logic_exit_code=1
gate_exit_code=1

log "INFO" "phase6_logic_process_drill_start scenario=${SCENARIO} db_path=${DB_PATH}"

PREPARE_SCENARIO=""
ACTIVE_DB_PATH="${DB_PATH}"
if [[ "${SCENARIO}" == "startup_validation" ]]; then
  PREPARE_SCENARIO="startup_validation_prepare"
else
  PREPARE_SCENARIO="checkpoint_restore_prepare"
fi

"${FAULT_DRIVER_BIN}" \
  --scenario "${PREPARE_SCENARIO}" \
  --db-path "${DB_PATH}" \
  --backend-state-path "${BACKEND_STATE_PATH}" \
  --key "${KEY}" \
  --value-hex "${VALUE_HEX}" \
  --ready-file "${READY_FILE}" \
  --sleep-ms 30000 \
  >"${PREPARE_LOG}" 2>&1 &
prepare_pid=$!

if ! wait_for_file "${READY_FILE}" 5000; then
  kill -9 "${prepare_pid}" >/dev/null 2>&1 || true
  wait "${prepare_pid}" 2>/dev/null || true
  log "ERROR" "phase6 logic-process prepare phase did not become ready"
  record_acceptance "${timestamp_utc}" "${status}" "${logic_exit_code}" "${gate_exit_code}"
  exit 1
fi

kill -9 "${prepare_pid}" >/dev/null 2>&1 || true
wait "${prepare_pid}" 2>/dev/null || true

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
    log "ERROR" "phase6 logic-process checkpoint create/restore failed checkpoint_create_exit=${checkpoint_create_exit} checkpoint_restore_exit=${checkpoint_restore_exit}"
    record_acceptance "${timestamp_utc}" "${status}" "${logic_exit_code}" "${gate_exit_code}"
    exit 1
  fi
  ACTIVE_DB_PATH="${RESTORE_DB_PATH}"
fi

write_temp_logic_config "${TEMP_CONFIG_PATH}" "${ACTIVE_DB_PATH}"

set +e
"${LOGIC_BIN}" --config "${TEMP_CONFIG_PATH}" >"${LOGIC_LOG_FILE}" 2>&1 &
logic_pid=$!

if [[ "${SCENARIO}" == "checkpoint_restore" ]]; then
  wait_for_log_signal "${LOGIC_LOG_FILE}" "LogicServer initialized" 3000 || true
  kill "${logic_pid}" >/dev/null 2>&1 || true
fi
wait "${logic_pid}"
logic_exit_code=$?
set -e

set +e
"${ADMIN_BIN}" health --db-path "${ACTIVE_DB_PATH}" >"${HEALTH_FILE}"
"${ADMIN_BIN}" validate \
  --db-path "${ACTIVE_DB_PATH}" \
  --enable-v2-encode true \
  --enable-v2-read-fallback true >"${VALIDATE_FILE}"
bash "${LOGIC_PROCESS_GATE_SCRIPT}" \
  --scenario "${SCENARIO}" \
  --logic-log-file "${LOGIC_LOG_FILE}" \
  --health-file "${HEALTH_FILE}" \
  --validate-file "${VALIDATE_FILE}" \
  --report-file "${GATE_REPORT_FILE}"
gate_exit_code=$?
set -e

if [[ "${gate_exit_code}" == "0" ]]; then
  status="pass"
  log "INFO" "phase6_logic_process_drill_result status=pass scenario=${SCENARIO}"
else
  log "ERROR" "phase6_logic_process_drill_result status=fail scenario=${SCENARIO} logic_exit_code=${logic_exit_code} gate_exit_code=${gate_exit_code}"
fi

record_acceptance "${timestamp_utc}" "${status}" "${logic_exit_code}" "${gate_exit_code}"

if [[ "${status}" == "pass" ]]; then
  exit 0
fi
exit 1
