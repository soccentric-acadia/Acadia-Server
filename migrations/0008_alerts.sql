-- Migration 0008: alert_rules table for threshold-based alerting.

BEGIN;

CREATE TABLE IF NOT EXISTS alert_rules (
    id           TEXT    NOT NULL PRIMARY KEY,
    module_id    TEXT    NOT NULL,
    field_path   TEXT    NOT NULL,
    operator     TEXT    NOT NULL CHECK (operator IN ('>', '<', '>=', '<=', '==')),
    threshold    REAL    NOT NULL,
    severity     TEXT    NOT NULL DEFAULT 'warning' CHECK (severity IN ('info', 'warning', 'critical')),
    enabled      INTEGER NOT NULL DEFAULT 1,
    created_by   TEXT    REFERENCES users(id) ON DELETE SET NULL,
    created_at   TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at   TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS alert_rules_module_idx  ON alert_rules (module_id);
CREATE INDEX IF NOT EXISTS alert_rules_enabled_idx ON alert_rules (enabled) WHERE enabled = 1;

INSERT INTO schema_migrations (version) VALUES ('0008_alerts')
    ON CONFLICT (version) DO NOTHING;

COMMIT;
