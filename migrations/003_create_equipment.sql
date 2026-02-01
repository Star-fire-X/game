-- 装备表
CREATE TABLE IF NOT EXISTS character_equipment (
    id BIGSERIAL PRIMARY KEY,
    character_id BIGINT NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    slot SMALLINT NOT NULL,
    item_template_id INT NOT NULL,
    instance_id BIGINT NOT NULL,
    durability INT DEFAULT 100,
    enhancement_level SMALLINT DEFAULT 0,
    UNIQUE (character_id, slot)
);

CREATE INDEX idx_equipment_char ON character_equipment(character_id);
