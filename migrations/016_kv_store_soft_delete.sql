-- Migration 016: Add logical delete support for kv_store
-- Stage2 storage_engine soft-delete semantics.

ALTER TABLE kv_store
    ADD COLUMN IF NOT EXISTS is_deleted BOOLEAN NOT NULL DEFAULT FALSE;

CREATE INDEX IF NOT EXISTS idx_kv_store_is_deleted
    ON kv_store (is_deleted);
