#!/usr/bin/env bash
#PBS -l select=1
#PBS -l walltime=01:00:00
#PBS -N tekapp-build-dev
#PBS -q debug
#PBS -A radix-io

# Non-interactive dev build script for Improv (ANL LCRC) — submit with `qsub`.
#
# Installs only tekapp's *dependencies* via Spack (from spack-dev.yaml) and
# then builds tekapp itself from the local source checkout with cmake.
#
# Behaviour (idempotent — never prompts):
#   * If ./spack already exists, leave it alone.
#   * If the tekapp-improv-dev spack env already exists, activate it.
#   * Concretize only if the env hasn't been concretized yet.
#   * `spack install` is always invoked (it's a no-op if everything is built).
#   * cmake configure runs only when CMakeCache.txt is missing; `make` always
#     runs (incremental build).

# Can be submitted as a PBS job (`qsub platforms/improv/build-dev.sh`) or
# invoked directly from a login node (`./platforms/improv/build-dev.sh`).
set -euo pipefail
if [[ -n "${PBS_O_WORKDIR:-}" ]]; then
    cd "$PBS_O_WORKDIR"
    SCRIPT_DIR="$PBS_O_WORKDIR/platforms/improv"
else
    SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &>/dev/null && pwd )"
    cd "$SCRIPT_DIR/../.."
fi
PROJECT_ROOT="$(pwd)"

export SPACK_DISABLE_LOCAL_CONFIG=true

echo "==> Ensuring Spack is present"
if [[ ! -d spack ]]; then
    git clone --depth 1 https://github.com/spack/spack.git
fi
source spack/share/spack/setup-env.sh

echo "==> Ensuring tekapp-improv-dev Spack environment exists"
if ! spack env list | awk '{print $1}' | grep -qx tekapp-improv-dev; then
    spack env create tekapp-env "$SCRIPT_DIR/spack-dev.yaml"
fi
spack env activate tekapp-env

echo "==> Concretizing (only if env hasn't been concretized yet)"
if [[ ! -f "$SPACK_ENV/spack.lock" ]]; then
    spack concretize -f
fi

echo "==> Installing tekapp dependencies"
spack install --fail-fast

echo "==> Loading Improv modules"
module load gcc/13.2.0
module load openmpi/5.0.2-gcc-13.2.0

echo "==> Building tekapp from source (cmake + make)"
mkdir -p build
cd build
if [[ ! -f CMakeCache.txt ]]; then
    cmake .. -DCMAKE_CXX_COMPILER=mpicxx -DCMAKE_C_COMPILER=mpicc
fi
make

echo "==> Build complete"
echo "    Local launchers: $PROJECT_ROOT/build/bin/tekapp-{daq,dist,sirt,denoiser}"
echo "    Spack env: spack env activate tekapp-env"
