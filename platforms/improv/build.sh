#!/usr/bin/env bash
#PBS -l select=1
#PBS -l walltime=01:00:00
#PBS -N tekapp-build
#PBS -q debug
#PBS -A radix-io

# Non-interactive build script for Improv (ANL LCRC) — submit with `qsub`.
#
# Behaviour (idempotent — never prompts):
#   * If ./spack already exists, leave it alone.
#   * If the tekapp-improv spack env already exists, activate it.
#   * Concretize + install only if the env's tekapp package isn't importable.

# This script needs to be submitted from the root of tekapp
# (i.e. qsub platforms/improv/build.sh)
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

echo "==> Ensuring tekapp-improv Spack environment exists"
if ! spack env list | awk '{print $1}' | grep -qx tekapp-improv; then
    spack env create tekapp-improv "$SCRIPT_DIR/spack.yaml"
fi
spack env activate tekapp-improv

echo "==> Checking whether tekapp is already installed in the environment"
if spack -e tekapp-improv python -c 'import tekapp' >/dev/null 2>&1; then
    echo "    tekapp importable — skipping concretize + install"
else
    echo "    tekapp not importable — concretizing and installing"
    spack concretize -f
    spack install --fail-fast
fi

# Uncomment the following to also build from source

#echo "==> Building local checkout (cmake + make)"
#mkdir -p build
#cd build
#if [[ ! -f CMakeCache.txt ]]; then
#    cmake .. -DCMAKE_CXX_COMPILER=mpicxx -DCMAKE_C_COMPILER=mpicc
#fi
#make

echo "==> Build complete"
echo "    Spack env: spack env activate tekapp-improv"
