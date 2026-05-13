# Server App Generation Prompt (C++, One-Shot Build)

You are building the **Server App** for an Independent Validation & Verification (IV&V) tool suite. This is one of five components. Read this entire document before writing code. Deliver the complete, working server in a single pass — no checkpoints, no clarification rounds. Where ambiguity exists, make the most defensible choice and document it under "Decisions Log" in the final README.

---

## 1. System context

The full suite has five components. You are only building #4.

1. **Yocto OS** — Raspberry Pi targets, supports OTA (swupdate). Done.
2. **HAL** — C++ library on target. 50+ modules (CPU, memory, peripherals, sensors). Four execution modes. Done.
3. **Client App** — C++ on target. gRPC to this Server App.
4. **Server App** — *what you are building*. C++ on x86 Linux. gRPC for the client, REST for the Web UI. Built-in Postgres database.
5. **Web UI App** — Next.js dashboard. REST to this Server App.

The system is purpose-built for **a single board at a time**.

The proto layer already exists in a sibling `protos/` directory:
- `protos/common.proto` — `ExecutionMode`, `TestStatus`, `ResultEnvelope`, `CommonConfig`, `MonitoringParams`, `EventCondition`.
- `protos/hal_service.proto` — single stable `HalService`. Module-specific payloads ride in `google.protobuf.Any`.
- `protos/modules/<module>.proto` — one per HAL module, each with `Config` and `Result`.
- `protos/registry/modules.yaml` — manifest of all modules.

**Do not modify the protos.** Consume them as-is.

---

## 2. Scope

The Server App must:

1. **gRPC server** — implement `HalService`. The on-target Client App connects, fetches config, executes tests, streams results back.
2. **Configuration store** — operators (via Web UI → REST) define configurations: module(s), mode, params, target. Server stores and serves to clients.
3. **Result ingestion** — receive `ResultEnvelope`s (unary for single-shot, server-streaming for long-term/monitoring/event-based). Persist as they arrive.
4. **Log ingestion** — receive kernel/syslog/app logs from the target. Separate path from test results.
5. **Persistence** — PostgreSQL. All runs, results, configs, logs, board inventory, audit log live here.
6. **REST API** — mirrors proto concepts. Web UI consumes this. OpenAPI 3 spec published.
7. **Target lifecycle** — track board online/offline, last-seen, OTA version, reported HAL module inventory.
8. **Run orchestration** — a "run" applies one config against the board. Lifecycle: queued → running → completed/failed/cancelled. Support cancel; support pause/resume where the mode permits.
9. **Templates & schedules** — store reusable configs (templates); support one-time and cron-style scheduled runs (server-side scheduler).
10. **OTA orchestration** — accept swupdate artifact upload, verify checksum/signature, push to board, track progress, record history, support rollback.
11. **Auth** — single local user (username/password), API tokens for headless access, audit log of significant actions.
12. **Notifications** — emit notifications on run complete and test failure (in-app via the REST surface; email as a server-side delivery mechanism — config-driven).

Out of scope: Web UI itself, HAL changes, proto changes.

---

## 3. Architectural decisions — already made, do not relitigate

- **Language**: C++20.
- **Build**: CMake 3.22+. Modern target-based CMake. `find_package` for all deps. Provide a `CMakePresets.json`.
- **Dependency management**: vcpkg (manifest mode, `vcpkg.json` checked in). Fallback to `FetchContent` only for deps not in vcpkg.
- **gRPC**: official `grpc` + `protobuf` from vcpkg. Use the **async API** for streaming RPCs to handle dozens of concurrent streams. Sync API acceptable for unary RPCs.
- **REST framework**: **Drogon** (async, coroutine-friendly, JSON via Jsoncpp, built-in ORM with PostgreSQL support, WebSocket-capable for live tailing).
- **Database**: PostgreSQL 15+. Drogon ORM for typical CRUD; raw SQL via Drogon's `DbClient` for complex queries.
- **Migrations**: SQL files under `migrations/`, applied via a small migration runner the server runs at startup (idempotent, tracked in a `schema_migrations` table).
- **JSON**: Jsoncpp (Drogon's default) for HTTP layer. Use protobuf's `JsonPrintOptions` / `JsonParseOptions` when round-tripping protos to JSON for REST.
- **Logging**: spdlog with the `async` sink, structured JSON output to stdout. Context-bound via MDC-like pattern (run_id, target_id, module_id).
- **Metrics**: `prometheus-cpp` exposing `/metrics`.
- **Testing**: GoogleTest + GoogleMock. Integration tests use a real Postgres via `testcontainers-cpp` or a `docker-compose` test fixture.
- **Concurrency**: gRPC async server on its own completion-queue-driven thread pool. Drogon on its own event loop. Both share a thread-safe service layer.
- **Result payload storage**: store `ResultEnvelope.payload` as raw bytes (`BYTEA`) plus `type_url` in a dedicated column. **Do not** explode each module's result into its own table.
- **Module registry**: load `protos/registry/modules.yaml` at startup via `yaml-cpp`. Validate every incoming `module_id`. Reject unknown modules with a clear gRPC status.
- **Streaming + backpressure**: every server-streaming handler persists each `ResultEnvelope` as it arrives. Use a bounded MPSC queue per stream feeding a DB writer; if the queue fills, propagate backpressure to the client by stalling reads — never drop, never OOM.
- **REST mirrors gRPC, it does not replace it.** Client App uses gRPC only. REST exists for the Web UI.
- **Single board**: the server tracks at most one board at a time. Multi-board is not a feature.

---

## 4. Required project layout

```
server/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  README.md
  .env.example
  migrations/
    0001_init.sql
    0002_runs.sql
    0003_results.sql
    0004_logs.sql
    0005_ota.sql
    0006_audit.sql
    ...
  protos/                          # symlink or copy reference — do NOT modify
  src/
    main.cpp
    config/
      Settings.h / .cc             # env-driven config, single struct
    logging/
      LoggingSetup.h / .cc
    registry/
      ModuleRegistry.h / .cc       # loads modules.yaml, exposes lookup API
    db/
      DbClient.h / .cc             # Drogon DbClient wrapper, pool config
      MigrationRunner.h / .cc
      repositories/
        TargetRepository.h / .cc
        TestConfigRepository.h / .cc
        RunRepository.h / .cc
        ResultRepository.h / .cc
        LogRepository.h / .cc
        TemplateRepository.h / .cc
        ScheduleRepository.h / .cc
        OtaRepository.h / .cc
        AuditRepository.h / .cc
        UserRepository.h / .cc
        ApiTokenRepository.h / .cc
    services/                      # business logic, shared between gRPC and REST
      TargetService.h / .cc
      ConfigService.h / .cc
      RunService.h / .cc
      ResultService.h / .cc
      LogService.h / .cc
      TemplateService.h / .cc
      ScheduleService.h / .cc
      OtaService.h / .cc
      AuthService.h / .cc
      NotificationService.h / .cc
      ModuleService.h / .cc
    grpc_server/
      HalServiceImpl.h / .cc       # implements HalService
      handlers/
        SingleShotHandler.h / .cc
        LongTermHandler.h / .cc
        MonitoringHandler.h / .cc
        EventBasedHandler.h / .cc
        StopHandler.h / .cc
        ListModulesHandler.h / .cc
        LogIngestHandler.h / .cc   # if defined in protos
      StreamBackpressure.h         # bounded queue helper
      GrpcServerRunner.h / .cc     # owns CQ, threads, lifecycle
    rest_api/
      DrogonAppFactory.h / .cc
      controllers/
        HealthController.h / .cc
        ModulesController.h / .cc
        TargetsController.h / .cc
        ConfigsController.h / .cc
        RunsController.h / .cc
        ResultsController.h / .cc
        LogsController.h / .cc
        TemplatesController.h / .cc
        SchedulesController.h / .cc
        OtaController.h / .cc
        ReportsController.h / .cc
        AuthController.h / .cc
        AuditController.h / .cc
        SettingsController.h / .cc
      filters/
        AuthFilter.h / .cc         # username/password session or API token
        AuditFilter.h / .cc
      openapi/
        Generator.h / .cc          # serves /openapi.json
    orchestration/
      RunManager.h / .cc           # lifecycle of a run
      Scheduler.h / .cc            # cron-style scheduled runs
      TargetHeartbeat.h / .cc      # online/offline tracking
      OtaOrchestrator.h / .cc      # OTA push + progress + rollback
    notifications/
      InAppNotifier.h / .cc
      EmailNotifier.h / .cc        # SMTP config-driven
    util/
      Uuid.h / .cc
      Time.h / .cc
      Hash.h / .cc                 # for OTA artifact verification
      ProtoJson.h / .cc            # proto <-> JSON helpers using protobuf util
      Errors.h / .cc
  generated/                       # protoc output (build dir, not committed)
  tests/
    unit/
      ...                          # mirror src/ structure
    integration/
      grpc/
      rest/
      db/
    fixtures/
  scripts/
    generate_protos.sh             # protoc invocation
    run_dev.sh
    seed_db.sh
  docker/
    Dockerfile                     # multi-stage, non-root, slim final
    docker-compose.yml             # server + postgres for local dev
```

---

## 5. Database schema — minimum tables

UUID primary keys throughout. `TIMESTAMPTZ` for all times.

- **users** — `id`, `username` (unique), `password_hash` (argon2id), `created_at`, `updated_at`.
- **api_tokens** — `id`, `user_id`, `name`, `token_hash`, `last_used_at`, `created_at`, `revoked_at`.
- **board** — single row enforced (or constraint to one). `id`, `name`, `hostname`, `model`, `os_version`, `hal_version`, `client_version`, `tags` (jsonb), `hal_module_inventory` (jsonb), `first_seen`, `last_seen`, `status`.
- **test_configs** — `id`, `name`, `description`, `mode`, `common_config` (jsonb), `modules` (jsonb: list of `{module_id, enabled, frequency, monitoring_params, event_condition, module_config_payload (base64), module_config_type_url, notes}`), `created_at`, `created_by`, `is_archived`.
- **templates** — same shape as test_configs but flagged as a template; reusable across runs.
- **runs** — `id`, `name`, `description`, `test_config_snapshot` (jsonb, frozen at start), `mode`, `status`, `started_at`, `ended_at`, `error_message`, `tags` (jsonb), `notes` (text), `baseline_flag` (bool), `attached_files` (jsonb: list of paths/blobs).
- **results** — `id`, `run_id`, `module_id`, `module_version`, `timestamp`, `mode`, `status`, `error_message`, `payload_bytes` (BYTEA), `payload_type_url`. Indexed on `(run_id, timestamp)` and `(module_id, timestamp)`.
- **log_entries** — `id`, `source` (kernel/syslog/app/...), `severity`, `timestamp`, `message`, `metadata` (jsonb), `run_id` (nullable, when correlated). Time-partitioned or aggressively indexed on `(timestamp, source)`.
- **schedules** — `id`, `template_id`, `cron_expression` (or `run_at` for one-time), `enabled`, `last_run_at`, `next_run_at`, `created_at`.
- **ota_upgrades** — `id`, `artifact_path`, `artifact_hash`, `from_version`, `to_version`, `status`, `started_at`, `ended_at`, `error_message`, `rolled_back_from` (nullable, self-ref).
- **audit_log** — `id`, `user_id` (nullable for system actions), `action`, `target_type`, `target_id`, `metadata` (jsonb), `created_at`.
- **schema_migrations** — `version`, `applied_at`.
- **notifications** — `id`, `kind`, `payload` (jsonb), `read_at` (nullable), `created_at`.

---

## 6. gRPC service — exact behavior

Read the actual `hal_service.proto` for signatures. Expected RPCs and behaviors:

- `RunSingleShot(SingleShotRequest) returns (ResultEnvelope)` — unary; server validates `module_id` against registry, dispatches via the client's view of the config, persists the returned result, returns the envelope to the caller.
- `StartLongTerm(LongTermRequest) returns (stream ResultEnvelope)` — server-streaming; each envelope persisted as it arrives, then re-emitted to any REST/WebSocket subscribers via `RunService`.
- `StartMonitoring(MonitoringRequest) returns (stream ResultEnvelope)` — same as above with `MonitoringParams` carried through.
- `StartEventBased(EventBasedRequest) returns (stream ResultEnvelope)` — same; expect lower-frequency, condition-triggered events.
- `Stop(StopRequest) returns (StopResponse)` — cancel an active run/stream.
- `ListModules(ListModulesRequest) returns (ListModulesResponse)` — return the registry contents.

If the Client App's protocol also includes log-ingest streaming (check `protos/`), implement it under `grpc_server/handlers/LogIngestHandler.cc` and route into `LogService`.

For each streaming handler, the contract is:
1. Validate `module_id` against `ModuleRegistry`. Reject with `INVALID_ARGUMENT` if unknown.
2. Open or join the corresponding `Run` in `RunManager`. Mark it running. Audit log entry.
3. For each incoming `ResultEnvelope`: enqueue into the per-stream bounded queue. The DB writer task drains the queue into `ResultRepository`. If the queue fills, pause reads from the stream until drained (apply backpressure).
4. On stream end (client closes, server cancels, error): finalize the run (status, ended_at), audit log entry, notify subscribers, emit notifications if configured.

---

## 7. REST API surface — minimum endpoints

Mirror proto concepts. All under `/api/v1`. Authenticated via session cookie (login flow) or `Authorization: Bearer <token>`.

**Health & meta**
- `GET /health/live`
- `GET /health/ready`
- `GET /openapi.json`
- `GET /metrics` (Prometheus, may be on a separate port)

**Auth**
- `POST /auth/login` — `{username, password}` → session cookie
- `POST /auth/logout`
- `POST /auth/change-password` — `{old_password, new_password}`
- `GET  /auth/me`

**API tokens**
- `GET    /tokens`
- `POST   /tokens` — `{name}` → returns plaintext token once
- `DELETE /tokens/{id}`

**Board**
- `GET    /board` — current board (or 404 if none)
- `POST   /board` — register the board (single-board guard: 409 if already exists)
- `PATCH  /board` — rename, tag
- `DELETE /board` — remove the board

**Modules**
- `GET /modules` — list from registry
- `GET /modules/{id}` — module metadata + schema descriptor

**Test configs**
- `POST   /configs`
- `GET    /configs`
- `GET    /configs/{id}`
- `PATCH  /configs/{id}`
- `DELETE /configs/{id}`

**Templates**
- Same shape as configs, under `/templates`
- `POST /templates/{id}/instantiate` → returns a draft config

**Runs**
- `POST   /runs` — `{config_id}` or `{template_id}` or inline `{config: {...}}`; starts the run
- `GET    /runs` — paginated; filters: status, mode, tag, date range, text search
- `GET    /runs/{id}` — run header + per-module summary (last result before end)
- `POST   /runs/{id}/cancel`
- `POST   /runs/{id}/pause`         (if mode supports)
- `POST   /runs/{id}/resume`        (if mode supports)
- `POST   /runs/{id}/notes`         — set/append notes
- `POST   /runs/{id}/tags`          — set tags
- `POST   /runs/{id}/baseline`      — mark as baseline (only one at a time per scope; document the scope)
- `POST   /runs/{id}/attachments`   — multipart upload
- `GET    /runs/{id}/attachments/{aid}`
- `DELETE /runs/{id}`
- `POST   /runs/{id}/rerun`         — start a new run with the same config snapshot

**Results**
- `GET /runs/{id}/results` — paginated; filters: module_id, status, since, until; `?since=<ts>` for incremental polling
- `GET /results/{id}` — single result with decoded payload (server pre-decodes the `Any` to JSON using the registry; falls back to base64+type_url if decode fails)
- `GET /runs/{a}/compare/{b}` — comparison summary (per module: changed, drifted, regressed, recovered)
- `GET /runs/{id}/results/export?format=csv|json`

**Trends & analytics**
- `GET /modules/{id}/trend?from=&to=&field=`     — module-level trend across runs
- `GET /modules/{id}/distribution?field=&runs=`  — histogram data
- `GET /modules/{id}/heatmap?from=&to=`          — run × time grid

**Logs**
- `GET /logs` — paginated; filters: source, severity, time range, free-text, run_id
- `GET /logs/stream` — SSE for live tail (Drogon WebSocket alternative acceptable; document choice)
- `GET /logs/export`
- `POST /logs/bookmarks`
- `GET  /logs/bookmarks`
- `DELETE /logs/bookmarks/{id}`
- `GET  /logs/views` / `POST /logs/views` — saved filters

**Schedules**
- `POST   /schedules`
- `GET    /schedules`
- `PATCH  /schedules/{id}` — pause/resume, edit cron
- `DELETE /schedules/{id}`
- `GET    /schedules/{id}/history`

**OTA**
- `POST  /ota/upload` — multipart; returns artifact id after checksum verification
- `POST  /ota/upgrade` — `{artifact_id, post_upgrade_template_id?}` → triggers push to board
- `GET   /ota/upgrades` — history
- `GET   /ota/upgrades/{id}` — status + progress
- `POST  /ota/upgrades/{id}/rollback`

**Reports**
- `POST /reports/runs/{id}` — `{format: pdf|html, template_id?}` → returns a download
- `GET  /reports/templates`
- `POST /reports/templates`
- `POST /reports/schedules` — scheduled email reports

**Notifications**
- `GET   /notifications` — paginated
- `POST  /notifications/{id}/read`
- `POST  /notifications/read-all`

**Audit**
- `GET /audit` — paginated, filterable

**Settings**
- `GET   /settings` — display, data retention, network, SMTP, etc.
- `PATCH /settings`

---

## 8. Operational requirements

- **Concurrency**: handle dozens of concurrent streams. gRPC async server with N completion-queue threads (N = hardware_concurrency by default, overridable). Drogon configured with sensible thread pool. Document defaults.
- **Backpressure**: bounded queues everywhere a producer and consumer have different rates. Document queue sizes via config. Never drop data silently.
- **Graceful shutdown**: SIGTERM/SIGINT → stop accepting new streams + new HTTP requests, drain in-flight work with a timeout, flush DB writer queues, close DB pool, exit. Document timeout.
- **Configuration**: 12-factor — every knob via env var, parsed once into a `Settings` struct at startup. Provide `.env.example`.
- **Logging**: structured JSON to stdout via spdlog. Include `run_id`, `target_id`, `module_id`, `request_id` in context.
- **Metrics**: Prometheus at `/metrics`. Minimum metrics: active gRPC streams, results ingested/sec, log entries ingested/sec, REST request latency histogram, DB pool stats, run state counts.
- **Health**: `/health/live` returns immediately; `/health/ready` checks DB reachability and registry loaded.
- **CORS**: configurable allowlist for the Web UI origin.
- **Security**: passwords hashed with argon2id. API tokens stored as hashes only. CSRF protection on session-cookie endpoints. Rate-limit `/auth/login`.
- **OTA artifact storage**: filesystem path under `/var/lib/ivv/ota/` by default, configurable. Verify SHA-256 (and signature if configured) before any upgrade can be triggered.

---

## 9. Testing requirements

- **Unit tests** for every repository, every service, the registry loader, every gRPC handler, the proto↔JSON converter, the migration runner, the scheduler.
- **Integration tests**:
  - Spin up Postgres (testcontainers-cpp or docker-compose fixture).
  - In-process gRPC server with a real channel.
  - Drogon test client for REST.
  - End-to-end: launch a run, stream a sequence of results, verify persistence, query via REST, cancel mid-stream, verify state.
- **Streaming tests**: slow-consumer scenario must apply backpressure, not OOM, not drop.
- **Coverage target**: 80% on `src/` excluding `generated/`.

---

## 10. Build, run, deploy

- `CMakePresets.json` provides at least `debug`, `release`, `test`, `asan`.
- `cmake --preset release && cmake --build --preset release` produces a single executable `ivv_server`.
- `scripts/generate_protos.sh` runs `protoc` against `../protos/` and writes to `build/generated/`. CMake invokes this automatically at configure time.
- `docker/docker-compose.yml` brings up the server + Postgres locally for dev.
- `docker/Dockerfile`: multi-stage, build stage uses full toolchain + vcpkg, final stage is `debian:stable-slim` (or `gcr.io/distroless/cc-debian12`), non-root user, ~100 MB target image. Health check defined.
- `migrations/` applied automatically at server startup; idempotent; failure aborts startup with a clear log line.
- README must cover: prerequisites, local dev workflow, env vars, how to add a new REST endpoint, how to add a new gRPC handler, how to add a new module repository, how to write a migration, how to run tests, how the streaming backpressure works.

---

## 11. Hard rules

- Do not modify the proto files, the HAL, or anything outside `server/`.
- Do not invent RPC signatures — read the actual protos.
- Do not commit `generated/` proto output as authoritative source.
- Do not reuse database IDs, proto field tags, or `module_id` values — treat all as permanent.
- Do not explode module results into per-module tables. Single `results` table with payload bytes + type_url.
- Do not write a switch-case over `module_id` anywhere — dispatch is registry-driven.
- Do not mix sync and async DB clients on the same code path.
- Do not silently degrade if a feature in this spec is impossible given the actual protos — document the conflict in the README's "Decisions Log".
- Drogon and gRPC must not share thread pools or block each other. Document the threading model.
- All endpoints in Section 7 must be implemented. Missing endpoints are not acceptable.
- All migrations must be additive and reversible (provide a `down` script or document why not).

---

## 12. Deliverables — one-shot

Produce in a single pass:

1. Complete source tree as per Section 4.
2. All migrations.
3. CMake + vcpkg manifest + presets.
4. `protoc` integration that generates from the existing `../protos/`.
5. Full unit + integration test suite passing.
6. Dockerfile + docker-compose for local dev.
7. `.env.example`.
8. README covering everything in Section 10 plus a **"Decisions Log"** section listing every ambiguity encountered and how it was resolved.
9. `scripts/run_dev.sh` to bring up everything locally.

The server must be runnable end-to-end: `docker-compose up` brings up Postgres + server; `/health/ready` returns 200; `/openapi.json` returns a valid OpenAPI 3 document; gRPC connections accepted on the configured port; a sample `ListModules` call succeeds against the loaded registry.

Begin now. Build the entire server in this pass.