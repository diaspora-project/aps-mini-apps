#!/bin/bash

# Run the tekapp pipeline (DAQ -> DIST -> SIRT -> DEN) using the Docker
# image built by platforms/docker/build.sh, against a Mofka deployment that
# also runs in a container (image-tagged `tekapp:latest`, started via
# bedrock). SIRT uses host mpiexec with per-rank `docker run` — the host's
# MPI launcher must be ABI-compatible with the container's mpich and
# `--network host` is required so ranks and bedrock share a network namespace.
#
# Prereq: run `bash scripts/configure-mofka.sh --platform local` once on the
# host to generate mofka.json + mofka-config.env in the cwd. Use --from-file
# with the saved mofka-answers.env for reproducible runs.
#
# Bind-mounting $WORKDIR:$WORKDIR plus `-u $(id -u):$(id -g)` keeps file
# ownership correct on Linux hosts; on Docker Desktop the uid/gid mapping is
# handled by the VM and the `-u` flag may need to be dropped.

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &>/dev/null && pwd )"
PROJECT_ROOT="$( cd -- "$SCRIPT_DIR/../.." &>/dev/null && pwd )"
IMAGE="tekapp:latest"
WORKDIR="$PROJECT_ROOT"
cd "$WORKDIR"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "ERROR: docker image '$IMAGE' not found. Run platforms/docker/build.sh first." >&2
    exit 1
fi

if [[ ! -f mofka-config.env || ! -f mofka.json ]]; then
    echo "ERROR: mofka-config.env / mofka.json missing in $(pwd)." >&2
    echo "       Run: bash $PROJECT_ROOT/scripts/configure-mofka.sh --platform local" >&2
    exit 1
fi
source mofka-config.env

SIRT_RANKS="${SIRT_RANKS:-2}"

APP="docker run --rm --network host -u $(id -u):$(id -g) -v $WORKDIR:$WORKDIR -w $WORKDIR $IMAGE"
BEDROCK_APP="docker run --rm --network host -u $(id -u):$(id -g) -v $WORKDIR:$WORKDIR -w $WORKDIR --name tekapp-bedrock $IMAGE"

echo "Deploying Mofka (protocol: $BEDROCK_PROTOCOL)"
$BEDROCK_APP bedrock "$BEDROCK_PROTOCOL" -c mofka.json -v trace 1> mofka.out 2> mofka.err &
MOFKA_PID=$!

sleep 2

DIASPORA_CTL_DRIVER_ARGS="--driver mofka --driver.group_file $MOFKA_FLOCK_FILE"

echo "Starting topic creations"
for topic in "${MOFKA_TOPICS[@]}"; do
    flags_var="MOFKA_TOPIC_FLAGS_${topic}"
    $APP diaspora-ctl topic create --name "$topic" $DIASPORA_CTL_DRIVER_ARGS ${!flags_var}
done

echo "{\"group_file\":\"./$MOFKA_FLOCK_FILE\"}" > diaspora-mofka-driver-config.json
DRIVER_ARGS="--driver_type mofka --driver_config_file diaspora-mofka-driver-config.json"

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

echo "Launching SIRT (host mpiexec, docker run per rank)"
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

PIDS=("$DAQ_PID" "$DIST_PID" "$SIRT_PID" "$DEN_PID" "$MOFKA_PID")

cleanup() {
    kill "${PIDS[@]}" 2>/dev/null || true
    docker rm -f tekapp-bedrock 2>/dev/null || true
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
$APP bedrock-shutdown "$BEDROCK_PROTOCOL" -f "$MOFKA_FLOCK_FILE"
wait $MOFKA_PID || true
echo "Run completed successfully"
