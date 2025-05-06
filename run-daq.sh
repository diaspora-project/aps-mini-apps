source activate-spack.sh
# source envpy/bin/activate

# Check if the number of arguments is correct
if [ "$#" -ne 3 ]; then
	echo "Usage: run-daq.sh <sirt_ranks> <num_sinograms>"
	echo "  <sirt_ranks>    Number of ranks for the SIRT process"
	echo "  <num_sinograms> Number of sinograms to process"
	echo "  <logdir>       Directory to store the log files"
	exit 1
fi

sirt_ranks=$1
num_sinograms=$2
logdir=$3
echo "Number of processes $sirt_ranks"
echo "Number of sinograms $num_sinograms"

trap "kill 0; exit 1" SIGINT SIGTERM

METADATA_PROVIDER=$(
    mofkactl metadata add \
            --rank 0 \
            --groupfile mofka.json \
            --type log \
            --config.path /tmp/mofka-log \
            --config.create_if_missing true
        )

DATA_PROVIDER=$(
    mofkactl data add \
            --rank 0 \
            --groupfile mofka.json \
            --type abtio \
            --config.path /tmp/mofka-data \
            --config.create_if_missing true
        )

mofkactl topic create daq_dist \
	--groupfile mofka.json

mofkactl partition add daq_dist \
	--type default \
	--rank 0 \
	--groupfile mofka.json \
	--metadata "${METADATA_PROVIDER}" \
	--data "${DATA_PROVIDER}"

#DIST topics
mofkactl topic create dist_sirt \
	--groupfile mofka.json

mofkactl topic create handshake_s_d \
	--groupfile mofka.json

mofkactl topic create handshake_d_s \
	--groupfile mofka.json

mofkactl partition add handshake_s_d \
	--type default \
	--rank 0 \
	--groupfile mofka.json \
	--metadata "${METADATA_PROVIDER}" \
	--data "${DATA_PROVIDER}"

for i in $(seq 1 $sirt_ranks)
do
	mofkactl partition add dist_sirt \
		--type memory \
		--rank 0 \
		--groupfile mofka.json \
		--metadata "${METADATA_PROVIDER}" \
		--data "${DATA_PROVIDER}"

	mofkactl partition add handshake_d_s \
		--type memory \
		--rank 0 \
		--groupfile mofka.json \
		--metadata "${METADATA_PROVIDER}" \
		--data "${DATA_PROVIDER}"
done

mofkactl topic create sirt_den \
	--groupfile mofka.json


mofkactl partition add sirt_den \
	--type memory \
	--rank 0 \
	--groupfile mofka.json \
	--metadata "${METADATA_PROVIDER}" \
	--data "${DATA_PROVIDER}"

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
	--group_file mofka.json \
	--logdir ${logdir}

