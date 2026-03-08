#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

MODE=""
MODE_COUNT=0
CONFIG_PATH="${PROJECT_ROOT}/config/logic.yaml"
TARGET_KEY_ID=""
ENV_NAME_OVERRIDE=""
PID_OVERRIDE=""
SKIP_RELOAD=false
DRY_RUN=false
REPORT_FILE=""

usage() {
  cat <<'EOF'
Usage: scripts/run_storage_engine_phase4_key_rotation.sh [options]

Modes (required, choose one):
  --precheck                  Validate current config/env readiness
  --apply                     Switch active key to --target-key-id
  --rollback                  Roll back active key to --target-key-id

Options:
  --config <path>             logic.yaml path (default: config/logic.yaml)
  --target-key-id <id>        Target active key id for apply/rollback
  --env-name <name>           Override storage_engine.encryption_key_env
  --pid <pid>                 Override mir2_logic PID for SIGUSR1 reload
  --skip-reload               Do not send SIGUSR1 after config mutation
  --dry-run                   Print actions only, do not modify config
  --report-file <path>        Report output path
  -h, --help                  Show help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --precheck)
      MODE="precheck"
      MODE_COUNT=$((MODE_COUNT + 1))
      shift
      ;;
    --apply)
      MODE="apply"
      MODE_COUNT=$((MODE_COUNT + 1))
      shift
      ;;
    --rollback)
      MODE="rollback"
      MODE_COUNT=$((MODE_COUNT + 1))
      shift
      ;;
    --config)
      CONFIG_PATH="$2"
      shift 2
      ;;
    --target-key-id)
      TARGET_KEY_ID="$2"
      shift 2
      ;;
    --env-name)
      ENV_NAME_OVERRIDE="$2"
      shift 2
      ;;
    --pid)
      PID_OVERRIDE="$2"
      shift 2
      ;;
    --skip-reload)
      SKIP_RELOAD=true
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

if [[ -z "${MODE}" ]]; then
  echo "One of --precheck/--apply/--rollback is required." >&2
  usage
  exit 1
fi
if [[ "${MODE_COUNT}" -gt 1 ]]; then
  echo "Only one of --precheck/--apply/--rollback may be specified." >&2
  usage
  exit 1
fi

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/phase4_key_rotation_$(date +%Y%m%d_%H%M%S).report.txt"
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

trim() {
  local v="$1"
  v="${v#"${v%%[![:space:]]*}"}"
  v="${v%"${v##*[![:space:]]}"}"
  echo "${v}"
}

normalize_yaml_scalar() {
  local raw="$1"
  raw="${raw%%#*}"
  raw="$(trim "${raw}")"
  raw="${raw%\"}"
  raw="${raw#\"}"
  raw="${raw%\'}"
  raw="${raw#\'}"
  echo "${raw}"
}

extract_storage_engine_value() {
  local key="$1"
  awk -v key="${key}" '
    BEGIN { in_storage=0 }
    $0 ~ /^storage_engine:[[:space:]]*$/ { in_storage=1; next }
    in_storage && $0 ~ /^[^[:space:]]/ { in_storage=0 }
    in_storage {
      pat = "^[[:space:]]*" key ":[[:space:]]*(.*)$"
      if (match($0, pat, m)) {
        print m[1]
        exit 0
      }
    }
  ' "${CONFIG_PATH}"
}

set_storage_engine_value() {
  local key="$1"
  local value="$2"
  local tmp
  tmp="$(mktemp)"
  if ! awk -v key="${key}" -v value="${value}" '
    BEGIN { in_storage=0; done=0 }
    $0 ~ /^storage_engine:[[:space:]]*$/ { in_storage=1; print; next }
    in_storage && $0 ~ /^[^[:space:]]/ { in_storage=0 }
    in_storage && $0 ~ ("^[[:space:]]*" key ":[[:space:]]*") {
      print "  " key ": " value
      done=1
      next
    }
    { print }
    END {
      if (!done) {
        exit 3
      }
    }
  ' "${CONFIG_PATH}" >"${tmp}"; then
    rm -f "${tmp}"
    return 1
  fi
  mv "${tmp}" "${CONFIG_PATH}"
  return 0
}

keyring_contains_key_id() {
  local keyring="$1"
  local key_id="$2"
  local normalized item
  local -a items
  [[ -n "${keyring}" ]] || return 1

  normalized="${keyring//;/,}"
  IFS=',' read -r -a items <<< "${normalized}"

  for item in "${items[@]}"; do
    item="$(trim "${item}")"
    [[ -n "${item}" ]] || continue
    if [[ "${item}" == "${key_id}="* || "${item}" == "${key_id}:"* ]]; then
      return 0
    fi
  done
  return 1
}

resolve_env_name() {
  if [[ -n "${ENV_NAME_OVERRIDE}" ]]; then
    echo "${ENV_NAME_OVERRIDE}"
    return 0
  fi
  local raw
  raw="$(extract_storage_engine_value "encryption_key_env" || true)"
  normalize_yaml_scalar "${raw}"
}

resolve_logic_pid() {
  if [[ -n "${PID_OVERRIDE}" ]]; then
    echo "${PID_OVERRIDE}"
    return 0
  fi
  mapfile -t pids < <(pgrep -x mir2_logic || true)
  if [[ "${#pids[@]}" -eq 1 ]]; then
    echo "${pids[0]}"
    return 0
  fi
  if [[ "${#pids[@]}" -gt 1 ]]; then
    log "ERROR" "Found multiple mir2_logic pids: ${pids[*]}, please use --pid"
    return 1
  fi
  log "ERROR" "mir2_logic process not found; use --pid or --skip-reload"
  return 1
}

reload_logic_runtime_config() {
  if [[ "${SKIP_RELOAD}" == "true" ]]; then
    log "INFO" "skip-reload=true, skip sending SIGUSR1"
    return 0
  fi
  local pid
  pid="$(resolve_logic_pid)" || return 1
  if [[ "${DRY_RUN}" == "true" ]]; then
    log "INFO" "[dry-run] kill -USR1 ${pid}"
    return 0
  fi
  kill -USR1 "${pid}"
  log "INFO" "Sent SIGUSR1 to mir2_logic pid=${pid}"
  return 0
}

require_config_file() {
  if [[ ! -f "${CONFIG_PATH}" ]]; then
    log "ERROR" "Config file not found: ${CONFIG_PATH}"
    exit 1
  fi
}

run_precheck() {
  require_config_file

  local enc_enabled_raw active_key_raw env_name keyring
  enc_enabled_raw="$(extract_storage_engine_value "enable_data_encryption" || true)"
  active_key_raw="$(extract_storage_engine_value "encryption_active_key_id" || true)"
  env_name="$(resolve_env_name)"

  local enc_enabled active_key_id
  enc_enabled="$(normalize_yaml_scalar "${enc_enabled_raw}")"
  active_key_id="$(normalize_yaml_scalar "${active_key_raw}")"

  log "INFO" "config.path=${CONFIG_PATH}"
  log "INFO" "config.enable_data_encryption=${enc_enabled}"
  log "INFO" "config.encryption_active_key_id=${active_key_id}"
  log "INFO" "config.encryption_key_env=${env_name}"

  if [[ "${enc_enabled}" != "true" ]]; then
    log "ERROR" "enable_data_encryption must be true for key rotation."
    return 1
  fi
  if [[ -z "${active_key_id}" ]]; then
    log "ERROR" "encryption_active_key_id is empty."
    return 1
  fi
  if [[ -z "${env_name}" ]]; then
    log "ERROR" "encryption_key_env is empty (and --env-name not provided)."
    return 1
  fi

  keyring="${!env_name:-}"
  if [[ -z "${keyring}" ]]; then
    log "ERROR" "Environment variable ${env_name} is empty or missing."
    return 1
  fi

  if ! keyring_contains_key_id "${keyring}" "${active_key_id}"; then
    log "ERROR" "Keyring in ${env_name} does not contain current active key id=${active_key_id}."
    return 1
  fi

  if [[ -n "${TARGET_KEY_ID}" ]] && ! keyring_contains_key_id "${keyring}" "${TARGET_KEY_ID}"; then
    log "ERROR" "Keyring in ${env_name} does not contain target key id=${TARGET_KEY_ID}."
    return 1
  fi

  log "INFO" "Precheck passed."
  return 0
}

apply_or_rollback() {
  local op_name="$1"
  if [[ -z "${TARGET_KEY_ID}" ]]; then
    log "ERROR" "--target-key-id is required for ${op_name}."
    return 1
  fi

  run_precheck

  local current_raw current_active
  current_raw="$(extract_storage_engine_value "encryption_active_key_id" || true)"
  current_active="$(normalize_yaml_scalar "${current_raw}")"

  if [[ "${current_active}" == "${TARGET_KEY_ID}" ]]; then
    log "INFO" "Current active key id already ${TARGET_KEY_ID}, nothing to do."
    return 0
  fi

  local backup_path
  backup_path="${PROJECT_ROOT}/logs/phase4_key_rotation_$(date +%Y%m%d_%H%M%S_%N)_${op_name}_$$.logic.yaml.bak"
  if [[ "${DRY_RUN}" == "true" ]]; then
    log "INFO" "[dry-run] cp ${CONFIG_PATH} ${backup_path}"
    log "INFO" "[dry-run] set storage_engine.encryption_active_key_id=\"${TARGET_KEY_ID}\""
  else
    mkdir -p "$(dirname "${backup_path}")"
    cp "${CONFIG_PATH}" "${backup_path}"
    if ! set_storage_engine_value "encryption_active_key_id" "\"${TARGET_KEY_ID}\""; then
      log "ERROR" "Failed to update encryption_active_key_id in ${CONFIG_PATH}"
      return 1
    fi
    log "INFO" "Backup created: ${backup_path}"
    log "INFO" "Updated encryption_active_key_id: ${current_active} -> ${TARGET_KEY_ID}"
  fi

  reload_logic_runtime_config

  if [[ "${DRY_RUN}" != "true" ]]; then
    local verify_raw verify_id
    verify_raw="$(extract_storage_engine_value "encryption_active_key_id" || true)"
    verify_id="$(normalize_yaml_scalar "${verify_raw}")"
    if [[ "${verify_id}" != "${TARGET_KEY_ID}" ]]; then
      log "ERROR" "Post-update verification failed: expected ${TARGET_KEY_ID}, got ${verify_id}"
      return 1
    fi
  fi

  log "INFO" "${op_name} completed."
  return 0
}

main() {
  case "${MODE}" in
    precheck)
      run_precheck
      ;;
    apply)
      apply_or_rollback "apply"
      ;;
    rollback)
      apply_or_rollback "rollback"
      ;;
    *)
      log "ERROR" "Unsupported mode: ${MODE}"
      return 1
      ;;
  esac
}

main "$@"
