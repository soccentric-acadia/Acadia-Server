-- Migration: 0005_ota
-- Creates the ota_upgrades table.

BEGIN;

CREATE TABLE IF NOT EXISTS ota_upgrades (
    id                TEXT    NOT NULL PRIMARY KEY,
    artifact_path     TEXT    NOT NULL,
    artifact_hash     TEXT    NOT NULL,
    artifact_size     INTEGER NOT NULL DEFAULT 0,
    from_version      TEXT    NOT NULL DEFAULT '',
    to_version        TEXT    NOT NULL DEFAULT '',
    status            TEXT    NOT NULL DEFAULT 'uploaded'
        CHECK (status IN (
            'uploaded', 'pending', 'pushing', 'verifying',
            'rebooting', 'completed', 'failed', 'rolled_back'
        )),
    progress          INTEGER NOT NULL DEFAULT 0 CHECK (progress BETWEEN 0 AND 100),
    started_at        TEXT,
    ended_at          TEXT,
    error_message     TEXT,
    rolled_back_from  TEXT    REFERENCES ota_upgrades(id) ON DELETE SET NULL,
    created_at        TEXT    NOT NULL DEFAULT (datetime('now')),
    created_by        TEXT    REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS ota_upgrades_status_idx     ON ota_upgrades (status);
CREATE INDEX IF NOT EXISTS ota_upgrades_created_at_idx ON ota_upgrades (created_at DESC);

INSERT INTO schema_migrations (version) VALUES ('0005_ota')
    ON CONFLICT (version) DO NOTHING;

COMMIT;
