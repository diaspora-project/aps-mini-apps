#!/bin/bash

# Run the tekapp pipeline (DAQ -> DIST -> SIRT -> DEN) using the Apptainer
# image built by platforms/apptainer/build.sh, with the diaspora "files"
# driver. SIRT uses host mpiexec with per-rank `apptainer exec` (host MPI
# bind-in), so the host must have an MPI launcher ABI-compatible with the
# container's mpich.

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &>/dev/null && pwd )"
PROJECT_ROOT="$( cd -- "$SCRIPT_DIR/../.." &>/dev/null && pwd )"
SIF="$PROJECT_ROOT/build/apptainer/tekapp.sif"
WORKDIR="$PROJECT_ROOT"
cd "$WORKDIR"

if [[ ! -f "$SIF" ]]; then
    echo "ERROR: $SIF not found. Run platforms/apptainer/build.sh first." >&2
    exit 1
fi

# Bind the project root so the container sees ./aps-miniapp-data and the
# driver config file at the same path as on the host.
APP="apptainer exec --bind $WORKDIR:$WORKDIR --pwd $WORKDIR $SIF"

SIRT_RANKS=2

DIASPORA_CTL_DRIVER_ARGS="--driver files --driver.root_path ./aps-miniapp-data"
rm -rf aps-miniapp-data

echo "Starting topic creations"
# DAQ -> DIST topic
$APP diaspora-ctl topic create --name daq_dist $DIASPORA_CTL_DRIVER_ARGS --topic.num_partitions 1
# DIST topics
$APP diaspora-ctl topic create --name dist_sirt $DIASPORA_CTL_DRIVER_ARGS --topic.num_partitions $SIRT_RANKS
$APP diaspora-ctl topic create --name handshake_s_d $DIASPORA_CTL_DRIVER_ARGS --topic.num_partitions 1
$APP diaspora-ctl topic create --name handshake_d_s $DIASPORA_CTL_DRIVER_ARGS --topic.num_partitions $SIRT_RANKS
# SIRT -> DEN topic (one partition per SIRT rank to avoid concurrent write conflicts)
$APP diaspora-ctl topic create --name sirt_den $DIASPORA_CTL_DRIVER_ARGS --topic.num_partitions $SIRT_RANKS

echo '{"root_path":"./aps-miniapp-data"}' > diaspora-files-driver-config.json
DRIVER_ARGS="--driver_type files --driver_config_file diaspora-files-driver-config.json"

echo "Completed topic creations"

echo "Launching DAQ"
$APP tekapp-daq --mode 2 \
    --num_sinograms 2 --num_sinogram_columns 2560 --num_sinogram_projections 16 \
    --batchsize 4 $DRIVER_ARGS 1>daq.out 2>daq.err &
DAQ_PID=$!
echo "DAQ launched with PID $DAQ_PID"

echo "Launching DIST"
$APP tekapp-dist --cast_to_float32 \
    --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560 --batchsize 4 \
    $DRIVER_ARGS 1>dist.out 2>dist.err &
DIST_PID=$!
echo "DIST launched with PID $DIST_PID"

echo "Launching SIRT (host mpiexec, apptainer exec per rank)"
mpiexec -n $SIRT_RANKS $APP tekapp-sirt --write-freq 4 \
    --window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 \
    $DRIVER_ARGS --batchsize 4 1>sirt.out 2>sirt.err &
SIRT_PID=$!
echo "SIRT launched with PID $SIRT_PID"

echo "Launching DEN"
$APP tekapp-denoiser \
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
