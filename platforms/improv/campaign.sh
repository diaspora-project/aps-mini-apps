#!/usr/bin/env bash
#PBS -l select=5:ncpus=128:mpiprocs=128
#PBS -l place=scatter
#PBS -l walltime=04:00:00
#PBS -N APS-campaign
#PBS -q debug
#PBS -A radix-io

# Campaign driver: run platforms/improv/run-with-<DRIVER>-driver.sh
# NUM_RUNS times from $PBS_O_WORKDIR (same cwd the inner scripts already
# use). After each run, the per-component logs (*.out, *.err, *.ts.txt)
# are moved into $PBS_O_WORKDIR/campaigns/<jobid>/run-NNN/, and
# cleanup.sh is run between iterations to reset the workdir state.
# A failed run is recorded but does not abort the campaign.
#
# Prereq for DRIVER=mofka: run `bash scripts/configure-mofka.sh
# --platform improv` once in $PBS_O_WORKDIR before submitting.
# Node count: #PBS -l select=5 must be >= what the mofka inner script
# needs (BEDROCK_NODES + 3 + SIRT_NODES); 4 is enough for files-only.

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
CLEANUP_SCRIPT="$PBS_O_WORKDIR/cleanup.sh"
[[ -r "$INNER_SCRIPT" ]]   || { echo "ERROR: $INNER_SCRIPT not found" >&2; exit 2; }
[[ -r "$CLEANUP_SCRIPT" ]] || { echo "ERROR: $CLEANUP_SCRIPT not found" >&2; exit 2; }

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

for ((i=1; i<=NUM_RUNS; i++)); do
    RUN_DIR="$CAMPAIGN_DIR/run-$(printf '%03d' "$i")"

    echo "========== Run $i / $NUM_RUNS  ($RUN_DIR) ==========" | tee -a "$SUMMARY"
    start=$(date +%s)
    bash "$INNER_SCRIPT" > "campaign-run.out" 2> "campaign-run.err"
    rc=$?
    elapsed=$(( $(date +%s) - start ))
    echo "run $i: rc=$rc elapsed=${elapsed}s" | tee -a "$SUMMARY"

    # Stash this run's outputs before cleanup wipes them.
    mkdir -p "$RUN_DIR"
    for f in daq.out daq.err dist.out dist.err sirt.out sirt.err \
             den.out den.err mofka.out mofka.err \
             campaign-run.out campaign-run.err; do
        [[ -e "$f" ]] && mv "$f" "$RUN_DIR/" || true
    done
    # *.ts.txt timestamps (per component, may be ranked: sirt.0.ts.txt etc.)
    for f in *.ts.txt; do
        [[ -e "$f" ]] && mv "$f" "$RUN_DIR/" || true
    done

    # Reset workdir state for the next run (removes the files the inner
    # script left behind: aps-miniapp-data/, *.h5, mofka.flock, etc.)
    if (( i < NUM_RUNS )); then
        echo "running cleanup.sh" | tee -a "$SUMMARY"
        bash "$CLEANUP_SCRIPT" >> "$RUN_DIR/cleanup.out" 2>> "$RUN_DIR/cleanup.err" || true
    fi
done

echo | tee -a "$SUMMARY"
echo "Campaign complete $(date). Summary: $SUMMARY" | tee -a "$SUMMARY"
