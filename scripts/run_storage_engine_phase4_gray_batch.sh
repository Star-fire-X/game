#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROTATION_SCRIPT="${PROJECT_ROOT}/scripts/run_storage_engine_phase4_key_rotation.sh"
HEALTH_GATE_SCRIPT="${PROJECT_ROOT}/scripts/run_storage_engine_phase4_health_gate.sh"

BATCH=""
CONFIG_PATH="${PROJECT_ROOT}/config/logic.yaml"
TARGET_KEY_ID=""
ROLLBACK_KEY_ID=""
ENV_NAME=""
PID=""
DB_PATH=""
GATE_INPUT_FILE=""
MAX_DECRYPT_FAILURES=0
MAX_DECODE_ERRORS=0
MIN_ENCRYPTED_DECODE_READS=1
MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS=0
MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS=0
REQUIRE_ENCRYPTION_ENABLED=true
REQUIRE_V2_ENCODE=true
SKIP_RELOAD=false
SKIP_GATE=false
DRY_RUN=false
AUTO_ROLLBACK_ON_GATE_FAIL=false
REPORT_FILE=""
ROTATION_REPORT_FILE=""
GATE_REPORT_FILE=""
ROLLBACK_REPORT_FILE=""
ACCEPTANCE_CSV=""
GATE_OBS_STATUS="not_run"
GATE_OBS_REASONS=""
GATE_OBS_DECRYPT_FAILURES=""
GATE_OBS_DECODE_ERRORS=""
GATE_OBS_ENCRYPTED_DECODE_READS=""
GATE_OBS_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS=""
GATE_OBS_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS=""
ACCEPTANCE_CSV_HEADER="timestamp_utc,batch,target_key_id,rollback_key_id,status,stage,reason,auto_rollback_on_gate_fail,rollback_attempted,rollback_succeeded,active_key_before,active_key_after,exit_code,max_decrypt_failures,max_decode_errors,min_encrypted_decode_reads,max_runtime_enable_data_encryption_audits,max_runtime_encryption_key_env_audits,require_encryption_enabled,require_v2_encode,gate_status,gate_reasons,gate_decrypt_failures,gate_decode_errors,gate_encrypted_decode_reads,gate_runtime_enable_data_encryption_audits,gate_runtime_encryption_key_env_audits,config_path,report_file,rotation_report_file,gate_report_file,rollback_report_file"

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase4_gray_batch.sh [options]

Required:
  --batch <5|25|100>                   Gray batch label
  --target-key-id <id>                 Target active key id

Optional:
  --rollback-key-id <id>               Rollback key id for gate-fail rollback
  --config <path>                      logic.yaml path (default: config/logic.yaml)
  --env-name <name>                    Override encryption_key_env
  --pid <pid>                          Override mir2_logic pid
  --db-path <path>                     RocksDB path for health gate
  --gate-input-file <path>             Use saved health summary instead of live command
  --max-decrypt-failures <n>           Gate threshold (default: 0)
  --max-decode-errors <n>              Gate threshold (default: 0)
  --min-encrypted-decode-reads <n>     Gate threshold (default: 1)
  --max-runtime-enable-data-encryption-audits <n>
                                       Gate threshold for runtime_config_audit_key_enable_data_encryption_total (default: 0)
  --max-runtime-encryption-key-env-audits <n>
                                       Gate threshold for runtime_config_audit_key_encryption_key_env_total (default: 0)
  --require-encryption-enabled <bool>  Require enable_data_encryption=true in gate (default: true)
  --require-v2-encode <bool>           Require enable_v2_encode=true in gate (default: true)
  --skip-reload                        Do not send SIGUSR1
  --skip-gate                          Skip health gate
  --auto-rollback-on-gate-fail         Roll back to --rollback-key-id when gate fails
  --dry-run                            Do not modify config in rotation step
  --report-file <path>                 Main rollout report file
  --rotation-report-file <path>        Rotation sub-report path
  --gate-report-file <path>            Gate sub-report path
  --rollback-report-file <path>        Rollback sub-report path
  --acceptance-csv <path>              Append rollout acceptance row to CSV
  -h, --help                           Show help
EOF
}

is_uint() {
  [[ "${1}" =~ ^[0-9]+$ ]]
}

normalize_bool() {
  local raw="$1"
  case "${raw}" in
    1|true|TRUE|True|yes|YES|Yes) echo "true" ;;
    0|false|FALSE|False|no|NO|No) echo "false" ;;
    *) return 1 ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --batch)
      BATCH="$2"
      shift 2
      ;;
    --config)
      CONFIG_PATH="$2"
      shift 2
      ;;
    --target-key-id)
      TARGET_KEY_ID="$2"
      shift 2
      ;;
    --rollback-key-id)
      ROLLBACK_KEY_ID="$2"
      shift 2
      ;;
    --env-name)
      ENV_NAME="$2"
      shift 2
      ;;
    --pid)
      PID="$2"
      shift 2
      ;;
    --db-path)
      DB_PATH="$2"
      shift 2
      ;;
    --gate-input-file)
      GATE_INPUT_FILE="$2"
      shift 2
      ;;
    --max-decrypt-failures)
      MAX_DECRYPT_FAILURES="$2"
      shift 2
      ;;
    --max-decode-errors)
      MAX_DECODE_ERRORS="$2"
      shift 2
      ;;
    --min-encrypted-decode-reads)
      MIN_ENCRYPTED_DECODE_READS="$2"
      shift 2
      ;;
    --max-runtime-enable-data-encryption-audits)
      MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS="$2"
      shift 2
      ;;
    --max-runtime-encryption-key-env-audits)
      MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS="$2"
      shift 2
      ;;
    --require-encryption-enabled)
      REQUIRE_ENCRYPTION_ENABLED="$2"
      shift 2
      ;;
    --require-v2-encode)
      REQUIRE_V2_ENCODE="$2"
      shift 2
      ;;
    --skip-reload)
      SKIP_RELOAD=true
      shift
      ;;
    --skip-gate)
      SKIP_GATE=true
      shift
      ;;
    --auto-rollback-on-gate-fail)
      AUTO_ROLLBACK_ON_GATE_FAIL=true
      shift
      ;;
    --dry-run)
      DRY_RUN=true
      shift
      ;;
    --report-file)
      REPORT_FILE="$2"
      shift 2
      ;;
    --rotation-report-file)
      ROTATION_REPORT_FILE="$2"
      shift 2
      ;;
    --gate-report-file)
      GATE_REPORT_FILE="$2"
      shift 2
      ;;
    --rollback-report-file)
      ROLLBACK_REPORT_FILE="$2"
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

if [[ "${BATCH}" != "5" && "${BATCH}" != "25" && "${BATCH}" != "100" ]]; then
  echo "invalid --batch: ${BATCH} (expect 5/25/100)" >&2
  exit 1
fi
if [[ -z "${TARGET_KEY_ID}" ]]; then
  echo "--target-key-id is required" >&2
  exit 1
fi
if [[ "${SKIP_GATE}" != "true" &&
      "${AUTO_ROLLBACK_ON_GATE_FAIL}" == "true" &&
      -z "${ROLLBACK_KEY_ID}" ]]; then
  echo "--rollback-key-id is required when --auto-rollback-on-gate-fail is set" >&2
  exit 1
fi
if [[ ! -x "${ROTATION_SCRIPT}" ]]; then
  echo "rotation script not executable: ${ROTATION_SCRIPT}" >&2
  exit 1
fi
if [[ ! -x "${HEALTH_GATE_SCRIPT}" ]]; then
  echo "health gate script not executable: ${HEALTH_GATE_SCRIPT}" >&2
  exit 1
fi
if ! is_uint "${MAX_DECRYPT_FAILURES}" ||
   ! is_uint "${MAX_DECODE_ERRORS}" ||
   ! is_uint "${MIN_ENCRYPTED_DECODE_READS}" ||
   ! is_uint "${MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS}" ||
   ! is_uint "${MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS}"; then
  echo "gate thresholds must be unsigned integers" >&2
  exit 1
fi
raw_require_encryption_enabled="${REQUIRE_ENCRYPTION_ENABLED}"
REQUIRE_ENCRYPTION_ENABLED="$(normalize_bool "${raw_require_encryption_enabled}")" || {
  echo "invalid --require-encryption-enabled: ${raw_require_encryption_enabled}" >&2
  exit 1
}
raw_require_v2_encode="${REQUIRE_V2_ENCODE}"
REQUIRE_V2_ENCODE="$(normalize_bool "${raw_require_v2_encode}")" || {
  echo "invalid --require-v2-encode: ${raw_require_v2_encode}" >&2
  exit 1
}
if [[ "${SKIP_GATE}" != "true" && -z "${DB_PATH}" && -z "${GATE_INPUT_FILE}" ]]; then
  echo "--db-path is required unless --skip-gate or --gate-input-file is set" >&2
  exit 1
fi

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase4_gray_batch_${BATCH}_$(date +%Y%m%d_%H%M%S).report.txt"
fi
if [[ -z "${ROTATION_REPORT_FILE}" ]]; then
  ROTATION_REPORT_FILE="${PROJECT_ROOT}/logs/phase4_gray_batch_${BATCH}_rotation_$(date +%Y%m%d_%H%M%S).report.txt"
fi
if [[ -z "${GATE_REPORT_FILE}" ]]; then
  GATE_REPORT_FILE="${PROJECT_ROOT}/logs/phase4_gray_batch_${BATCH}_gate_$(date +%Y%m%d_%H%M%S).report.txt"
fi
if [[ -z "${ROLLBACK_REPORT_FILE}" ]]; then
  ROLLBACK_REPORT_FILE="${PROJECT_ROOT}/logs/phase4_gray_batch_${BATCH}_rollback_$(date +%Y%m%d_%H%M%S).report.txt"
fi
mkdir -p "$(dirname "${REPORT_FILE}")"
mkdir -p "$(dirname "${ROTATION_REPORT_FILE}")"
mkdir -p "$(dirname "${GATE_REPORT_FILE}")"
mkdir -p "$(dirname "${ROLLBACK_REPORT_FILE}")"
touch "${REPORT_FILE}" "${ROTATION_REPORT_FILE}" "${GATE_REPORT_FILE}" "${ROLLBACK_REPORT_FILE}"
if [[ -n "${ACCEPTANCE_CSV}" ]]; then
  mkdir -p "$(dirname "${ACCEPTANCE_CSV}")"
fi

log() {
  local level="$1"
  shift
  local msg="$*"
  local ts
  ts="$(date '+%Y-%m-%d %H:%M:%S')"
  echo "${ts} [${level}] ${msg}" | tee -a "${REPORT_FILE}"
}

extract_active_key_id() {
  awk '
    BEGIN { in_storage=0 }
    $0 ~ /^storage_engine:[[:space:]]*$/ { in_storage=1; next }
    in_storage && $0 ~ /^[^[:space:]]/ { in_storage=0 }
    in_storage && match($0, /^[[:space:]]*encryption_active_key_id:[[:space:]]*(.*)$/, m) {
      v = m[1]
      gsub(/[[:space:]]*#.*/, "", v)
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", v)
      gsub(/^"/, "", v)
      gsub(/"$/, "", v)
      print v
      exit 0
    }
  ' "${CONFIG_PATH}"
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
  if [[ ! -f "${ACCEPTANCE_CSV}" || ! -s "${ACCEPTANCE_CSV}" ]]; then
    printf '%s\n' "${ACCEPTANCE_CSV_HEADER}" >"${ACCEPTANCE_CSV}"
    return 0
  fi
  local current_header
  current_header="$(head -n 1 "${ACCEPTANCE_CSV}" | tr -d '\r')"
  if [[ "${current_header}" != "${ACCEPTANCE_CSV_HEADER}" ]]; then
    echo "acceptance csv header mismatch: ${ACCEPTANCE_CSV}" >&2
    echo "expected: ${ACCEPTANCE_CSV_HEADER}" >&2
    echo "actual:   ${current_header}" >&2
    return 1
  fi
  return 0
}

parse_gate_report_metrics() {
  local line
  line="$(grep -E 'phase4_health_gate_result status=' "${GATE_REPORT_FILE}" | tail -n 1 || true)"
  if [[ -z "${line}" ]]; then
    return 1
  fi

  declare -A kv=()
  local key
  local value
  local token
  for token in ${line}; do
    if [[ "${token}" == *=* ]]; then
      key="${token%%=*}"
      value="${token#*=}"
      kv["${key}"]="${value}"
    fi
  done

  GATE_OBS_STATUS="${kv[status]:-}"
  GATE_OBS_REASONS="${kv[reasons]:-}"
  GATE_OBS_DECRYPT_FAILURES="${kv[decrypt_failures]:-}"
  GATE_OBS_DECODE_ERRORS="${kv[decode_errors]:-}"
  GATE_OBS_ENCRYPTED_DECODE_READS="${kv[encrypted_decode_reads]:-}"
  GATE_OBS_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS="${kv[runtime_config_audit_key_enable_data_encryption_total]:-}"
  GATE_OBS_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS="${kv[runtime_config_audit_key_encryption_key_env_total]:-}"
  return 0
}

mark_gate_report_missing() {
  GATE_OBS_STATUS="error"
  GATE_OBS_REASONS="gate_report_missing"
  GATE_OBS_DECRYPT_FAILURES=""
  GATE_OBS_DECODE_ERRORS=""
  GATE_OBS_ENCRYPTED_DECODE_READS=""
  GATE_OBS_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS=""
  GATE_OBS_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS=""
}

record_acceptance() {
  local timestamp_utc="$1"
  local status="$2"
  local stage="$3"
  local reason="$4"
  local rollback_attempted="$5"
  local rollback_succeeded="$6"
  local active_key_before="$7"
  local active_key_after="$8"
  local exit_code="$9"

  if [[ -z "${ACCEPTANCE_CSV}" ]]; then
    return 0
  fi

  {
    csv_escape "${timestamp_utc}"; printf ','
    csv_escape "${BATCH}"; printf ','
    csv_escape "${TARGET_KEY_ID}"; printf ','
    csv_escape "${ROLLBACK_KEY_ID}"; printf ','
    csv_escape "${status}"; printf ','
    csv_escape "${stage}"; printf ','
    csv_escape "${reason}"; printf ','
    csv_escape "${AUTO_ROLLBACK_ON_GATE_FAIL}"; printf ','
    csv_escape "${rollback_attempted}"; printf ','
    csv_escape "${rollback_succeeded}"; printf ','
    csv_escape "${active_key_before}"; printf ','
    csv_escape "${active_key_after}"; printf ','
    csv_escape "${exit_code}"; printf ','
    csv_escape "${MAX_DECRYPT_FAILURES}"; printf ','
    csv_escape "${MAX_DECODE_ERRORS}"; printf ','
    csv_escape "${MIN_ENCRYPTED_DECODE_READS}"; printf ','
    csv_escape "${MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS}"; printf ','
    csv_escape "${MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS}"; printf ','
    csv_escape "${REQUIRE_ENCRYPTION_ENABLED}"; printf ','
    csv_escape "${REQUIRE_V2_ENCODE}"; printf ','
    csv_escape "${GATE_OBS_STATUS}"; printf ','
    csv_escape "${GATE_OBS_REASONS}"; printf ','
    csv_escape "${GATE_OBS_DECRYPT_FAILURES}"; printf ','
    csv_escape "${GATE_OBS_DECODE_ERRORS}"; printf ','
    csv_escape "${GATE_OBS_ENCRYPTED_DECODE_READS}"; printf ','
    csv_escape "${GATE_OBS_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS}"; printf ','
    csv_escape "${GATE_OBS_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS}"; printf ','
    csv_escape "${CONFIG_PATH}"; printf ','
    csv_escape "${REPORT_FILE}"; printf ','
    csv_escape "${ROTATION_REPORT_FILE}"; printf ','
    csv_escape "${GATE_REPORT_FILE}"; printf ','
    csv_escape "${ROLLBACK_REPORT_FILE}"; printf '\n'
  } >>"${ACCEPTANCE_CSV}"
}

run_rotation() {
  local -a cmd=(
    bash "${ROTATION_SCRIPT}"
    --apply
    --config "${CONFIG_PATH}"
    --target-key-id "${TARGET_KEY_ID}"
    --report-file "${ROTATION_REPORT_FILE}"
  )
  if [[ -n "${ENV_NAME}" ]]; then
    cmd+=(--env-name "${ENV_NAME}")
  fi
  if [[ -n "${PID}" ]]; then
    cmd+=(--pid "${PID}")
  fi
  if [[ "${SKIP_RELOAD}" == "true" ]]; then
    cmd+=(--skip-reload)
  fi
  if [[ "${DRY_RUN}" == "true" ]]; then
    cmd+=(--dry-run)
  fi
  "${cmd[@]}"
}

run_gate() {
  local -a cmd=(
    bash "${HEALTH_GATE_SCRIPT}"
    --max-decrypt-failures "${MAX_DECRYPT_FAILURES}"
    --max-decode-errors "${MAX_DECODE_ERRORS}"
    --min-encrypted-decode-reads "${MIN_ENCRYPTED_DECODE_READS}"
    --max-runtime-enable-data-encryption-audits "${MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS}"
    --max-runtime-encryption-key-env-audits "${MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS}"
    --require-encryption-enabled "${REQUIRE_ENCRYPTION_ENABLED}"
    --require-v2-encode "${REQUIRE_V2_ENCODE}"
    --report-file "${GATE_REPORT_FILE}"
  )
  if [[ -n "${GATE_INPUT_FILE}" ]]; then
    cmd+=(--input-file "${GATE_INPUT_FILE}")
  else
    cmd+=(--db-path "${DB_PATH}")
  fi
  "${cmd[@]}"
}

run_rollback() {
  local -a cmd=(
    bash "${ROTATION_SCRIPT}"
    --rollback
    --config "${CONFIG_PATH}"
    --target-key-id "${ROLLBACK_KEY_ID}"
    --report-file "${ROLLBACK_REPORT_FILE}"
  )
  if [[ -n "${ENV_NAME}" ]]; then
    cmd+=(--env-name "${ENV_NAME}")
  fi
  if [[ -n "${PID}" ]]; then
    cmd+=(--pid "${PID}")
  fi
  if [[ "${SKIP_RELOAD}" == "true" ]]; then
    cmd+=(--skip-reload)
  fi
  if [[ "${DRY_RUN}" == "true" ]]; then
    cmd+=(--dry-run)
  fi
  "${cmd[@]}"
}

log "INFO" "phase4_gray_batch_start batch=${BATCH} target_key_id=${TARGET_KEY_ID} dry_run=${DRY_RUN} skip_gate=${SKIP_GATE} auto_rollback_on_gate_fail=${AUTO_ROLLBACK_ON_GATE_FAIL} max_runtime_enable_data_encryption_audits=${MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS} max_runtime_encryption_key_env_audits=${MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS} require_encryption_enabled=${REQUIRE_ENCRYPTION_ENABLED} require_v2_encode=${REQUIRE_V2_ENCODE}"

if ! ensure_acceptance_csv_schema; then
  exit 1
fi

timestamp_utc="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
active_key_before="$(extract_active_key_id || true)"
active_key_after="${active_key_before}"
result_status="unknown"
result_stage="unknown"
result_reason=""
rollback_attempted="false"
rollback_succeeded="false"
exit_code=1
GATE_OBS_STATUS="not_run"
GATE_OBS_REASONS=""
GATE_OBS_DECRYPT_FAILURES=""
GATE_OBS_DECODE_ERRORS=""
GATE_OBS_ENCRYPTED_DECODE_READS=""
GATE_OBS_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS=""
GATE_OBS_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS=""

if ! run_rotation; then
  result_status="blocked"
  result_stage="rotation"
  result_reason="rotation_failed"
  exit_code=1
  log "ERROR" "phase4_gray_batch_result status=blocked stage=rotation batch=${BATCH}"
  active_key_after="$(extract_active_key_id || true)"
  record_acceptance "${timestamp_utc}" "${result_status}" "${result_stage}" \
      "${result_reason}" "${rollback_attempted}" "${rollback_succeeded}" \
      "${active_key_before}" "${active_key_after}" "${exit_code}"
  exit "${exit_code}"
fi

if [[ "${SKIP_GATE}" == "true" ]]; then
  GATE_OBS_STATUS="skipped"
  GATE_OBS_REASONS="skip_gate"
  result_status="pass"
  result_stage="rotation_only"
  result_reason="skip_gate"
  exit_code=0
  log "INFO" "phase4_gray_batch_result status=pass stage=rotation_only batch=${BATCH}"
  active_key_after="$(extract_active_key_id || true)"
  record_acceptance "${timestamp_utc}" "${result_status}" "${result_stage}" \
      "${result_reason}" "${rollback_attempted}" "${rollback_succeeded}" \
      "${active_key_before}" "${active_key_after}" "${exit_code}"
  exit "${exit_code}"
fi

if ! run_gate; then
  if ! parse_gate_report_metrics; then
    mark_gate_report_missing
  fi
  result_status="blocked"
  result_stage="health_gate"
  result_reason="health_gate_failed"
  exit_code=1
  if [[ "${AUTO_ROLLBACK_ON_GATE_FAIL}" == "true" ]]; then
    rollback_attempted="true"
    log "WARN" "phase4_gray_batch_gate_failed batch=${BATCH}, attempting rollback to key=${ROLLBACK_KEY_ID}"
    if run_rollback; then
      rollback_succeeded="true"
      result_status="blocked_rolled_back"
      result_reason="health_gate_failed_auto_rolled_back"
      exit_code=1
      log "ERROR" "phase4_gray_batch_result status=blocked_rolled_back stage=health_gate batch=${BATCH}"
      active_key_after="$(extract_active_key_id || true)"
      record_acceptance "${timestamp_utc}" "${result_status}" "${result_stage}" \
          "${result_reason}" "${rollback_attempted}" "${rollback_succeeded}" \
          "${active_key_before}" "${active_key_after}" "${exit_code}"
      exit "${exit_code}"
    fi
    rollback_succeeded="false"
    result_status="blocked_rollback_failed"
    result_reason="health_gate_failed_rollback_failed"
    exit_code=2
    log "ERROR" "phase4_gray_batch_result status=blocked_rollback_failed stage=health_gate batch=${BATCH}"
    active_key_after="$(extract_active_key_id || true)"
    record_acceptance "${timestamp_utc}" "${result_status}" "${result_stage}" \
        "${result_reason}" "${rollback_attempted}" "${rollback_succeeded}" \
        "${active_key_before}" "${active_key_after}" "${exit_code}"
    exit "${exit_code}"
  fi
  log "ERROR" "phase4_gray_batch_result status=blocked stage=health_gate batch=${BATCH}"
  active_key_after="$(extract_active_key_id || true)"
  record_acceptance "${timestamp_utc}" "${result_status}" "${result_stage}" \
      "${result_reason}" "${rollback_attempted}" "${rollback_succeeded}" \
      "${active_key_before}" "${active_key_after}" "${exit_code}"
  exit "${exit_code}"
fi

if ! parse_gate_report_metrics; then
  mark_gate_report_missing
  result_status="blocked"
  result_stage="health_gate"
  result_reason="health_gate_report_missing"
  exit_code=1
  log "ERROR" "phase4_gray_batch_result status=blocked stage=health_gate batch=${BATCH}"
  active_key_after="$(extract_active_key_id || true)"
  record_acceptance "${timestamp_utc}" "${result_status}" "${result_stage}" \
      "${result_reason}" "${rollback_attempted}" "${rollback_succeeded}" \
      "${active_key_before}" "${active_key_after}" "${exit_code}"
  exit "${exit_code}"
fi

result_status="pass"
result_stage="health_gate"
result_reason="health_gate_passed"
exit_code=0
log "INFO" "phase4_gray_batch_result status=pass stage=health_gate batch=${BATCH}"
active_key_after="$(extract_active_key_id || true)"
record_acceptance "${timestamp_utc}" "${result_status}" "${result_stage}" \
    "${result_reason}" "${rollback_attempted}" "${rollback_succeeded}" \
    "${active_key_before}" "${active_key_after}" "${exit_code}"
exit "${exit_code}"
