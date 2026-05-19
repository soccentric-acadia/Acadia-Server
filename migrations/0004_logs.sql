-- Migration: 0004_logs
-- Creates log_entries, log_bookmarks, and saved_log_views tables.

BEGIN;

CREATE TABLE IF NOT EXISTS log_entries (
    id         TEXT NOT NULL PRIMARY KEY,
    source     TEXT NOT NULL DEFAULT 'app',
    severity   TEXT NOT NULL DEFAULT 'info'
        CHECK (severity IN ('trace', 'debug', 'info', 'warn', 'error', 'critical')),
    timestamp  TEXT NOT NULL DEFAULT (datetime('now')),
    message    TEXT NOT NULL DEFAULT '',
    metadata   TEXT NOT NULL DEFAULT '{}',
    run_id     TEXT REFERENCES runs(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS log_entries_ts_source_idx ON log_entries (timestamp DESC, source);
CREATE INDEX IF NOT EXISTS log_entries_run_idx       ON log_entries (run_id) WHERE run_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS log_entries_severity_idx  ON log_entries (severity, timestamp DESC);
CREATE INDEX IF NOT EXISTS log_entries_message_idx   ON log_entries (message);

-- ── Log Bookmarks ─────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS log_bookmarks (
    id         TEXT NOT NULL PRIMARY KEY,
    user_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    log_id     TEXT NOT NULL REFERENCES log_entries(id) ON DELETE CASCADE,
    note       TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE UNIQUE INDEX IF NOT EXISTS log_bookmarks_user_log_idx ON log_bookmarks (user_id, log_id);

-- ── Saved Log Views ───────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS saved_log_views (
    id         TEXT NOT NULL PRIMARY KEY,
    user_id    TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name       TEXT NOT NULL,
    filters    TEXT NOT NULL DEFAULT '{}',
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS saved_log_views_user_idx ON saved_log_views (user_id);

INSERT INTO schema_migrations (version) VALUES ('0004_logs')
    ON CONFLICT (version) DO NOTHING;

COMMIT;
