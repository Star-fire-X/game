-- 背包表
CREATE TABLE IF NOT EXISTS character_inventory (
    id BIGSERIAL PRIMARY KEY,
    character_id BIGINT NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    slot INT NOT NULL,
    item_template_id INT NOT NULL,
    instance_id BIGINT NOT NULL,
    quantity INT DEFAULT 1,
    durability INT DEFAULT 100,
    enhancement_level SMALLINT DEFAULT 0,
    UNIQUE (character_id, slot)
);

CREATE INDEX idx_inventory_char ON character_inventory(character_id);
