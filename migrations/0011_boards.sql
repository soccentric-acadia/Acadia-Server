-- migrations/0011_boards.sql — Client board lifecycle state (task 91).
-- HelloReport registers a board; StateReport updates health/active-run state.
-- Persisted durably so REST health views reflect reality, and reconnect/heartbeat
-- updates are idempotent.

CREATE TABLE IF NOT EXISTS boards (
    id                  TEXT PRIMARY KEY,      -- board_id from HelloReport
    client_version      TEXT NOT NULL DEFAULT '',
    first_seen_at       TEXT NOT NULL DEFAULT '',   -- ISO-8601 UTC
    last_seen_at        TEXT NOT NULL DEFAULT '',   -- last StateReport/heartbeat
    overall_state       INTEGER NOT NULL DEFAULT 0, -- RunState enum
    last_spool_bytes    INTEGER NOT NULL DEFAULT 0,
    last_spool_cap      INTEGER NOT NULL DEFAULT 0,
    last_spool_pressure REAL NOT NULL DEFAULT 0,
    active_run_id       TEXT NOT NULL DEFAULT '',
    active_run_state    INTEGER NOT NULL DEFAULT 0,
    last_active_seq     INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_boards_last_seen ON boards(last_seen_at);
