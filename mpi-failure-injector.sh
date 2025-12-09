#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<EOF
Usage:
  $(basename "$0") -s /path/to/FailureInjector.py -t <global_mtbf_seconds> -n <num_ranks> [options]

Required:
  -s  Path to your Python injector script (the one that takes: <failure_mode> <mtbf>)
  -t  Global MTBF (seconds) for the entire cluster (float)
  -n  Number of MPI ranks (usually one per node)

Optional:
  -m  Mode for the injector: random|single (default: random)
  -H  Hostfile for MPI (MPICH/Hydra style)
  -p  PPN (processes per node), default: 1
  -P  Python executable (default: python)
  --  Everything after '--' is passed to mpiexec verbatim

Examples:
  $(basename "$0") -s ./FailureInjector.py -t 600 -n 8 -H hosts.txt
  $(basename "$0") -s ./FailureInjector.py -t 900 -n 4 -m single -H uniq_hosts -p 1

Notes:
- We enforce global MTBF by setting per-rank MTBF = global_MTBF * num_ranks.
- Works with MPICH/Hydra mpiexec flags: --hostfile, --ppn, -n
EOF
}

# Defaults
MODE="random"
HOSTFILE=""
PPN="1"
PYTHON="python"

# Parse args
MPIRAW_ARGS=()
SCRIPT=""
GLOBAL_MTBF=""
NUM_RANKS=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -s) SCRIPT="$2"; shift 2 ;;
    -t) GLOBAL_MTBF="$2"; shift 2 ;;
    -n) NUM_RANKS="$2"; shift 2 ;;
    -m) MODE="$2"; shift 2 ;;
    -H) HOSTFILE="$2"; shift 2 ;;
    -p) PPN="$2"; shift 2 ;;
    -P) PYTHON="$2"; shift 2 ;;
    --) shift; MPIRAW_ARGS+=("$@"); break ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      usage; exit 1 ;;
  esac
done

# Validate
[[ -n "${SCRIPT}" && -f "${SCRIPT}" ]] || { echo "ERROR: injector script not found: ${SCRIPT}" >&2; exit 2; }
[[ -n "${GLOBAL_MTBF}" ]] || { echo "ERROR: missing -t <global_mtbf_seconds>" >&2; exit 2; }
[[ -n "${NUM_RANKS}" ]] || { echo "ERROR: missing -n <num_ranks>" >&2; exit 2; }

# Compute per-rank MTBF = global_MTBF * num_ranks  (float-safe via awk)
PER_RANK_MTBF=$(awk -v mtbf="${GLOBAL_MTBF}" -v n="${NUM_RANKS}" 'BEGIN{ printf "%.6f", mtbf * n }')

echo "[launcher] Global MTBF: ${GLOBAL_MTBF}s; Ranks: ${NUM_RANKS}; Per-rank MTBF: ${PER_RANK_MTBF}s"
echo "[launcher] Mode: ${MODE}; Script: ${SCRIPT}"
[[ -n "${HOSTFILE}" ]] && echo "[launcher] Hostfile: ${HOSTFILE}"
echo "[launcher] PPN: ${PPN}; Python: ${PYTHON}"

# Build mpiexec command (MPICH/Hydra style)
MPI_CMD=( mpiexec -n "${NUM_RANKS}" --ppn "${PPN}" )
[[ -n "${HOSTFILE}" ]] && MPI_CMD+=( --hostfile "${HOSTFILE}" )
# pass through any extra mpiexec args after --
[[ ${#MPIRAW_ARGS[@]} -gt 0 ]] && MPI_CMD+=( "${MPIRAW_ARGS[@]}" )

# Sanitize LD_PRELOAD/XALT in case your cluster injects them
MPI_CMD+=( env -u LD_PRELOAD -u XALT_EXECUTABLE_TRACKING -u XALT_RUNPATH \
              -u SINGULARITYENV_LD_PRELOAD -u APPTAINERENV_LD_PRELOAD \
           bash -lc "${PYTHON} '$(readlink -f "$SCRIPT")' ${MODE} ${PER_RANK_MTBF}" )

# Forward Ctrl-C nicely
trap 'echo "[launcher] Caught SIGINT, forwarding to mpiexec...";' INT

echo "[launcher] Executing:"
printf '  %q ' "${MPI_CMD[@]}"; echo
"${MPI_CMD[@]}"