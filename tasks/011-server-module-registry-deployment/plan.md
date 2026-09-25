# Task 011 — Server module registry deployment (VB-4) — Acadia-Server scope

Parent task: [`Acadia/tasks/011-server-module-registry-deployment/plan.md`](https://github.com/soccentric-acadia/Acadia/blob/develop/tasks/011-server-module-registry-deployment/plan.md)

## Scope in this repository

- Vendored `protos/registry/modules.yaml` (from `Acadia-HAL@7184d94`) — the
  registry never existed in this repo before.
- `docker/Dockerfile.production`: copies the registry into the image and
  sets `ACADIA_REGISTRY_PATH` by default.
- `src/main.cpp`: a missing-but-configured registry now fails startup
  (`spdlog::critical` + `return 1`) instead of only warning (SRV-106).

## Local tests

No build toolchain available in this session (Drogon/gRPC/Postgres,
multi-stage Docker build) — see the parent task's `validation.md` for the
diff-level review performed instead, and the explicit list of skipped
build/runtime checks.

## Role in cross-repository validation

Provider-side fix. `Acadia-Dashboard` (unaffected by this task directly)
will start receiving real data from `/api/v1/modules` instead of falling
back to its static catalog once this is deployed — confirming that end to
end is out of scope here (no running stack in this session).
