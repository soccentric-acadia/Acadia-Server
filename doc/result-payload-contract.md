# Result Payload Any Contract (Task 86)

Single documented + tested payload contract shared by HAL, Client, Server, fake
servers, and PySDK — so no component parses positional or conflicting types.

## The contract

- **On the wire**, every `ResultEnvelope` carries a `google.protobuf.Any payload`.
- **Batch samples** (`TestResultBatch.samples`, the Client→Server path) carry
  `hal.common.TestResult` at `type.googleapis.com/hal.common.TestResult`.
- **Per-module richness** lives inside `TestResult` (typed `metrics`,
  `monitoring`, `event` maps) — not a different Any type per module.
- **PySDK** unpacks an envelope by its type URL; the canonical result is
  `hal.common.TestResult`, and module-specific fields are accessed via the typed
  metrics maps on `TestResult.output`.

## How each component conforms

| Component | Behavior |
|---|---|
| HAL | Emits `ResultEnvelope` with `TestResult`-typed payload; typed metrics in `output.metrics`. |
| Client | Serializes results into `TestResultBatch.samples` as `TestResult`. |
| Server (ClientService) | Unpacks each sample as `hal.common.TestResult` (`ingest_test_result`). |
| Server (HalService) | `persist_result` expects `type.googleapis.com/hal.common.TestResult`. |
| PySDK | `_convert.unpack_result` resolves the Any type URL and decodes `TestResult`/module fields. |
| Fake servers | Emit `TestResult`-typed payloads so contract tests match the real Server. |

## Conformance gate

The Server `.claude/validate.sh` and PySDK `validate.sh` both assert the
canonical `hal.common.TestResult` type URL appears in the wire handling, so a
drift (e.g. Server switching to a module-specific top-level Any) is caught.
The Dashboard contract tests and PySDK wire tests exercise this end-to-end.
