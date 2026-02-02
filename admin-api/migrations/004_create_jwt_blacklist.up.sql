-- Migration: 004_create_jwt_blacklist
-- JWT blacklist for token revocation

CREATE TABLE admin.jwt_blacklist (
    jti VARCHAR(64) PRIMARY KEY,
    user_id INT NOT NULL,
    reason VARCHAR(128),
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_blacklist_expires ON admin.jwt_blacklist(expires_at);
CREATE INDEX idx_blacklist_user ON admin.jwt_blacklist(user_id);
