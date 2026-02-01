-- monsters (placeholder)
CREATE TABLE IF NOT EXISTS monsters (
    id SERIAL PRIMARY KEY,
    name VARCHAR(64) NOT NULL,
    level INT DEFAULT 1,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
