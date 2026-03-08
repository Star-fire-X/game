-- Create auction listings table for auction house persistence and recovery.
CREATE TABLE IF NOT EXISTS auction_listings (
  listing_id BIGSERIAL PRIMARY KEY,
  seller_character_id BIGINT NOT NULL,
  buyer_character_id BIGINT NOT NULL DEFAULT 0,
  item_id INTEGER NOT NULL,
  item_count INTEGER NOT NULL CHECK (item_count > 0),
  unit_price INTEGER NOT NULL CHECK (unit_price > 0),
  status SMALLINT NOT NULL DEFAULT 0 CHECK (status BETWEEN 0 AND 3),
  item_returned BOOLEAN NOT NULL DEFAULT FALSE,
  version BIGINT NOT NULL DEFAULT 0,
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  expires_at TIMESTAMPTZ NOT NULL,
  sold_at TIMESTAMPTZ,
  cancelled_at TIMESTAMPTZ,
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_auction_status_expires
  ON auction_listings (status, expires_at);

CREATE INDEX IF NOT EXISTS idx_auction_seller_status
  ON auction_listings (seller_character_id, status);

CREATE INDEX IF NOT EXISTS idx_auction_pending_return
  ON auction_listings (seller_character_id, item_returned, status);
