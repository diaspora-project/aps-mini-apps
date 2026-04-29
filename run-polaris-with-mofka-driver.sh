#!/usr/bin/env bash
#PBS -l select=6
#PBS -l walltime=01:00:00
#PBS -N APS
#PBS -q debug-scaling
#PBS -l filesystems=home:eagle

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

echo "Defining workflow topology/mapping"
nodes=($(cat "$PBS_NODEFILE"))
node_mofka=${nodes[0]}
node_daq=${nodes[1]}
node_dist=${nodes[2]}
node_den=${nodes[3]}
node_sirt=("${nodes[@]:4}")

echo "Mofka node: ${node_mofka}"
echo "DAQ node: ${node_daq}"
echo "DIST node: ${node_dist}"
echo "DEN node: ${node_den}"
echo "SIRT node(s): ${node_sirt[@]}"

nnodes=`wc -l < $PBS_NODEFILE`
sirt_ranks=$(((nnodes - 4)*2))
printf "%s\n" "${node_sirt[@]}" > sirt_file

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

simple_mpiexec() {
    # Note: we use node_daq as simple_mpiexec is used only before DAQ is launched
    mpiexec --single-node-vni -n 1 --ppn 1 --hosts "${node_daq}" $@
}

rm mofka.flock || true

#export HG_LOG_LEVEL=debug
#export FI_LOG_LEVEL=Debug

echo "Deploying Mofka"
mpiexec --single-node-vni -n 1 --ppn 1 --hosts $node_mofka \
    bedrock cxi -c mofka.json 1> mofka.out 2> mofka.err &
MOFKA_PID=$!

echo "Waiting for mofka.flock file to be created"
while [ ! -f "mofka.flock" ]; do
    sleep 1
done
sleep 5

DIASPORA_CTL_DRIVER_ARGS="--driver mofka --driver.group_file mofka.flock"

echo "Starting topic creations"
# setup topics and partitions
echo "Creating DAQ -> DIST topic"
simple_mpiexec diaspora-ctl topic create --name daq_dist $DIASPORA_CTL_DRIVER_ARGS --topic.partitions 1
echo "Creating  DIST topics (dist_sirt, handshake_s_d, handshake_d_s)"
simple_mpiexec diaspora-ctl topic create --name dist_sirt $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $sirt_ranks
simple_mpiexec diaspora-ctl topic create --name handshake_s_d $DIASPORA_CTL_DRIVER_ARGS --topic.partitions 1
simple_mpiexec diaspora-ctl topic create --name handshake_d_s $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $sirt_ranks
echo "Creating  SIRT -> DEN topic (one partition per SIRT rank to avoid concurrent write conflicts)"
simple_mpiexec diaspora-ctl topic create --name sirt_den $DIASPORA_CTL_DRIVER_ARGS --topic.partitions $sirt_ranks

echo "Completed topic creations"

echo '{"group_file":"./mofka.flock"}' > diaspora-mofka-driver-config.json
DRIVER_ARGS="--driver_type mofka --driver_config_file diaspora-mofka-driver-config.json"

echo "Launching DAQ"
# Launch DAQ
mpiexec --single-node-vni -n 1 --ppn 1 -d 16 --hosts $node_daq \
    python ./build/python/streamer-daq/DAQStream.py --mode 1 --simulation_file \
        ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize 4 \
        --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
        --synch_count 1 $DRIVER_ARGS 1>daq.out 2>daq.err &
DAQ_PID=$!
echo "DAQ launched with PID $DAQ_PID"

echo "Launching DIST"
# Launch Dist
mpiexec --single-node-vni -n 1 --ppn 1 -d 16 --hosts $node_dist \
    python ./build/python/streamer-dist/ModDistStreamPubDemo.py  --cast_to_float32 \
        --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560  --batchsize 4 \
        $DRIVER_ARGS 1>dist.out 2>dist.err &
DIST_PID=$!
echo "DIST launched with PID $DIST_PID"

echo "Launching SIRT"
# Launch SIRT
mpiexec --single-node-vni -n $sirt_ranks --ppn 2 --line-buffer -l -d 16 --hostfile sirt_file \
    ./build/bin/sirt_stream --write-freq 4  \
        --window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 \
        $DRIVER_ARGS --batchsize 4 1>sirt.out 2>sirt.err &
SIRT_PID=$!
echo "SIRT launched with PID $SIRT_PID"

echo "Launching DEN"
# Launch DEN
mpiexec --single-node-vni -n 1 --ppn 1 -d 16 --hosts $node_den \
    python ./build/python/streamer-denoiser/denoiser.py \
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
simple_mpiexec bedrock-shutdown cxi -f mofka.flock
wait $MOFKA_PID || true
echo "Run completed successfully"
