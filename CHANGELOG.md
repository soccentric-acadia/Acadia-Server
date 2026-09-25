# Changelog

## 2026-09-25 — Task 016: Ingest correctness fixes (VB-6) + a severe data-loss bug

- **Parent task:** `soccentric-acadia/Acadia` `tasks/016-ingest-correctness-fixes/`.
- **User-visible behaviour:** result ingest is now correct. Fixed:
  - **SRV-045** (ack race): each sample's pending-ack slot is registered
    under `ack_mu` before it is enqueued, not after.
  - **SRV-046** (empty batches never acked): `handle_result_batch` acks
    immediately when a batch ends up with zero enqueued samples.
  - **SRV-047** (failed ingest still acked): a failed
    `ingest_test_result` now withholds the ack instead of acking data that
    was never persisted.
  - **SRV-049** (concurrent `stream->Write`): every ack now routes through a
    new `send_ack` helper, serializing with `ClientRegistry::SendControl`.
  - **SRV-050** (wrong/early finalization): the terminal batch's actual
    `RunState` is captured and applied only after the writer thread drains,
    via a new, unit-tested `terminal_run_state_to_status` function.
  - **New, not previously tracked (`improvements.md` SRV-151):** the
    pre-existing duplicate-batch check was keyed identically for every
    sample in one network batch — only the first sample of any
    multi-sample batch was ever persisted. Moved the dedupe decision to
    once per network batch, before enqueueing.
  - **New, unrelated to VB-6 (`improvements.md` SRV-152):** fixed a
    pre-existing compile break — `StateReport::spool_pressure()` doesn't
    exist in `client_service.proto`.
- **Key files:** `src/grpc_server/ClientServiceImpl.{h,cc}`,
  `src/grpc_server/ClientRegistry.{h,cc}` (doc comment only),
  `tests/unit/grpc/ClientServiceLogicTest.cc` (new).
- **Validation:** real build + test run — `ninja -C build` clean, 88/88 unit
  tests passing (83 pre-existing + 5 new). First Acadia-Server change in
  this pass validated with an actual toolchain rather than diff review.

## 2026-09-25 — Task 012: Live-run view contract fix (VB-5, DSH-001, SRV-141)

- **Parent task:** `soccentric-acadia/Acadia` `tasks/012-live-run-view-contract-fix/`.
- **User-visible behaviour:** none in the API surface — same route, method,
  and event names. The one-time initial `state` SSE event on
  `GET /api/v1/runs/{id}/events` now carries the full run row (via the
  existing `run_to_json`) instead of just `{run_id, status}`, so the
  Dashboard's live-run view has enough to render immediately on connect.
- **Key files:** `src/rest_api/controllers/RunsController.cc`.
- **Validation:** no build toolchain available in this session; validated by
  pattern-matching against the identical `Json::StreamWriterBuilder` usage
  already in the same function — see the parent task's `validation.md`.

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
