#!/usr/bin/env bash

# Build the tekapp Apptainer image from platforms/apptainer/spack.yaml.
#
# Idempotent:
#   * If ./spack already exists, leave it alone.
#   * Regenerates tekapp.def each run (cheap), but rebuilds tekapp.sif only
#     if missing or older than the .def.
#
# Requires `apptainer` on the host PATH (we don't try to install it).

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &>/dev/null && pwd )"
PROJECT_ROOT="$( cd -- "$SCRIPT_DIR/../.." &>/dev/null && pwd )"
cd "$PROJECT_ROOT"

set -euo pipefail

export SPACK_DISABLE_LOCAL_CONFIG=true

echo "==> Checking apptainer is installed"
if ! command -v apptainer >/dev/null; then
    echo "ERROR: apptainer not found on PATH. Install it before running this script." >&2
    exit 1
fi

echo "==> Ensuring Spack is present"
if [[ ! -d spack ]]; then
    git clone --depth 1 https://github.com/spack/spack.git
fi
source spack/share/spack/setup-env.sh

echo "==> Generating Apptainer definition file"
mkdir -p "$PROJECT_ROOT/build/apptainer"
DEF="$PROJECT_ROOT/build/apptainer/tekapp.def"
SIF="$PROJECT_ROOT/build/apptainer/tekapp.sif"
# `spack containerize` reads ./spack.yaml from cwd; no env needed.
( cd "$SCRIPT_DIR" && spack containerize ) > "$DEF"

if [[ ! -f "$SIF" || "$DEF" -nt "$SIF" ]]; then
    echo "==> Building tekapp.sif (apptainer build --fakeroot)"
    apptainer build --fakeroot "$SIF" "$DEF"
else
    echo "    tekapp.sif up-to-date — skipping apptainer build"
fi

echo "==> Done"
echo "    Image:   $SIF"
echo "    Def:     $DEF"
echo "    Run it:  bash $SCRIPT_DIR/run-with-files-driver.sh"
