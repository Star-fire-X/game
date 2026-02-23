-- Create mails table for mailbox subsystem.
CREATE TABLE IF NOT EXISTS mails (
  id BIGSERIAL PRIMARY KEY,
  from_id BIGINT NOT NULL,
  to_id BIGINT NOT NULL,
  subject VARCHAR(128) NOT NULL,
  content TEXT NOT NULL,
  gold INTEGER NOT NULL DEFAULT 0,
  items JSONB NOT NULL DEFAULT '[]'::jsonb,
  is_read BOOLEAN NOT NULL DEFAULT FALSE,
  claimed BOOLEAN NOT NULL DEFAULT FALSE,
  send_time TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  expire_time TIMESTAMPTZ NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_mails_to_id ON mails (to_id);
CREATE INDEX IF NOT EXISTS idx_mails_expire_time ON mails (expire_time);
