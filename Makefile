# Makefile for Acadia Server

.PHONY: all build image run clean

JOBS        := $(shell nproc)
DOCKER_JOBS := $(shell echo $$(( $(shell nproc) < 4 ? $(shell nproc) : 4 )))
ACADIA_ROOT := $(shell dirname $(realpath $(CURDIR)))
IMAGE       := acadia-server
BUILD_DIR   := /workspace/Acadia-Server/build

DOCKER_RUN := docker run --rm -t --init --platform linux/amd64 \
        -v $(ACADIA_ROOT):/workspace \
        -v ccache-acadia-server:/home/acadia/.ccache \
        $(IMAGE) bash -c

all: build

# Build the Docker image if it doesn't exist yet
image:
	@if ! docker image inspect $(IMAGE) >/dev/null 2>&1; then \
		$(MAKE) -C docker build-server; \
	fi

# Compile inside the container; output lands in ./build on the host via the volume mount
build: image
	$(DOCKER_RUN) "mkdir -p $(BUILD_DIR) && \
		cmake -GNinja -S /workspace/Acadia-Server -B $(BUILD_DIR) && \
		ninja -C $(BUILD_DIR) -j$(DOCKER_JOBS)"

# Run the server container with the SQLite database on the host volume
run: build
	docker run --rm -it --init --platform linux/amd64 \
		-v $(ACADIA_ROOT):/workspace \
		-e ACADIA_DB_PATH=/workspace/Acadia-Server/acadia.db \
		-p 8080:8080 \
		-p 50051:50051 \
		$(IMAGE) $(BUILD_DIR)/acadia-server

clean:
	rm -rf build
