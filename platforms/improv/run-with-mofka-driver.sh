#!/usr/bin/env bash
#PBS -l select=5:ncpus=128:mpiprocs=128
#PBS -l place=scatter
#PBS -l walltime=01:00:00
#PBS -N APS
#PBS -q debug
#PBS -A radix-io

# Improv run script using the Mofka driver.
# Node layout: nodes[0:BEDROCK_NODES]=MOFKA, then DAQ, DIST, DEN, then SIRT.
#
# Prereq: run `bash scripts/configure-mofka.sh --platform improv` once to
# generate mofka.json + mofka-config.env in the cwd. Use --from-file with the
# saved mofka-answers.env for reproducible jobs.
#
# Note: the static `#PBS -l select=5` above must be >= BEDROCK_NODES + 3 +
# SIRT_NODES (DAQ/DIST/DEN take one node each). Edit `select=` to match
# whenever BEDROCK_NODES or SIRT_NODES change. SIRT_NODES and SIRT_PPN come
# from mofka-config.env (see scripts/configure-mofka.sh --sirt-nodes / --sirt-ppn).

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

cd $PBS_O_WORKDIR

echo "Loading modules"
module load gcc/13.2.0
module load openmpi/5.0.2-gcc-13.2.0

echo "Activating environment"
export SPACK_DISABLE_LOCAL_CONFIG=true
source spack/share/spack/setup-env.sh
spack env activate tekapp-env

# Use the build tree if present; otherwise rely on the installed tekapp-* on PATH.
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &>/dev/null && pwd )"
if [ -d "$SCRIPT_DIR/../../build/bin" ]; then
    export PATH="$SCRIPT_DIR/../../build/bin:$PATH"
    export PYTHONPATH="$SCRIPT_DIR/../../build/python:${PYTHONPATH:-}"
fi

if [[ ! -f mofka-config.env || ! -f mofka.json ]]; then
    echo "ERROR: mofka-config.env / mofka.json missing in $(pwd)." >&2
    echo "       Run: bash scripts/configure-mofka.sh --platform improv" >&2
    exit 1
fi
source mofka-config.env

echo "Defining workflow topology/mapping"
# PBS_NODEFILE on Improv lists each node once per core, with the FQDN
# (e.g. "i006.lcrc.anl.gov"). openmpi's mpiexec wants short names and
# explicit slot counts ("--host node:N" or "node slots=N" in a hostfile),
# otherwise it counts one slot per --host occurrence and refuses to launch.
declare -A SLOTS_PER_NODE
nodes=()
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    short="${line%%.*}"
    if [[ -z "${SLOTS_PER_NODE[$short]:-}" ]]; then
        nodes+=("$short")
        SLOTS_PER_NODE[$short]=1
    else
        SLOTS_PER_NODE[$short]=$((SLOTS_PER_NODE[$short]+1))
    fi
done < "$PBS_NODEFILE"

# SIRT_NODES/SIRT_PPN/SIRT_RANKS come from mofka-config.env. They also drive
# the dist_sirt / handshake_d_s / sirt_den topic partition counts created by
# configure-mofka.sh, so launched ranks and partitions can never drift apart.
: "${SIRT_NODES:?missing in mofka-config.env — rerun configure-mofka.sh}"
: "${SIRT_PPN:?missing in mofka-config.env — rerun configure-mofka.sh}"
: "${SIRT_RANKS:?missing in mofka-config.env — rerun configure-mofka.sh}"

nnodes=${#nodes[@]}
required_nodes=$((BEDROCK_NODES + 3 + SIRT_NODES))
if (( nnodes < required_nodes )); then
    echo "ERROR: PBS allocated $nnodes node(s), but the workflow needs $required_nodes" >&2
    echo "       (BEDROCK_NODES=$BEDROCK_NODES + 3 for DAQ/DIST/DEN + SIRT_NODES=$SIRT_NODES)." >&2
    echo "       Edit '#PBS -l select=...' or rerun configure-mofka.sh with smaller --sirt-nodes." >&2
    exit 1
fi

mofka_nodes=("${nodes[@]:0:$BEDROCK_NODES}")
node_daq=${nodes[$BEDROCK_NODES]}
node_dist=${nodes[$((BEDROCK_NODES+1))]}
node_den=${nodes[$((BEDROCK_NODES+2))]}
node_sirt=("${nodes[@]:$((BEDROCK_NODES+3)):$SIRT_NODES}")

echo "Mofka node(s): ${mofka_nodes[*]}"
echo "DAQ node: ${node_daq} (${SLOTS_PER_NODE[$node_daq]} slots)"
echo "DIST node: ${node_dist} (${SLOTS_PER_NODE[$node_dist]} slots)"
echo "DEN node: ${node_den} (${SLOTS_PER_NODE[$node_den]} slots)"
echo "SIRT node(s): ${node_sirt[*]}  (SIRT_NODES=$SIRT_NODES, SIRT_PPN=$SIRT_PPN, SIRT_RANKS=$SIRT_RANKS)"

: > sirt_file
for n in "${node_sirt[@]}"; do
    # We override the physical slot count with SIRT_PPN so openmpi places
    # exactly SIRT_PPN ranks on each SIRT node, regardless of #cores.
    echo "$n slots=$SIRT_PPN" >> sirt_file
done

simple_mpiexec() {
    # Used only before DAQ is launched; co-locates with node_daq.
    # --bind-to none is required by Mochi components that spawn extra threads.
    mpiexec -n 1 -N 1 --bind-to none --host "${node_daq}:${SLOTS_PER_NODE[$node_daq]}" $@
}

rm -f "$MOFKA_FLOCK_FILE"

total_bedrock=$((BEDROCK_NODES * BEDROCK_PPN))
mofka_host_entries=()
for n in "${mofka_nodes[@]}"; do
    mofka_host_entries+=("${n}:${SLOTS_PER_NODE[$n]}")
done
mofka_host_list=$(IFS=,; echo "${mofka_host_entries[*]}")
echo "Deploying Mofka ($total_bedrock proc(s) on ${BEDROCK_NODES} node(s), protocol: $BEDROCK_PROTOCOL)"
mpiexec -n "$total_bedrock" -N "$BEDROCK_PPN" --bind-to none --host "$mofka_host_list" \
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
mpiexec -n 1 -N 1 --bind-to none --host "${node_daq}:${SLOTS_PER_NODE[$node_daq]}" \
    tekapp-daq --mode 1 --simulation_file \
        ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize 4 \
        --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
        --synch_count 1 $DRIVER_ARGS 1>daq.out 2>daq.err &
DAQ_PID=$!
echo "DAQ launched with PID $DAQ_PID"

echo "Launching DIST"
mpiexec -n 1 -N 1 --bind-to none --host "${node_dist}:${SLOTS_PER_NODE[$node_dist]}" \
    tekapp-dist  --cast_to_float32 \
        --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560  --batchsize 4 \
        $DRIVER_ARGS 1>dist.out 2>dist.err &
DIST_PID=$!
echo "DIST launched with PID $DIST_PID"

echo "Launching SIRT"
mpiexec -n "$SIRT_RANKS" -N "$SIRT_PPN" --bind-to none --hostfile sirt_file \
    tekapp-sirt --write-freq 4  \
        --window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 \
        $DRIVER_ARGS --batchsize 4 1>sirt.out 2>sirt.err &
SIRT_PID=$!
echo "SIRT launched with PID $SIRT_PID"

echo "Launching DEN"
mpiexec -n 1 -N 1 --bind-to none --host "${node_den}:${SLOTS_PER_NODE[$node_den]}" \
    tekapp-denoiser \
        --model testA40GPU-it07500.h5 \
        $DRIVER_ARGS --batchsize 4 --nproc_sirt "$SIRT_RANKS" 1>den.out 2>den.err &
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
    # `wait -n -p var` is bash 5.1+; the compute nodes ship an older bash.
    # Wait for any to exit, then scan to find which PID is gone.
    wait -n "${running[@]}"
    status=$?

    exited_pid=""
    for pid in "${running[@]}"; do
        if ! kill -0 "$pid" 2>/dev/null; then
            exited_pid=$pid
            break
        fi
    done
    [[ -z "$exited_pid" ]] && continue

    name=${NAME[$exited_pid]}
    errfile=${ERR[$exited_pid]}

    echo "$name exited with code $status"

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
