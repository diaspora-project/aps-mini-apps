#!/usr/bin/env bash

# Non-interactive dev build script for a local workstation.
#
# Installs only tekapp's *dependencies* via Spack (from spack-dev.yaml) and
# then builds tekapp itself from the local source checkout with cmake.
#
# Behaviour (idempotent — never prompts):
#   * If ./spack already exists, leave it alone.
#   * If the tekapp-dev-env spack env already exists, activate it.
#   * Concretize only if the env hasn't been concretized yet.
#   * `spack install` is always invoked (it's a no-op if everything is built).
#   * cmake configure runs only when CMakeCache.txt is missing; `make` always
#     runs (incremental build).

# This script can be run from anywhere; it locates itself and the project root.
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &>/dev/null && pwd )"
PROJECT_ROOT="$( cd -- "$SCRIPT_DIR/../.." &>/dev/null && pwd )"
cd "$PROJECT_ROOT"

set -euo pipefail

export SPACK_DISABLE_LOCAL_CONFIG=true

echo "==> Ensuring Spack is present"
if [[ ! -d spack ]]; then
    git clone --depth 1 https://github.com/spack/spack.git
fi
source spack/share/spack/setup-env.sh

echo "==> Ensuring tekapp-dev-env Spack environment exists"
if ! spack env list | awk '{print $1}' | grep -qx tekapp-dev-env; then
    spack env create tekapp-dev-env "$SCRIPT_DIR/spack-dev.yaml"
fi
spack env activate tekapp-dev-env

echo "==> Concretizing (only if env hasn't been concretized yet)"
if [[ ! -f "$SPACK_ENV/spack.lock" ]]; then
    spack concretize -f
fi

echo "==> Installing tekapp dependencies"
spack install --fail-fast

echo "==> Building tekapp from source (cmake + make)"
mkdir -p build
cd build
if [[ ! -f CMakeCache.txt ]]; then
    cmake ..
fi
make

echo "==> Build complete"
echo "    Local launchers: $PROJECT_ROOT/build/bin/tekapp-{daq,dist,sirt,denoiser}"
echo "    Spack env: spack env activate tekapp-dev-env"
