#!/bin/bash
#PBS -l select=5:system=polaris
#PBS -l walltime=01:00:00
#PBS -N APS
#PBS -q run_next

#file systems used by the job
#PBS -l filesystems=home:eagle

#Project name
#PBS -A Diaspora

# load modules and activate spack env
ml load PrgEnv-gnu
. ~/spack/share/spack/setup-env.sh
spack env activate APS_FULL

export MARGO_ENABLE_MONITORING=1
export MARGO_MONITORING_FILENAME_PREFIX=mofka
export MARGO_MONITORING_DISABLE_TIME_SERIES=true

set -eu
cd $PBS_O_WORKDIR

# Assuming you have four nodes allocated, use the PBS_NODEFILE to get the node names
nodes=$(cat "$PBS_NODEFILE")
nodes_array=($nodes)

node_daq=${nodes_array[0]}
node_dist=${nodes_array[1]}
node_sirt=${nodes_array[2]}
node_den=${nodes_array[3]}
node_mofka=${nodes_array[4]}

rm -rf mofka.json

#LD_PRELOAD=/home/agueroudji/spack/var/spack/environments/APS_VAL/.spack-env/view/lib/libasan.so.8  \
mpiexec -n 1 -ppn 1 -d 16 --hosts $node_mofka  bedrock cxi -v trace -c config.json 1> bedrock.out 2> bedrock.err &
BEDROCK_PID=$!

echo "Waiting for mofka file to be created"
while [ ! -f "mofka.json" ]; do
    sleep 1  # Wait for 1 second before checking again
done
sleep 1 # Sleep one more second, for good measure

cat << EOF > gdb_commands.txt
set pagination off
run
backtrace full
quit
EOF

# Start streamer-daq service
#LD_PRELOAD=/home/agueroudji/spack/var/spack/environments/APS_VAL/.spack-env/view/lib/libasan.so.8 \
#valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all --verbose \
mpiexec -n 1 -ppn 1 -d 16 --hosts $node_daq gdb -batch -x gdb_commands.txt --args python ./build/python/streamer-daq/DAQStream.py --mode 1 --simulation_file \
      ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize 64 \
      --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
      --synch_count 1 --protocol cxi --group_file mofka.json 1> daq.out 2> daq.err &


echo "streamer-daq started"

# Wait for streamer-daq to be ready (use a sleep to ensure it starts properly)
sleep 10

# Start streamer-dist service
#LD_PRELOAD=/home/agueroudji/spack/var/spack/environments/APS_VAL/.spack-env/view/lib/libasan.so.8 \
#valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all --verbose \
mpiexec -n 1 -ppn 1 -d 16 --hosts $node_dist gdb -batch -x gdb_commands.txt --args python ./build/python/streamer-dist/ModDistStreamPubDemo.py  --cast_to_float32 \
      --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560  --batchsize 64 \
      --protocol cxi --group_file mofka.json 1> dist.out 2> dist.err &

# Wait for streamer-dist to be ready (use a sleep to ensure it starts properly)
sleep 10

echo "streamer-dist started"
# Start streamer-sirt service
#LD_PRELOAD=/home/agueroudji/spack/var/spack/environments/APS_VAL/.spack-env/view/lib/libasan.so.8 \
#valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all --verbose  \
mpiexec -n 2 -ppn 2 -d 16 --hosts $node_sirt gdb -batch -x gdb_commands.txt --args ./build/bin/sirt_stream  --write-freq 4  \
--window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 --protocol cxi --group-file mofka.json \
1> sirt.out 2> sirt.err


echo "streamer-sirt started"
# Start streamer-dist service
#LD_PRELOAD=/home/agueroudji/spack/var/spack/environments/APS_VAL/.spack-env/view/lib/libasan.so.8 \
#valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all --verbose \


#LD_PRELOAD=/home/agueroudji/spack/var/spack/environments/APS_VAL/.spack-env/view/lib/libasan.so.8 \
#valgrind --leak-check=full --track-origins=yes --show-leak-kinds=all --verbose \
# gdb -batch -x gdb_commands.txt --args python ./build/python/streamer-denoiser/denoiser.py \
# --model ./build/python/streamer-denoiser/testA40GPU-it07500.h5 \
# --protocol cxi --group_file mofka.json 1> den.out 2> den.err

echo "Stopping bedrock"
mpiexec -n 1 --ppn 1 -d 16 --hosts $node_mofka bedrock-shutdown cxi -s mofka.ssg 1> bedrock-shutdown.out 2> bedrock-shutdown.err

echo "Waiting for bedrock to stop"
wait $BEDROCK_PID

