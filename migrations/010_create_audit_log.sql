-- audit log (placeholder)
CREATE TABLE IF NOT EXISTS audit_log (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT,
    action VARCHAR(64) NOT NULL,
    details TEXT,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
