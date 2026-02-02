-- Seed data rollback
DELETE FROM admin.alert_configs WHERE rule_name IN ('service_down', 'cpu_high', 'memory_high');
DELETE FROM admin.users WHERE username = 'admin';
