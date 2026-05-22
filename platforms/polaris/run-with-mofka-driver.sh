#!/usr/bin/env bash
#PBS -l select=6
#PBS -l walltime=01:00:00
#PBS -N APS
#PBS -q debug-scaling
#PBS -l filesystems=home:eagle

# Prereq: run `bash scripts/configure-mofka.sh --platform polaris` once to
# generate mofka.json + mofka-config.env in the cwd. Use --from-file with the
# saved mofka-answers.env for reproducible jobs.
#
# Note: the static `#PBS -l select=6` above must be >= BEDROCK_NODES + 3 +
# ceil(SIRT_RANKS / 2). If BEDROCK_NODES > 1, edit `select=` to match.

# Some Polaris nodes are occasionally assigned without a usable CXI (HPE
# Slingshot) service, which breaks libfabric/mpich at startup. We probe
# each node with `cxi_service list -s 1`; nodes where it fails are listed
# on stdout. If IGNORE_NODES_WITHOUT_CXI_SERVICE=true, those nodes are
# dropped from the workflow topology (provided enough viable nodes remain);
# otherwise the script aborts.
IGNORE_NODES_WITHOUT_CXI_SERVICE=true
MIN_VIABLE_NODES=5

set -euo pipefail

echo "####################################################"
echo "User: $PBS_O_LOGNAME"
echo "Batch job started on $PBS_O_HOST"
echo "PBS job id: $PBS_JOBID"
echo "PBS job name: $PBS_JOBNAME"
echo "PBS working directory: $PBS_O_WORKDIR"
echo "Job started on" `hostname` `date`
echo "Current directory:" `pwd`
echo "PBS environment: $PBS_ENVIRONMENT"
echo "####################################################"
echo "####################################################"
echo "The Job is being executed on the following node:"
cat ${PBS_NODEFILE}
echo "####################################################"

export PALS_LOCAL_LAUNCH=0

cd $PBS_O_WORKDIR

echo "Loading modules"
ml load PrgEnv-gnu
ml load cray-mpich
ml load libfabric

echo "Activating environment"
export SPACK_DISABLE_LOCAL_CONFIG=true
eval `spack/bin/spack env activate --sh APS`

# Use the build tree if present; otherwise rely on the installed tekapp-* on PATH.
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &>/dev/null && pwd )"
if [ -d "$SCRIPT_DIR/../../build/bin" ]; then
    export PATH="$SCRIPT_DIR/../../build/bin:$PATH"
    export PYTHONPATH="$SCRIPT_DIR/../../build/python:${PYTHONPATH:-}"
fi

if [[ ! -f mofka-config.env || ! -f mofka.json ]]; then
    echo "ERROR: mofka-config.env / mofka.json missing in $(pwd)." >&2
    echo "       Run: bash scripts/configure-mofka.sh --platform polaris" >&2
    exit 1
fi
source mofka-config.env

echo "Probing CXI service on each allocated node"
all_nodes=($(cat "$PBS_NODEFILE"))
viable_nodes=()
bad_nodes=()
for n in "${all_nodes[@]}"; do
    if ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o ConnectTimeout=10 \
            "$n" 'cxi_service list -s 1' >/dev/null 2>&1; then
        viable_nodes+=("$n")
    else
        bad_nodes+=("$n")
    fi
done

if (( ${#bad_nodes[@]} > 0 )); then
    echo "Nodes without functional CXI service:"
    printf '  %s\n' "${bad_nodes[@]}"
    if [[ "${IGNORE_NODES_WITHOUT_CXI_SERVICE}" != "true" ]]; then
        echo "ERROR: IGNORE_NODES_WITHOUT_CXI_SERVICE is not true; aborting." >&2
        exit 1
    fi
    if (( ${#viable_nodes[@]} < MIN_VIABLE_NODES )); then
        echo "ERROR: only ${#viable_nodes[@]} viable node(s) remain after filtering;" >&2
        echo "       need at least ${MIN_VIABLE_NODES} to run the workflow." >&2
        exit 1
    fi
    echo "Continuing with ${#viable_nodes[@]} viable node(s) after filtering."
    # Rewrite PBS_NODEFILE so any downstream tool (mpiexec --hostfile, etc.)
    # sees only the viable subset.
    PBS_NODEFILE="$PBS_O_WORKDIR/pbs_nodefile.filtered"
    printf '%s\n' "${viable_nodes[@]}" > "$PBS_NODEFILE"
    export PBS_NODEFILE
fi

echo "Defining workflow topology/mapping"
nodes=("${viable_nodes[@]}")
mofka_nodes=("${nodes[@]:0:$BEDROCK_NODES}")
node_daq=${nodes[$BEDROCK_NODES]}
node_dist=${nodes[$((BEDROCK_NODES+1))]}
node_den=${nodes[$((BEDROCK_NODES+2))]}
node_sirt=("${nodes[@]:$((BEDROCK_NODES+3))}")

echo "Mofka node(s): ${mofka_nodes[*]}"
echo "DAQ node: ${node_daq}"
echo "DIST node: ${node_dist}"
echo "DEN node: ${node_den}"
echo "SIRT node(s): ${node_sirt[@]}"

nnodes=`wc -l < $PBS_NODEFILE`
sirt_ranks=$(((nnodes - BEDROCK_NODES - 3)*2))
printf "%s\n" "${node_sirt[@]}" > sirt_file

simple_mpiexec() {
    # Note: we use node_daq as simple_mpiexec is used only before DAQ is launched
    mpiexec --single-node-vni -n 1 --ppn 1 --hosts "${node_daq}" $@
}

rm -f "$MOFKA_FLOCK_FILE"

#export HG_LOG_LEVEL=debug
#export FI_LOG_LEVEL=Debug

total_bedrock=$((BEDROCK_NODES * BEDROCK_PPN))
mofka_host_list=$(IFS=,; echo "${mofka_nodes[*]}")
echo "Deploying Mofka ($total_bedrock proc(s) on ${BEDROCK_NODES} node(s), protocol: $BEDROCK_PROTOCOL)"
mpiexec --single-node-vni -n "$total_bedrock" --ppn "$BEDROCK_PPN" --hosts "$mofka_host_list" \
    bedrock "$BEDROCK_PROTOCOL" -c mofka.json 1> mofka.out 2> mofka.err &
MOFKA_PID=$!

echo "Waiting for $MOFKA_FLOCK_FILE file to be created"
while [ ! -f "$MOFKA_FLOCK_FILE" ]; do
    sleep 1
done
sleep 5

DIASPORA_CTL_DRIVER_ARGS="--driver mofka --driver.group_file $MOFKA_FLOCK_FILE"

echo "Starting topic creations"
for topic in "${MOFKA_TOPICS[@]}"; do
    flags_var="MOFKA_TOPIC_FLAGS_${topic}"
    echo "  creating topic '$topic'"
    simple_mpiexec diaspora-ctl topic create --name "$topic" \
        $DIASPORA_CTL_DRIVER_ARGS ${!flags_var}
done

echo "Completed topic creations"

echo "{\"group_file\":\"./$MOFKA_FLOCK_FILE\"}" > diaspora-mofka-driver-config.json
DRIVER_ARGS="--driver_type mofka --driver_config_file diaspora-mofka-driver-config.json"

echo "Launching DAQ"
# Launch DAQ
mpiexec --single-node-vni -n 1 --ppn 1 -d 16 --hosts $node_daq \
    tekapp-daq --mode 1 --simulation_file \
        ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize 4 \
        --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
        --synch_count 1 $DRIVER_ARGS 1>daq.out 2>daq.err &
DAQ_PID=$!
echo "DAQ launched with PID $DAQ_PID"

echo "Launching DIST"
# Launch Dist
mpiexec --single-node-vni -n 1 --ppn 1 -d 16 --hosts $node_dist \
    tekapp-dist  --cast_to_float32 \
        --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560  --batchsize 4 \
        $DRIVER_ARGS 1>dist.out 2>dist.err &
DIST_PID=$!
echo "DIST launched with PID $DIST_PID"

echo "Launching SIRT"
# Launch SIRT
mpiexec --single-node-vni -n $sirt_ranks --ppn 2 --line-buffer -l -d 16 --hostfile sirt_file \
    tekapp-sirt --write-freq 4  \
        --window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 \
        $DRIVER_ARGS --batchsize 4 1>sirt.out 2>sirt.err &
SIRT_PID=$!
echo "SIRT launched with PID $SIRT_PID"

echo "Launching DEN"
# Launch DEN
mpiexec --single-node-vni -n 1 --ppn 1 -d 16 --hosts $node_den \
    tekapp-denoiser \
        --model testA40GPU-it07500.h5 \
        $DRIVER_ARGS --batchsize 4 --nproc_sirt 2 1>den.out 2>den.err &
DEN_PID=$!
echo "DEN launched with PID $DEN_PID"

echo "Waiting for all the commands to finish"

declare -A NAME ERR

NAME[$DAQ_PID]="DAQ"
NAME[$DIST_PID]="DIST"
NAME[$SIRT_PID]="SIRT"
NAME[$DEN_PID]="DEN"

ERR[$DAQ_PID]="daq.err"
ERR[$DIST_PID]="dist.err"
ERR[$SIRT_PID]="sirt.err"
ERR[$DEN_PID]="den.err"

PIDS=("$DAQ_PID" "$DIST_PID" "$SIRT_PID" "$DEN_PID" "$MOFKA_PID")

cleanup() {
    kill "${PIDS[@]}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

running=("$DAQ_PID" "$DIST_PID" "$SIRT_PID" "$DEN_PID")

while ((${#running[@]})); do
    # Wait for any process to exit
    wait -n -p exited_pid "${running[@]}"
    status=$?

    name=${NAME[$exited_pid]}
    errfile=${ERR[$exited_pid]}

    echo "$name exited with code $status"

    # Remove exited PID from running list
    for i in "${!running[@]}"; do
        [[ ${running[i]} -eq $exited_pid ]] && unset 'running[i]'
    done

    if (( status != 0 )); then
        echo "====================== STDERR from $name ===================="
        cat "$errfile"
        echo "================== END OF STDERR from $name =================="

        echo "Killing remaining processes..."
        kill "${running[@]}" 2>/dev/null || true
        exit "$status"
    fi
done

echo "All components finished, shutting down Mofka"
simple_mpiexec bedrock-shutdown "$BEDROCK_PROTOCOL" -f "$MOFKA_FLOCK_FILE"
wait $MOFKA_PID || true
echo "Run completed successfully"
