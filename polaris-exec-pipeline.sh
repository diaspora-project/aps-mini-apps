#!/usr/bin/env bash

# --- Safety ---
set -eE -o pipefail

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
rm -rf /tmp/scratch/* /tmp/persistent/* || true

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

# --- Start timing just before orchestration ---
start_ns=$(date +%s%N)
start_iso=$(date -Iseconds)

echo "Start Mofka server ---------------------------------------------------"
bash run-mofka-polaris.sh > "${logdir}/mofka.out" 2> "${logdir}/mofka.err" &
echo "bash run-mofka.sh"
sleep 10

echo "Start DAQ ------------------------------------------------------------"
# bash run-daq.sh "${sirt_ranks}" "${sirt_tasks}" "${num_sinograms}" "${logdir}" > "${logdir}/daq.out" 2> "${logdir}/daq.err" &
bash run-daq.sh "${sirt_ranks}" "${sirt_tasks}" "${num_sinograms}" "${logdir}" >> "${logdir}/daq.out" 2>> "${logdir}/daq.err" &
echo "bash run-daq.sh ${sirt_ranks} ${sirt_tasks} ${num_sinograms} ${logdir}"
sleep 20

echo "Start DIST -----------------------------------------------------------"
bash run-dist.sh "${num_sinograms}" "${sirt_tasks}" "${logdir}" > "${logdir}/dist.out" 2> "${logdir}/dist.err" &
echo "bash run-dist.sh ${num_sinograms} ${sirt_tasks} ${logdir}"
# sleep 10  # intentionally not sleeping to avoid extra idle time

echo "Start SIRT -----------------------------------------------------------"
bash run-sirt-polaris.sh "${sirt_ranks}" "${logdir}" > "${logdir}/sirt.out" 2> "${logdir}/sirt.err" &
echo "bash run-sirt-polaris.sh ${sirt_ranks} ${logdir}"

echo "Start Exp Control ----------------------------------------------------"
# Note: runs in background; tee ensures logs are written and exit codes propagate via -o pipefail
bash run-exp-control.sh "${failure_mode}" "${mtbf}" "${logdir}" 2> "${logdir}/exp-control.err" | tee "${logdir}/exp-control.out" &
echo "bash run-exp-control.sh ${failure_mode} ${mtbf} ${logdir}"

echo "Start DEN ------------------------------------------------------------"
echo "bash run-den.sh ${sirt_tasks} ${logdir}"
# IMPORTANT: DEN is the foreground block until pipeline finishes
bash run-den.sh "${sirt_tasks}" "${logdir}" 2> "${logdir}/den.err" | tee "${logdir}/den.out"

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
