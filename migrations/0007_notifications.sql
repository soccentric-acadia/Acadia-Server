-- Migration: 0007_notifications
-- Creates the notifications table.

BEGIN;

CREATE TABLE IF NOT EXISTS notifications (
    id         TEXT NOT NULL PRIMARY KEY,
    kind       TEXT NOT NULL,
    payload    TEXT NOT NULL DEFAULT '{}',
    read_at    TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS notifications_read_at_idx    ON notifications (read_at) WHERE read_at IS NULL;
CREATE INDEX IF NOT EXISTS notifications_created_at_idx ON notifications (created_at DESC);
CREATE INDEX IF NOT EXISTS notifications_kind_idx       ON notifications (kind);

INSERT INTO schema_migrations (version) VALUES ('0007_notifications')
    ON CONFLICT (version) DO NOTHING;

COMMIT;
