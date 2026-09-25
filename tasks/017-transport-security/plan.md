# Task 017 — Transport security (VB-8)

Parent task: `Acadia/tasks/017-transport-security/plan.md`.

## Scope in this repository

- `src/grpc_server/StreamAuth.h`/`.cc` (new): shared token-comparison logic
  used by both the pre-existing ClientService stream-token auth (X2-003)
  and the new HalService call auth (SRV-025).
- `src/grpc_server/HalServiceImpl.h`/`.cc`: `check_auth()` on all 8 RPCs.
- `src/grpc_server/GrpcServerRunner.cc`: TLS/mTLS `ServerCredentials`
  (SRV-021).
- `src/config/Settings.h`/`.cc`: new `AuthSettings::hal_service_token`,
  `NetworkSettings::grpc_tls_{cert,key,ca}_pem_path`.
- `src/rest_api/DrogonAppFactory.cc` + all 13 controller headers: SRV-153
  fix (namespace-qualified `AuthFilter` string).

## Interfaces affected

- gRPC server listening port: credential type now configurable
  (plaintext/TLS/mTLS) instead of hardcoded plaintext.
- `HalService` RPC contract: unchanged wire format; new
  `UNAUTHENTICATED` rejection when `ACADIA_HAL_SERVICE_TOKEN` is set and
  the caller's `x-acadia-hal-token` metadata is missing/wrong.
- REST API: no route signature changes; `AuthFilter` now actually runs
  (previously it silently did not) — this is a behavior fix, not an
  interface change.

## Local tests

- `tests/unit/grpc/StreamAuthSharedTest.cc` (new, 4 tests).
- Full existing unit suite (`acadia-unit-tests`) re-run: 92/92 pass.

## Role in cross-repository validation

This repo is the only one with code changes for VB-8. Cross-repo relevance:
Acadia-Client already supports gRPC TLS on its side (no code change
needed for server-only TLS to interoperate); Acadia-Dashboard's session/
cookie auth flow (DSH-003) is what SRV-153's fix makes actually load-bearing
for the first time — see parent plan's impact matrix and the Dashboard
regression-pass follow-up noted there.
