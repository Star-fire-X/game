-- items template (placeholder)
CREATE TABLE IF NOT EXISTS items (
    id SERIAL PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    type SMALLINT NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
