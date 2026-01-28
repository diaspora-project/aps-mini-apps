# Force kill all processes if press CTRL+C or if any command fails
trap "pkill -9 -f 'bedrock'; pkill -9 -f 'streamer-daq'; pkill -9 -f 'streamer-dist'; pkill -9 -f 'sirt_stream'; pkill -9 -f 'streamer-den'; exit 1" SIGINT SIGTERM EXIT

set -e

echo "Cleaning up previous runs --------------------------------------------"
pkill -9 -f "bedrock" || true
pkill -9 -f "streamer-daq" || true
pkill -9 -f "streamer-dist" || true
pkill -9 -f "streamer-sirt" || true
pkill -9 -f "sirt_stream" || true
pkill -9 -f "streamer-den" || true
pkill -INT -f "veloc" || true
pkill -9 -f "veloc-backend" || true
pkill -9 -f "FailureInjector" || true

# Remove previous checkpoints
rm -rf /tmp/scratch/*
rm -rf /tmp/persistent/*
rm -rf /lus/eagle/projects/APSDataAnalysis/ndhai/veloc/tmp/scratch/*
rm -rf /lus/eagle/projects/APSDataAnalysis/ndhai/veloc/tmp/persistent/*

# Check if the number of arguments is corre
if [ "$#" -ne 8 ]; then
    echo "Usage: exec-pipeline.sh <sirt_ranks> <num_sinograms>"
    echo "  <sirt_ranks>    Number of SIRT workers/processes"
    echo "  <sirt_tasks>    Number of SIRT tasks/threads"
    echo "  <num_sinograms> Number of sinograms to process"
    echo "  <failure_mode>  single|periodic|random"
    echo "  <mtbf>          Mean time between failures (in seconds)"
    echo "  <slowdown>      Slowdown sample index"
    echo "  <load-balance>  enable load balancing"
    echo "  <ckpt-freq>     Checkpoint frequency iteration per ckpt"
    exit 1
fi
sirt_ranks=$1
sirt_tasks=$2
num_sinograms=$3
failure_mode=$4
mtbf=$5
slowdownindex=$6
load_balance=$7
ckpt_freq=$8

DATE=$(date +"%Y-%m-%d-%Hh%Mmin%Ssec")
logdir=build/logs/D${DATE}
mkdir -p ${logdir}
echo "Logging execution information at ${logdir}"
ln -sfn "`pwd`/${logdir}" "build/logs/latest"
echo "Updated symlink: ${latest_link} -> ${logdir}"

rm mofka.json || true
echo "Start Mofka server ---------------------------------------------------"
bash run-mofka.sh > ${logdir}/mofka.out 2> ${logdir}/mofka.err &
echo bash run-mofka.sh
sleep 10

echo "Start QUEUE SETUP ----------------------------------------------------"
bash run-queue-setup.sh ${sirt_ranks} ${sirt_tasks} ${num_sinograms} ${logdir}
echo bash run-daq.sh ${sirt_ranks} ${sirt_tasks} ${num_sinograms} ${logdir}

# --- Start timing just before orchestration ---
start_ns=$(date +%s%N)
start_iso=$(date -Iseconds)

echo "Start DAQ ------------------------------------------------------------"
bash run-daq.sh ${sirt_ranks} ${sirt_tasks} ${num_sinograms} ${logdir} >> ${logdir}/daq.out 2>> ${logdir}/daq.err &
echo bash run-daq.sh ${sirt_ranks} ${sirt_tasks} ${num_sinograms} ${logdir}
# sleep 10

echo "Start DIST -----------------------------------------------------------"
bash run-dist.sh ${num_sinograms} ${sirt_tasks} ${load_balance} ${logdir} >> ${logdir}/dist.out 2>> ${logdir}/dist.err &
# bash run-dist.sh ${num_sinograms} ${sirt_tasks} ${logdir} > ${logdir}/dist.log 2> ${logdir}/dist.log &
echo bash run-dist.sh ${num_sinograms} ${sirt_tasks} ${logdir}
# sleep 10

echo "Start SIRT -----------------------------------------------------------"
bash run-sirt.sh ${sirt_ranks} ${logdir} $slowdownindex $ckpt_freq >> ${logdir}/sirt.out 2>> ${logdir}/sirt.err &
echo bash run-sirt.sh ${sirt_ranks} ${logdir}

# echo "Start Exp Control ----------------------------------------------------"
# bash run-exp-control.sh ${mtbf} ${logdir} 2> ${logdir}/exp-control.err | tee ${logdir}/exp-control.out &
bash run-exp-control.sh "${failure_mode}" "${mtbf}" "${logdir}" 2>> "${logdir}/exp-control.err" | tee "${logdir}/exp-control.out" &
echo bash run-exp-control.sh ${mtbf} ${logdir}

echo "Start DEN ------------------------------------------------------------"
echo bash run-den.sh ${sirt_tasks} ${logdir}
bash run-den.sh ${sirt_tasks} ${logdir} 2>> ${logdir}/den.err | tee ${logdir}/den.out

# --- If we reached here, DEN completed; mark end time BEFORE cleanup ---
end_ns=$(date +%s%N)
end_iso=$(date -Iseconds)
dur_ns=$(( end_ns - start_ns ))
dur_ms=$(( dur_ns / 1000000 ))
dur_s_whole=$(( dur_ns / 1000000000 ))
ms=$(( (dur_ns / 1000000) % 1000 ))

# format H:M:S.mmm
h=$(( dur_s_whole / 3600 ))
m=$(( (dur_s_whole % 3600) / 60 ))
s=$(( dur_s_whole % 60 ))
duration_fmt=$(printf "%02d:%02d:%02d.%03d" "$h" "$m" "$s" "$ms")

{
  echo "E2E_START: ${start_iso}"
  echo "E2E_END:   ${end_iso}"
  echo "E2E_MS:    ${dur_ms}"
  echo "E2E_HMS:   ${duration_fmt}"
} | tee "${logdir}/e2e_time.txt"

echo "Clean up after run ---------------------------------------------------"
pkill -9 -f "bedrock" || true
pkill -9 -f "streamer-daq" || true
pkill -9 -f "streamer-dist" || true
pkill -9 -f "streamer-sirt" || true
pkill -9 -f "sirt_stream" || true
pkill -9 -f "streamer-den" || true
pkill -INT -f "veloc" || true
pkill -9 -f "veloc-backend" || true
pkill -9 -f "FailureInjector" || true
echo "COMPLETE"


