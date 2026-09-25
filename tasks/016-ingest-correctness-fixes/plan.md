# Task 016 — Ingest correctness fixes (VB-6) — Acadia-Server scope

Parent task: [`Acadia/tasks/016-ingest-correctness-fixes/plan.md`](https://github.com/soccentric-acadia/Acadia/blob/develop/tasks/016-ingest-correctness-fixes/plan.md)

## Scope in this repository

- `src/grpc_server/ClientServiceImpl.{h,cc}`: fixed the ack race (SRV-045),
  empty-batch ack (SRV-046), ack-on-failed-ingest (SRV-047), concurrent
  stream writes (SRV-049), and premature/wrong-status run finalization
  (SRV-050) — plus a previously-untracked bug found while fixing these,
  where only the first sample of any multi-sample result batch was ever
  persisted. Also fixed an unrelated, pre-existing compile break
  (`StateReport::spool_pressure()`, a proto field that has never existed).
- `src/grpc_server/ClientRegistry.{h,cc}`: doc comment only, no behavior
  change — records why `SendControl` holds its mutex across the blocking
  `Write` call (this task's `send_ack` helper now relies on that).
- `tests/unit/grpc/ClientServiceLogicTest.cc` (new): 5 unit tests for the
  new `terminal_run_state_to_status` pure function.

## Local tests

Real toolchain built specifically for this task (see the parent task's
`execution.md` for the exact package list and from-source builds of
yaml-cpp/toml++/Drogon):

```
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release
ninja -C build                # acadia-server + both test binaries build clean
./build/bin/acadia-unit-tests  # 88/88 passed
```

## Role in cross-repository validation

Self-contained — the wire contract (`client_service.proto`, owned by
`Acadia-HAL`) is unchanged; a sibling `Acadia-HAL` checkout was only needed
to build this repo at all (its protos are fetched as a build-time sibling
per the existing convention), not because anything there changed.
