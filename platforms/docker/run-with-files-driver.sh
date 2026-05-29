#!/bin/bash

# Variant selector: 'tekapp' (real pipeline) or 'fakapp' (protocol-only
# stand-in built from src/fake_sirt + python/fakapp). Override on the
# command line, e.g.: APP_NAME=fakapp bash platforms/docker/run-with-files-driver.sh
APP_NAME="${APP_NAME:-tekapp}"

# Run the tekapp pipeline (DAQ -> DIST -> SIRT -> DEN) using the Docker
# image built by platforms/docker/build.sh, with the diaspora "files"
# driver. SIRT uses host mpiexec with per-rank `docker run` (host MPI
# launcher invoking a containerised binary), so the host must have an MPI
# launcher ABI-compatible with the container's mpich and `--network host`
# is required so the ranks share a network namespace.
#
# If your host MPI is not compatible (e.g. Docker Desktop on macOS/Windows),
# replace the SIRT block with a single-container inner mpiexec, e.g.:
#     $APP mpiexec -n $SIRT_RANKS ${APP_NAME}-sirt ...
# (you lose host-launcher integration but it's fully portable).
#
# Bind-mounting $WORKDIR:$WORKDIR plus `-u $(id -u):$(id -g)` keeps file
# ownership correct on Linux hosts; on Docker Desktop the uid/gid mapping
# is handled by the VM and the `-u` flag may need to be dropped.

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &>/dev/null && pwd )"
PROJECT_ROOT="$( cd -- "$SCRIPT_DIR/../.." &>/dev/null && pwd )"
IMAGE="tekapp:latest"
WORKDIR="$PROJECT_ROOT"
cd "$WORKDIR"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "ERROR: docker image '$IMAGE' not found. Run platforms/docker/build.sh first." >&2
    exit 1
fi

# Bind the project root so the container sees ./aps-miniapp-data and the
# driver config file at the same path as on the host.
APP="docker run --rm --network host -u $(id -u):$(id -g) -v $WORKDIR:$WORKDIR -w $WORKDIR $IMAGE"

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
$APP ${APP_NAME}-daq --mode 2 \
    --num_sinograms 2 --num_sinogram_columns 2560 --num_sinogram_projections 16 \
    --batchsize 4 $DRIVER_ARGS 1>daq.out 2>daq.err &
DAQ_PID=$!
echo "DAQ launched with PID $DAQ_PID"

echo "Launching DIST"
$APP ${APP_NAME}-dist --cast_to_float32 \
    --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560 --batchsize 4 \
    $DRIVER_ARGS 1>dist.out 2>dist.err &
DIST_PID=$!
echo "DIST launched with PID $DIST_PID"

echo "Launching SIRT (host mpiexec, docker run per rank)"
mpiexec -n $SIRT_RANKS $APP ${APP_NAME}-sirt --write-freq 4 \
    --window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 \
    $DRIVER_ARGS --batchsize 4 1>sirt.out 2>sirt.err &
SIRT_PID=$!
echo "SIRT launched with PID $SIRT_PID"

echo "Launching DEN"
$APP ${APP_NAME}-denoiser \
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
