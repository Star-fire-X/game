#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ADMIN_BIN="${PROJECT_ROOT}/build-wsl/bin/mir2_storage_admin"
DB_PATH=""
INPUT_FILE=""
MAX_TOMBSTONE_GC_PENDING=0
MIN_TOMBSTONE_GC_RECLAIMED=0
MAX_TOMBSTONE_GC_FAILED=0
REPORT_FILE=""

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase5_tombstone_gc_gate.sh [options]

Options:
  --db-path <path>                     RocksDB path (required unless --input-file used)
  --admin-bin <path>                   mir2_storage_admin binary path
  --input-file <path>                  Read health summary from file
  --max-tombstone-gc-pending <n>       Gate threshold (default: 0)
  --min-tombstone-gc-reclaimed <n>     Gate threshold (default: 0)
  --max-tombstone-gc-failed <n>        Gate threshold (default: 0)
  --report-file <path>                 Report output path
  -h, --help                           Show help
EOF
}

is_uint() {
  [[ "${1}" =~ ^[0-9]+$ ]]
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
    --max-tombstone-gc-pending)
      MAX_TOMBSTONE_GC_PENDING="$2"
      shift 2
      ;;
    --min-tombstone-gc-reclaimed)
      MIN_TOMBSTONE_GC_RECLAIMED="$2"
      shift 2
      ;;
    --max-tombstone-gc-failed)
      MAX_TOMBSTONE_GC_FAILED="$2"
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

if ! is_uint "${MAX_TOMBSTONE_GC_PENDING}"; then
  echo "invalid --max-tombstone-gc-pending: ${MAX_TOMBSTONE_GC_PENDING}" >&2
  exit 1
fi
if ! is_uint "${MIN_TOMBSTONE_GC_RECLAIMED}"; then
  echo "invalid --min-tombstone-gc-reclaimed: ${MIN_TOMBSTONE_GC_RECLAIMED}" >&2
  exit 1
fi
if ! is_uint "${MAX_TOMBSTONE_GC_FAILED}"; then
  echo "invalid --max-tombstone-gc-failed: ${MAX_TOMBSTONE_GC_FAILED}" >&2
  exit 1
fi

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase5_tombstone_gc_gate_$(date +%Y%m%d_%H%M%S).report.txt"
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
  "tombstone_gc_pending"
  "tombstone_gc_reclaimed_total"
  "tombstone_gc_failed_total"
)
for key in "${required_fields[@]}"; do
  if [[ -z "${kv[${key}]:-}" ]]; then
    log "ERROR" "missing field in health output: ${key}"
    exit 1
  fi
done

tombstone_gc_pending="${kv[tombstone_gc_pending]}"
tombstone_gc_reclaimed_total="${kv[tombstone_gc_reclaimed_total]}"
tombstone_gc_failed_total="${kv[tombstone_gc_failed_total]}"

if ! is_uint "${tombstone_gc_pending}" ||
   ! is_uint "${tombstone_gc_reclaimed_total}" ||
   ! is_uint "${tombstone_gc_failed_total}"; then
  log "ERROR" "non-numeric counters in health output"
  exit 1
fi

fail_reasons=()
if [[ "${tombstone_gc_pending}" -gt "${MAX_TOMBSTONE_GC_PENDING}" ]]; then
  fail_reasons+=("tombstone_gc_pending>${MAX_TOMBSTONE_GC_PENDING}")
fi
if [[ "${tombstone_gc_reclaimed_total}" -lt "${MIN_TOMBSTONE_GC_RECLAIMED}" ]]; then
  fail_reasons+=("tombstone_gc_reclaimed_total<${MIN_TOMBSTONE_GC_RECLAIMED}")
fi
if [[ "${tombstone_gc_failed_total}" -gt "${MAX_TOMBSTONE_GC_FAILED}" ]]; then
  fail_reasons+=("tombstone_gc_failed_total>${MAX_TOMBSTONE_GC_FAILED}")
fi

if [[ ${#fail_reasons[@]} -eq 0 ]]; then
  log "INFO" "phase5_tombstone_gc_gate_result status=pass tombstone_gc_pending=${tombstone_gc_pending} tombstone_gc_reclaimed_total=${tombstone_gc_reclaimed_total} tombstone_gc_failed_total=${tombstone_gc_failed_total}"
  exit 0
fi

log "ERROR" "phase5_tombstone_gc_gate_result status=fail reasons=$(IFS=,; echo "${fail_reasons[*]}") tombstone_gc_pending=${tombstone_gc_pending} tombstone_gc_reclaimed_total=${tombstone_gc_reclaimed_total} tombstone_gc_failed_total=${tombstone_gc_failed_total}"
exit 1
