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

echo "==> Ensuring Spack is present"
if [[ ! -d spack ]]; then
    git clone --depth 1 https://github.com/spack/spack.git
fi
source spack/share/spack/setup-env.sh

echo "==> Ensuring tekapp-improv Spack environment exists"
if ! spack env list | awk '{print $1}' | grep -qx tekapp-env; then
    spack env create tekapp-env "$SCRIPT_DIR/spack.yaml"
fi
spack env activate tekapp-env

echo "==> Checking whether tekapp is already installed in the environment"
if spack -e tekapp-improv python -c 'import tekapp' >/dev/null 2>&1; then
    echo "    tekapp importable — skipping concretize + install"
else
    echo "    tekapp not importable — concretizing and installing"
    spack concretize -f
    spack install --fail-fast
fi

echo "==> Build complete"
echo "    Spack env: spack env activate tekapp-env"
