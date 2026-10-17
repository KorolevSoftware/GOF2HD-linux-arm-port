#!/usr/bin/env bash
# Build the same ARMv7 environment used for the ROCKNIX/Panfrost build.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE="${GOF_DOCKER_IMAGE:-gof2hd-build:armhf}"

docker build --platform linux/arm/v7 -t "$IMAGE" -f "$ROOT/Dockerfile" "$ROOT"
docker run --rm --platform linux/arm/v7 \
    --user "$(id -u):$(id -g)" \
    --mount "type=bind,source=$ROOT,target=/work" \
    -e GOF_JOBS="${GOF_JOBS:-2}" -e CFLAGS \
    "$IMAGE"
