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

# This script needs to be submitted from the root of tekapp
# (i.e. qsub platforms/improv/build-dev.sh)
cd $PBS_O_WORKDIR
SCRIPT_DIR=$(pwd)/platforms/improv

set -euo pipefail

export SPACK_DISABLE_LOCAL_CONFIG=true

echo "==> Loading Improv modules"
module load python
module load gcc/13.2.0
module load openmpi
module load perl

echo "==> Ensuring Spack is present"
if [[ ! -d spack ]]; then
    git clone --depth 1 https://github.com/spack/spack.git
fi
source spack/share/spack/setup-env.sh

echo "==> Ensuring tekapp-improv-dev Spack environment exists"
if ! spack env list | awk '{print $1}' | grep -qx tekapp-improv-dev; then
    spack env create tekapp-improv-dev "$SCRIPT_DIR/spack-dev.yaml"
fi
spack env activate tekapp-improv-dev

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
    cmake .. -DCMAKE_CXX_COMPILER=mpicxx -DCMAKE_C_COMPILER=mpicc
fi
make

echo "==> Build complete"
echo "    Local launchers: $PBS_O_WORKDIR/build/bin/tekapp-{daq,dist,sirt,denoiser}"
echo "    Spack env: spack env activate tekapp-improv-dev"
