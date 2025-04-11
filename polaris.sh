#!/bin/bash
#PBS -l walltime=02:00:00
#PBS -N APS
#PBS -q run_next

#file systems used by the job
#PBS -l filesystems=home:eagle


# load modules and activate spack env
ml load PrgEnv-gnu
. /eagle/radix-io/agueroudji/spack/share/spack/setup-env.sh
spack env activate APS

echo Here is the module list
module list
export FI_PROVIDER=cxi

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


# export MARGO_ENABLE_MONITORING=1
export MARGO_MONITORING_FILENAME_PREFIX=mofka
export MARGO_MONITORING_DISABLE_TIME_SERIES=true

# export HG_LOG_LEVEL=error
# export FI_LOG_LEVEL=debug


set -euo pipefail
cd $PBS_O_WORKDIR

rm -rf mofka.json
NNODES=`wc -l < $PBS_NODEFILE`
echo "Checking working nodes"
mpiexec --no-vni -n $NNODES --ppn 1 --line-buffer -l cxi_service list -s 1 1> cxi.o 2>cxi.e
echo "Checking Done"

nodes=$(cat "$PBS_NODEFILE")
nodes_array=($nodes)

node_mofka=${nodes_array[0]}
node_daq=${nodes_array[1]}
node_dist=${nodes_array[2]}
node_den=${nodes_array[3]}
node_sirt=("${nodes_array[@]:4}")

sirt_ranks=$(($(($NNODES-4))*2))

printf "%s\n" "${node_sirt[@]}" > sirt_file

# cat << EOF > gdb_commands.txt
# set pagination off
# run
# backtrace full
# quit
# EOF

batchsize=$(echo $PBS_O_WORKDIR | grep -oP '/BATCH_\K[0-9]+(?=/DATE_)')
echo Retreived batchsize $batchsize

mpiexec --no-vni -n $sirt_ranks --ppn $sirt_ranks --hosts $node_mofka bedrock cxi -c config.json 1> bedrock.out 2> bedrock.err &
#mpiexec --no-vni -n 1 --ppn 1 --hosts $node_mofka bedrock cxi -c config.json 1> bedrock.out 2> bedrock.err &
BEDROCK_PID=$!

echo "Waiting for mofka file to be created"
while [ ! -f "mofka.json" ]; do
    sleep 1
done
sleep 5

#DAQ topic
mofkactl topic create daq_dist \
	--groupfile mofka.json

METADATA_PROVIDER=$(
    python -m mochi.mofka.mofkactl metadata add \
            --rank 0 \
            --groupfile mofka.json \
            --type log \
            --config.path /local/scratch/log-daq-dist \
            --config.create_if_missing true
        )

DATA_PROVIDER=$(
    python -m mochi.mofka.mofkactl data add \
            --rank 0 \
            --groupfile mofka.json \
            --type pmdk \
            --config.path /local/scratch/mofka-daq-dist \
			--config.create_if_missing_with_size 5368709120
        )

mofkactl partition add daq_dist \
	--type memory \
	--rank 0 \
	--groupfile mofka.json #--metadata "${METADATA_PROVIDER}" --data "${DATA_PROVIDER}"

#DIST topics
mofkactl topic create dist_sirt \
	--groupfile mofka.json

METADATA_PROVIDER=$(
    python -m mochi.mofka.mofkactl metadata add \
            --rank 0 \
            --groupfile mofka.json \
            --type log \
            --config.path /local/scratch/log-hs \
            --config.create_if_missing true
        )

DATA_PROVIDER=$(
    python -m mochi.mofka.mofkactl data add \
            --rank 0 \
            --groupfile mofka.json \
            --type pmdk \
            --config.path /local/scratch/mofka-hs \
			--config.create_if_missing_with_size 5368709120
        )

mofkactl topic create handshake_s_d \
	--groupfile mofka.json

mofkactl topic create handshake_d_s \
	--groupfile mofka.json

mofkactl partition add handshake_s_d \
	--type memory \
	--rank 0 \
	--groupfile mofka.json #--metadata "${METADATA_PROVIDER}" --data "${DATA_PROVIDER}"

for i in $(seq 0 $(($sirt_ranks-1)))
do

	METADATA_PROVIDER=$(
		python -m mochi.mofka.mofkactl metadata add \
				--rank $i \
				--groupfile mofka.json \
				--type log \
				--config.path /local/scratch/log-dist-sirt-$i \
				--config.create_if_missing true
			)

	DATA_PROVIDER=$(
		python -m mochi.mofka.mofkactl data add \
				--rank $i \
				--groupfile mofka.json \
				--type pmdk \
				--config.path /local/scratch/mofka-dist-sirt-$i \
				--config.create_if_missing_with_size 5368709120

			)
	mofkactl partition add dist_sirt \
		--type memory \
		--rank $i \
		--groupfile mofka.json #--metadata "${METADATA_PROVIDER}" --data "${DATA_PROVIDER}"

	mofkactl partition add handshake_d_s \
		--type memory \
		--rank $i \
		--groupfile mofka.json
done

mofkactl topic create sirt_den \
	--groupfile mofka.json

for i in $(seq 0 $(($sirt_ranks-1)))
do
	METADATA_PROVIDER=$(
		python -m mochi.mofka.mofkactl metadata add \
				--rank $i \
				--groupfile mofka.json \
				--type log \
				--config.path /local/scratch/log-sirt-den-$i \
				--config.create_if_missing true
			)

	DATA_PROVIDER=$(
		python -m mochi.mofka.mofkactl data add \
				--rank $i \
				--groupfile mofka.json \
				--type pmdk \
				--config.path /local/scratch/mofka-sirt-den-$i \
				--config.create_if_missing_with_size 5368709120
			)

	mofkactl partition add sirt_den \
		--type memory \
		--rank $i \
		--groupfile mofka.json #--metadata "${METADATA_PROVIDER}" --data "${DATA_PROVIDER}"
done

echo "starting streamer-daq"

mpiexec  --no-vni -n 1 --ppn 1 -d 16 --hosts $node_daq python ./build/python/streamer-daq/DAQStream.py --mode 1 --simulation_file \
./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize $batchsize  \
--publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
--synch_count 1 --group_file mofka.json 1> daq.out 2> daq.err #&

echo "streamer-daq finished"
echo "starting streamer-dist"
mpiexec --no-vni -n 1 --ppn 1 -d 16 --hosts $node_dist python ./build/python/streamer-dist/ModDistStreamPubDemo.py  --cast_to_float32 \
--normalize --beg_sinogram 1000 --num_sinograms $sirt_ranks --num_columns 2560  --batchsize $batchsize  \
--group_file mofka.json  --nproc_sirt $sirt_ranks 1> dist.out 2> dist.err #&

echo "streamer-dist finished"
echo "starting streamer-sirt"
mpiexec --no-vni -v --np $sirt_ranks --ppn 2 --line-buffer -l -d 16 --hostfile sirt_file ./build/bin/sirt_stream  --write-freq 4  \
--window-iter 1 --window-step 4 --window-length 4 -t 8 -c 1427 --group-file mofka.json --batchsize $batchsize  \
1> sirt.out 2> sirt.err #&
echo "streamer-sirt finished"

echo "starting streamer-den"
mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_den  python ./build/python/streamer-denoiser/denoiser.py \
--model ./build/python/streamer-denoiser/testA40GPU-it07500.h5 \
--group_file mofka.json --batchsize $batchsize --nproc_sirt $sirt_ranks 1> den.out 2> den.err
echo "streamer-den finished"

echo "Job Finished: " `date`
