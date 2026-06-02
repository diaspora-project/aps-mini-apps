#!/usr/bin/env bash
#PBS -l select=5:ncpus=128:mpiprocs=128
#PBS -l place=scatter
#PBS -l walltime=04:00:00
#PBS -N APS-campaign
#PBS -q debug
#PBS -A radix-io

# Campaign driver: run platforms/improv/run-with-<DRIVER>-driver.sh
# NUM_RUNS times, each from its own per-run directory under
# $PBS_O_WORKDIR/campaigns/<jobid>/run-NNN/. A failed run is recorded
# but does not abort the campaign.
#
# Prereq for DRIVER=mofka: run `bash scripts/configure-mofka.sh
# --platform improv` once in $PBS_O_WORKDIR before submitting; the
# resulting mofka.json + mofka-config.env are symlinked into every
# run dir. The #PBS -l select=5 above must be >= what
# run-with-mofka-driver.sh needs (BEDROCK_NODES + 3 + SIRT_NODES);
# for a files-only campaign 4 nodes are enough.

# === Campaign configuration (edit before submitting) ===
NUM_RUNS=5
DRIVER="mofka"   # "mofka" or "files"
# =======================================================

set -uo pipefail   # NOT -e: we want to survive individual run failures

cd "$PBS_O_WORKDIR"

case "$DRIVER" in
    mofka|files) ;;
    *) echo "ERROR: DRIVER must be 'mofka' or 'files' (got '$DRIVER')" >&2; exit 2 ;;
esac

INNER_SCRIPT="$PBS_O_WORKDIR/platforms/improv/run-with-${DRIVER}-driver.sh"
[[ -r "$INNER_SCRIPT" ]] || { echo "ERROR: $INNER_SCRIPT not found" >&2; exit 2; }

if [[ "$DRIVER" == "mofka" ]]; then
    if [[ ! -f mofka.json || ! -f mofka-config.env ]]; then
        echo "ERROR: mofka.json / mofka-config.env missing in $(pwd)." >&2
        echo "       Run: bash scripts/configure-mofka.sh --platform improv" >&2
        exit 2
    fi
fi

CAMPAIGN_DIR="$PBS_O_WORKDIR/campaigns/${PBS_JOBID:-local-$(date +%s)}"
mkdir -p "$CAMPAIGN_DIR"
SUMMARY="$CAMPAIGN_DIR/summary.txt"
{
    echo "Campaign started $(date)"
    echo "NUM_RUNS=$NUM_RUNS DRIVER=$DRIVER"
    echo "CAMPAIGN_DIR=$CAMPAIGN_DIR"
    echo "INNER_SCRIPT=$INNER_SCRIPT"
    echo
} | tee "$SUMMARY"

# Shared inputs the inner script references via relative paths from cwd.
# Symlinked into each run dir so the inner script's references resolve.
SHARED_LINKS=(spack data)
[[ "$DRIVER" == "mofka" ]] && SHARED_LINKS+=(mofka.json mofka-config.env)

for ((i=1; i<=NUM_RUNS; i++)); do
    RUN_DIR="$CAMPAIGN_DIR/run-$(printf '%03d' "$i")"
    mkdir -p "$RUN_DIR"

    for f in "${SHARED_LINKS[@]}"; do
        if [[ -e "$PBS_O_WORKDIR/$f" ]]; then
            ln -sfn "$PBS_O_WORKDIR/$f" "$RUN_DIR/$f"
        else
            echo "WARNING: $PBS_O_WORKDIR/$f does not exist; not symlinking" | tee -a "$SUMMARY"
        fi
    done

    echo "========== Run $i / $NUM_RUNS  ($RUN_DIR) ==========" | tee -a "$SUMMARY"
    start=$(date +%s)
    RUN_DIR="$RUN_DIR" bash "$INNER_SCRIPT" \
        > "$RUN_DIR/campaign-run.out" 2> "$RUN_DIR/campaign-run.err"
    rc=$?
    elapsed=$(( $(date +%s) - start ))
    echo "run $i: rc=$rc elapsed=${elapsed}s" | tee -a "$SUMMARY"
done

echo | tee -a "$SUMMARY"
echo "Campaign complete $(date). Summary: $SUMMARY" | tee -a "$SUMMARY"
