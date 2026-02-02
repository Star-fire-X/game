-- Migration: 003_create_alert_tables (down)
DROP TABLE IF EXISTS admin.alert_history;
DROP TABLE IF EXISTS admin.alert_configs;
