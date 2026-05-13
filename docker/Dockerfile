# syntax=docker/dockerfile:1
# Multi-stage build for Acadia Server.
# Stage 1: Build
FROM debian:bookworm AS builder

ARG VCPKG_ROOT=/opt/vcpkg
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git curl zip unzip tar pkg-config \
    libssl-dev libpq-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
RUN git clone --depth=1 https://github.com/microsoft/vcpkg ${VCPKG_ROOT} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics
ENV PATH="${VCPKG_ROOT}:${PATH}"

WORKDIR /src
COPY . .

RUN cmake --preset release \
    -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build/release --target acadia-server -j"$(nproc)"

# Stage 2: Runtime
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 libpq5 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Non-root user
RUN useradd --uid 10001 --no-create-home --shell /usr/sbin/nologin acadia

WORKDIR /app
COPY --from=builder /src/build/release/acadia-server ./acadia-server
COPY --from=builder /src/migrations ./migrations

USER acadia

EXPOSE 9090 8080

ENTRYPOINT ["/app/acadia-server"]
