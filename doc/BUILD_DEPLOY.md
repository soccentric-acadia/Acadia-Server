# Acadia-Server Build and Deployment

## Scope

Acadia-Server is the C++ service and test project. Its root Makefile supports native CMake/Ninja
builds, ARM64 Docker builds for the client-side cross path, an x86 Ubuntu 22.04 Docker server
build, CTest, formatting/static analysis/Doxygen, and password-based deployment of cross-built
test applications.

## 1. Prerequisites

- Linux with GNU Make, CMake, Ninja, a C++ compiler, and the libraries found by the root
CMake/vcpkg configuration, including Drogon, spdlog, yaml-cpp, GoogleTest, OpenSSL,
protobuf/gRPC, and optional Prometheus support as configured.
- Docker for the `acadia` ARM64 image and `acadia-server` x86 image; QEMU/buildx support may be
needed for non-native platforms.
- `clang-format`, `cmake-format`, `cppcheck`, Doxygen, and Graphviz for quality/documentation
targets.
- For local infrastructure, Docker Compose and the PostgreSQL configuration described by
`docker/` and `.env.example`/existing setup docs.
- `sshpass`, SSH/SCP access, and a reachable target for `deploy-*`/`run-*`.

## 2. Setup

```sh
make help
make docker-install
make docker-image
make docker-image-server
```

The Docker targets use the parent `Acadia/` mount so Acadia-Client, Acadia-HAL, and server
sources resolve in the container. For native dependency setup, follow the existing
`LOCAL_SETUP.md` and `docker/README.md`; set `VCPKG_ROOT` when using vcpkg manifest mode.

## 3. Build and compile

```sh
make configure
make build
make docker-build
make docker-build-server
```

Native output is under `build/native/`. The ARM64 cross build is under the mounted Acadia-Client
path's `build/cross/`; the server Docker build is under `build/docker/` in Acadia-Server. Native
and container CMake configuration enables `BUILD_TESTS=ON` and uses Ninja.

## 4. Test and lint

```sh
make test
make docker-test
make docker-test-server
make format
make cppcheck
make doc
```

Native tests use CTest. `docker-test` runs the ARM64 cross suite through QEMU;
`docker-test-server` runs x86 server tests. `format` may modify C++ and CMake files; `cppcheck`
and `doc` require their named tools. These commands do not validate a deployed PostgreSQL, TLS,
client, or physical device integration.

## 5. Packaging and artifacts

No package target is declared in this root Makefile. Build artifacts are CMake/Ninja executables
and libraries under the native or Docker build directories. Docker images are `acadia` for the
ARM64 client-side build environment and `acadia-server` for the x86 server image. Container
Compose assets provide development infrastructure, not a production release bundle.

## 6. Deployment, install, and run

```sh
make deploy-a
make deploy-b
make deploy IP_A=192.168.1.20 IP_B=192.168.1.21
make run-a
make run-b APP_NAME arg1
make run APP_NAME arg1
```

`deploy-a`/`deploy-b` cross-build, remove `/tmp/acu-test` on the target, and copy
`build/cross/lib` and `build/cross/bin` there. Defaults are `192.168.1.3` and `192.168.1.13`,
user `nvidia`, password `nvidia`. These are conditional credential-bearing operations: override
`IP_A`, `IP_B`, `USER`, and `PASS`, and verify the target before execution. `run` executes
native binaries with `LD_LIBRARY_PATH`; remote targets execute from `/tmp/acu-test`.

For local service development, use the existing Docker instructions and scripts rather than
inventing a Make target. The documented configuration and database/TLS environment values are
external deployment inputs.

## 7. Cleanup

```sh
make clean
make docker-clean
make docker-clean-server
```

`clean` removes `build/`. The Docker clean targets remove their respective container build
directories while leaving images and ccache volumes in place.

## 8. Hardware, cloud, and manual verification caveats

The Makefile has no cloud deployment target and its remote deploy flow is for test applications,
not a production service installer. Docker/QEMU and CTest do not prove PostgreSQL migrations,
authentication, TLS, API behavior, client/HAL interoperability, device discovery, or target ABI.
Manually validate service startup with the intended config and `.env` values, database
connectivity, health/API endpoints, authenticated client handshake, and any target-device run on
real hardware. The SSH recipes disable strict host-key checking and use plaintext `sshpass`;
restrict them to controlled development networks.
