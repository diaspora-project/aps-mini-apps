 . ~/spack/share/spack/setup-env.sh;
spack env activate APS_GDB;

export MARGO_ENABLE_MONITORING=1
export MARGO_MONITORING_FILENAME_PREFIX=mofka
export MARGO_MONITORING_DISABLE_TIME_SERIES=true

# Launch bedrock
bedrock cxi  -c config.json

echo "Waiting for mofka file to be created"
while [ ! -f "mofka.json" ]; do
    sleep 1  # Wait for 1 second before checking again
done
sleep 5 # Sleep one more second, for good measure

# setup Mofka topics and partitions
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


# Launch DAQ
python ./build/python/streamer-daq/DAQStream.py --mode 1 --simulation_file \
./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize 4 \
--publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
--synch_count 1 --protocol cxi --group_file mofka.json

# Launch Dist
python ./build/python/streamer-dist/ModDistStreamPubDemo.py  --cast_to_float32 \
--normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560  --batchsize 4 \
--protocol cxi --group_file mofka.json

# Launch SIRT
mpiexec -n 2 ./build/bin/sirt_stream  --write-freq 4  \
--window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 \
--protocol cxi --group-file mofka.json --batchsize 4

# Launch DEN
python ./build/python/streamer-denoiser/denoiser.py \
--model ./build/python/streamer-denoiser/testA40GPU-it07500.h5 \
--protocol cxi --group_file mofka.json --batchsize 4 --nproc_sirt 2
