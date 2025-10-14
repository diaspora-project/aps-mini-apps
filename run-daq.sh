source activate-spack.sh
# source envpy/bin/activate

# Check if the number of arguments is correct
if [ "$#" -ne 4 ]; then
	echo "Usage: run-daq.sh <sirt_ranks> <sirt_tasks> <num_sinograms> <logdir>"
	echo "  <sirt_ranks>	Number of SIRT process"
	echo "  <sirt_tasks>	Number of SIRT tasks"
	echo "  <num_sinograms>	Number of sinograms to process"
	echo "  <logdir>		Directory to store the log files"
	exit 1
fi

sirt_ranks=$1
sirt_tasks=$2
num_sinograms=$3
logdir=$4
echo "Number of tasks/processes $sirt_tasks/$sirt_ranks"
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

echo create daq_dist topic and partition

mofkactl topic create daq_dist \
	--groupfile mofka.json

mofkactl partition add daq_dist \
	--type default \
	--rank 0 \
	--groupfile mofka.json \
	--metadata "${METADATA_PROVIDER}" \
	--data "${DATA_PROVIDER}"

echo create dist topics and partitions for handshakes and data

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

echo create dist partitions for sirt action control

# Action channel for flow control and load balancing
mofkactl topic create dist_sirt_action \
	--groupfile mofka.json
mofkactl topic create sirt_dist_action \
	--groupfile mofka.json

echo create sirt topics for handshakes with dist

for i in $(seq 1 $sirt_tasks)
do
	mofkactl partition add dist_sirt \
		--type default \
		--rank 0 \
		--groupfile mofka.json \
		--metadata "${METADATA_PROVIDER}" \
		--data "${DATA_PROVIDER}"
	
	mofkactl partition add handshake_d_s \
		--type default \
		--rank 0 \
		--groupfile mofka.json \
		--metadata "${METADATA_PROVIDER}" \
		--data "${DATA_PROVIDER}"
done

echo create sirt topics for returning handshakes

for i in $(seq 1 $sirt_ranks)
do
	mofkactl partition add handshake_s_d \
		--type default \
		--rank 0 \
		--groupfile mofka.json \
		--metadata "${METADATA_PROVIDER}" \
		--data "${DATA_PROVIDER}"
done

echo create sirt partitions for action control

mofkactl partition add sirt_dist_action \
	--type default \
	--rank 0 \
	--groupfile mofka.json \
	--metadata "${METADATA_PROVIDER}" \
	--data "${DATA_PROVIDER}"

mofkactl partition add dist_sirt_action \
	--type default \
	--rank 0 \
	--groupfile mofka.json \
	--metadata "${METADATA_PROVIDER}" \
	--data "${DATA_PROVIDER}"

sleep 10

echo create sirt_den topic

mofkactl topic create sirt_den \
	--groupfile mofka.json

echo create sirt_den partition


mofkactl partition add sirt_den \
	--type default \
	--rank 0 \
	--groupfile mofka.json \
	--metadata "${METADATA_PROVIDER}" \
	--data "${DATA_PROVIDER}"

echo "Starting DAQ ..."

python -u ./build/python/streamer-daq/DAQStream.py \
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

