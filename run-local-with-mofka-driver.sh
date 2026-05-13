#!/bin/bash

PROTOCOL=${PROTOCOL:-"na+sm"}

eval `spack env activate --sh tekapp-env`

SIRT_RANKS=2

echo "Creating Mofka configuration file (mofka.json)"
cat >mofka.json <<EOL
{
    "libraries": [
        "libflock-bedrock-module.so",
        "libyokan-bedrock-module.so",
        "libwarabi-bedrock-module.so",
        "libmofka-bedrock-module.so"
    ],
    "providers": [
        {
            "name" : "group_manager",
            "type" : "flock",
            "provider_id" : 1,
            "config": {
                "bootstrap": "self",
                "file": "mofka.flock",
                "group": {
                    "type": "static"
                }
            }
        },
        {
            "name": "master",
            "provider_id": 2,
            "type": "yokan",
            "tags" : [ "mofka:master" ],
            "config" : {
                "database" : {
                    "type": "map"
                }
            }
        }
    ]
}
EOL

echo "Deploying Mofka"
bedrock $PROTOCOL -c mofka.json -v trace 1> mofka.out 2> mofka.err &
MOFKA_PID=$!

sleep 2

DIASPORA_CTL_DRIVER_ARGS="--driver mofka --driver.group_file mofka.flock"

echo "Starting topic creations"
# setup topics and partitions
# DAQ -> DIST topic
diaspora-ctl topic create --name daq_dist $DIASPORA_CTL_DRIVER_ARGS --topic.partitions 1
# DIST topics
diaspora-ctl topic create --name dist_sirt $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $SIRT_RANKS
diaspora-ctl topic create --name handshake_s_d $DIASPORA_CTL_DRIVER_ARGS --topic.partitions 1
diaspora-ctl topic create --name handshake_d_s $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $SIRT_RANKS
# SIRT -> DEN topic (one partition per SIRT rank to avoid concurrent write conflicts)
diaspora-ctl topic create --name sirt_den $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $SIRT_RANKS

echo '{"group_file":"./mofka.flock"}' > diaspora-mofka-driver-config.json
DRIVER_ARGS="--driver_type mofka --driver_config_file diaspora-mofka-driver-config.json"

echo "Completed topic creations"

export PYTHONPATH=$(pwd)/build/python:${PYTHONPATH:-}

echo "Launching DAQ"
# Launch DAQ in "mode 1" (data coming from an HDF5 file)
#python -m tekapp.streamer_daq --mode 1 --simulation_file \
#    ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize 4 \
#    --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
#    --synch_count 1 $DRIVER_ARGS 1>daq.out 2>daq.err &
# Launchd DAQ in "mode 2" (syntetic data generation)
python -m tekapp.streamer_daq --mode 2 \
    --num_sinograms 2 --num_sinogram_columns 2560 --num_sinogram_projections 16 \
    --batchsize 4 $DRIVER_ARGS 1>daq.out 2>daq.err &
DAQ_PID=$!
echo "DAQ launched with PID $DAQ_PID"

echo "Launching DIST"
# Launch Dist
python -m tekapp.streamer_dist  --cast_to_float32 \
    --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560  --batchsize 4 \
    $DRIVER_ARGS 1>dist.out 2>dist.err &
DIST_PID=$!
echo "DIST launched with PID $DIST_PID"

echo "Launching SIRT"
# Launch SIRT
mpiexec -n $SIRT_RANKS ./build/bin/sirt_stream --write-freq 4  \
    --window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 \
    $DRIVER_ARGS --batchsize 4 1>sirt.out 2>sirt.err &
SIRT_PID=$!
echo "SIRT launched with PID $SIRT_PID"

echo "Launching DEN"
# Launch DEN
python -m tekapp.streamer_denoiser \
    --model ./build/python/streamer-denoiser/testA40GPU-it07500.h5 \
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
bedrock-shutdown $PROTOCOL -f mofka.flock
wait $MOFKA_PID || true
echo "Run completed successfully"
