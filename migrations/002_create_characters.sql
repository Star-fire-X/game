-- 角色表（拆分JSON后）
CREATE TABLE IF NOT EXISTS characters (
    id BIGSERIAL PRIMARY KEY,
    account_id BIGINT NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name VARCHAR(24) UNIQUE NOT NULL,
    char_class SMALLINT NOT NULL,
    gender SMALLINT NOT NULL DEFAULT 0,
    level INT DEFAULT 1,
    experience INT DEFAULT 0,
    hp INT NOT NULL,
    max_hp INT NOT NULL,
    mp INT NOT NULL,
    max_mp INT NOT NULL,
    attack INT NOT NULL,
    defense INT NOT NULL,
    magic_attack INT NOT NULL,
    magic_defense INT NOT NULL,
    speed INT NOT NULL,
    gold BIGINT DEFAULT 0,
    map_id INT DEFAULT 1,
    pos_x INT DEFAULT 100,
    pos_y INT DEFAULT 100,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    last_login TIMESTAMPTZ,
    CONSTRAINT name_length CHECK (LENGTH(name) >= 2)
);

CREATE INDEX idx_characters_account ON characters(account_id);
CREATE INDEX idx_characters_name ON characters(name);
CREATE INDEX idx_characters_map ON characters(map_id);
