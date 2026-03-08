-- Rollback for migration 016: remove logical delete support from kv_store.
-- WARNING: This drops the is_deleted column and may lose tombstone state.

DROP INDEX IF EXISTS idx_kv_store_is_deleted;

ALTER TABLE kv_store
    DROP COLUMN IF EXISTS is_deleted;
