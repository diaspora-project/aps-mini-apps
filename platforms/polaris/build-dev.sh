#!/usr/bin/env bash
#PBS -l select=1
#PBS -l walltime=01:00:00
#PBS -N tekapp-build-dev
#PBS -q debug
#PBS -l filesystems=home:eagle

# Non-interactive dev build script for Polaris — submit with `qsub`.
#
# Installs only tekapp's *dependencies* via Spack (from spack-dev.yaml) and
# then builds tekapp itself from the local source checkout with cmake.
#
# Behaviour (idempotent — never prompts):
#   * If ./spack already exists, leave it alone.
#   * If the APS-dev spack env already exists, activate it.
#   * Concretize only if the env hasn't been concretized yet.
#   * `spack install` is always invoked (it's a no-op if everything is built).
#   * cmake configure runs only when CMakeCache.txt is missing; `make` always
#     runs (incremental build).

# Can be submitted as a PBS job (`qsub platforms/polaris/build-dev.sh`) or
# invoked directly from a login node (`./platforms/polaris/build-dev.sh`).
set -euo pipefail
if [[ -n "${PBS_O_WORKDIR:-}" ]]; then
    cd "$PBS_O_WORKDIR"
    SCRIPT_DIR="$PBS_O_WORKDIR/platforms/polaris"
else
    SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &>/dev/null && pwd )"
    cd "$SCRIPT_DIR/../.."
fi
PROJECT_ROOT="$(pwd)"

# Proxy settings
export HTTP_PROXY="http://proxy.alcf.anl.gov:3128"
export HTTPS_PROXY="http://proxy.alcf.anl.gov:3128"
export http_proxy="http://proxy.alcf.anl.gov:3128"
export https_proxy="http://proxy.alcf.anl.gov:3128"
export ftp_proxy="http://proxy.alcf.anl.gov:3128"
export no_proxy="admin,polaris-adminvm-01,localhost,*.cm.polaris.alcf.anl.gov,polaris-*,*.polaris.alcf.anl.gov,*.alcf.anl.gov"

export SPACK_DISABLE_LOCAL_CONFIG=true

echo "==> Loading Polaris modules"
module use /soft/modulefiles
module load PrgEnv-gnu/8.6.0
module load cray-mpich/9.0.1
module load libfabric/2.2.0rc1
module load spack-pe-base
module load cmake

echo "==> Ensuring Spack is present"
if [[ ! -d spack ]]; then
    git clone --depth 1 https://github.com/spack/spack.git
fi
source spack/share/spack/setup-env.sh

echo "==> Ensuring APS-dev Spack environment exists"
if ! spack env list | awk '{print $1}' | grep -qx APS-dev; then
    spack env create APS-dev "$SCRIPT_DIR/spack-dev.yaml"
fi
spack env activate APS-dev

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
    cmake .. -DCMAKE_CXX_COMPILER=CC -DCMAKE_C_COMPILER=cc
fi
make

echo "==> Build complete"
echo "    Local launchers: $PROJECT_ROOT/build/bin/tekapp-{daq,dist,sirt,denoiser}"
echo "    Spack env: spack env activate APS-dev"
