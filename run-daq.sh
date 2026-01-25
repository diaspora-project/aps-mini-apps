source activate-spack.sh
# source envpy/bin/activate

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

GROUPFILE="${SCRIPT_DIR}/mofka.json"

echo group file: $GROUPFILE

# GROUPFILE="mofka.json"

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
	--group_file $GROUPFILE \
	--logdir ${logdir}

