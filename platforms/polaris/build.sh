#!/usr/bin/env bash
#PBS -l select=1
#PBS -l walltime=01:00:00
#PBS -N tekapp-build
#PBS -q debug
#PBS -l filesystems=home:eagle

# Non-interactive build script for Polaris — submit with `qsub`.
#
# Behaviour (idempotent — never prompts):
#   * If ./spack already exists, leave it alone.
#   * If the APS spack env already exists, activate it.
#   * Concretize + install only if the env's tekapp package isn't importable.

# This script needs to be run from the root of tekapp
# (i.e. qsub platforms/polaris/build.sh)
cd $PBS_O_WORKDIR
SCRIPT_DIR=$(pwd)/platforms/polaris

set -euo pipefail

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

echo "==> Ensuring APS Spack environment exists"
if ! spack env list | awk '{print $1}' | grep -qx APS; then
    spack env create APS "$SCRIPT_DIR/spack.yaml"
fi
spack env activate APS

echo "==> Checking whether tekapp is already installed in the environment"
if spack -e APS python -c 'import tekapp' >/dev/null 2>&1; then
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
#    cmake .. -DCMAKE_CXX_COMPILER=CC -DCMAKE_C_COMPILER=cc
#fi
#make

echo "==> Build complete"
echo "    Local launchers: $PROJECT_ROOT/build/bin/tekapp-{daq,dist,sirt,denoiser}"
echo "    Spack env: spack env activate APS"
