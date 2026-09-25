# Task 012 — Live-run view contract fix (VB-5, DSH-001, SRV-141) — Acadia-Server scope

Parent task: [`Acadia/tasks/012-live-run-view-contract-fix/plan.md`](https://github.com/soccentric-acadia/Acadia/blob/develop/tasks/012-live-run-view-contract-fix/plan.md)

## Scope in this repository

`src/rest_api/controllers/RunsController.cc`: the `events()` SSE handler's
one-time initial `state` event now carries the full run row (via the
existing `run_to_json` helper already used by `GET /runs/{id}`) instead of a
hand-built `{run_id, status}` object, so the Dashboard's live-run view has
enough to render immediately on connect without an extra REST round-trip.

No REST route, request/response shape, or other endpoint changed — only the
SSE payload's JSON content on this one path.

## Local tests

No build toolchain available in this session (Drogon/gRPC/Postgres,
multi-stage build) — validated by pattern-matching against the identical
`Json::StreamWriterBuilder` usage already in the same function, see the
parent task's `validation.md`.

## Role in cross-repository validation

Provider-side fix for `Acadia-Dashboard`'s live-run page, which is the only
known consumer of this endpoint.
