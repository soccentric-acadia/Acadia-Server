-- 0015_sessions.sql — persistent, DB-backed session tokens (SRV-016).
--
-- Drogon's in-memory session store is lost on restart and is not shared
-- across instances. Sessions now live in Postgres: the login handler mints
-- an opaque token, stores it here, presents it as an HttpOnly SameSite cookie
-- (`acadia_session`), and the AuthFilter validates it on every request.

BEGIN;

CREATE TABLE IF NOT EXISTS sessions (
    token      TEXT        NOT NULL PRIMARY KEY,   -- random 256-bit hex
    user_id    UUID        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at TIMESTAMPTZ NOT NULL,
    last_seen_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS sessions_user_idx ON sessions (user_id);

INSERT INTO schema_migrations (version) VALUES ('0015_sessions')
    ON CONFLICT (version) DO NOTHING;

COMMIT;

-- DOWN (for reference — not auto-applied):
-- DROP TABLE IF EXISTS sessions;
