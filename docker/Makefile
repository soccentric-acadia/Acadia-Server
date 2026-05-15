IMAGE_NAME ?= acadia

.PHONY: build build-server run clean delete-images

build:
	@echo "Building Docker image $(IMAGE_NAME) for Raspberry Pi 5 (ARM64)..."
	docker buildx build --platform linux/arm64 --load --build-arg UID=$(shell id -u) --build-arg GID=$(shell id -g) -t $(IMAGE_NAME) .

build-server:
	@echo "Building Docker image $(IMAGE_NAME)-server for x86 Ubuntu 22.04..."
	docker buildx build --platform linux/amd64 --load \
	    --build-arg UID=$(shell id -u) --build-arg GID=$(shell id -g) \
	    -f Dockerfile.server -t $(IMAGE_NAME)-server .

run:
	# No --gpus flag: Pi 5 has VideoCore VII (not CUDA)
	docker run -it --rm --hostname rpi5 $(IMAGE_NAME)

clean:
	docker rmi $(IMAGE_NAME) || true

delete-images:
	docker rmi $(docker images -q) || true
