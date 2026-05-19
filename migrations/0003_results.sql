-- Migration: 0003_results
-- Creates the results table.

BEGIN;

CREATE TABLE IF NOT EXISTS results (
    id               TEXT    NOT NULL PRIMARY KEY,
    run_id           TEXT    NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
    module_id        TEXT    NOT NULL,
    module_version   TEXT    NOT NULL DEFAULT '',
    timestamp        TEXT    NOT NULL DEFAULT (datetime('now')),
    mode             TEXT    NOT NULL DEFAULT 'single_shot',
    status           TEXT    NOT NULL DEFAULT 'pass'
        CHECK (status IN ('pass', 'fail', 'error', 'unknown')),
    error_message    TEXT,
    payload_bytes    BLOB,
    payload_type_url TEXT    NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS results_run_ts_idx    ON results (run_id, timestamp);
CREATE INDEX IF NOT EXISTS results_module_ts_idx ON results (module_id, timestamp);
CREATE INDEX IF NOT EXISTS results_status_idx    ON results (status);

INSERT INTO schema_migrations (version) VALUES ('0003_results')
    ON CONFLICT (version) DO NOTHING;

COMMIT;
