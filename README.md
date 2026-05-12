# Acadia-Server

x86 backend for the Acadia IV&V Suite. Accepts gRPC streams from Acadia-Client,
persists to SQLite, and exposes data to Acadia-Dashboard over GraphQL + WebSocket.

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

## Implementation Status

| Step | Status |
|------|--------|
| 1 · Skeleton + config + logger | ✓ done |
| 2 · DB layer + migrations | — |
| 3 · DB CRUD (runs + batches) | — |
| 4 · Ingest queue + result writer | — |
| 5 · gRPC bidi server | — |
| … | — |
