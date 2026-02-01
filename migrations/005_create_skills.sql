-- 角色技能表
CREATE TABLE IF NOT EXISTS character_skills (
    id BIGSERIAL PRIMARY KEY,
    character_id BIGINT NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    skill_id INT NOT NULL,
    level INT DEFAULT 1,
    experience INT DEFAULT 0,
    UNIQUE (character_id, skill_id)
);

CREATE INDEX idx_skills_char ON character_skills(character_id);
