-- Migration 011: Create kv_store table for generic persistence
-- Used by StorageEngine as the PostgreSQL backend for key-value storage.
-- Stores FlatBuffers-serialized data as BYTEA.

CREATE TABLE IF NOT EXISTS kv_store (
    key       TEXT    PRIMARY KEY,
    version   BIGINT  NOT NULL,
    data      BYTEA   NOT NULL,
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_kv_store_version ON kv_store(version);
