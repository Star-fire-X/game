-- 账号表
CREATE TABLE IF NOT EXISTS accounts (
    id BIGSERIAL PRIMARY KEY,
    username VARCHAR(64) UNIQUE NOT NULL,
    password_hash VARCHAR(128) NOT NULL,
    email VARCHAR(128),
    created_at TIMESTAMPTZ DEFAULT NOW(),
    last_login TIMESTAMPTZ,
    banned BOOLEAN DEFAULT FALSE,
    CONSTRAINT username_length CHECK (LENGTH(username) >= 2)
);

CREATE INDEX idx_accounts_username ON accounts(username);
CREATE INDEX idx_accounts_created_at ON accounts(created_at);
