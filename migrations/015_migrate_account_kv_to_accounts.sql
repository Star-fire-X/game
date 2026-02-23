-- Migration 015: Account data migration helpers (kv_store -> accounts)
--
-- Notes:
-- - JSON decode/upsert execution is handled by application script:
--   scripts/migrate_account_kv_to_accounts.sh
-- - This migration only prepares tracking tables and helper indexes.

BEGIN;

CREATE TABLE IF NOT EXISTS account_kv_migration_batches (
    batch_id TEXT PRIMARY KEY,
    started_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    completed_at TIMESTAMPTZ,
    status TEXT NOT NULL DEFAULT 'running',
    source_count BIGINT NOT NULL DEFAULT 0,
    migrated_count BIGINT NOT NULL DEFAULT 0,
    failed_count BIGINT NOT NULL DEFAULT 0,
    checksum TEXT
);

CREATE TABLE IF NOT EXISTS account_kv_migration_rows (
    batch_id TEXT NOT NULL,
    username TEXT NOT NULL,
    account_id BIGINT,
    status TEXT NOT NULL,  -- migrated | failed | skipped
    error_message TEXT,
    migrated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (batch_id, username),
    CONSTRAINT fk_account_kv_migration_batch
        FOREIGN KEY (batch_id)
        REFERENCES account_kv_migration_batches(batch_id)
        ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_account_kv_migration_rows_username
    ON account_kv_migration_rows (username);
CREATE INDEX IF NOT EXISTS idx_account_kv_migration_rows_status
    ON account_kv_migration_rows (status);

CREATE INDEX IF NOT EXISTS idx_kv_store_account_username_prefix
    ON kv_store (key)
    WHERE key LIKE 'account:username:%';

COMMIT;
