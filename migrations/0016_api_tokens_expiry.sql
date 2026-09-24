-- 0016_api_tokens_expiry.sql — API token expiry and scopes (SRV-013).
--
-- Tokens were immortal and scope-less. Going forward:
--   expires_at  NULL (unlimited, backward compatible) or a point in time;
--               find_by_hash rejects tokens past expires_at in SQL.
--   scopes      comma-separated scope list ('read', 'write', 'admin');
--               '' = legacy full-access token (backward compatible).

BEGIN;

ALTER TABLE api_tokens ADD COLUMN IF NOT EXISTS expires_at TIMESTAMPTZ;
ALTER TABLE api_tokens ADD COLUMN IF NOT EXISTS scopes TEXT NOT NULL DEFAULT '';

-- Fast expiry pruning without a full scan on every auth check.
CREATE INDEX IF NOT EXISTS api_tokens_expires_idx ON api_tokens (expires_at) WHERE expires_at IS NOT NULL;

INSERT INTO schema_migrations (version) VALUES ('0016_api_tokens_expiry')
    ON CONFLICT (version) DO NOTHING;

COMMIT;

-- DOWN (for reference — not auto-applied):
-- ALTER TABLE api_tokens DROP COLUMN IF EXISTS expires_at;
-- ALTER TABLE api_tokens DROP COLUMN IF EXISTS scopes;
