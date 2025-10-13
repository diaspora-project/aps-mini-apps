#!/bin/bash
#PBS -l select=2:system=polaris
#PBS -l walltime=01:00:00
#PBS -N Flink
#PBS -q debug-scaling

#file systems used by the job
#PBS -l filesystems=home:eagle

#Project name
#PBS -A diaspora

set -euo pipefail

# --- tiny helper: update ntask_sirt in a JSON file using jq (atomic write)

DIR="$HOME/diaspora/src/aps-mini-apps"
echo "DIR: $DIR"

num_sirts=(1 2 4 8 16)
failure_periods=(160 80 40 20 10)

TOP="$(cat "$DIR/recent-run")"

# optional: ensure we clean up the cluster if something fails mid-loop
cleanup_cluster() {
  # best-effort stop if the script dies in the middle
  if [[ -x "$WORKSPACE/polaris-exec-pipeline.sh" ]]; then
    (cd "$WORKSPACE" && bash polaris-exec-pipeline.sh) || true
  fi
}
trap cleanup_cluster EXIT

count=0
for num_sirt in "${num_sirts[@]}"; do
  for failure_period in "${failure_periods[@]}"; do
    taskmanager_per_node="$num_sirt"
    WORKSPACE="$TOP/num_sirt-$num_sirt-failure_period-$failure_period"

    mkdir -p "$WORKSPACE"
    cd "$WORKSPACE"

    echo "num_sirt: $num_sirt  failure_period: $failure_period ====================================="
    echo "Copy execution scripts from $DIR to workspace $WORKSPACE"
    rsync "$DIR/" "$WORKSPACE/" > /dev/null

    num_task=$num_sirt
    num_sinogram=$num_sirt
    echo "Run the test"
    bash polaris-exec-pipeline.sh \
        $num_sirt \
        $num_task \
        $num_sinogram \
        periodic \
        $failure_period \
        > test-log-num-sirt-$num_sirt-failure-period-$failure_period.out 2> test-log-num-sirt-$num_sirt-failure-period-$failure_period.err

    echo "Stop Flink cluster"
    cd "$WORKSPACE"
    bash stop-all.sh
    sleep 1

    count=$((count + 1))
  done
done

trap - EXIT
