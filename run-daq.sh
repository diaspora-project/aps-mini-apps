. ~/spack/share/spack/setup-env.sh
spack env activate APS
source envpy/bin/activate

sirt_ranks=$1
num_sinograms=$2
echo "Number of processes $sirt_ranks"
echo "Number of sinograms $num_sinograms"

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

python ./build/python/streamer-daq/DAQStream.py \
	--mode 1 \
	--simulation_file ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 \
	--d_iteration 1 \
	--batchsize 4 \
	--publisher_addr tcp://0.0.0.0:50000 \
	--iteration_sleep 1 \
	--proj_sleep 0.1 \
	--num_sinograms ${num_sinograms} \
	--synch_addr tcp://0.0.0.0:50001 \
	--synch_count 1 \
	--protocol na+sm \
	--group_file mofka.json

