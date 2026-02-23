#!/usr/bin/env bash
set -euo pipefail

PGHOST="${PGHOST:-${MIR2_DB_HOST:-127.0.0.1}}"
PGPORT="${PGPORT:-${MIR2_DB_PORT:-5432}}"
PGUSER="${PGUSER:-${MIR2_DB_USER:-mir2}}"
PGPASSWORD="${PGPASSWORD:-${MIR2_DB_PASSWORD:-mir2_password}}"
PGDATABASE="${PGDATABASE:-${MIR2_DB_NAME:-mir2_game}}"

BATCH_ID=""
DRY_RUN=false

usage() {
  cat <<'EOF'
Usage: scripts/rollback_account_migration.sh --batch-id <id> [--dry-run]

Rollback behavior (batch scoped):
1. Read migrated usernames from account_kv_migration_rows for the batch.
2. Restore accounts rows from kv_store payload for those usernames.
3. If kv payload for a migrated username no longer exists, delete that accounts row.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --batch-id)
      BATCH_ID="$2"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=true
      shift
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

if [[ -z "${BATCH_ID}" ]]; then
  echo "--batch-id is required" >&2
  usage
  exit 1
fi

if ! command -v psql >/dev/null 2>&1; then
  echo "psql is required" >&2
  exit 1
fi
if ! command -v jq >/dev/null 2>&1; then
  echo "jq is required" >&2
  exit 1
fi
if ! command -v base64 >/dev/null 2>&1; then
  echo "base64 is required" >&2
  exit 1
fi

psql_cmd() {
  PGPASSWORD="${PGPASSWORD}" \
    psql -h "${PGHOST}" -p "${PGPORT}" -U "${PGUSER}" -d "${PGDATABASE}" \
      -v ON_ERROR_STOP=1 "$@"
}

upsert_account() {
  local account_id="$1"
  local username="$2"
  local password_hash="$3"
  local email="$4"
  local created_at="$5"
  local last_login="$6"
  local banned="$7"

  if [[ -n "${account_id}" && "${account_id}" != "0" ]]; then
    psql_cmd \
      -v account_id="${account_id}" \
      -v username="${username}" \
      -v password_hash="${password_hash}" \
      -v email="${email}" \
      -v created_at="${created_at}" \
      -v last_login="${last_login}" \
      -v banned="${banned}" \
      -c "INSERT INTO accounts
            (id, username, password_hash, email, created_at, last_login, banned)
          VALUES
            (:account_id::BIGINT,
             :'username',
             :'password_hash',
             NULLIF(:'email', ''),
             CASE
               WHEN :created_at::BIGINT > 0
               THEN TO_TIMESTAMP(:created_at::DOUBLE PRECISION / 1000.0)
               ELSE NOW()
             END,
             CASE
               WHEN :last_login::BIGINT > 0
               THEN TO_TIMESTAMP(:last_login::DOUBLE PRECISION / 1000.0)
               ELSE NULL
             END,
             :banned::BOOLEAN)
          ON CONFLICT (username) DO UPDATE SET
            id = EXCLUDED.id,
            password_hash = EXCLUDED.password_hash,
            email = EXCLUDED.email,
            last_login = EXCLUDED.last_login,
            banned = EXCLUDED.banned;" >/dev/null
  else
    psql_cmd \
      -v username="${username}" \
      -v password_hash="${password_hash}" \
      -v email="${email}" \
      -v created_at="${created_at}" \
      -v last_login="${last_login}" \
      -v banned="${banned}" \
      -c "INSERT INTO accounts
            (username, password_hash, email, created_at, last_login, banned)
          VALUES
            (:'username',
             :'password_hash',
             NULLIF(:'email', ''),
             CASE
               WHEN :created_at::BIGINT > 0
               THEN TO_TIMESTAMP(:created_at::DOUBLE PRECISION / 1000.0)
               ELSE NOW()
             END,
             CASE
               WHEN :last_login::BIGINT > 0
               THEN TO_TIMESTAMP(:last_login::DOUBLE PRECISION / 1000.0)
               ELSE NULL
             END,
             :banned::BOOLEAN)
          ON CONFLICT (username) DO UPDATE SET
            password_hash = EXCLUDED.password_hash,
            email = EXCLUDED.email,
            last_login = EXCLUDED.last_login,
            banned = EXCLUDED.banned;" >/dev/null
  fi
}

USER_FILE="$(mktemp)"
trap 'rm -f "${USER_FILE}"' EXIT

psql_cmd -At \
  -v batch_id="${BATCH_ID}" \
  -c "SELECT username
      FROM account_kv_migration_rows
      WHERE batch_id = :'batch_id'
        AND status = 'migrated'
      ORDER BY username;" >"${USER_FILE}"

if [[ ! -s "${USER_FILE}" ]]; then
  echo "No migrated usernames found for batch ${BATCH_ID}"
  exit 0
fi

RESTORED_COUNT=0
DELETED_COUNT=0
FAILED_COUNT=0

while IFS= read -r username; do
  [[ -z "${username}" ]] && continue
  key="account:username:${username}"
  payload_b64="$(psql_cmd -At \
      -v key="${key}" \
      -c "SELECT encode(data, 'base64') FROM kv_store WHERE key = :'key';" || true)"

  if [[ -z "${payload_b64}" ]]; then
    if [[ "${DRY_RUN}" == "false" ]]; then
      psql_cmd -v username="${username}" \
        -c "DELETE FROM accounts WHERE username = :'username';" >/dev/null
    fi
    DELETED_COUNT=$((DELETED_COUNT + 1))
    continue
  fi

  if ! json_payload="$(printf '%s' "${payload_b64}" | base64 --decode 2>/dev/null)"; then
    FAILED_COUNT=$((FAILED_COUNT + 1))
    continue
  fi
  if ! jq -e . >/dev/null 2>&1 <<<"${json_payload}"; then
    FAILED_COUNT=$((FAILED_COUNT + 1))
    continue
  fi

  account_id="$(jq -r '.id // 0' <<<"${json_payload}")"
  password_hash="$(jq -r '.password_hash // ""' <<<"${json_payload}")"
  email="$(jq -r '.email // ""' <<<"${json_payload}")"
  created_at="$(jq -r '.created_at // 0' <<<"${json_payload}")"
  last_login="$(jq -r '.last_login // 0' <<<"${json_payload}")"
  banned="$(jq -r '.banned // false' <<<"${json_payload}")"

  if [[ -z "${password_hash}" ]]; then
    FAILED_COUNT=$((FAILED_COUNT + 1))
    continue
  fi

  if [[ "${DRY_RUN}" == "false" ]]; then
    if ! upsert_account "${account_id}" "${username}" "${password_hash}" "${email}" \
        "${created_at}" "${last_login}" "${banned}"; then
      FAILED_COUNT=$((FAILED_COUNT + 1))
      continue
    fi
  fi

  RESTORED_COUNT=$((RESTORED_COUNT + 1))
done <"${USER_FILE}"

if [[ "${DRY_RUN}" == "false" ]]; then
  psql_cmd \
    -v batch_id="${BATCH_ID}" \
    -c "UPDATE account_kv_migration_batches
        SET status = CASE
              WHEN ${FAILED_COUNT} = 0 THEN 'rolled_back'
              ELSE 'rollback_with_failures'
            END,
            completed_at = NOW()
        WHERE batch_id = :'batch_id';" >/dev/null
fi

echo "rollback batch=${BATCH_ID}"
echo "dry_run=${DRY_RUN}"
echo "restored_count=${RESTORED_COUNT}"
echo "deleted_count=${DELETED_COUNT}"
echo "failed_count=${FAILED_COUNT}"
