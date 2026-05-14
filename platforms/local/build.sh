#!/usr/bin/env bash

# Non-interactive build script for a local workstation.
#
# Behaviour (idempotent — never prompts):
#   * If ./spack already exists, leave it alone.
#   * If the tekapp-env spack env already exists, activate it.
#   * Concretize + install only if the env's tekapp package isn't importable.

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

echo "==> Ensuring tekapp-env Spack environment exists"
if ! spack env list | awk '{print $1}' | grep -qx tekapp-env; then
    spack env create tekapp-env "$SCRIPT_DIR/spack.yaml"
fi
spack env activate tekapp-env

echo "==> Checking whether tekapp is already installed in the environment"
if spack -e tekapp-env python -c 'import tekapp' >/dev/null 2>&1; then
    echo "    tekapp importable — skipping concretize + install"
else
    echo "    tekapp not importable — concretizing and installing"
    spack concretize -f
    spack install --fail-fast
fi

# Uncomment the following to build from source

#echo "==> Building local checkout (cmake + make)"
#mkdir -p build
#cd build
#if [[ ! -f CMakeCache.txt ]]; then
#    cmake ..
#fi
#make

echo "==> Build complete"
echo "    Local launchers: $PROJECT_ROOT/build/bin/tekapp-{daq,dist,sirt,denoiser}"
echo "    Spack env: spack env activate tekapp-env"
