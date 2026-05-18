#!/usr/bin/env bash

# Build the tekapp Docker image from platforms/docker/spack.yaml.
#
# Idempotent:
#   * If ./spack already exists, leave it alone.
#   * Regenerates the Dockerfile each run (cheap), but rebuilds the image
#     only if the stamp file is missing or older than the Dockerfile.
#
# Requires `docker` on the host PATH (we don't try to install it).

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &>/dev/null && pwd )"
PROJECT_ROOT="$( cd -- "$SCRIPT_DIR/../.." &>/dev/null && pwd )"
cd "$PROJECT_ROOT"

set -euo pipefail

export SPACK_DISABLE_LOCAL_CONFIG=true

echo "==> Checking docker is installed"
if ! command -v docker >/dev/null; then
    echo "ERROR: docker not found on PATH. Install it before running this script." >&2
    exit 1
fi

echo "==> Ensuring Spack is present"
if [[ ! -d spack ]]; then
    git clone --depth 1 https://github.com/spack/spack.git
fi
source spack/share/spack/setup-env.sh

echo "==> Generating Dockerfile"
mkdir -p "$PROJECT_ROOT/build/docker"
DOCKERFILE="$PROJECT_ROOT/build/docker/Dockerfile"
STAMP="$PROJECT_ROOT/build/docker/.tekapp-image.stamp"
IMAGE="tekapp:latest"
# `spack containerize` reads ./spack.yaml from cwd; no env needed.
( cd "$SCRIPT_DIR" && spack containerize ) > "$DOCKERFILE"

if [[ ! -f "$STAMP" || "$DOCKERFILE" -nt "$STAMP" ]]; then
    echo "==> Building $IMAGE (docker build)"
    docker build -t "$IMAGE" -f "$DOCKERFILE" "$PROJECT_ROOT"
    docker image inspect -f '{{.Id}}' "$IMAGE" > "$STAMP"
else
    echo "    $IMAGE up-to-date — skipping docker build"
fi

echo "==> Done"
echo "    Image:      $IMAGE"
echo "    Dockerfile: $DOCKERFILE"
echo "    Run it:     bash $SCRIPT_DIR/run-with-files-driver.sh"
