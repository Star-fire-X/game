-- Seed data for admin schema
-- Default super admin user (password: admin123)

INSERT INTO admin.users (username, password_hash, role, is_active)
VALUES (
    'admin',
    '$2a$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/X4.VTtYWWQJqhN8/X',
    'super_admin',
    true
) ON CONFLICT (username) DO NOTHING;

-- Default alert configs
INSERT INTO admin.alert_configs (rule_name, rule_type, severity, is_enabled)
VALUES
    ('service_down', 'service_down', 'critical', true),
    ('cpu_high', 'cpu_high', 'warning', true),
    ('memory_high', 'memory_high', 'warning', true)
ON CONFLICT DO NOTHING;
