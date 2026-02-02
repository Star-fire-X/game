-- Migration: 003_create_alert_tables
-- Alert configuration and history tables

CREATE TABLE admin.alert_configs (
    id SERIAL PRIMARY KEY,
    rule_name VARCHAR(64) NOT NULL,
    rule_type VARCHAR(32) NOT NULL,
    service_name VARCHAR(32),
    threshold DECIMAL,
    severity VARCHAR(16) NOT NULL CHECK (severity IN ('critical', 'warning', 'info')),
    is_enabled BOOLEAN DEFAULT true,
    dedup_window_seconds INT DEFAULT 300,
    notify_wechat BOOLEAN DEFAULT true,
    notify_webhook BOOLEAN DEFAULT false,
    webhook_url VARCHAR(512),
    webhook_headers JSONB,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE admin.alert_history (
    id BIGSERIAL PRIMARY KEY,
    rule_id INT REFERENCES admin.alert_configs(id),
    rule_name VARCHAR(64) NOT NULL,
    service_name VARCHAR(32),
    severity VARCHAR(16) NOT NULL,
    message TEXT NOT NULL,
    details JSONB,
    notified_wechat BOOLEAN DEFAULT false,
    notified_webhook BOOLEAN DEFAULT false,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    resolved_at TIMESTAMPTZ
);

CREATE INDEX idx_alert_history_created ON admin.alert_history(created_at DESC);
CREATE INDEX idx_alert_history_service ON admin.alert_history(service_name, created_at DESC);
