#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SCENARIO=""
KILL_POINT="prepare_ready"
SUMMARY_FILE=""
CHECKPOINT_CREATE_FILE=""
CHECKPOINT_RESTORE_FILE=""
HEALTH_FILE=""
VALIDATE_FILE=""
REPORT_FILE=""

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase6_release_gate.sh [options]

Options:
  --scenario <durable_async|tombstone_gc|startup_validation|checkpoint_restore>  Drill scenario (required)
  --kill-point <name>                      Kill point label (default: prepare_ready)
  --summary-file <path>                    Phase6 driver summary output (required)
  --checkpoint-create-file <path>          checkpoint-create summary output
  --checkpoint-restore-file <path>         checkpoint-restore summary output
  --health-file <path>                     storage_admin health output (required)
  --validate-file <path>                   storage_admin validate output (required)
  --report-file <path>                     Report output path
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
    --kill-point)
      KILL_POINT="$2"
      shift 2
      ;;
    --summary-file)
      SUMMARY_FILE="$2"
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

if [[ "${SCENARIO}" != "durable_async" &&
      "${SCENARIO}" != "tombstone_gc" &&
      "${SCENARIO}" != "startup_validation" &&
      "${SCENARIO}" != "checkpoint_restore" ]]; then
  echo "invalid --scenario: ${SCENARIO}" >&2
  exit 1
fi
if [[ -z "${SUMMARY_FILE}" || -z "${HEALTH_FILE}" || -z "${VALIDATE_FILE}" ]]; then
  echo "--summary-file, --health-file and --validate-file are required" >&2
  exit 1
fi
if [[ "${SCENARIO}" == "checkpoint_restore" &&
      ( -z "${CHECKPOINT_CREATE_FILE}" || -z "${CHECKPOINT_RESTORE_FILE}" ) ]]; then
  echo "--checkpoint-create-file and --checkpoint-restore-file are required for checkpoint_restore" >&2
  exit 1
fi

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase6_release_gate_$(date +%Y%m%d_%H%M%S).report.txt"
fi
mkdir -p "$(dirname "${REPORT_FILE}")"
touch "${REPORT_FILE}"

summary_line="$(load_line "${SUMMARY_FILE}" "phase6_fault_driver_result")"
if [[ -z "${summary_line}" ]]; then
  log "ERROR" "missing phase6_fault_driver_result line"
  exit 1
fi
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
checkpoint_create_line=""
checkpoint_restore_line=""
if [[ "${SCENARIO}" == "checkpoint_restore" ]]; then
  checkpoint_create_line="$(load_line "${CHECKPOINT_CREATE_FILE}" "storage_admin_checkpoint_create_summary")"
  checkpoint_restore_line="$(load_line "${CHECKPOINT_RESTORE_FILE}" "storage_admin_checkpoint_restore_summary")"
  if [[ -z "${checkpoint_create_line}" ]]; then
    log "ERROR" "missing storage_admin_checkpoint_create_summary line"
    exit 1
  fi
  if [[ -z "${checkpoint_restore_line}" ]]; then
    log "ERROR" "missing storage_admin_checkpoint_restore_summary line"
    exit 1
  fi
fi

declare -A summary_kv=()
declare -A health_kv=()
declare -A validate_kv=()
declare -A checkpoint_create_kv=()
declare -A checkpoint_restore_kv=()

for token in ${summary_line}; do
  if [[ "${token}" == *=* ]]; then
    summary_kv["${token%%=*}"]="${token#*=}"
  fi
done
for token in ${health_line}; do
  if [[ "${token}" == *=* ]]; then
    health_kv["${token%%=*}"]="${token#*=}"
  fi
done
for token in ${validate_line}; do
  if [[ "${token}" == *=* ]]; then
    validate_kv["${token%%=*}"]="${token#*=}"
  fi
done
for token in ${checkpoint_create_line}; do
  if [[ "${token}" == *=* ]]; then
    checkpoint_create_kv["${token%%=*}"]="${token#*=}"
  fi
done
for token in ${checkpoint_restore_line}; do
  if [[ "${token}" == *=* ]]; then
    checkpoint_restore_kv["${token%%=*}"]="${token#*=}"
  fi
done

fail_reasons=()
if [[ "${SCENARIO}" != "startup_validation" &&
      "${summary_kv[status]:-}" != "ok" ]]; then
  fail_reasons+=("driver_status!=ok")
fi
if [[ "${SCENARIO}" != "startup_validation" &&
      "${validate_kv[total_corrupted]:-}" != "0" ]]; then
  fail_reasons+=("validate_total_corrupted!=0")
fi

if [[ "${SCENARIO}" == "durable_async" ]]; then
  if [[ "${summary_kv[backend_key_present]:-}" != "true" ]]; then
    fail_reasons+=("backend_key_present!=true")
  fi
  if [[ "${summary_kv[outbox_depth]:-}" != "0" ]]; then
    fail_reasons+=("summary_outbox_depth!=0")
  fi
  if [[ "${health_kv[outbox_depth]:-}" != "0" ]]; then
    fail_reasons+=("health_outbox_depth!=0")
  fi
elif [[ "${SCENARIO}" == "tombstone_gc" ]]; then
  if [[ "${summary_kv[tombstone_gc_pending]:-}" != "0" ]]; then
    fail_reasons+=("tombstone_gc_pending>0")
  fi
  if [[ "${summary_kv[tombstone_gc_failed_total]:-}" != "0" ]]; then
    fail_reasons+=("tombstone_gc_failed_total>0")
  fi
  if [[ -z "${summary_kv[tombstone_gc_reclaimed_total]:-}" ||
        "${summary_kv[tombstone_gc_reclaimed_total]}" == "0" ]]; then
    fail_reasons+=("tombstone_gc_reclaimed_total<1")
  fi
  if [[ "${health_kv[tombstone_gc_pending]:-}" != "0" ]]; then
    fail_reasons+=("health_tombstone_gc_pending>0")
  fi
  if [[ "${health_kv[tombstone_gc_failed_total]:-}" != "0" ]]; then
    fail_reasons+=("health_tombstone_gc_failed_total>0")
  fi
elif [[ "${SCENARIO}" == "startup_validation" ]]; then
  if [[ "${summary_kv[status]:-}" != "init_failed" ]]; then
    fail_reasons+=("driver_status!=init_failed")
  fi
  if [[ "${validate_kv[total_corrupted]:-}" == "0" ||
        -z "${validate_kv[total_corrupted]:-}" ]]; then
    fail_reasons+=("validate_total_corrupted<=0")
  fi
else
  if [[ "${summary_kv[restored_key_present]:-}" != "true" ]]; then
    fail_reasons+=("restored_key_present!=true")
  fi
  if [[ "${checkpoint_create_kv[status]:-}" != "ok" ]]; then
    fail_reasons+=("checkpoint_create_status!=ok")
  fi
  if [[ "${checkpoint_restore_kv[status]:-}" != "ok" ]]; then
    fail_reasons+=("checkpoint_restore_status!=ok")
  fi
  if [[ "${validate_kv[total_corrupted]:-}" != "0" ]]; then
    fail_reasons+=("validate_total_corrupted!=0")
  fi
fi

if [[ ${#fail_reasons[@]} -eq 0 ]]; then
  log "INFO" "phase6_release_gate_result status=pass scenario=${SCENARIO} kill_point=${KILL_POINT}"
  exit 0
fi

log "ERROR" "phase6_release_gate_result status=fail scenario=${SCENARIO} kill_point=${KILL_POINT} reasons=$(IFS=,; echo "${fail_reasons[*]}")"
exit 1
