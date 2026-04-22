#!/usr/bin/env bash

set -e

export SPACK_DISABLE_LOCAL_CONFIG=true

git_sync() {

    if [[ $# -eq 0 ]]; then
        echo "Usage: git_sync [git-flags] <git-url>"
        return 1
    fi

    local url="${@: -1}"
    local args=("${@:1:$#-1}")
    local folder
    folder="$(basename "$url" .git)"

    if [[ ! -d "$folder" ]]; then
        echo "Cloning into '$folder'..."
        git clone "${args[@]}" "$url" "$folder"
        return $?
    fi

    echo "Folder '$folder' already exists."
    echo "What would you like to do?"
    echo "  [L] Leave as-is (default)"
    echo "  [O] Overwrite (delete and re-clone)"
    echo "  [P] Pull latest changes"
    printf "Choice [L/o/p]: "
    read -r choice

    case "${choice,,}" in
        o)
            echo "Removing '$folder' and re-cloning..."
            rm -rf "$folder"
            git clone "${args[@]}" "$url" "$folder"
            ;;
        p)
            echo "Pulling latest changes in '$folder'..."
            git -C "$folder" pull
            ;;
        *)
            echo "Leaving '$folder' as-is."
            ;;
    esac
}

echo "==> Cloning Spack"
git_sync --depth 1 https://github.com/spack/spack.git

echo "==> Setting up Spack"
source spack/share/spack/setup-env.sh

echo "==> Cloning mochi-spack-packages"
git_sync https://github.com/mochi-hpc/mochi-spack-packages.git

echo "==> Cloning diaspora-spack-packages"
git_sync https://github.com/diaspora-project/diaspora-spack-packages.git

echo "==> Creating Spack environment"
aps_envs=$(spack env list | grep "APS")
need_concretize=false
need_install=false
if [[ -n "$aps_envs" ]]; then
    echo "APS spack environment already exists:"
    printf "Do you want to erase it? [y/N]: "
    read -r choice

    if [[ "${choice,,}" == "y" ]]; then
        spack env rm -y APS
        spack env create APS spack_polaris.yaml

        echo "==> Activating environment"
        spack env activate APS

        echo "==> Adding repositories to environment"
        spack repo add diaspora-spack-packages/spack_repo/diaspora
        spack repo add mochi-spack-packages/spack_repo/mochi
        need_concretize=true
        need_install=true
    else
        echo "Leaving environment as-is."
        echo "==> Activating environment"
        spack env activate APS
    fi
else
    spack env create APS spack_polaris.yaml

    echo "==> Activating environment"
    spack env activate APS

    echo "==> Adding repositories to environment"
    spack repo add diaspora-spack-packages/spack_repo/diaspora
    spack repo add mochi-spack-packages/spack_repo/mochi

    need_concretize=true
    need_install=true
fi

echo "==> Concretizing environment"
if [[ "${need_concretize}" == "false" ]]; then
    printf "Do you want to re-concretize the environment? [y/N]: "
    read -r choice
    if [[ "${choice,,}" == "y" ]]; then
        need_concretize=true
    fi
fi
if [[ "${need_concretize}" == "true" ]]; then
    spack concretize -f
    need_install=true
fi

echo "==> Installing environment"
if [[ "${need_install}" == "false" ]]; then
    printf "Do you want to re-install the environment? [y/N]: "
    read -r choice
    if [[ "${choice,,}" == "y" ]]; then
        need_install=true
    fi
fi
if [[ "${need_install}" == "true" ]]; then
    spack install --fail-fast -j 4
fi

module use /soft/modulefiles
module load PrgEnv-gnu/8.6.0
module load cray-mpich/9.0.1
module load libfabric/2.2.0rc1
module load spack-pe-base
module load cmake

echo "==> Preparing DAQ"
mkdir -p build/python/streamer-daq
pushd build/python/streamer-daq
cp ../../../python/streamer-daq/DAQStream.py .
popd

echo "==> Preparing DIST"
mkdir -p build/python/streamer-dist
pushd build/python/streamer-dist
cp ../../../python/streamer-dist/ModDistStreamPubDemo.py .
cp ../../../python/streamer-dist/diaspora_dist.py .
cp -r ../../../python/common ../
popd

echo "==> Preparing SIRT"
pushd include/tracelib
flatc -c trace_prot.fbs
popd

pushd build
cmake .. -DCMAKE_CXX_COMPILER=CC -DCMAKE_C_COMPILER=cc
make
popd

echo "==> Preparing DEN"
mkdir -p build/python/streamer-denoiser
pushd build/python/streamer-denoiser
cp ../../../python/streamer-denoiser/* ./
popd
