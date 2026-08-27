# Changelog

All notable changes to Acadia-Server are documented here. Keep a Changelog;
versioning: SemVer (`project()` version in CMakeLists.txt).

## [Unreleased]

### Added
- Token auth on the ClientService stream: `x-acadia-stream-token` metadata
  checked against `ACADIA_STREAM_TOKEN` before any state is touched; 5 unit
  tests for the pure auth function (X2-003).
- Migration `0013_ota_board_idx.sql`: indexes `ota_upgrades.board_id` for the
  per-board OTA dispatch path (SRV-021).
- Real-tree migration integration test: all migrations apply on a fresh
  Postgres and are idempotent on re-run (SRV-020).

### Fixed
- MigrationRunner: comment-prefixed SQL chunks were silently DROPPED by the
  statement splitter (fresh-DB startup failure: `relation "users" does not
  exist`); the runner also strips migration files' own BEGIN/COMMIT so it
  owns the transaction boundary (SRV-020).

### Changed
- Makefile default target is the x86 server build; ARM64 target kept as
  `docker-build` (SRV-001).
- `make version` target (CONV-004).
- Duplicate `test/` tree consolidated into `tests/legacy` (SRV-002).

[Unreleased]: https://github.com/soccentric-acadia/Acadia-Server/compare/master...develop
