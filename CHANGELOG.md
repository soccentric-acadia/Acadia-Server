# Changelog

## 2026-09-25 — Task 011: Ship the module registry, fail fast when it's missing (VB-4, SRV-106)

- **Parent task:** `soccentric-acadia/Acadia` `tasks/011-server-module-registry-deployment/`.
- **User-visible behaviour:** `/api/v1/modules` is no longer permanently
  empty — the production image now ships a vendored HAL module registry.
  A deployment with a misconfigured `ACADIA_REGISTRY_PATH` now fails to
  start (loudly) instead of silently running with no module validation.
- **Key files:**
  - `protos/registry/modules.yaml` (new) — vendored from
    `Acadia-HAL@7184d94` (`master`); see the file's own header for
    provenance and the drift caveat.
  - `docker/Dockerfile.production` — copies the registry to
    `/etc/acadia/modules.yaml`, sets `ACADIA_REGISTRY_PATH` there by
    default.
  - `src/main.cpp` — missing-but-configured registry path is now a fatal
    startup error (`spdlog::critical` + exit 1), matching the existing
    DB-init/migration failure pattern. `ACADIA_REGISTRY_PATH=""` remains a
    documented way to opt into running without a registry.
- **Validation:** diff-level review only; no build toolchain available in
  this session for this Drogon/gRPC/Postgres service — see the parent
  task's `validation.md`.

## 2026-09-21 — Root-agent feature workflow guard (task 006)

- Added a canonical instruction requiring leaf-started agents to refuse feature
  changes and direct the user to the Acadia parent root.

## 2026-09-18 — Agent documentation and task workflow standardization

- Added canonical `AGENTS.md` and `CLAUDE.md` pointers plus a concise canonical
  agent document with Docker server build and test instructions.
- Added ignored local `Plan.md` and `Task.md` workflow artifacts.
- Verified the required document shape and the declared `make build` and
  `make test` targets.

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
