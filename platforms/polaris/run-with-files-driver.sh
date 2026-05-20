#!/usr/bin/env bash
#PBS -l select=5
#PBS -l walltime=01:00:00
#PBS -N APS
#PBS -q debug-scaling
#PBS -l filesystems=home:eagle

# Polaris run script using the diaspora "files" driver instead of Mofka.
# No bedrock server is needed — the files driver uses a shared directory
# (on Eagle/home) as the message store, so we save one node compared to
# run-with-mofka-driver.sh.

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

echo "Defining workflow topology/mapping"
nodes=($(cat "$PBS_NODEFILE"))
node_daq=${nodes[0]}
node_dist=${nodes[1]}
node_den=${nodes[2]}
node_sirt=("${nodes[@]:3}")

echo "DAQ node: ${node_daq}"
echo "DIST node: ${node_dist}"
echo "DEN node: ${node_den}"
echo "SIRT node(s): ${node_sirt[@]}"

nnodes=`wc -l < $PBS_NODEFILE`
sirt_ranks=$(((nnodes - 3)*2))
printf "%s\n" "${node_sirt[@]}" > sirt_file

simple_mpiexec() {
    # Used only before DAQ is launched; co-locates with node_daq.
    mpiexec --single-node-vni -n 1 --ppn 1 --hosts "${node_daq}" $@
}

# Shared message store for the files driver. Must live on a filesystem
# visible from every node — $PBS_O_WORKDIR is on home/eagle (per the
# `filesystems=home:eagle` PBS directive), so this works.
DATA_ROOT="$PBS_O_WORKDIR/aps-miniapp-data"
rm -rf "$DATA_ROOT"

DIASPORA_CTL_DRIVER_ARGS="--driver files --driver.root_path $DATA_ROOT"

echo "Starting topic creations"
echo "Creating DAQ -> DIST topic"
simple_mpiexec diaspora-ctl topic create --name daq_dist $DIASPORA_CTL_DRIVER_ARGS --topic.partitions 1
echo "Creating DIST -> SIRT topic (one partition per SIRT rank)"
simple_mpiexec diaspora-ctl topic create --name dist_sirt $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $sirt_ranks
echo "Creating SIRT -> DEN topic (one partition per SIRT rank to avoid concurrent write conflicts)"
simple_mpiexec diaspora-ctl topic create --name sirt_den $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $sirt_ranks

echo "Completed topic creations"

echo "{\"root_path\":\"$DATA_ROOT\"}" > diaspora-files-driver-config.json
DRIVER_ARGS="--driver_type files --driver_config_file diaspora-files-driver-config.json"

echo "Launching DAQ"
mpiexec --single-node-vni -n 1 --ppn 1 -d 16 --hosts $node_daq \
    tekapp-daq --mode 1 --simulation_file \
        ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize 4 \
        --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
        --synch_count 1 $DRIVER_ARGS 1>daq.out 2>daq.err &
DAQ_PID=$!
echo "DAQ launched with PID $DAQ_PID"

echo "Launching DIST"
mpiexec --single-node-vni -n 1 --ppn 1 -d 16 --hosts $node_dist \
    tekapp-dist  --cast_to_float32 \
        --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560  --batchsize 4 \
        $DRIVER_ARGS 1>dist.out 2>dist.err &
DIST_PID=$!
echo "DIST launched with PID $DIST_PID"

echo "Launching SIRT"
mpiexec --single-node-vni -n $sirt_ranks --ppn 2 --line-buffer -l -d 16 --hostfile sirt_file \
    tekapp-sirt --write-freq 4  \
        --window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 \
        $DRIVER_ARGS --batchsize 4 1>sirt.out 2>sirt.err &
SIRT_PID=$!
echo "SIRT launched with PID $SIRT_PID"

echo "Launching DEN"
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

PIDS=("$DAQ_PID" "$DIST_PID" "$SIRT_PID" "$DEN_PID")

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

echo "Run completed successfully"
