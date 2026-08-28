# Acadia Server

The **Acadia Server** is the central orchestration and data-collection backend for the Acadia
IV&V (Independent Validation & Verification) tool suite. It exposes:

- A **gRPC `ClientService` stream** for the embedded Client App to receive run
  configuration/control and submit results, logs, ACKs, and state reports. The
  Client dials the Server; the Server does not initiate a connection to the
  Client. The Server's separate `HalService` facade is not the Client result
  submission channel.
- A **REST/HTTP API** (`/api/v1`) for the Web UI and external tooling.
- A **PostgreSQL** backend with versioned migrations.
- **OTA firmware upgrade** orchestration.
- **Prometheus metrics** (optional).

---

## 🚀 Quick Start (Docker — Recommended)

**The easiest and most reliable way** to get started on any machine:

```bash
cd docker
./setup.sh
```

This will:
1. Check Docker prerequisites
2. Create `.env` from `.env.example`
3. Build the Docker image for your platform (auto-detected)
4. Start PostgreSQL and dev environment
5. You're ready to build!

Then enter the container and build:
```bash
docker exec -it acadia-dev bash
cmake --preset debug && cmake --build build/debug
```

### Alternative: VS Code Dev Container

Open this project in VS Code and:
1. Install the "Dev Containers" extension
2. Press `F1` → "Dev Containers: Reopen in Container"
3. Wait for build (first time takes ~10 min)

Everything is configured automatically!

See [docker/README.md](docker/README.md) for detailed Docker documentation.

---

## Architecture Overview

```
 HAL Client (embedded)  ──gRPC (port 50051)──►  ClientServiceImpl
                                                    │
Web UI / REST clients  ──HTTP (port 8080)──►  Drogon REST API
                                                    │
                                         ┌──────────┴──────────┐
                                         │   Service Layer      │
                                         │  RunService          │
                                         │  LogService          │
                                         │  AuthService         │
                                         │  OtaService  ...     │
                                         └──────────┬──────────┘
                                                    │
                                         ┌──────────┴──────────┐
                                         │   Repository Layer   │
                                         │  (Drogon ORM / PG)  │
                                         └──────────┬──────────┘
                                                    │
                                              PostgreSQL 15+
```

---

## Prerequisites (Native Build)

**Note:** Using Docker (see Quick Start above) avoids all manual dependency installation.

For native builds without Docker:

| Tool | Version |
|------|---------|
| CMake | 3.22+ |
| vcpkg | latest |
| C++ compiler | GCC 12+ or Clang 16+ |
| PostgreSQL | 15+ |
| protoc + grpc_cpp_plugin | matching vcpkg grpc version |

Set `VCPKG_ROOT` in your environment before building.

---

## Quick Start (Local Dev)

```bash
cp .env.example .env
# Edit .env — at minimum set ACADIA_DB_* and ACADIA_SESSION_SECRET

# Start Postgres
docker compose -f docker/docker-compose.yml up postgres -d

# Build and run (loads .env automatically)
./scripts/run_dev.sh
```

Or with Docker Compose (full stack):

```bash
docker compose -f docker/docker-compose.yml up --build
```

---

## Building

```bash
# Debug (default dev preset)
cmake --preset debug
cmake --build build/debug

# Release
cmake --preset release
cmake --build build/release

# Run tests
cmake --preset test
cmake --build build/test
ctest --test-dir build/test --output-on-failure

# ASan
cmake --preset asan
cmake --build build/asan
```

---

## Configuration

All configuration is via environment variables. See `.env.example` for the full list.

| Variable | Default | Description |
|----------|---------|-------------|
| `ACADIA_GRPC_LISTEN` | `0.0.0.0:50051` | Server gRPC bind address; the Client connects to this endpoint |
| `ACADIA_SERVER_ADDRESS` | `127.0.0.1:50051` | Client destination for the long-lived `ClientService` stream |
| `ACADIA_HTTP_LISTEN` | `0.0.0.0:8080` | HTTP bind address |
| `ACADIA_DB_HOST` | `localhost` | PostgreSQL host |
| `ACADIA_DB_NAME` | `acadia` | Database name |
| `ACADIA_DB_USER` | `acadia` | DB username |
| `ACADIA_DB_PASSWORD` | *(required)* | DB password |
| `ACADIA_SESSION_SECRET` | *(required, ≥32 chars)* | Session signing key |
| `ACADIA_REGISTRY_PATH` | `registry.yaml` | Module registry YAML |
| `ACADIA_OTA_STORAGE_PATH` | `/var/acadia/ota` | OTA artifact directory |
| `ACADIA_LOG_LEVEL` | `info` | Log level (trace/debug/info/warn/error) |

---

## Module Registry

Modules are declared in a YAML file (path set by `ACADIA_REGISTRY_PATH`):

```yaml
modules:
  - id: "com.example.temperature"
    name: "Temperature Monitor"
    description: "Reads temperature from sensor A"
    version: "1.2.0"
    result_type_url: "type.googleapis.com/acadia.TempResult"
    config_type_url: "type.googleapis.com/acadia.TempConfig"
    modes: ["single_shot", "monitoring"]
```

---

## Database Migrations

SQL migration files live in `migrations/`. They are applied automatically at startup in
alphabetical order. Migrations are idempotent (tracked in `schema_migrations`). To add a new
migration, create `migrations/NNNN_description.sql`.

---

## API

All protected endpoints require either:
- A session cookie (from `POST /api/v1/auth/login`), or
- An `Authorization: Bearer <token>` header.

### Key endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/v1/health` | Health check |
| POST | `/api/v1/auth/login` | Login → sets session |
| POST | `/api/v1/auth/tokens` | Create API token |
| GET | `/api/v1/modules` | List test modules |
| POST | `/api/v1/runs` | Start a test run |
| GET | `/api/v1/runs/{id}` | Get run details |
| GET | `/api/v1/runs/{id}/logs` | List run logs |
| GET | `/api/v1/runs/{id}/logs/tail` | SSE live log tail |
| GET | `/api/v1/target` | Get board info |
| POST | `/api/v1/target/heartbeat` | Board heartbeat |
| POST | `/api/v1/ota/upload` | Upload OTA artifact |
| POST | `/api/v1/ota/{id}/trigger` | Trigger OTA upgrade |

---

## Decisions Log

| Decision | Rationale |
|----------|-----------|
| gRPC sync API for unary, streaming wait-loop | Simpler than async CQ for initial impl; async CQ adds complexity without benefit at this scale |
| Drogon for REST | Built-in PG ORM, async, WebSocket, SSE, session management |
| argon2id for passwords | OWASP recommended |
| SHA-256 for API tokens | Tokens are long random strings; SHA-256 is fast and sufficient |
| 1-minute minimum cron resolution | Full cron parser adds a dependency; document the limitation |
| Single `board` row (unique partial index) | The system targets exactly one physical test board |
| Migrations at startup | Ensures schema is always up to date; no separate migration tool needed |
| vcpkg manifest mode | Reproducible dependency pinning per repo |

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

Binary: `build/acadia-server`

## Run (dev)

```sh
./build/acadia-server --config etc/acadia/server.toml --log-level debug
```

Without a config file the binary starts with compiled-in defaults.

## Test

```sh
cmake --build build --target config-test
ctest --test-dir build --output-on-failure
```

## Config

Default path: `/etc/acadia/server.toml`. See `etc/acadia/server.toml` for all keys and defaults.

CLI flags override file values:

| Flag | Default |
|------|---------|
| `--config PATH` | `/etc/acadia/server.toml` |
| `--log-level LEVEL` | `info` |

## Project Description

Acadia-Server is the central C++ backend for Acadia IV&V execution, result collection, operator
APIs, and board lifecycle operations. The repository combines a Drogon HTTP service, a gRPC
`HalService` boundary for embedded clients, service and repository layers, PostgreSQL
migrations, configuration files, Docker/dev-container support, and optional metrics. Documented
responsibilities include authentication and API tokens, module registry access, run creation and
inspection, result/log ingestion, live log tails over SSE, target heartbeat, OTA artifact upload
and triggering, and database-backed persistence.

The implementation is organized under `src/`, with protocol/configuration and migration surfaces
alongside unit, integration, and configuration tests. Service names in the architecture include
run, log, auth, and OTA concerns; the repository also contains registry, result conversion,
hashing, time, UUID, anomaly, threshold, badge, and stream-backpressure test areas. The API uses
session cookies or bearer tokens for protected endpoints and environment/config-file values for
listener, database, registry, storage, and logging behavior. PostgreSQL schema migrations are
applied at startup according to the existing documentation.

Build presets include debug, release, test, and ASan workflows; focused checks are `ctest
--test-dir build --output-on-failure` and the configuration test target. Presence of endpoints
and tests is not a claim that a live board, database, OTA artifact, or browser client has been
exercised. Deployment credentials, session secrets, database connectivity, gRPC streaming,
migration upgrades, and destructive operations require manual verification.

## Feature Status

| # | Name / Description | Implemented? | Test Coverage? | Manual Test |
|---:|---|---|---|---|
| 1 | CMake build, config loading, and logging | Implemented | `config_test.cpp` | [ ] Manual verification required |
| 2 | PostgreSQL repository layer and startup migrations | Partial: code/migrations present; production execution unverified | Migration integration tests | [ ] Manual verification required |
| 3 | gRPC `HalService` boundary and streaming | Partial: service surface present; live interoperability unverified | Stream backpressure test | [ ] Manual verification required |
| 4 | REST API and health endpoint | Implemented | Endpoint coverage varies | [ ] Manual verification required |
| 5 | Session authentication and bearer API tokens | Implemented | Focused coverage not fully mapped | [ ] Manual verification required |
| 6 | Module registry and typed result conversion | Implemented | `ModuleRegistryTest`, `ResultConvertTest` | [ ] Manual verification required |
| 7 | Run state and badges | Partial: service surface present | Run/badge tests present | [ ] Manual verification required |
| 8 | Logs and SSE live tail | Implemented | Stream tests; deployment unknown | [ ] Manual verification required |
| 9 | Target information and heartbeat | Implemented | Unknown | [ ] Manual verification required |
| 10 | OTA upload and trigger orchestration | Partial: API/service surface documented | Unknown | [ ] Manual verification required |
| 11 | Anomaly and threshold services | Implemented | Focused service tests present | [ ] Manual verification required |
| 12 | Hash, time, UUID, and configuration utilities | Implemented | Focused unit tests present | [ ] Manual verification required |
| 13 | Docker, presets, ASan, and deployment docs | Implemented | Build execution not claimed | [ ] Manual verification required |

## Test Evidence

- `test/config_test.cpp`.
- `tests/unit/` covers gRPC backpressure, registry, services, and utilities.
- `tests/integration/MigrationRunnerTest.cc` covers migration behavior.
- Typical commands: `cmake --preset test`, `cmake --build build/test`, and `ctest --test-dir
build/test --output-on-failure`.

## Implementation Status

| Step | Status |
|------|--------|
| 1 · Skeleton + config + logger | ✓ done |
| 2 · DB layer + migrations | — |
| 3 · DB CRUD (runs + batches) | — |
| 4 · Ingest queue + result writer | — |
| 5 · gRPC bidi server | — |
| … | — |
