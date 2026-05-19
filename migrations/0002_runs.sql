-- Migration: 0002_runs
-- Creates test_configs, templates, and runs tables.

BEGIN;

-- ── Test Configurations ───────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS test_configs (
    id               TEXT    NOT NULL PRIMARY KEY,
    name             TEXT    NOT NULL,
    description      TEXT    NOT NULL DEFAULT '',
    mode             TEXT    NOT NULL DEFAULT 'single_shot'
        CHECK (mode IN ('single_shot', 'long_term', 'monitoring', 'event_based')),
    common_config    TEXT    NOT NULL DEFAULT '{}',
    modules          TEXT    NOT NULL DEFAULT '[]',
    created_at       TEXT    NOT NULL DEFAULT (datetime('now')),
    created_by       TEXT    REFERENCES users(id) ON DELETE SET NULL,
    is_archived      INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS test_configs_created_by_idx ON test_configs (created_by);
CREATE INDEX IF NOT EXISTS test_configs_archived_idx   ON test_configs (is_archived);

-- ── Templates ─────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS templates (
    id               TEXT    NOT NULL PRIMARY KEY,
    name             TEXT    NOT NULL,
    description      TEXT    NOT NULL DEFAULT '',
    mode             TEXT    NOT NULL DEFAULT 'single_shot'
        CHECK (mode IN ('single_shot', 'long_term', 'monitoring', 'event_based')),
    common_config    TEXT    NOT NULL DEFAULT '{}',
    modules          TEXT    NOT NULL DEFAULT '[]',
    created_at       TEXT    NOT NULL DEFAULT (datetime('now')),
    created_by       TEXT    REFERENCES users(id) ON DELETE SET NULL,
    is_archived      INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS templates_created_by_idx ON templates (created_by);
CREATE INDEX IF NOT EXISTS templates_archived_idx   ON templates (is_archived);

-- ── Runs ──────────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS runs (
    id                   TEXT    NOT NULL PRIMARY KEY,
    name                 TEXT    NOT NULL DEFAULT '',
    description          TEXT    NOT NULL DEFAULT '',
    test_config_snapshot TEXT    NOT NULL DEFAULT '{}',
    mode                 TEXT    NOT NULL DEFAULT 'single_shot',
    status               TEXT    NOT NULL DEFAULT 'queued'
        CHECK (status IN ('queued','running','paused','completed','failed','cancelled')),
    started_at           TEXT,
    ended_at             TEXT,
    error_message        TEXT,
    tags                 TEXT    NOT NULL DEFAULT '[]',
    notes                TEXT    NOT NULL DEFAULT '',
    baseline_flag        INTEGER NOT NULL DEFAULT 0,
    attached_files       TEXT    NOT NULL DEFAULT '[]',
    created_at           TEXT    NOT NULL DEFAULT (datetime('now')),
    created_by           TEXT    REFERENCES users(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS runs_status_idx     ON runs (status);
CREATE INDEX IF NOT EXISTS runs_started_at_idx ON runs (started_at);
CREATE INDEX IF NOT EXISTS runs_created_by_idx ON runs (created_by);
CREATE INDEX IF NOT EXISTS runs_mode_idx       ON runs (mode);

CREATE UNIQUE INDEX IF NOT EXISTS runs_baseline_mode_idx
    ON runs (mode) WHERE baseline_flag = 1;

-- ── Schedules ─────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS schedules (
    id              TEXT    NOT NULL PRIMARY KEY,
    template_id     TEXT    NOT NULL REFERENCES templates(id) ON DELETE CASCADE,
    cron_expression TEXT,
    run_at          TEXT,
    enabled         INTEGER NOT NULL DEFAULT 1,
    last_run_at     TEXT,
    next_run_at     TEXT,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now')),
    created_by      TEXT    REFERENCES users(id) ON DELETE SET NULL,
    CONSTRAINT schedule_type_check CHECK (
        (cron_expression IS NOT NULL AND run_at IS NULL) OR
        (cron_expression IS NULL     AND run_at IS NOT NULL)
    )
);

CREATE INDEX IF NOT EXISTS schedules_template_idx    ON schedules (template_id);
CREATE INDEX IF NOT EXISTS schedules_next_run_at_idx ON schedules (next_run_at) WHERE enabled = 1;

INSERT INTO schema_migrations (version) VALUES ('0002_runs')
    ON CONFLICT (version) DO NOTHING;

COMMIT;

COMMIT;

-- DOWN:
-- DROP TABLE IF EXISTS schedules;
-- DROP TABLE IF EXISTS runs;
-- DROP TABLE IF EXISTS templates;
-- DROP TABLE IF EXISTS test_configs;
