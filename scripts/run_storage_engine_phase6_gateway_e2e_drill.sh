#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SCENARIO=""
GATEWAY_BIN=""
LOGIC_BIN=""
MOCK_CLIENT_BIN=""
FAULT_DRIVER_BIN=""
ADMIN_BIN=""
GATEWAY_E2E_GATE_SCRIPT=""
CONFIG_TEMPLATE_GATEWAY=""
CONFIG_TEMPLATE_LOGIC=""
DB_HOST=""
DB_PORT=""
DB_USER=""
DB_PASSWORD=""
DB_NAME=""
DB_PATH=""
BACKEND_STATE_PATH=""
REPORT_FILE=""
ACCEPTANCE_CSV=""
CLIENT_REPORT_FILE=""
VALIDATE_FILE=""
LOGIC_LOG_FILE=""
GATEWAY_LOG_FILE=""
GATE_REPORT_FILE=""
VERIFY_REPORT_FILE=""
PRE_MOVE_READY_FILE=""
PRE_MOVE_CONTINUE_FILE=""
TEMP_GATEWAY_CONFIG_PATH=""
TEMP_LOGIC_CONFIG_PATH=""
GATEWAY_HOST="127.0.0.1"
GATEWAY_PORT="17000"
LOGIC_PORT="18002"
CLIENT_USERNAME="phase6_user"
CLIENT_PASSWORD="phase6_pw"
CLIENT_ROLE_NAME="phase6role"
MOVE_X="101"
MOVE_Y="100"
PG_DOCKER_CONTAINER="legend2-postgres"
CLIENT_PASSWORD_HASH='$2b$12$Q9AaHeMvGvbPfx.f22E9TuIOYYzHsPyup0l71aS3oRwFMp2F2znCS'

ACCEPTANCE_CSV_HEADER="timestamp_utc,scenario,status,gate_exit_code,db_path,backend_state_path,client_report_file,validate_file,logic_log_file,gateway_log_file,gate_report_file,report_file"

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase6_gateway_e2e_drill.sh [options]

Options:
  --scenario <login_select_move_disconnect>     Gateway E2E scenario
  --gateway-bin <path>                          mir2_gateway binary
  --logic-bin <path>                            mir2_logic binary
  --mock-client-bin <path>                      Phase6 mock client binary
  --fault-driver-bin <path>                     Phase6 fault driver binary
  --admin-bin <path>                            mir2_storage_admin binary
  --gateway-e2e-gate-script <path>              Gateway E2E gate script
  --config-template-gateway <path>              Gateway config template
  --config-template-logic <path>                Logic config template
  --db-host <host>                              PostgreSQL host
  --db-port <port>                              PostgreSQL port
  --db-user <user>                              PostgreSQL user
  --db-password <password>                      PostgreSQL password
  --db-name <name>                              PostgreSQL database name
  --db-path <path>                              L2 RocksDB path
  --backend-state-path <path>                   Backend state path
  --report-file <path>                          Drill report output
  --acceptance-csv <path>                       Acceptance CSV path
  --gateway-port <port>                         Gateway port (default: 17000)
  --logic-port <port>                           Logic port (default: 18002)
  -h, --help                                    Show help
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

record_acceptance() {
  local timestamp_utc="$1"
  local status="$2"
  local gate_exit_code="$3"

  if [[ -z "${ACCEPTANCE_CSV}" ]]; then
    return 0
  fi

  {
    csv_escape "${timestamp_utc}"; printf ','
    csv_escape "${SCENARIO}"; printf ','
    csv_escape "${status}"; printf ','
    csv_escape "${gate_exit_code}"; printf ','
    csv_escape "${DB_PATH}"; printf ','
    csv_escape "${BACKEND_STATE_PATH}"; printf ','
    csv_escape "${CLIENT_REPORT_FILE}"; printf ','
    csv_escape "${VALIDATE_FILE}"; printf ','
    csv_escape "${LOGIC_LOG_FILE}"; printf ','
    csv_escape "${GATEWAY_LOG_FILE}"; printf ','
    csv_escape "${GATE_REPORT_FILE}"; printf ','
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

write_temp_gateway_config() {
  local output_path="$1"
  local log_dir
  log_dir="$(dirname "${GATEWAY_LOG_FILE}")"
  mkdir -p "${log_dir}"
  cat >"${output_path}" <<EOF
server:
  id: 1
  name: "Mir2-Gateway-Phase6"
  bind_ip: "${GATEWAY_HOST}"
  port: ${GATEWAY_PORT}
  udp_port: 0
  metrics_port: 0
  io_threads: 1
  max_connections: 128

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
    port: ${LOGIC_PORT}
    transport: "tcp"
    uds_path: ""
EOF
}

write_temp_logic_config() {
  local output_path="$1"
  local log_dir
  log_dir="$(dirname "${LOGIC_LOG_FILE}")"
  mkdir -p "${log_dir}"
  cat >"${output_path}" <<EOF
server:
  id: 5
  name: "Mir2-Logic-Phase6-Gateway"
  bind_ip: "127.0.0.1"
  port: ${LOGIC_PORT}
  udp_port: 0
  metrics_port: 0
  enable_network_listener: true

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
    port: ${LOGIC_PORT}
    transport: "tcp"
    uds_path: ""

storage_engine:
  l2_path: "${DB_PATH}"
  enable_metrics: false
  startup_fail_on_validation_error: true
EOF
}

extract_kv_from_line() {
  local line="$1"
  local key="$2"
  local token
  for token in ${line}; do
    if [[ "${token}" == "${key}="* ]]; then
      printf '%s\n' "${token#*=}"
      return 0
    fi
  done
  return 1
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
  [[ -f "${path}" ]]
}

seed_account_in_postgres() {
  if [[ "$(basename "${GATEWAY_BIN}")" == mock_* ]]; then
    return 0
  fi
  docker exec \
    -e PGPASSWORD="${DB_PASSWORD}" \
    "${PG_DOCKER_CONTAINER}" \
    psql -U "${DB_USER}" -d "${DB_NAME}" -v ON_ERROR_STOP=1 \
    -c "INSERT INTO accounts (username, password_hash, email, banned)
        VALUES ('${CLIENT_USERNAME}', '${CLIENT_PASSWORD_HASH}', '${CLIENT_USERNAME}@example.com', FALSE)
        ON CONFLICT (username) DO UPDATE
        SET password_hash = EXCLUDED.password_hash,
            email = EXCLUDED.email,
            banned = FALSE;"
}

ensure_kv_store_soft_delete_schema() {
  if [[ "$(basename "${GATEWAY_BIN}")" == mock_* ]]; then
    return 0
  fi

  local has_is_deleted
  has_is_deleted="$(
    docker exec \
      -e PGPASSWORD="${DB_PASSWORD}" \
      "${PG_DOCKER_CONTAINER}" \
      psql -U "${DB_USER}" -d "${DB_NAME}" -t -A \
      -c "SELECT COUNT(*) FROM information_schema.columns
          WHERE table_name = 'kv_store' AND column_name = 'is_deleted';" \
      | tr -d '[:space:]'
  )"

  if [[ "${has_is_deleted}" == "1" ]]; then
    return 0
  fi

  docker exec \
    -e PGPASSWORD="${DB_PASSWORD}" \
    "${PG_DOCKER_CONTAINER}" \
    psql -U "${DB_USER}" -d "${DB_NAME}" -v ON_ERROR_STOP=1 \
    -c "ALTER TABLE kv_store
          ADD COLUMN IF NOT EXISTS is_deleted BOOLEAN NOT NULL DEFAULT FALSE;" \
    -c "CREATE INDEX IF NOT EXISTS idx_kv_store_is_deleted
          ON kv_store (is_deleted);"
}

query_l2_snapshot() {
  local player_id="$1"
  local line=""
  local output=""
  set +e
  output="$("${FAULT_DRIVER_BIN}" \
    --scenario "login_select_move_disconnect_verify" \
    --db-path "${DB_PATH}" \
    --backend-state-path "${BACKEND_STATE_PATH}" \
    --key "char:${player_id}" 2>&1)"
  local rc=$?
  set -e
  line="$(printf '%s\n' "${output}" | grep -m1 '^phase6_fault_driver_verify_result' || true)"
  L2_SNAPSHOT_PRESENT="false"
  L2_SNAPSHOT_VERSION="0"
  L2_SNAPSHOT_HEX=""
  if [[ -n "${line}" ]]; then
    L2_SNAPSHOT_PRESENT="$(extract_kv_from_line "${line}" "snapshot_present" || echo "false")"
    L2_SNAPSHOT_VERSION="$(extract_kv_from_line "${line}" "snapshot_version" || echo "0")"
    L2_SNAPSHOT_HEX="$(extract_kv_from_line "${line}" "snapshot_hex" || echo "")"
  fi
  return "${rc}"
}

query_postgres_snapshot() {
  local player_id="$1"
  POSTGRES_SNAPSHOT_PRESENT="false"
  POSTGRES_SNAPSHOT_VERSION="0"
  POSTGRES_SNAPSHOT_HEX=""
  if [[ "$(basename "${GATEWAY_BIN}")" == mock_* ]]; then
    return 1
  fi

  local row
  row="$(
    docker exec \
      -e PGPASSWORD="${DB_PASSWORD}" \
      "${PG_DOCKER_CONTAINER}" \
      psql -U "${DB_USER}" -d "${DB_NAME}" -t -A -F '|' \
      -c "SELECT version, encode(data, 'hex')
          FROM kv_store
          WHERE key = 'char:${player_id}' AND is_deleted = FALSE;" \
      | tr -d '\r'
  )"
  row="$(printf '%s' "${row}" | head -n 1)"
  if [[ -z "${row}" ]]; then
    return 1
  fi

  IFS='|' read -r POSTGRES_SNAPSHOT_VERSION POSTGRES_SNAPSHOT_HEX <<<"${row}"
  POSTGRES_SNAPSHOT_PRESENT="true"
  return 0
}

decode_snapshot_position() {
  local snapshot_hex="$1"
  local line=""
  line="$("${ADMIN_BIN}" decode-character-snapshot \
    --hex "${snapshot_hex}" \
    --expected-x "${MOVE_X}" \
    --expected-y "${MOVE_Y}" | tr -d '\r' | head -n 1)"
  DECODE_ACTUAL_X="$(extract_kv_from_line "${line}" "actual_x" || echo "0")"
  DECODE_ACTUAL_Y="$(extract_kv_from_line "${line}" "actual_y" || echo "0")"
  DECODE_POSITION_MATCHES="$(extract_kv_from_line "${line}" "position_matches" || echo "false")"
}

capture_baseline_version() {
  local player_id="$1"
  BASELINE_VERSION="0"
  if query_l2_snapshot "${player_id}" && [[ "${L2_SNAPSHOT_PRESENT}" == "true" ]]; then
    BASELINE_VERSION="${L2_SNAPSHOT_VERSION}"
    return 0
  fi
  if query_postgres_snapshot "${player_id}" && [[ "${POSTGRES_SNAPSHOT_PRESENT}" == "true" ]]; then
    BASELINE_VERSION="${POSTGRES_SNAPSHOT_VERSION}"
    return 0
  fi
  return 0
}

run_verify_loop() {
  local player_id="$1"
  local baseline_version="$2"
  local verify_result="FAIL"
  local verify_stage="l2"
  local snapshot_source="l2"
  local l2_snapshot_present="false"
  local postgres_snapshot_present="false"
  local snapshot_version="0"
  local version_delta="0"
  local actual_x="0"
  local actual_y="0"

  : >"${VERIFY_REPORT_FILE}"
  printf 'phase6_gateway_e2e_verify_start player_id=%s baseline_version=%s move_target_x=%s move_target_y=%s\n' \
    "${player_id}" "${baseline_version}" "${MOVE_X}" "${MOVE_Y}" >>"${VERIFY_REPORT_FILE}"

  local attempt
  for attempt in $(seq 1 25); do
    if query_l2_snapshot "${player_id}"; then
      l2_snapshot_present="${L2_SNAPSHOT_PRESENT}"
      if [[ "${L2_SNAPSHOT_PRESENT}" == "true" && -n "${L2_SNAPSHOT_HEX}" ]]; then
        decode_snapshot_position "${L2_SNAPSHOT_HEX}"
        snapshot_version="${L2_SNAPSHOT_VERSION}"
        actual_x="${DECODE_ACTUAL_X}"
        actual_y="${DECODE_ACTUAL_Y}"
        if (( snapshot_version >= baseline_version )); then
          version_delta=$(( snapshot_version - baseline_version ))
        else
          version_delta=0
        fi
        if (( snapshot_version >= baseline_version + 1 )) &&
           (( version_delta >= 1 )) &&
           [[ "${actual_x}" == "${MOVE_X}" ]] &&
           [[ "${actual_y}" == "${MOVE_Y}" ]]; then
          verify_result="PASS"
          verify_stage="l2"
          snapshot_source="l2"
          postgres_snapshot_present="false"
          break
        fi
      fi
    fi

    if query_postgres_snapshot "${player_id}"; then
      postgres_snapshot_present="${POSTGRES_SNAPSHOT_PRESENT}"
      if [[ "${POSTGRES_SNAPSHOT_PRESENT}" == "true" && -n "${POSTGRES_SNAPSHOT_HEX}" ]]; then
        decode_snapshot_position "${POSTGRES_SNAPSHOT_HEX}"
        snapshot_version="${POSTGRES_SNAPSHOT_VERSION}"
        actual_x="${DECODE_ACTUAL_X}"
        actual_y="${DECODE_ACTUAL_Y}"
        if (( snapshot_version >= baseline_version )); then
          version_delta=$(( snapshot_version - baseline_version ))
        else
          version_delta=0
        fi
        if (( snapshot_version >= baseline_version + 1 )) &&
           (( version_delta >= 1 )) &&
           [[ "${actual_x}" == "${MOVE_X}" ]] &&
           [[ "${actual_y}" == "${MOVE_Y}" ]]; then
          verify_result="PASS"
          verify_stage="postgres_fallback"
          snapshot_source="postgres"
          break
        fi
      fi
    fi

    sleep 0.2
  done

  printf 'phase6_gateway_e2e_verify_result verify_result=%s verify_stage=%s snapshot_source=%s l2_snapshot_present=%s postgres_snapshot_present=%s baseline_version=%s snapshot_version=%s version_delta=%s move_target_x=%s move_target_y=%s actual_x=%s actual_y=%s\n' \
    "${verify_result}" "${verify_stage}" "${snapshot_source}" \
    "${l2_snapshot_present}" "${postgres_snapshot_present}" \
    "${baseline_version}" "${snapshot_version}" "${version_delta}" \
    "${MOVE_X}" "${MOVE_Y}" "${actual_x}" "${actual_y}" >>"${VERIFY_REPORT_FILE}"

  [[ "${verify_result}" == "PASS" ]]
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scenario)
      SCENARIO="$2"
      shift 2
      ;;
    --gateway-bin)
      GATEWAY_BIN="$2"
      shift 2
      ;;
    --logic-bin)
      LOGIC_BIN="$2"
      shift 2
      ;;
    --mock-client-bin)
      MOCK_CLIENT_BIN="$2"
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
    --gateway-e2e-gate-script)
      GATEWAY_E2E_GATE_SCRIPT="$2"
      shift 2
      ;;
    --config-template-gateway)
      CONFIG_TEMPLATE_GATEWAY="$2"
      shift 2
      ;;
    --config-template-logic)
      CONFIG_TEMPLATE_LOGIC="$2"
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
    --gateway-port)
      GATEWAY_PORT="$2"
      shift 2
      ;;
    --logic-port)
      LOGIC_PORT="$2"
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
    --report-file)
      REPORT_FILE="$2"
      shift 2
      ;;
    --acceptance-csv)
      ACCEPTANCE_CSV="$2"
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
if [[ -z "${GATEWAY_E2E_GATE_SCRIPT}" || -z "${CONFIG_TEMPLATE_GATEWAY}" ||
      -z "${CONFIG_TEMPLATE_LOGIC}" || -z "${DB_PATH}" ||
      -z "${BACKEND_STATE_PATH}" ]]; then
  echo "--gateway-e2e-gate-script, --config-template-gateway, --config-template-logic, --db-path and --backend-state-path are required" >&2
  exit 1
fi
if [[ ! -f "${CONFIG_TEMPLATE_GATEWAY}" || ! -f "${CONFIG_TEMPLATE_LOGIC}" ]]; then
  echo "config template not found" >&2
  exit 1
fi
if [[ ! -x "${GATEWAY_BIN}" || ! -x "${LOGIC_BIN}" || ! -x "${MOCK_CLIENT_BIN}" ||
      ! -x "${FAULT_DRIVER_BIN}" || ! -x "${ADMIN_BIN}" ]]; then
  echo "gateway/logic/mock-client/fault-driver/admin binary missing or not executable" >&2
  exit 1
fi

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase6_gateway_e2e_${SCENARIO}.report.txt"
fi
ARTIFACT_DIR="$(dirname "${REPORT_FILE}")"
if [[ -z "${CLIENT_REPORT_FILE:-}" ]]; then
  CLIENT_REPORT_FILE="${ARTIFACT_DIR}/phase6_gateway_e2e_${SCENARIO}.client.report.txt"
fi
if [[ -z "${VALIDATE_FILE:-}" ]]; then
  VALIDATE_FILE="${ARTIFACT_DIR}/phase6_gateway_e2e_${SCENARIO}.validate.txt"
fi
if [[ -z "${LOGIC_LOG_FILE:-}" ]]; then
  LOGIC_LOG_FILE="${ARTIFACT_DIR}/phase6_gateway_e2e_${SCENARIO}.logic.log"
fi
if [[ -z "${GATEWAY_LOG_FILE:-}" ]]; then
  GATEWAY_LOG_FILE="${ARTIFACT_DIR}/phase6_gateway_e2e_${SCENARIO}.gateway.log"
fi
if [[ -z "${GATE_REPORT_FILE:-}" ]]; then
  GATE_REPORT_FILE="${ARTIFACT_DIR}/phase6_gateway_e2e_${SCENARIO}.gate.report.txt"
fi
if [[ -z "${VERIFY_REPORT_FILE:-}" ]]; then
  VERIFY_REPORT_FILE="${ARTIFACT_DIR}/phase6_gateway_e2e_${SCENARIO}.verify.report.txt"
fi
if [[ -z "${PRE_MOVE_READY_FILE:-}" ]]; then
  PRE_MOVE_READY_FILE="${ARTIFACT_DIR}/phase6_gateway_e2e_${SCENARIO}.pre_move.ready"
fi
if [[ -z "${PRE_MOVE_CONTINUE_FILE:-}" ]]; then
  PRE_MOVE_CONTINUE_FILE="${ARTIFACT_DIR}/phase6_gateway_e2e_${SCENARIO}.pre_move.continue"
fi
if [[ -z "${TEMP_GATEWAY_CONFIG_PATH:-}" ]]; then
  TEMP_GATEWAY_CONFIG_PATH="${ARTIFACT_DIR}/phase6_gateway_e2e_${SCENARIO}.gateway.yaml"
fi
if [[ -z "${TEMP_LOGIC_CONFIG_PATH:-}" ]]; then
  TEMP_LOGIC_CONFIG_PATH="${ARTIFACT_DIR}/phase6_gateway_e2e_${SCENARIO}.logic.yaml"
fi

mkdir -p "$(dirname "${REPORT_FILE}")" "$(dirname "${CLIENT_REPORT_FILE}")" \
  "$(dirname "${VALIDATE_FILE}")" "$(dirname "${LOGIC_LOG_FILE}")" \
  "$(dirname "${GATEWAY_LOG_FILE}")" "$(dirname "${GATE_REPORT_FILE}")" \
  "$(dirname "${VERIFY_REPORT_FILE}")" \
  "$(dirname "${PRE_MOVE_READY_FILE}")" "$(dirname "${PRE_MOVE_CONTINUE_FILE}")" \
  "$(dirname "${TEMP_GATEWAY_CONFIG_PATH}")" "$(dirname "${TEMP_LOGIC_CONFIG_PATH}")" \
  "$(dirname "${DB_PATH}")" "$(dirname "${BACKEND_STATE_PATH}")"
touch "${REPORT_FILE}"

if ! ensure_acceptance_csv_schema; then
  exit 1
fi

timestamp_utc="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
status="fail"
gate_exit_code=1
gateway_pid=""
logic_pid=""

log "INFO" "phase6_gateway_e2e_drill_start scenario=${SCENARIO} db_path=${DB_PATH}"

cleanup() {
  if [[ -n "${gateway_pid}" ]]; then
    kill "${gateway_pid}" >/dev/null 2>&1 || true
    wait "${gateway_pid}" 2>/dev/null || true
  fi
  if [[ -n "${logic_pid}" ]]; then
    kill "${logic_pid}" >/dev/null 2>&1 || true
    wait "${logic_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

write_temp_gateway_config "${TEMP_GATEWAY_CONFIG_PATH}"
write_temp_logic_config "${TEMP_LOGIC_CONFIG_PATH}"

ensure_kv_store_soft_delete_schema >/dev/null
seed_account_in_postgres >/dev/null

"${FAULT_DRIVER_BIN}" \
  --scenario "login_select_move_disconnect_prepare" \
  --db-path "${DB_PATH}" \
  --backend-state-path "${BACKEND_STATE_PATH}" >/dev/null 2>&1

"${LOGIC_BIN}" --config "${TEMP_LOGIC_CONFIG_PATH}" >"${LOGIC_LOG_FILE}" 2>&1 &
logic_pid=$!
if ! wait_for_log_signal "${LOGIC_LOG_FILE}" "LogicServer initialized" 3000; then
  log "ERROR" "logic did not report initialized"
  record_acceptance "${timestamp_utc}" "${status}" "${gate_exit_code}"
  exit 1
fi

"${GATEWAY_BIN}" --config "${TEMP_GATEWAY_CONFIG_PATH}" >"${GATEWAY_LOG_FILE}" 2>&1 &
gateway_pid=$!
if ! wait_for_log_signal "${GATEWAY_LOG_FILE}" "Gateway lifecycle transition flushing -> serving" 5000; then
  log "ERROR" "gateway did not reach serving state"
  record_acceptance "${timestamp_utc}" "${status}" "${gate_exit_code}"
  exit 1
fi

"${MOCK_CLIENT_BIN}" \
  --gateway-host "${GATEWAY_HOST}" \
  --gateway-port "${GATEWAY_PORT}" \
  --username "${CLIENT_USERNAME}" \
  --password "${CLIENT_PASSWORD}" \
  --create-role-name "${CLIENT_ROLE_NAME}" \
  --move-x "${MOVE_X}" \
  --move-y "${MOVE_Y}" \
  --pre-move-ready-file "${PRE_MOVE_READY_FILE}" \
  --pre-move-continue-file "${PRE_MOVE_CONTINUE_FILE}" \
  --report-file "${CLIENT_REPORT_FILE}" >/dev/null 2>&1 &
mock_client_pid=$!

if ! wait_for_file "${PRE_MOVE_READY_FILE}" 5000; then
  log "ERROR" "mock client did not reach pre-move barrier"
  record_acceptance "${timestamp_utc}" "${status}" "${gate_exit_code}"
  exit 1
fi

pre_move_line="$(head -n 1 "${PRE_MOVE_READY_FILE}" | tr -d '\r')"
player_id="$(extract_kv_from_line "${pre_move_line}" "player_id" || true)"
if [[ -z "${player_id}" ]]; then
  log "ERROR" "pre-move barrier did not report player_id"
  record_acceptance "${timestamp_utc}" "${status}" "${gate_exit_code}"
  exit 1
fi

capture_baseline_version "${player_id}"
printf 'continue\n' >"${PRE_MOVE_CONTINUE_FILE}"

if ! wait "${mock_client_pid}"; then
  log "ERROR" "mock client exited with failure"
  record_acceptance "${timestamp_utc}" "${status}" "${gate_exit_code}"
  exit 1
fi

sleep 1
cleanup
gateway_pid=""
logic_pid=""

"${ADMIN_BIN}" validate --db-path "${DB_PATH}" >"${VALIDATE_FILE}"

if ! run_verify_loop "${player_id}" "${BASELINE_VERSION}"; then
  log "ERROR" "verify loop did not observe a passing snapshot"
fi

set +e
bash "${GATEWAY_E2E_GATE_SCRIPT}" \
  --scenario "${SCENARIO}" \
  --client-report-file "${CLIENT_REPORT_FILE}" \
  --validate-file "${VALIDATE_FILE}" \
  --verify-file "${VERIFY_REPORT_FILE}" \
  --logic-log-file "${LOGIC_LOG_FILE}" \
  --gateway-log-file "${GATEWAY_LOG_FILE}" \
  --report-file "${GATE_REPORT_FILE}"
gate_exit_code=$?
set -e

if [[ "${gate_exit_code}" == "0" ]]; then
  status="pass"
  log "INFO" "phase6_gateway_e2e_drill_result status=pass scenario=${SCENARIO}"
else
  log "ERROR" "phase6_gateway_e2e_drill_result status=fail scenario=${SCENARIO} gate_exit_code=${gate_exit_code}"
fi

record_acceptance "${timestamp_utc}" "${status}" "${gate_exit_code}"

if [[ "${status}" == "pass" ]]; then
  exit 0
fi
exit 1
