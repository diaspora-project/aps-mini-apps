source activate-spack.sh
# source envpy/bin/activate

# SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# cd "${SCRIPT_DIR}"

# GROUPFILE="${SCRIPT_DIR}/mofka.json"

GROUPFILE="mofka.json"

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
            --groupfile $GROUPFILE \
            --type log \
            --config.path /tmp/mofka-log \
            --config.create_if_missing true
        )

DATA_PROVIDER=$(
    mofkactl data add \
            --rank 0 \
            --groupfile $GROUPFILE \
            --type abtio \
            --config.path /tmp/mofka-data \
            --config.create_if_missing true
        )

echo create daq_dist topic and partitions

mofkactl topic create daq_dist \
	--groupfile $GROUPFILE

mofkactl partition add daq_dist \
	--type default \
	--rank 0 \
	--groupfile $GROUPFILE \
	--metadata "${METADATA_PROVIDER}" \
	--data "${DATA_PROVIDER}"


echo create dist_sirt topics

#DIST topics
mofkactl topic create dist_sirt \
	--groupfile $GROUPFILE
for i in $(seq 1 $sirt_tasks)
do
	mofkactl partition add dist_sirt \
		--type default \
		--rank 0 \
		--groupfile $GROUPFILE \
		--metadata "${METADATA_PROVIDER}" \
		--data "${DATA_PROVIDER}"
done

echo create dist-sirt handshakes topics

mofkactl topic create handshake_s_d \
	--groupfile $GROUPFILE

mofkactl partition add handshake_s_d \
		--type default \
		--rank 0 \
		--groupfile $GROUPFILE \
		--metadata "${METADATA_PROVIDER}" \
		--data "${DATA_PROVIDER}"

echo create sirt-dist handshakes topics

mofkactl topic create handshake_d_s \
	--groupfile $GROUPFILE

mofkactl partition add handshake_d_s \
		--type default \
		--rank 0 \
		--groupfile $GROUPFILE \
		--metadata "${METADATA_PROVIDER}" \
		--data "${DATA_PROVIDER}"

echo create dist-sirt action control topics

# Action channel for flow control and load balancing
mofkactl topic create dist_sirt_action \
	--groupfile $GROUPFILE
mofkactl topic create sirt_dist_action \
	--groupfile $GROUPFILE

# echo create sirt partitions for dist-sirt and their handshake

echo create dist/sirt action patitions

mofkactl partition add sirt_dist_action \
	--type default \
	--rank 0 \
	--groupfile $GROUPFILE \
	--metadata "${METADATA_PROVIDER}" \
	--data "${DATA_PROVIDER}"

mofkactl partition add dist_sirt_action \
	--type default \
	--rank 0 \
	--groupfile $GROUPFILE \
	--metadata "${METADATA_PROVIDER}" \
	--data "${DATA_PROVIDER}"

echo create sirt-den topics and partitions

mofkactl topic create sirt_den \
	--groupfile $GROUPFILE

echo create sirt_den partition

mofkactl partition add sirt_den \
	--type default \
	--rank 0 \
	--groupfile $GROUPFILE \
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
	--proj_sleep 0.01 \
	--num_sinograms ${num_sinograms} \
	--synch_addr tcp://0.0.0.0:50001 \
	--synch_count 1 \
	--protocol na+sm \
	--group_file $GROUPFILE \
	--logdir ${logdir}

