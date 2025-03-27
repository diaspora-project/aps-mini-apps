# Force kill all processes if press CTRL+C or if any command fails
trap "pkill -9 -f 'bedrock'; pkill -9 -f 'streamer-daq'; pkill -9 -f 'streamer-dist'; pkill -9 -f 'sirt_stream'; pkill -9 -f 'streamer-den'; exit 1" SIGINT SIGTERM EXIT

set -e

echo "Cleaning up previous runs --------------------------------------------"
pkill -9 -f "bedrock" || true
pkill -9 -f "streamer-daq" || true
pkill -9 -f "streamer-dist" || true
pkill -9 -f "streamer-sirt" || true
pkill -9 -f "streamer-den" || true

# Remove previous checkpoints
rm -rf /tmp/scratch/*
rm -rf /tmp/persistent/*

# Check if the number of arguments is correct
if [ "$#" -ne 2 ]; then
    echo "Usage: exec-pipeline.sh <sirt_ranks> <num_sinograms>"
    echo "  <sirt_ranks>    Number of ranks for the SIRT process"
    echo "  <num_sinograms> Number of sinograms to process"
    exit 1
fi
sirt_ranks=$1
num_sinograms=$2

DATE=$(date +"%Y-%m-%d-%Hh%Mmin%Ssec")
logdir=build/logs/D${DATE}
mkdir -p ${logdir}
echo "Logging execution information at ${logdir}"

echo "Start Mofka server ---------------------------------------------------"
bash run-mofka.sh > ${logdir}/mofka.out 2> ${logdir}/mofka.err &
sleep 10
echo "Start DAQ ------------------------------------------------------------"
bash run-daq.sh ${sirt_ranks} ${num_sinograms} ${logdir} > ${logdir}/daq.out 2> ${logdir}/daq.err &
sleep 10
echo "Start DIST -----------------------------------------------------------"
bash run-dist.sh ${num_sinograms} ${logdir} > ${logdir}/dist.out 2> ${logdir}/dist.err &
echo "Start SIRT -----------------------------------------------------------"
bash run-sirt.sh ${sirt_ranks} ${logdir} > ${logdir}/sirt.out 2> ${logdir}/sirt.err &
echo "Start DEN ------------------------------------------------------------"
bash run-den.sh ${sirt_ranks} ${logdir} 2> ${logdir}/den.err | tee ${logdir}/den.out

echo "COMPLETE"

