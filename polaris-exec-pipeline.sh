#!/usr/bin/env bash

# --- Safety ---
set -eE -o pipefail

# Load modules and activate spack env
source activate-spack.sh
# Activate python virtualenv
source pyvenv/bin/activate

# Kill all related processes on error/ctrl-c
cleanup_on_error() {
  echo "!! Aborting, cleaning up processes..." >&2
  pkill -9 -f 'bedrock' || true
  pkill -9 -f 'streamer-daq' || true
  pkill -9 -f 'streamer-dist' || true
  pkill -9 -f 'streamer-sirt' || true
  pkill -9 -f 'sirt_stream' || true
  pkill -9 -f 'streamer-den' || true
  pkill -INT -f 'veloc' || true
  pkill -9 -f 'veloc-backend' || true
  pkill -9 -f 'FailureInjector' || true
}
trap cleanup_on_error SIGINT SIGTERM ERR

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
ckpt_dir=/eagle/Diaspora/ndhai/veloc
rm -rf $ckpt_dir/tmp/scratch/* $ckpt_dir/tmp/persistent/* || true

# --- Args ---
if [ "$#" -ne 5 ]; then
  echo "Usage: $0 <sirt_ranks> <sirt_tasks> <num_sinograms> <failure_mode> <mtbf>" >&2
  echo "  <sirt_ranks>    Number of SIRT workers/processes" >&2
  echo "  <sirt_tasks>    Number of SIRT tasks/threads" >&2
  echo "  <num_sinograms> Number of sinograms to process" >&2
  echo "  <failure_mode>  single|periodic|random" >&2
  echo "  <mtbf>          Mean time between failures (seconds)" >&2
  exit 1
fi
sirt_ranks=$1
sirt_tasks=$2
num_sinograms=$3
failure_mode=$4
mtbf=$5

DATE=$(date +"%Y-%m-%d-%Hh%Mmin%Ssec")
logdir="build/logs/D${DATE}"
mkdir -p "${logdir}"
echo "Logging execution information at ${logdir}"
ln -sfn "$(pwd)/${logdir}" "build/logs/latest"
echo "Updated symlink: build/logs/latest -> ${logdir}"


# TODO: Assign tasks to nodes here
nodes=$(cat "$PBS_NODEFILE")
nodes_array=($nodes)

node_daq=${nodes_array[0]}
node_dist=${nodes_array[0]}
# node_sirts=${nodes_array[0]}
node_sirts=$nodes_array
node_den=${nodes_array[0]}
node_mofka=${nodes_array[0],nodes_array[1]}
node_control=${nodes_array[0]}

export MARGO_ENABLE_MONITORING=1
export MARGO_MONITORING_FILENAME_PREFIX=mofka
export MARGO_MONITORING_DISABLE_TIME_SERIES=true

export HG_LOG_LEVEL=error
export FI_LOG_LEVEL=Trace

exec_dir=`pwd`

# --- Start timing just before orchestration ---
start_ns=$(date +%s%N)
start_iso=$(date -Iseconds)

echo "Start Mofka server ---------------------------------------------------"
# mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_mofka bash $exec_dir/run-mofka-polaris.sh > "${logdir}/mofka.out" 2> "${logdir}/mofka.err" &
mpiexec -ppn 1 -d 16 --hosts $node_mofka bedrock cxi -v trace -c config.json > "${logdir}/mofka.out" 2> "${logdir}/mofka.err" &
# mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_mofka bedrock na+sm -c config.json > "${logdir}/mofka.out" 2> "${logdir}/mofka.err" &
# bedrock na+sm -c config.json > "${logdir}/mofka.out" 2> "${logdir}/mofka.err" &
# echo "mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_mofka bash $exec_dir/run-mofka-polaris.sh"
sleep 10

echo "Start DAQ ------------------------------------------------------------"
# bash run-daq.sh "${sirt_ranks}" "${sirt_tasks}" "${num_sinograms}" "${logdir}" > "${logdir}/daq.out" 2> "${logdir}/daq.err" &
# mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_daq bash $exec_dir/run-daq.sh "${sirt_ranks}" "${sirt_tasks}" "${num_sinograms}" "${logdir}" >> "${logdir}/daq.log" 2>> "${logdir}/daq.log" &
bash $exec_dir/run-daq.sh "${sirt_ranks}" "${sirt_tasks}" "${num_sinograms}" "${logdir}" >> "${logdir}/daq.log" 2>> "${logdir}/daq.log" &
echo "mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_daq bash $exec_dir/run-daq.sh ${sirt_ranks} ${sirt_tasks} ${num_sinograms} ${logdir}"
sleep 20

echo "Start DIST -----------------------------------------------------------"
# mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_dist bash $exec_dir/run-dist.sh "${num_sinograms}" "${sirt_tasks}" "${logdir}" > "${logdir}/dist.out" 2> "${logdir}/dist.err" &
bash $exec_dir/run-dist.sh "${num_sinograms}" "${sirt_tasks}" "${logdir}" > "${logdir}/dist.out" 2> "${logdir}/dist.err" &
echo "mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_dist bash $exec_dir/run-dist.sh ${num_sinograms} ${sirt_tasks} ${logdir}"
# sleep 10  # intentionally not sleeping to avoid extra idle time

echo "Start SIRT -----------------------------------------------------------"
# mpiexec --no-vni -n $sirt_ranks -ppn $sirt_ranks -d 16 --hosts $node_sirts bash $exec_dir/run-sirt-polaris.sh "${sirt_ranks}" "${logdir}" > "${logdir}/sirt.out" 2> "${logdir}/sirt.err" &
bash $exec_dir/run-sirt-polaris.sh "${sirt_ranks}" "${logdir}" > "${logdir}/sirt.out" 2> "${logdir}/sirt.err" &
echo "mpiexec --no-vni -n $sirt_ranks -ppn $sirt_ranks -d 16 --hosts $node_sirts bash $exec_dir/run-sirt-polaris.sh ${sirt_ranks} ${logdir}"

echo "Start Exp Control ----------------------------------------------------"
# Note: runs in background; tee ensures logs are written and exit codes propagate via -o pipefail
bash $exec_dir/run-exp-control.sh "${failure_mode}" "${mtbf}" "${logdir}" 2> "${logdir}/exp-control.err" | tee "${logdir}/exp-control.out" &
echo "mpiexec --no-vni -n $sirt_ranks -ppn $sirt_ranks -d 16 --hosts $node_control bash $exec_dir/run-exp-control.sh ${failure_mode} ${mtbf} ${logdir}"

echo "Start DEN ------------------------------------------------------------"
echo "mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_den $exec_dir/run-den.sh bash ${num_sinograms} ${logdir}"
# IMPORTANT: DEN is the foreground block until pipeline finishes
# mpiexec --no-vni -n 1 -ppn 1 -d 16 --hosts $node_den bash $exec_dir/run-den.sh "${num_sinograms}" "${logdir}" 2> "${logdir}/den.err" | tee "${logdir}/den.out"
bash $exec_dir/run-den.sh "${num_sinograms}" "${logdir}" 2> "${logdir}/den.err" | tee "${logdir}/den.out"

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
# Stop failure injector first, then the rest
pkill -9 -f "FailureInjector" || true
pkill -9 -f "bedrock" || true
pkill -9 -f "streamer-daq" || true
pkill -9 -f "streamer-dist" || true
pkill -9 -f "streamer-sirt" || true
pkill -9 -f "sirt_stream" || true
pkill -9 -f "streamer-den" || true
pkill -INT -f "veloc" || true
pkill -9 -f "veloc-backend" || true

echo "COMPLETE (E2E ${duration_fmt}, ${dur_ms} ms)  Logs: ${logdir}"

# Clear error trap so normal exit doesn’t run cleanup_on_error
trap - SIGINT SIGTERM ERR
exit 0
