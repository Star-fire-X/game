#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MIGRATION_FILE="${PROJECT_ROOT}/migrations/016_kv_store_soft_delete.sql"
ROLLBACK_FILE="${PROJECT_ROOT}/migrations/016_kv_store_soft_delete_rollback.sql"

PGHOST="${PGHOST:-${MIR2_DB_HOST:-127.0.0.1}}"
PGPORT="${PGPORT:-${MIR2_DB_PORT:-5432}}"
PGUSER="${PGUSER:-${MIR2_DB_USER:-mir2}}"
PGPASSWORD="${PGPASSWORD:-${MIR2_DB_PASSWORD:-mir2_password}}"
PGDATABASE="${PGDATABASE:-${MIR2_DB_NAME:-mir2_game}}"

MODE=""
DRY_RUN=false
FORCE_ROLLBACK=false
REPORT_FILE=""

usage() {
  cat <<'EOF'
Usage: scripts/run_kv_store_soft_delete_migration_016.sh [options]

Modes (required, choose one):
  --apply                 Apply migration 016
  --verify                Verify migration 016 acceptance checks
  --rollback              Roll back migration 016 (schema rollback)

Options:
  --force-rollback        Allow rollback even when is_deleted=true rows exist
  --dry-run               Print SQL/actions only, no schema mutation
  --report-file <path>    Output operation report file
  -h, --help              Show this help

Environment (optional):
  PGHOST, PGPORT, PGUSER, PGPASSWORD, PGDATABASE
  MIR2_DB_HOST, MIR2_DB_PORT, MIR2_DB_USER, MIR2_DB_PASSWORD, MIR2_DB_NAME
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply)
      MODE="apply"
      shift
      ;;
    --verify)
      MODE="verify"
      shift
      ;;
    --rollback)
      MODE="rollback"
      shift
      ;;
    --force-rollback)
      FORCE_ROLLBACK=true
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
  echo "One of --apply/--verify/--rollback is required." >&2
  usage
  exit 1
fi

if [[ -z "${REPORT_FILE}" ]]; then
  REPORT_FILE="${PROJECT_ROOT}/logs/migration_016_kv_store_soft_delete_$(date +%Y%m%d_%H%M%S).report.txt"
fi
mkdir -p "$(dirname "${REPORT_FILE}")"
touch "${REPORT_FILE}"

if ! command -v psql >/dev/null 2>&1; then
  echo "psql is required" >&2
  exit 1
fi

log() {
  local level="$1"
  shift
  local message="$*"
  local ts
  ts="$(date '+%Y-%m-%d %H:%M:%S')"
  echo "${ts} [${level}] ${message}" | tee -a "${REPORT_FILE}"
}

psql_cmd() {
  PGPASSWORD="${PGPASSWORD}" \
    psql -X -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${PGDATABASE}" \
      -v ON_ERROR_STOP=1 "$@"
}

psql_scalar() {
  local sql="$1"
  psql_cmd -At -c "${sql}" | tr -d '[:space:]'
}

ensure_prerequisites() {
  if [[ ! -f "${MIGRATION_FILE}" ]]; then
    log "ERROR" "Migration file not found: ${MIGRATION_FILE}"
    exit 1
  fi
  if [[ ! -f "${ROLLBACK_FILE}" ]]; then
    log "ERROR" "Rollback file not found: ${ROLLBACK_FILE}"
    exit 1
  fi

  local table_exists
  table_exists="$(psql_scalar "SELECT EXISTS (
      SELECT 1
      FROM information_schema.tables
      WHERE table_schema = current_schema()
        AND table_name = 'kv_store'
    );")"
  if [[ "${table_exists}" != "t" ]]; then
    log "ERROR" "Table kv_store does not exist in current schema."
    exit 1
  fi
}

print_current_snapshot() {
  local total_rows deleted_rows column_exists index_exists
  total_rows="$(psql_scalar "SELECT COUNT(*) FROM kv_store;")"
  column_exists="$(psql_scalar "SELECT EXISTS (
      SELECT 1
      FROM information_schema.columns
      WHERE table_schema = current_schema()
        AND table_name = 'kv_store'
        AND column_name = 'is_deleted'
    );")"
  index_exists="$(psql_scalar "SELECT EXISTS (
      SELECT 1
      FROM pg_indexes
      WHERE schemaname = current_schema()
        AND tablename = 'kv_store'
        AND indexname = 'idx_kv_store_is_deleted'
    );")"

  if [[ "${column_exists}" == "t" ]]; then
    deleted_rows="$(psql_scalar "SELECT COUNT(*) FROM kv_store WHERE is_deleted = TRUE;")"
  else
    deleted_rows="n/a"
  fi

  log "INFO" "snapshot.total_rows=${total_rows}"
  log "INFO" "snapshot.column_is_deleted=${column_exists}"
  log "INFO" "snapshot.index_idx_kv_store_is_deleted=${index_exists}"
  log "INFO" "snapshot.deleted_rows=${deleted_rows}"
}

verify_apply_state() {
  local column_exists not_null_ok default_ok index_exists null_rows
  column_exists="$(psql_scalar "SELECT EXISTS (
      SELECT 1
      FROM information_schema.columns
      WHERE table_schema = current_schema()
        AND table_name = 'kv_store'
        AND column_name = 'is_deleted'
    );")"
  not_null_ok="$(psql_scalar "SELECT EXISTS (
      SELECT 1
      FROM information_schema.columns
      WHERE table_schema = current_schema()
        AND table_name = 'kv_store'
        AND column_name = 'is_deleted'
        AND is_nullable = 'NO'
    );")"
  default_ok="$(psql_scalar "SELECT EXISTS (
      SELECT 1
      FROM information_schema.columns
      WHERE table_schema = current_schema()
        AND table_name = 'kv_store'
        AND column_name = 'is_deleted'
        AND COALESCE(column_default, '') ILIKE 'false%'
    );")"
  index_exists="$(psql_scalar "SELECT EXISTS (
      SELECT 1
      FROM pg_indexes
      WHERE schemaname = current_schema()
        AND tablename = 'kv_store'
        AND indexname = 'idx_kv_store_is_deleted'
    );")"
  null_rows="$(psql_scalar "SELECT COUNT(*) FROM kv_store WHERE is_deleted IS NULL;")"

  log "INFO" "verify.column_exists=${column_exists}"
  log "INFO" "verify.not_null=${not_null_ok}"
  log "INFO" "verify.default_false=${default_ok}"
  log "INFO" "verify.index_exists=${index_exists}"
  log "INFO" "verify.null_rows=${null_rows}"

  if [[ "${column_exists}" != "t" || "${not_null_ok}" != "t" || "${default_ok}" != "t" ||
        "${index_exists}" != "t" || "${null_rows}" != "0" ]]; then
    log "ERROR" "Migration 016 verification failed."
    return 1
  fi
  log "INFO" "Migration 016 verification passed."
  return 0
}

verify_rollback_state() {
  local column_exists index_exists
  column_exists="$(psql_scalar "SELECT EXISTS (
      SELECT 1
      FROM information_schema.columns
      WHERE table_schema = current_schema()
        AND table_name = 'kv_store'
        AND column_name = 'is_deleted'
    );")"
  index_exists="$(psql_scalar "SELECT EXISTS (
      SELECT 1
      FROM pg_indexes
      WHERE schemaname = current_schema()
        AND tablename = 'kv_store'
        AND indexname = 'idx_kv_store_is_deleted'
    );")"

  log "INFO" "verify.rollback.column_exists=${column_exists}"
  log "INFO" "verify.rollback.index_exists=${index_exists}"

  if [[ "${column_exists}" != "f" || "${index_exists}" != "f" ]]; then
    log "ERROR" "Rollback verification failed."
    return 1
  fi
  log "INFO" "Rollback verification passed."
  return 0
}

apply_migration() {
  log "INFO" "Applying migration 016 from ${MIGRATION_FILE}"
  if [[ "${DRY_RUN}" == "true" ]]; then
    log "INFO" "[dry-run] psql -f ${MIGRATION_FILE}"
    return 0
  fi
  psql_cmd -f "${MIGRATION_FILE}" >/dev/null
  return 0
}

rollback_migration() {
  local column_exists deleted_rows
  column_exists="$(psql_scalar "SELECT EXISTS (
      SELECT 1
      FROM information_schema.columns
      WHERE table_schema = current_schema()
        AND table_name = 'kv_store'
        AND column_name = 'is_deleted'
    );")"
  if [[ "${column_exists}" == "t" ]]; then
    deleted_rows="$(psql_scalar "SELECT COUNT(*) FROM kv_store WHERE is_deleted = TRUE;")"
    log "INFO" "rollback.guard.deleted_rows=${deleted_rows}"
    if [[ "${deleted_rows}" != "0" && "${FORCE_ROLLBACK}" != "true" ]]; then
      log "ERROR" "Rollback blocked: is_deleted=true rows exist. Use --force-rollback to proceed."
      return 1
    fi
  else
    log "INFO" "rollback.guard.column_missing=true"
  fi

  log "INFO" "Rolling back migration 016 with ${ROLLBACK_FILE}"
  if [[ "${DRY_RUN}" == "true" ]]; then
    log "INFO" "[dry-run] psql -f ${ROLLBACK_FILE}"
    return 0
  fi
  psql_cmd -f "${ROLLBACK_FILE}" >/dev/null
  return 0
}

main() {
  log "INFO" "mode=${MODE} dry_run=${DRY_RUN} force_rollback=${FORCE_ROLLBACK}"
  log "INFO" "target=${PGHOST}:${PGPORT}/${PGDATABASE} user=${PGUSER}"
  ensure_prerequisites
  print_current_snapshot

  case "${MODE}" in
    apply)
      apply_migration
      if [[ "${DRY_RUN}" == "false" ]]; then
        verify_apply_state
      fi
      ;;
    verify)
      verify_apply_state
      ;;
    rollback)
      rollback_migration
      if [[ "${DRY_RUN}" == "false" ]]; then
        verify_rollback_state
      fi
      ;;
    *)
      log "ERROR" "Unsupported mode: ${MODE}"
      return 1
      ;;
  esac

  print_current_snapshot
  log "INFO" "done"
  return 0
}

main "$@"
