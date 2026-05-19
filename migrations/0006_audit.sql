-- Migration: 0006_audit
-- Creates the audit_log table.

BEGIN;

CREATE TABLE IF NOT EXISTS audit_log (
    id          TEXT NOT NULL PRIMARY KEY,
    user_id     TEXT REFERENCES users(id) ON DELETE SET NULL,
    action      TEXT NOT NULL,
    target_type TEXT NOT NULL DEFAULT '',
    target_id   TEXT NOT NULL DEFAULT '',
    metadata    TEXT NOT NULL DEFAULT '{}',
    created_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS audit_log_user_idx       ON audit_log (user_id) WHERE user_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS audit_log_created_at_idx ON audit_log (created_at DESC);
CREATE INDEX IF NOT EXISTS audit_log_action_idx     ON audit_log (action);
CREATE INDEX IF NOT EXISTS audit_log_target_idx     ON audit_log (target_type, target_id);

INSERT INTO schema_migrations (version) VALUES ('0006_audit')
    ON CONFLICT (version) DO NOTHING;

COMMIT;
