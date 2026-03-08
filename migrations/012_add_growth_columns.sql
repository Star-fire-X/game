-- Migration 012: Add growth system columns
-- Supports: experience type upgrade, body_luck, bonus points, weight limits

ALTER TABLE characters ALTER COLUMN experience TYPE BIGINT;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS body_luck INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS bonus_remaining INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS bonus_dc INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS bonus_mc INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS bonus_sc INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS bonus_ac INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS bonus_mac INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS bonus_hp INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS bonus_mp INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS bonus_hit INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS bonus_speed INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS max_weight INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS max_wear_weight INT DEFAULT 0;
ALTER TABLE characters ADD COLUMN IF NOT EXISTS max_hand_weight INT DEFAULT 0;
