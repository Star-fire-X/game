-- ground items (placeholder)
CREATE TABLE IF NOT EXISTS ground_items (
    id BIGSERIAL PRIMARY KEY,
    map_id INT NOT NULL,
    pos_x INT NOT NULL,
    pos_y INT NOT NULL,
    item_template_id INT NOT NULL,
    instance_id BIGINT NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
