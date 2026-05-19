-- Migration 0009: app_settings key-value store.

BEGIN;

CREATE TABLE IF NOT EXISTS app_settings (
    key        TEXT NOT NULL PRIMARY KEY,
    value      TEXT NOT NULL,
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

INSERT INTO app_settings (key, value)
VALUES
    ('retention_days',  '90'),
    ('max_run_results', '0')
ON CONFLICT (key) DO NOTHING;

INSERT INTO schema_migrations (version) VALUES ('0009_app_settings')
    ON CONFLICT (version) DO NOTHING;

COMMIT;
