#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ADMIN_BIN="${PROJECT_ROOT}/build-wsl/bin/mir2_storage_admin"
DB_PATH=""
INPUT_FILE=""
L2_MAX_SIZE_MB=""
SOFT_LIMIT_RATIO="0.85"
HARD_LIMIT_RATIO="0.95"
MAX_USAGE_RATIO=""
MAX_SOFT_LIMIT_WRITES=""
MAX_HARD_LIMIT_REJECTS=""
MAX_HARD_LIMIT_BYPASSES=""
REQUIRE_SOFT_LIMIT_INACTIVE=false
REQUIRE_HARD_LIMIT_INACTIVE=true
REPORT_FILE=""

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase5_capacity_gate.sh [options]

Options:
  --db-path <path>                     RocksDB path (required unless --input-file used)
  --admin-bin <path>                   mir2_storage_admin binary path
  --input-file <path>                  Read health summary from file
  --l2-max-size-mb <n>                 Configured L2 max size in MB (required)
  --soft-limit-ratio <ratio>           Soft usage threshold (default: 0.85)
  --hard-limit-ratio <ratio>           Hard usage threshold (default: 0.95)
  --max-usage-ratio <ratio>            Hard gate threshold (default: hard-limit-ratio)
  --max-soft-limit-writes <n>          Optional cumulative soft-limit write threshold
  --max-hard-limit-rejects <n>         Optional cumulative hard-limit reject threshold
  --max-hard-limit-bypasses <n>        Optional cumulative hard-limit bypass threshold
  --require-soft-limit-inactive <bool> Require computed soft-limit inactive (default: false)
  --require-hard-limit-inactive <bool> Require computed hard-limit inactive (default: true)
  --report-file <path>                 Report output path
  -h, --help                           Show help
EOF
}

is_uint() {
  [[ "${1}" =~ ^[0-9]+$ ]]
}

is_number() {
  [[ "${1}" =~ ^[0-9]+([.][0-9]+)?$ ]]
}

normalize_bool() {
  local raw="$1"
  case "${raw}" in
    1|true|TRUE|True|yes|YES|Yes) echo "true" ;;
    0|false|FALSE|False|no|NO|No) echo "false" ;;
    *) return 1 ;;
  esac
}

float_gt() {
  awk -v lhs="$1" -v rhs="$2" 'BEGIN { exit !(lhs > rhs) }'
}

float_ge() {
  awk -v lhs="$1" -v rhs="$2" 'BEGIN { exit !(lhs >= rhs) }'
}

float_div() {
  awk -v lhs="$1" -v rhs="$2" 'BEGIN {
    if (rhs <= 0) {
      printf "0.000000";
      exit 0;
    }
    printf "%.6f", lhs / rhs;
  }'
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
    --l2-max-size-mb)
      L2_MAX_SIZE_MB="$2"
      shift 2
      ;;
    --soft-limit-ratio)
      SOFT_LIMIT_RATIO="$2"
      shift 2
      ;;
    --hard-limit-ratio)
      HARD_LIMIT_RATIO="$2"
      shift 2
      ;;
    --max-usage-ratio)
      MAX_USAGE_RATIO="$2"
      shift 2
      ;;
    --max-soft-limit-writes)
      MAX_SOFT_LIMIT_WRITES="$2"
      shift 2
      ;;
    --max-hard-limit-rejects)
      MAX_HARD_LIMIT_REJECTS="$2"
      shift 2
      ;;
    --max-hard-limit-bypasses)
      MAX_HARD_LIMIT_BYPASSES="$2"
      shift 2
      ;;
    --require-soft-limit-inactive)
      REQUIRE_SOFT_LIMIT_INACTIVE="$2"
      shift 2
      ;;
    --require-hard-limit-inactive)
      REQUIRE_HARD_LIMIT_INACTIVE="$2"
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

if ! is_uint "${L2_MAX_SIZE_MB}" || [[ "${L2_MAX_SIZE_MB}" == "0" ]]; then
  echo "invalid --l2-max-size-mb: ${L2_MAX_SIZE_MB}" >&2
  exit 1
fi
if ! is_number "${SOFT_LIMIT_RATIO}"; then
  echo "invalid --soft-limit-ratio: ${SOFT_LIMIT_RATIO}" >&2
  exit 1
fi
if ! is_number "${HARD_LIMIT_RATIO}"; then
  echo "invalid --hard-limit-ratio: ${HARD_LIMIT_RATIO}" >&2
  exit 1
fi
if [[ -z "${MAX_USAGE_RATIO}" ]]; then
  MAX_USAGE_RATIO="${HARD_LIMIT_RATIO}"
fi
if ! is_number "${MAX_USAGE_RATIO}"; then
  echo "invalid --max-usage-ratio: ${MAX_USAGE_RATIO}" >&2
  exit 1
fi
if [[ -n "${MAX_SOFT_LIMIT_WRITES}" ]] &&
   ! is_uint "${MAX_SOFT_LIMIT_WRITES}"; then
  echo "invalid --max-soft-limit-writes: ${MAX_SOFT_LIMIT_WRITES}" >&2
  exit 1
fi
if [[ -n "${MAX_HARD_LIMIT_REJECTS}" ]] &&
   ! is_uint "${MAX_HARD_LIMIT_REJECTS}"; then
  echo "invalid --max-hard-limit-rejects: ${MAX_HARD_LIMIT_REJECTS}" >&2
  exit 1
fi
if [[ -n "${MAX_HARD_LIMIT_BYPASSES}" ]] &&
   ! is_uint "${MAX_HARD_LIMIT_BYPASSES}"; then
  echo "invalid --max-hard-limit-bypasses: ${MAX_HARD_LIMIT_BYPASSES}" >&2
  exit 1
fi

raw_require_soft_limit_inactive="${REQUIRE_SOFT_LIMIT_INACTIVE}"
REQUIRE_SOFT_LIMIT_INACTIVE="$(normalize_bool "${raw_require_soft_limit_inactive}")" || {
  echo "invalid --require-soft-limit-inactive: ${raw_require_soft_limit_inactive}" >&2
  exit 1
}
raw_require_hard_limit_inactive="${REQUIRE_HARD_LIMIT_INACTIVE}"
REQUIRE_HARD_LIMIT_INACTIVE="$(normalize_bool "${raw_require_hard_limit_inactive}")" || {
  echo "invalid --require-hard-limit-inactive: ${raw_require_hard_limit_inactive}" >&2
  exit 1
}

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase5_capacity_gate_$(date +%Y%m%d_%H%M%S).report.txt"
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
  "approx_size_bytes"
  "l2_soft_limit_write_total"
  "l2_hard_limit_reject_total"
  "l2_hard_limit_bypass_total"
)
for key in "${required_fields[@]}"; do
  if [[ -z "${kv[${key}]:-}" ]]; then
    log "ERROR" "missing field in health output: ${key}"
    exit 1
  fi
done

approx_size_bytes="${kv[approx_size_bytes]}"
l2_soft_limit_write_total="${kv[l2_soft_limit_write_total]}"
l2_hard_limit_reject_total="${kv[l2_hard_limit_reject_total]}"
l2_hard_limit_bypass_total="${kv[l2_hard_limit_bypass_total]}"

if ! is_uint "${approx_size_bytes}" ||
   ! is_uint "${l2_soft_limit_write_total}" ||
   ! is_uint "${l2_hard_limit_reject_total}" ||
   ! is_uint "${l2_hard_limit_bypass_total}"; then
  log "ERROR" "non-numeric counters in health output"
  exit 1
fi

max_size_bytes=$((L2_MAX_SIZE_MB * 1024 * 1024))
usage_ratio="$(float_div "${approx_size_bytes}" "${max_size_bytes}")"
soft_limit_active="false"
hard_limit_active="false"
if float_ge "${usage_ratio}" "${SOFT_LIMIT_RATIO}"; then
  soft_limit_active="true"
fi
if float_ge "${usage_ratio}" "${HARD_LIMIT_RATIO}"; then
  hard_limit_active="true"
fi

fail_reasons=()
if float_gt "${usage_ratio}" "${MAX_USAGE_RATIO}"; then
  fail_reasons+=("usage_ratio>${MAX_USAGE_RATIO}")
fi
if [[ "${REQUIRE_SOFT_LIMIT_INACTIVE}" == "true" &&
      "${soft_limit_active}" == "true" ]]; then
  fail_reasons+=("soft_limit_active")
fi
if [[ "${REQUIRE_HARD_LIMIT_INACTIVE}" == "true" &&
      "${hard_limit_active}" == "true" ]]; then
  fail_reasons+=("hard_limit_active")
fi
if [[ -n "${MAX_SOFT_LIMIT_WRITES}" &&
      "${l2_soft_limit_write_total}" -gt "${MAX_SOFT_LIMIT_WRITES}" ]]; then
  fail_reasons+=("l2_soft_limit_write_total>${MAX_SOFT_LIMIT_WRITES}")
fi
if [[ -n "${MAX_HARD_LIMIT_REJECTS}" &&
      "${l2_hard_limit_reject_total}" -gt "${MAX_HARD_LIMIT_REJECTS}" ]]; then
  fail_reasons+=("l2_hard_limit_reject_total>${MAX_HARD_LIMIT_REJECTS}")
fi
if [[ -n "${MAX_HARD_LIMIT_BYPASSES}" &&
      "${l2_hard_limit_bypass_total}" -gt "${MAX_HARD_LIMIT_BYPASSES}" ]]; then
  fail_reasons+=("l2_hard_limit_bypass_total>${MAX_HARD_LIMIT_BYPASSES}")
fi

if [[ ${#fail_reasons[@]} -eq 0 ]]; then
  log "INFO" "phase5_capacity_gate_result status=pass usage_ratio=${usage_ratio} soft_limit_active=${soft_limit_active} hard_limit_active=${hard_limit_active} l2_soft_limit_write_total=${l2_soft_limit_write_total} l2_hard_limit_reject_total=${l2_hard_limit_reject_total} l2_hard_limit_bypass_total=${l2_hard_limit_bypass_total}"
  exit 0
fi

log "ERROR" "phase5_capacity_gate_result status=fail reasons=$(IFS=,; echo "${fail_reasons[*]}") usage_ratio=${usage_ratio} soft_limit_active=${soft_limit_active} hard_limit_active=${hard_limit_active} l2_soft_limit_write_total=${l2_soft_limit_write_total} l2_hard_limit_reject_total=${l2_hard_limit_reject_total} l2_hard_limit_bypass_total=${l2_hard_limit_bypass_total}"
exit 1
