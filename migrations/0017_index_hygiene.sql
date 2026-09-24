-- 0017_index_hygiene.sql — token hash uniqueness, remove duplicate user index.
--
-- SRV-077: api_tokens_hash_idx was NON-UNIQUE while the column was UNIQUE —
-- replace it with a single UNIQUE index so duplicate token hashes are
-- rejected by the schema, not by chance.
-- SRV-078: users.username already carries UNIQUE; the extra
-- users_username_idx was pure overhead — drop it.

BEGIN;

DROP INDEX IF EXISTS api_tokens_hash_idx;
CREATE UNIQUE INDEX IF NOT EXISTS api_tokens_hash_unique_idx ON api_tokens (token_hash);

DROP INDEX IF EXISTS users_username_idx;

INSERT INTO schema_migrations (version) VALUES ('0017_index_hygiene')
    ON CONFLICT (version) DO NOTHING;

COMMIT;

-- DOWN (for reference — not auto-applied):
-- DROP INDEX IF EXISTS api_tokens_hash_unique_idx;
-- CREATE INDEX IF NOT EXISTS api_tokens_hash_idx ON api_tokens (token_hash);
-- CREATE UNIQUE INDEX IF NOT EXISTS users_username_idx ON users (username);
