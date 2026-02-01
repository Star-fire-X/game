-- skill templates (placeholder)
CREATE TABLE IF NOT EXISTS skill_templates (
    id SERIAL PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    max_level INT DEFAULT 10,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
