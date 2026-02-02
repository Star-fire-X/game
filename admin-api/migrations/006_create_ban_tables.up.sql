-- 封禁记录表 (存储在 game schema)
CREATE SCHEMA IF NOT EXISTS game;

-- 账号封禁表
CREATE TABLE IF NOT EXISTS game.account_bans (
    id BIGSERIAL PRIMARY KEY,
    account_id BIGINT NOT NULL,
    ban_type VARCHAR(32) NOT NULL,  -- 'ban' or 'mute'
    duration_seconds INT NOT NULL,   -- 0 = permanent
    reason TEXT NOT NULL,
    operator_id INT NOT NULL,
    operator_name VARCHAR(64) NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    expires_at TIMESTAMPTZ,          -- NULL = permanent
    lifted_at TIMESTAMPTZ,           -- 解封时间
    lifted_by INT,                   -- 解封操作人
    lift_reason TEXT                 -- 解封原因
);

CREATE INDEX idx_account_bans_account ON game.account_bans(account_id);
CREATE INDEX idx_account_bans_type ON game.account_bans(ban_type);
CREATE INDEX idx_account_bans_expires ON game.account_bans(expires_at) WHERE expires_at IS NOT NULL;

-- 角色封禁表 (用于禁言等角色级别操作)
CREATE TABLE IF NOT EXISTS game.character_bans (
    id BIGSERIAL PRIMARY KEY,
    character_id BIGINT NOT NULL,
    character_name VARCHAR(64) NOT NULL,
    ban_type VARCHAR(32) NOT NULL,  -- 'mute'
    duration_seconds INT NOT NULL,
    reason TEXT NOT NULL,
    operator_id INT NOT NULL,
    operator_name VARCHAR(64) NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    expires_at TIMESTAMPTZ,
    lifted_at TIMESTAMPTZ,
    lifted_by INT,
    lift_reason TEXT
);

CREATE INDEX idx_character_bans_char ON game.character_bans(character_id);
CREATE INDEX idx_character_bans_expires ON game.character_bans(expires_at) WHERE expires_at IS NOT NULL;
