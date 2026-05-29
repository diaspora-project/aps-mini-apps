#!/usr/bin/env bash
#PBS -l select=4:ncpus=128:mpiprocs=128
#PBS -l place=scatter
#PBS -l walltime=01:00:00
#PBS -N APS
#PBS -q debug
#PBS -A radix-io

# Variant selector: 'tekapp' (real pipeline) or 'fakapp' (protocol-only
# stand-in built from src/fake_sirt + python/fakapp). Override on the
# command line, e.g.: APP_NAME=fakapp qsub platforms/improv/run-with-files-driver.sh
APP_NAME="${APP_NAME:-tekapp}"

# Improv run script using the diaspora "files" driver.
# Node layout: nodes[0]=DAQ, [1]=DIST, [2]=DEN, [3..]=SIRT.

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

node_daq=${nodes[0]}
node_dist=${nodes[1]}
node_den=${nodes[2]}
node_sirt=("${nodes[@]:3}")

echo "DAQ node: ${node_daq} (${SLOTS_PER_NODE[$node_daq]} slots)"
echo "DIST node: ${node_dist} (${SLOTS_PER_NODE[$node_dist]} slots)"
echo "DEN node: ${node_den} (${SLOTS_PER_NODE[$node_den]} slots)"
echo "SIRT node(s): ${node_sirt[@]}"

nnodes=${#nodes[@]}
sirt_ranks=$(((nnodes - 3)*2))
: > sirt_file
for n in "${node_sirt[@]}"; do
    echo "$n slots=${SLOTS_PER_NODE[$n]}" >> sirt_file
done

simple_mpiexec() {
    # Used only before DAQ is launched; co-locates with node_daq.
    # --bind-to none is required by Mochi components that spawn extra threads.
    mpiexec -n 1 -N 1 --bind-to none --host "${node_daq}:${SLOTS_PER_NODE[$node_daq]}" $@
}

# Shared message store for the files driver. Must live on a filesystem
# visible from every node — $PBS_O_WORKDIR is on /gpfs/fs1 on Improv.
DATA_ROOT="$PBS_O_WORKDIR/aps-miniapp-data"
rm -rf "$DATA_ROOT"

DIASPORA_CTL_DRIVER_ARGS="--driver files --driver.root_path $DATA_ROOT"

echo "Starting topic creations"
simple_mpiexec diaspora-ctl topic create --name daq_dist $DIASPORA_CTL_DRIVER_ARGS --topic.partitions 1
simple_mpiexec diaspora-ctl topic create --name dist_sirt $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $sirt_ranks
simple_mpiexec diaspora-ctl topic create --name handshake_s_d $DIASPORA_CTL_DRIVER_ARGS --topic.partitions 1
simple_mpiexec diaspora-ctl topic create --name handshake_d_s $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $sirt_ranks
simple_mpiexec diaspora-ctl topic create --name sirt_den $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $sirt_ranks

echo "Completed topic creations"

echo "{\"root_path\":\"$DATA_ROOT\"}" > diaspora-files-driver-config.json
DRIVER_ARGS="--driver_type files --driver_config_file diaspora-files-driver-config.json"

echo "Launching DAQ"
mpiexec -n 1 -N 1 --bind-to none --host "${node_daq}:${SLOTS_PER_NODE[$node_daq]}" \
    ${APP_NAME}-daq --mode 1 --simulation_file \
        ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize 4 \
        --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
        --synch_count 1 $DRIVER_ARGS 1>daq.out 2>daq.err &
DAQ_PID=$!
echo "DAQ launched with PID $DAQ_PID"

echo "Launching DIST"
mpiexec -n 1 -N 1 --bind-to none --host "${node_dist}:${SLOTS_PER_NODE[$node_dist]}" \
    ${APP_NAME}-dist  --cast_to_float32 \
        --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560  --batchsize 4 \
        $DRIVER_ARGS 1>dist.out 2>dist.err &
DIST_PID=$!
echo "DIST launched with PID $DIST_PID"

echo "Launching SIRT"
mpiexec -n $sirt_ranks -N 2 --bind-to none --hostfile sirt_file \
    ${APP_NAME}-sirt --write-freq 4  \
        --window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 \
        $DRIVER_ARGS --batchsize 4 1>sirt.out 2>sirt.err &
SIRT_PID=$!
echo "SIRT launched with PID $SIRT_PID"

echo "Launching DEN"
mpiexec -n 1 -N 1 --bind-to none --host "${node_den}:${SLOTS_PER_NODE[$node_den]}" \
    ${APP_NAME}-denoiser \
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

PIDS=("$DAQ_PID" "$DIST_PID" "$SIRT_PID" "$DEN_PID")

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

echo "Run completed successfully"
