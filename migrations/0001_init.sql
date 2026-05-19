-- Migration: 0001_init
-- Creates core identity tables: users, api_tokens, board, schema_migrations.

BEGIN;

-- ── Migration tracking ────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     TEXT NOT NULL PRIMARY KEY,
    applied_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

-- ── Users ─────────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS users (
    id            TEXT    NOT NULL PRIMARY KEY,
    username      TEXT    NOT NULL UNIQUE,
    password_hash TEXT    NOT NULL,
    created_at    TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at    TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE UNIQUE INDEX IF NOT EXISTS users_username_idx ON users (username);

-- ── API Tokens ────────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS api_tokens (
    id           TEXT NOT NULL PRIMARY KEY,
    user_id      TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    name         TEXT NOT NULL,
    token_hash   TEXT NOT NULL UNIQUE,
    last_used_at TEXT,
    created_at   TEXT NOT NULL DEFAULT (datetime('now')),
    revoked_at   TEXT
);

CREATE INDEX IF NOT EXISTS api_tokens_user_idx ON api_tokens (user_id);
CREATE INDEX IF NOT EXISTS api_tokens_hash_idx ON api_tokens (token_hash);

-- ── Board (single-row enforced by unique index on constant) ───────────────────
CREATE TABLE IF NOT EXISTS board (
    id                   TEXT    NOT NULL PRIMARY KEY,
    name                 TEXT    NOT NULL,
    hostname             TEXT,
    model                TEXT,
    os_version           TEXT,
    hal_version          TEXT,
    client_version       TEXT,
    tags                 TEXT    NOT NULL DEFAULT '{}',
    hal_module_inventory TEXT    NOT NULL DEFAULT '[]',
    first_seen           TEXT    NOT NULL DEFAULT (datetime('now')),
    last_seen            TEXT    NOT NULL DEFAULT (datetime('now')),
    status               TEXT    NOT NULL DEFAULT 'offline'
        CHECK (status IN ('online', 'offline'))
);

CREATE UNIQUE INDEX IF NOT EXISTS board_single_row_idx ON board ((1));

INSERT INTO schema_migrations (version) VALUES ('0001_init')
    ON CONFLICT (version) DO NOTHING;

COMMIT;
