#!/bin/bash
#PBS -l walltime=02:00:00
#PBS -N APS
#PBS -q run_next

#file systems used by the job
#PBS -l filesystems=home:eagle

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
echo "Full Environment:"
printenv
echo "####################################################"

echo "The Job is being executed on the following node:"
cat ${PBS_NODEFILE}
echo "####################################################"

# load modules and activate spack env
ml load PrgEnv-gnu
. ~/spack/share/spack/setup-env.sh
spack env activate APS

# export MARGO_ENABLE_MONITORING=1
# export MARGO_MONITORING_FILENAME_PREFIX=mofka
# export MARGO_MONITORING_DISABLE_TIME_SERIES=true

# export HG_LOG_LEVEL=error
# export FI_LOG_LEVEL=Trace

set -euo pipefail
cd $PBS_O_WORKDIR

rm -rf mofka.json
NNODES=`wc -l < $PBS_NODEFILE`
echo "Checking working nodes"
mpiexec --no-vni -n $NNODES --ppn 1 --line-buffer -l cxi_service list -s 1 1> cxi.o 2>cxi.e
echo "Checking Done"

# Assuming you have four nodes allocated, use the PBS_NODEFILE to get the node names
nodes=$(cat "$PBS_NODEFILE")
nodes_array=($nodes)

node_mofka=${nodes_array[0]}
node_daq=${nodes_array[1]}
node_sirt=("${nodes_array[@]:1}")

sirt_ranks=$(($(($NNODES-1))*2))

printf "%s\n" "${node_sirt[@]}" > sirt_file

cat << EOF > gdb_commands.txt
set pagination off
run
backtrace full
quit
EOF

batchsize=$(echo $PBS_O_WORKDIR | grep -oP '/B\K[0-9]+(?=/D)')
echo Retreived batchsize $batchsize

mpiexec --no-vni -n 1 --ppn 1 -d 16 --hosts $node_mofka bedrock cxi -c config.json 1> bedrock.out 2> bedrock.err &
BEDROCK_PID=$!

echo "Waiting for mofka file to be created"
while [ ! -f "mofka.json" ]; do
    sleep 1
done
sleep 5

#DAQ topic
mofkactl topic create daq_dist \
	--groupfile mofka.json

mofkactl partition add daq_dist \
	--type memory \
	--rank 0 \
	--groupfile mofka.json

#DIST topics
mofkactl topic create dist_sirt \
	--groupfile mofka.json

mofkactl topic create handshake_s_d \
	--groupfile mofka.json

mofkactl topic create handshake_d_s \
	--groupfile mofka.json

mofkactl partition add handshake_s_d \
	--type memory \
	--rank 0 \
	--groupfile mofka.json

for i in $(seq 1 $sirt_ranks)
do
	mofkactl partition add dist_sirt \
		--type memory \
		--rank 0 \
		--groupfile mofka.json

	mofkactl partition add handshake_d_s \
		--type memory \
		--rank 0 \
		--groupfile mofka.json
done

mofkactl topic create sirt_den \
	--groupfile mofka.json


mofkactl partition add sirt_den \
	--type memory \
	--rank 0 \
	--groupfile mofka.json

echo "starting streamer-daq"

mpiexec  --no-vni -n 1 --ppn 1 -d 16 --hosts $node_daq python ./build/python/streamer-daq/DAQStream.py --mode 1 --simulation_file \
./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize $batchsize  \
--publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
--synch_count 1 --group_file mofka.json 1> daq.out 2> daq.err

echo "streamer-daq finished"
echo "starting streamer-dist"
mpiexec --no-vni -n 1 --ppn 1 -d 16 --hosts $node_daq python ./build/python/streamer-dist/ModDistStreamPubDemo.py  --cast_to_float32 \
--normalize --beg_sinogram 1000 --num_sinograms $sirt_ranks --num_columns 2560  --batchsize $batchsize  \
--group_file mofka.json  --nproc_sirt $sirt_ranks 1> dist.out 2> dist.err

echo "streamer-dist finished"
echo "starting streamer-sirt"
mpiexec --no-vni -v --np $sirt_ranks --ppn 2 --line-buffer -l -d 4 --hostfile sirt_file ./build/bin/sirt_stream  --write-freq 4  \
--window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 --group-file mofka.json --batchsize $batchsize  \
1> sirt.out 2> sirt.err
echo "streamer-sirt finished"

echo "starting streamer-den"
mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_daq  python ./build/python/streamer-denoiser/denoiser.py \
--model ./build/python/streamer-denoiser/testA40GPU-it07500.h5 \
--group_file mofka.json --batchsize $batchsize --nproc_sirt $sirt_ranks 1> den.out 2> den.err
echo "streamer-den finished"

echo "Job Finished: " `date`
