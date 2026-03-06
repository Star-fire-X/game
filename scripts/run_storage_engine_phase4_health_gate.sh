#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ADMIN_BIN="${PROJECT_ROOT}/build-wsl/bin/mir2_storage_admin"
DB_PATH=""
INPUT_FILE=""
MAX_DECRYPT_FAILURES=0
MAX_DECODE_ERRORS=0
MIN_ENCRYPTED_DECODE_READS=0
MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS=0
MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS=0
REQUIRE_ENCRYPTION_ENABLED=true
REQUIRE_V2_ENCODE=true
REPORT_FILE=""

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase4_health_gate.sh [options]

Options:
  --db-path <path>                     RocksDB path (required unless --input-file used)
  --admin-bin <path>                   mir2_storage_admin binary path
  --input-file <path>                  Read health summary from file (for dry checks/testing)
  --max-decrypt-failures <n>           Gate threshold (default: 0)
  --max-decode-errors <n>              Gate threshold (default: 0)
  --min-encrypted-decode-reads <n>     Gate threshold (default: 0)
  --max-runtime-enable-data-encryption-audits <n>
                                       Gate threshold for runtime_config_audit_key_enable_data_encryption_total (default: 0)
  --max-runtime-encryption-key-env-audits <n>
                                       Gate threshold for runtime_config_audit_key_encryption_key_env_total (default: 0)
  --require-encryption-enabled <bool>  Require enable_data_encryption=true (default: true)
  --require-v2-encode <bool>           Require enable_v2_encode=true (default: true)
  --report-file <path>                 Report output path
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
    --db-path)
      DB_PATH="$2"
      shift 2
      ;;
    --admin-bin)
      ADMIN_BIN="$2"
      shift 2
      ;;
    --input-file)
      INPUT_FILE="$2"
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

if ! is_uint "${MAX_DECRYPT_FAILURES}"; then
  echo "invalid --max-decrypt-failures: ${MAX_DECRYPT_FAILURES}" >&2
  exit 1
fi
if ! is_uint "${MAX_DECODE_ERRORS}"; then
  echo "invalid --max-decode-errors: ${MAX_DECODE_ERRORS}" >&2
  exit 1
fi
if ! is_uint "${MIN_ENCRYPTED_DECODE_READS}"; then
  echo "invalid --min-encrypted-decode-reads: ${MIN_ENCRYPTED_DECODE_READS}" >&2
  exit 1
fi
if ! is_uint "${MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS}"; then
  echo "invalid --max-runtime-enable-data-encryption-audits: ${MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS}" >&2
  exit 1
fi
if ! is_uint "${MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS}"; then
  echo "invalid --max-runtime-encryption-key-env-audits: ${MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS}" >&2
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

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase4_health_gate_$(date +%Y%m%d_%H%M%S).report.txt"
fi
mkdir -p "$(dirname "${REPORT_FILE}")"
touch "${REPORT_FILE}"

log() {
  local level="$1"
  shift
  local msg="$*"
  local ts
  ts="$(date '+%Y-%m-%d %H:%M:%S')"
  echo "${ts} [${level}] ${msg}" | tee -a "${REPORT_FILE}"
}

health_output=""
if [[ -n "${INPUT_FILE}" ]]; then
  if [[ ! -f "${INPUT_FILE}" ]]; then
    log "ERROR" "input file not found: ${INPUT_FILE}"
    exit 1
  fi
  health_output="$(cat "${INPUT_FILE}")"
else
  if [[ -z "${DB_PATH}" ]]; then
    log "ERROR" "--db-path is required when --input-file is not set"
    exit 1
  fi
  if [[ ! -x "${ADMIN_BIN}" ]]; then
    log "ERROR" "admin bin not executable: ${ADMIN_BIN}"
    exit 1
  fi
  if ! health_output="$("${ADMIN_BIN}" health --db-path "${DB_PATH}")"; then
    log "ERROR" "failed to execute: ${ADMIN_BIN} health --db-path ${DB_PATH}"
    exit 1
  fi
fi

health_line="$(printf '%s\n' "${health_output}" | grep -m1 '^storage_admin_health_summary' || true)"
if [[ -z "${health_line}" ]]; then
  log "ERROR" "missing storage_admin_health_summary line"
  exit 1
fi

declare -A kv=()
for token in ${health_line}; do
  if [[ "${token}" == *=* ]]; then
    key="${token%%=*}"
    value="${token#*=}"
    kv["${key}"]="${value}"
  fi
done

required_fields=(
  "enable_data_encryption"
  "enable_v2_encode"
  "decode_errors"
  "encrypted_decode_reads"
  "decrypt_failures"
  "runtime_config_audit_key_enable_data_encryption_total"
  "runtime_config_audit_key_encryption_key_env_total"
)
for key in "${required_fields[@]}"; do
  if [[ -z "${kv[${key}]:-}" ]]; then
    log "ERROR" "missing field in health output: ${key}"
    exit 1
  fi
done

decode_errors="${kv[decode_errors]}"
encrypted_decode_reads="${kv[encrypted_decode_reads]}"
decrypt_failures="${kv[decrypt_failures]}"
runtime_enable_data_encryption_audits="${kv[runtime_config_audit_key_enable_data_encryption_total]}"
runtime_encryption_key_env_audits="${kv[runtime_config_audit_key_encryption_key_env_total]}"

if ! is_uint "${decode_errors}" ||
   ! is_uint "${encrypted_decode_reads}" ||
   ! is_uint "${decrypt_failures}" ||
   ! is_uint "${runtime_enable_data_encryption_audits}" ||
   ! is_uint "${runtime_encryption_key_env_audits}"; then
  log "ERROR" "non-numeric counters in health output"
  exit 1
fi

fail_reasons=()
if [[ "${REQUIRE_ENCRYPTION_ENABLED}" == "true" ]] &&
   [[ "${kv[enable_data_encryption]}" != "true" ]]; then
  fail_reasons+=("enable_data_encryption!=true")
fi
if [[ "${REQUIRE_V2_ENCODE}" == "true" ]] &&
   [[ "${kv[enable_v2_encode]}" != "true" ]]; then
  fail_reasons+=("enable_v2_encode!=true")
fi
if (( decrypt_failures > MAX_DECRYPT_FAILURES )); then
  fail_reasons+=("decrypt_failures>${MAX_DECRYPT_FAILURES}")
fi
if (( decode_errors > MAX_DECODE_ERRORS )); then
  fail_reasons+=("decode_errors>${MAX_DECODE_ERRORS}")
fi
if (( encrypted_decode_reads < MIN_ENCRYPTED_DECODE_READS )); then
  fail_reasons+=("encrypted_decode_reads<${MIN_ENCRYPTED_DECODE_READS}")
fi
if (( runtime_enable_data_encryption_audits > MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS )); then
  fail_reasons+=("runtime_config_audit_key_enable_data_encryption_total>${MAX_RUNTIME_ENABLE_DATA_ENCRYPTION_AUDITS}")
fi
if (( runtime_encryption_key_env_audits > MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS )); then
  fail_reasons+=("runtime_config_audit_key_encryption_key_env_total>${MAX_RUNTIME_ENCRYPTION_KEY_ENV_AUDITS}")
fi

if [[ ${#fail_reasons[@]} -eq 0 ]]; then
  log "INFO" "phase4_health_gate_result status=pass decrypt_failures=${decrypt_failures} decode_errors=${decode_errors} encrypted_decode_reads=${encrypted_decode_reads} runtime_config_audit_key_enable_data_encryption_total=${runtime_enable_data_encryption_audits} runtime_config_audit_key_encryption_key_env_total=${runtime_encryption_key_env_audits}"
  exit 0
fi

log "ERROR" "phase4_health_gate_result status=fail reasons=$(IFS=,; echo "${fail_reasons[*]}") decrypt_failures=${decrypt_failures} decode_errors=${decode_errors} encrypted_decode_reads=${encrypted_decode_reads} runtime_config_audit_key_enable_data_encryption_total=${runtime_enable_data_encryption_audits} runtime_config_audit_key_encryption_key_env_total=${runtime_encryption_key_env_audits}"
exit 1
