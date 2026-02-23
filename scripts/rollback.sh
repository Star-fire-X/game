#!/usr/bin/env bash

################################################################################
# Rollback Script
# Description: Stop LogicServer and start legacy World/Game/DB services (systemd)
# Usage: ./scripts/rollback.sh [options]
#
# WARNING: This script is DEPRECATED and NO LONGER FUNCTIONAL.
# The World/Game/DB server binaries have been removed from the codebase.
# This script is kept for historical reference only.
################################################################################

set -euo pipefail

# Color codes for output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Paths
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
readonly LOG_FILE="${PROJECT_ROOT}/logs/rollback-$(date +%Y%m%d_%H%M%S).log"

# Defaults (override via env or flags)
readonly DEFAULT_LOGIC_UNIT="mir2-logic.service"
readonly DEFAULT_WORLD_UNIT="mir2-world.service"
readonly DEFAULT_GAME_UNIT="mir2-game.service"
readonly DEFAULT_DB_UNIT="mir2-db.service"
readonly DEFAULT_TIMEOUT_SEC=60
readonly DEFAULT_INTERVAL_SEC=2

LOGIC_UNIT="${LOGIC_UNIT:-${DEFAULT_LOGIC_UNIT}}"
WORLD_UNIT="${WORLD_UNIT:-${DEFAULT_WORLD_UNIT}}"
GAME_UNIT="${GAME_UNIT:-${DEFAULT_GAME_UNIT}}"
DB_UNIT="${DB_UNIT:-${DEFAULT_DB_UNIT}}"
TIMEOUT_SEC="${TIMEOUT_SEC:-${DEFAULT_TIMEOUT_SEC}}"
INTERVAL_SEC="${INTERVAL_SEC:-${DEFAULT_INTERVAL_SEC}}"
DRY_RUN=false
USE_SUDO=false

SYSTEMCTL_CMD=(systemctl)

################################################################################
# Logging Functions
################################################################################

log() {
  local level="$1"
  shift
  local message="$*"
  local timestamp
  timestamp=$(date '+%Y-%m-%d %H:%M:%S')

  echo -e "${timestamp} [${level}] ${message}" | tee -a "${LOG_FILE}"
}

log_info() {
  log "INFO" "${BLUE}$*${NC}"
}

log_success() {
  log "SUCCESS" "${GREEN}$*${NC}"
}

log_warning() {
  log "WARNING" "${YELLOW}$*${NC}"
}

log_error() {
  log "ERROR" "${RED}$*${NC}"
}

################################################################################
# Utility Functions
################################################################################

usage() {
  cat <<'EOF'
Usage: ./scripts/rollback.sh [options]

Options:
  --logic-unit UNIT     LogicServer systemd unit (default: mir2-logic.service)
  --world-unit UNIT     WorldServer systemd unit (default: mir2-world.service)
  --game-unit UNIT      GameServer systemd unit (default: mir2-game.service)
  --db-unit UNIT        DbServer systemd unit (default: mir2-db.service)
  --timeout SEC         Wait timeout per unit (default: 60)
  --interval SEC        Wait interval in seconds (default: 2)
  --dry-run             Print actions without executing systemctl
  --sudo                Run systemctl via sudo
  -h, --help            Show this help

Environment overrides:
  LOGIC_UNIT, WORLD_UNIT, GAME_UNIT, DB_UNIT
  TIMEOUT_SEC, INTERVAL_SEC

Examples:
  ./scripts/rollback.sh
  ./scripts/rollback.sh --sudo --logic-unit logic-server
  ./scripts/rollback.sh --dry-run --world-unit mir2-world.service
EOF
}

normalize_unit() {
  local unit="$1"
  if [[ "${unit}" != *.service ]]; then
    echo "${unit}.service"
  else
    echo "${unit}"
  fi
}

validate_integer() {
  local name="$1"
  local value="$2"
  if ! [[ "${value}" =~ ^[0-9]+$ ]]; then
    log_error "${name} must be an integer: ${value}"
    exit 1
  fi
  if [[ "${value}" -lt 1 ]]; then
    log_error "${name} must be >= 1: ${value}"
    exit 1
  fi
}

print_cmd() {
  local out=""
  for arg in "$@"; do
    out+="$(printf "%q " "${arg}")"
  done
  printf '%s' "${out}"
}

run_cmd() {
  if ${DRY_RUN}; then
    log_info "[dry-run] $(print_cmd "$@")"
    return 0
  fi
  "$@"
}

check_systemd() {
  if ${DRY_RUN}; then
    log_warning "Dry-run mode: skipping systemd checks"
    return 0
  fi

  if ! command -v systemctl >/dev/null 2>&1; then
    log_error "systemctl not found. This script requires systemd."
    exit 1
  fi
}

ensure_unit_loaded() {
  local unit="$1"

  if ${DRY_RUN}; then
    return 0
  fi

  local load_state
  load_state=$(${SYSTEMCTL_CMD[@]} show "${unit}" -p LoadState --value 2>/dev/null || true)
  if [[ "${load_state}" != "loaded" ]]; then
    log_error "systemd unit not found or not loaded: ${unit}"
    exit 1
  fi
}

is_active() {
  local unit="$1"

  if ${DRY_RUN}; then
    return 1
  fi

  ${SYSTEMCTL_CMD[@]} is-active --quiet "${unit}"
}

wait_for_active() {
  local unit="$1"
  local timeout="$2"
  local elapsed=0

  while (( elapsed < timeout )); do
    if ${SYSTEMCTL_CMD[@]} is-active --quiet "${unit}"; then
      return 0
    fi
    sleep "${INTERVAL_SEC}"
    elapsed=$((elapsed + INTERVAL_SEC))
  done

  return 1
}

wait_for_inactive() {
  local unit="$1"
  local timeout="$2"
  local elapsed=0

  while (( elapsed < timeout )); do
    if ! ${SYSTEMCTL_CMD[@]} is-active --quiet "${unit}"; then
      return 0
    fi
    sleep "${INTERVAL_SEC}"
    elapsed=$((elapsed + INTERVAL_SEC))
  done

  return 1
}

stop_logic_server() {
  log_info "Stopping LogicServer: ${LOGIC_UNIT}"

  if ${DRY_RUN}; then
    run_cmd ${SYSTEMCTL_CMD[@]} stop "${LOGIC_UNIT}"
    return 0
  fi

  if is_active "${LOGIC_UNIT}"; then
    run_cmd ${SYSTEMCTL_CMD[@]} stop "${LOGIC_UNIT}"
    if ! wait_for_inactive "${LOGIC_UNIT}" "${TIMEOUT_SEC}"; then
      log_error "Timeout waiting for LogicServer to stop: ${LOGIC_UNIT}"
      exit 1
    fi
    log_success "LogicServer stopped"
  else
    log_warning "LogicServer already stopped"
  fi
}

start_service() {
  local unit="$1"
  local label="$2"

  log_info "Starting ${label}: ${unit}"

  if ${DRY_RUN}; then
    run_cmd ${SYSTEMCTL_CMD[@]} start "${unit}"
    return 0
  fi

  if is_active "${unit}"; then
    log_warning "${label} already active"
    return 0
  fi

  run_cmd ${SYSTEMCTL_CMD[@]} start "${unit}"
  if ! wait_for_active "${unit}" "${TIMEOUT_SEC}"; then
    log_error "Timeout waiting for ${label} to become active: ${unit}"
    exit 1
  fi
  log_success "${label} is active"
}

print_banner() {
  echo -e "${BLUE}"
  echo "============================================================"
  echo "  MIR2 Rollback (Logic -> World/Game/DB)"
  echo "============================================================"
  echo -e "${NC}"
}

################################################################################
# Main
################################################################################

main() {
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --logic-unit)
        LOGIC_UNIT="$2"
        shift 2
        ;;
      --world-unit)
        WORLD_UNIT="$2"
        shift 2
        ;;
      --game-unit)
        GAME_UNIT="$2"
        shift 2
        ;;
      --db-unit)
        DB_UNIT="$2"
        shift 2
        ;;
      --timeout)
        TIMEOUT_SEC="$2"
        shift 2
        ;;
      --interval)
        INTERVAL_SEC="$2"
        shift 2
        ;;
      --dry-run)
        DRY_RUN=true
        shift
        ;;
      --sudo)
        USE_SUDO=true
        shift
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        log_error "Unknown argument: $1"
        usage
        exit 1
        ;;
    esac
  done

  LOGIC_UNIT=$(normalize_unit "${LOGIC_UNIT}")
  WORLD_UNIT=$(normalize_unit "${WORLD_UNIT}")
  GAME_UNIT=$(normalize_unit "${GAME_UNIT}")
  DB_UNIT=$(normalize_unit "${DB_UNIT}")

  validate_integer "TIMEOUT_SEC" "${TIMEOUT_SEC}"
  validate_integer "INTERVAL_SEC" "${INTERVAL_SEC}"

  if ${USE_SUDO}; then
    if ! command -v sudo >/dev/null 2>&1; then
      log_error "sudo not found but --sudo was specified"
      exit 1
    fi
    SYSTEMCTL_CMD=(sudo systemctl)
  fi

  mkdir -p "$(dirname "${LOG_FILE}")"

  print_banner
  log_info "Rollback started at $(date '+%Y-%m-%d %H:%M:%S')"
  log_info "Log file: ${LOG_FILE}"
  log_info "Units: logic=${LOGIC_UNIT}, world=${WORLD_UNIT}, game=${GAME_UNIT}, db=${DB_UNIT}"

  check_systemd

  ensure_unit_loaded "${LOGIC_UNIT}"
  ensure_unit_loaded "${WORLD_UNIT}"
  ensure_unit_loaded "${GAME_UNIT}"
  ensure_unit_loaded "${DB_UNIT}"

  stop_logic_server

  # Start legacy services in dependency order
  start_service "${DB_UNIT}" "DbServer"
  start_service "${WORLD_UNIT}" "WorldServer"
  start_service "${GAME_UNIT}" "GameServer"

  log_success "Rollback completed successfully"
}

main "$@"
