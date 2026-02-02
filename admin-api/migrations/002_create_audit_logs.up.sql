-- Migration: 002_create_audit_logs
-- Audit logs table for tracking all operations

CREATE TABLE admin.audit_logs (
    id BIGSERIAL PRIMARY KEY,
    operator_id INT,
    operator_name VARCHAR(64) NOT NULL,
    action VARCHAR(64) NOT NULL,
    target_type VARCHAR(32),
    target_id VARCHAR(64),
    target_name VARCHAR(128),
    details JSONB,
    ip_address INET,
    user_agent TEXT,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_audit_operator ON admin.audit_logs(operator_id, created_at DESC);
CREATE INDEX idx_audit_action ON admin.audit_logs(action, created_at DESC);
CREATE INDEX idx_audit_created ON admin.audit_logs(created_at DESC);
