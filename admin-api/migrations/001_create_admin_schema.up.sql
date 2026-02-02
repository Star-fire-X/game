-- Migration: 001_create_admin_schema
-- Create admin schema for management platform

CREATE SCHEMA IF NOT EXISTS admin;

-- Admin users table
CREATE TABLE admin.users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(64) UNIQUE NOT NULL,
    password_hash VARCHAR(256) NOT NULL,
    totp_secret VARCHAR(64),
    totp_enabled BOOLEAN DEFAULT false,
    recovery_codes TEXT[],
    role VARCHAR(32) NOT NULL CHECK (role IN ('super_admin', 'admin', 'operator', 'viewer')),
    is_active BOOLEAN DEFAULT true,
    failed_attempts INT DEFAULT 0,
    locked_until TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    last_login_at TIMESTAMPTZ,
    last_login_ip INET
);

CREATE INDEX idx_admin_users_username ON admin.users(username);
CREATE INDEX idx_admin_users_role ON admin.users(role);
