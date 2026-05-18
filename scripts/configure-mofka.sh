#!/usr/bin/env bash
# configure-mofka.sh — interactive configurator for the tekapp/Mofka workflow.
#
# Produces three files in --out-dir (defaults to cwd):
#   * mofka.json        — Bedrock provider configuration
#   * mofka-config.env  — shell-sourceable settings consumed by the
#                         platforms/*/run-with-mofka-driver.sh scripts
#                         (protocol, node/ppn counts, per-topic flags)
#   * mofka-answers.env — raw answers, replayable via --from-file
#
# Usage:
#   bash scripts/configure-mofka.sh                        # interactive, generic defaults
#   bash scripts/configure-mofka.sh --platform polaris     # platform-tuned defaults
#   bash scripts/configure-mofka.sh --from-file mofka-answers.env \
#                                   --out-dir /path/to/job-cwd

set -euo pipefail

PLATFORM=""
FROM_FILE=""
OUT_DIR="$(pwd)"
SIRT_RANKS_FLAG=""

usage() { sed -n '2,16p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --platform)   PLATFORM="$2"; shift 2 ;;
        --from-file)  FROM_FILE="$2"; shift 2 ;;
        --out-dir)    OUT_DIR="$2"; shift 2 ;;
        --sirt-ranks) SIRT_RANKS_FLAG="$2"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "ERROR: unknown flag '$1' (use --help)" >&2; exit 1 ;;
    esac
done

mkdir -p "$OUT_DIR"

# --- colours ------------------------------------------------------------------

if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
    C_RESET=$'\e[0m'
    C_BOLD=$'\e[1m'
    C_DIM=$'\e[2m'
    C_RED=$'\e[31m'
    C_GREEN=$'\e[32m'
    C_YELLOW=$'\e[33m'
    C_CYAN=$'\e[36m'
    C_MAGENTA=$'\e[35m'
else
    C_RESET="" C_BOLD="" C_DIM="" C_RED="" C_GREEN="" C_YELLOW="" C_CYAN="" C_MAGENTA=""
fi

section()  { printf '\n%s== %s ==%s\n' "${C_GREEN}${C_BOLD}" "$*" "${C_RESET}"; }
subhead()  { printf '%s%s%s\n' "${C_MAGENTA}${C_BOLD}" "$*" "${C_RESET}"; }
explain()  { printf '%s%s%s\n' "${C_DIM}" "$*" "${C_RESET}"; }
info()     { printf '%s%s%s\n' "${C_CYAN}" "$*" "${C_RESET}"; }

# warn_block "first line" "second line" ... — prints "WARNING:" once, then the
# remaining lines indented to align under the first line.
warn_block() {
    local first="$1"; shift
    printf '%sWARNING:%s%s %s%s\n' "${C_YELLOW}${C_BOLD}" "${C_RESET}" "${C_YELLOW}" "$first" "${C_RESET}" >&2
    local line
    for line in "$@"; do
        printf '%s         %s%s\n' "${C_YELLOW}" "$line" "${C_RESET}" >&2
    done
}

# --- platform defaults --------------------------------------------------------

# Platform-keyed defaults. ES_PER_PROC = compute (execution streams) budget per
# bedrock process; used to derive RPC_THREAD_COUNT.
case "$PLATFORM" in
    polaris)
        DEFAULT_PROTOCOL="cxi"
        DEFAULT_PPN=2
        ES_PER_PROC=32              # 64 hw threads / 2 procs/node
        SCRATCH_BASE="/local/scratch"
        ;;
    improv)
        DEFAULT_PROTOCOL="verbs"
        DEFAULT_PPN=2
        ES_PER_PROC=64              # 128 cores / 2 procs/node
        SCRATCH_BASE="/scratch"
        ;;
    local)
        DEFAULT_PROTOCOL="tcp"
        DEFAULT_PPN=1
        ES_PER_PROC=4
        SCRATCH_BASE="."
        ;;
    "")
        DEFAULT_PROTOCOL="tcp"
        DEFAULT_PPN=1
        ES_PER_PROC=0               # signal: ask user for cores/proc
        SCRATCH_BASE="."
        ;;
    *)
        echo "ERROR: unknown --platform '$PLATFORM' (local|polaris|improv)" >&2
        exit 1
        ;;
esac

if [[ -n "$FROM_FILE" ]]; then
    [[ -f "$FROM_FILE" ]] || { echo "ERROR: --from-file '$FROM_FILE' not found" >&2; exit 1; }
    # shellcheck disable=SC1090
    source "$FROM_FILE"
fi

# --- prompt helpers -----------------------------------------------------------

# ask VAR "prompt" default [type] — set VAR from $FROM_FILE value, else prompt,
# else default. `type` is just appended to the prompt as a hint.
ask() {
    local var="$1" prompt="$2" default="$3" hint="${4:-}"
    local current="${!var:-$default}"
    if [[ -n "$FROM_FILE" ]]; then
        printf -v "$var" '%s' "$current"
        return
    fi
    local pfx="${C_CYAN}?${C_RESET} ${C_BOLD}${prompt}${C_RESET}"
    [[ -n "$hint" ]] && pfx+=" ${C_DIM}${hint}${C_RESET}"
    pfx+=" ${C_DIM}[default=${current}]${C_RESET}: "
    local response
    read -r -e -p "$pfx" response || true
    printf -v "$var" '%s' "${response:-$current}"
}

# ask_yesno VAR "prompt" default(yes|no)
ask_yesno() {
    local var="$1" prompt="$2" default="$3"
    local current="${!var:-$default}"
    if [[ -n "$FROM_FILE" ]]; then
        case "$current" in
            y|Y|yes|YES|true)  printf -v "$var" 'yes' ;;
            *)                 printf -v "$var" 'no'  ;;
        esac
        return
    fi
    local pfx="${C_CYAN}?${C_RESET} ${C_BOLD}${prompt}${C_RESET} ${C_DIM}(yes/no) [default=${current}]${C_RESET}: "
    local response
    read -r -e -p "$pfx" response || true
    response="${response:-$current}"
    case "$response" in
        y|Y|yes|YES|true)  printf -v "$var" 'yes' ;;
        *)                 printf -v "$var" 'no'  ;;
    esac
}

tcp_warning() {
    warn_block \
        "Mofka is optimized for HPC interconnects such as Verbs, Slingshot, etc." \
        "Using TCP will undermine its performance. Please do not use TCP for" \
        "performance evaluations."
}

is_tcp() {
    [[ "$1" == "tcp" || "$1" == "ofi+tcp" || "$1" == "na+tcp" || "$1" == *"+tcp"* ]]
}

# --- intro --------------------------------------------------------------------

if [[ -z "$FROM_FILE" ]]; then
    section "Mofka workflow configurator"
fi

# Unknown platform — ask the user for cores/node and NICs/node, then derive
# BEDROCK_PPN (one process per NIC) and ES_PER_PROC (cores split evenly).
if [[ -z "$PLATFORM" ]]; then
    if [[ -z "$FROM_FILE" ]]; then
        warn_block \
            "No --platform specified. Falling back to generic defaults." \
            "Pass --platform polaris|improv|local for platform-tuned defaults" \
            "(NIC count, core count, scratch path, etc.)."
        echo
        explain "Tell us about your node's hardware so we can size the bedrock"
        explain "deployment (one process per NIC, cores split evenly)."
    fi
    NPROC_ESTIMATE=$(nproc 2>/dev/null || echo 4)
    ask CORES_PER_NODE "Number of CPU cores per node" "$NPROC_ESTIMATE"
    ask NICS_PER_NODE  "Number of NICs per node"      1
    DEFAULT_PPN="$NICS_PER_NODE"
    ES_PER_PROC=$(( CORES_PER_NODE / DEFAULT_PPN ))
    [[ $ES_PER_PROC -lt 1 ]] && ES_PER_PROC=1
    PLATFORM="local"   # for the run-script hint at the end
fi

# --- bedrock-level prompts ----------------------------------------------------

section "Bedrock (Mofka server) deployment"
explain "Bedrock is the Mochi service launcher that hosts the Mofka providers."
explain "It is started under MPI. Protocol selects the Mercury transport;"
explain "ABI-compatible options are typically: na+sm (shared memory, single node),"
explain "tcp (any network), verbs (InfiniBand), cxi (Slingshot 11)."
ask BEDROCK_PROTOCOL "Bedrock transport protocol" "$DEFAULT_PROTOCOL"
if is_tcp "$BEDROCK_PROTOCOL"; then tcp_warning; fi

echo
explain "BEDROCK_NODES × BEDROCK_PPN = number of bedrock processes. With more"
explain "than one process, partitions are spread round-robin across them."
ask BEDROCK_NODES "Number of nodes for Mofka deployment"  1
ask BEDROCK_PPN   "Processes per node for Mofka"          "$DEFAULT_PPN"

echo
explain "The Flock group file is where bedrock writes its rendezvous address."
explain "Clients (diaspora-ctl, tekapp-*) use it to find Mofka."
ask MOFKA_FLOCK_FILE "Path to flock file" "mofka.flock"

# --- master database ----------------------------------------------------------

section "Master database (Yokan)"
explain "Holds Mofka's catalogue of topics and partitions. 'map' lives in RAM"
explain "(lost on shutdown); 'rocksdb' persists to disk (survives restarts)."
ask MASTER_DB_TYPE "Master DB backend (map|rocksdb)" "map"
if [[ "$MASTER_DB_TYPE" == "rocksdb" ]]; then
    explain "Pick a path on node-local fast storage when available."
    ask MASTER_DB_PATH "Path for master rocksdb DB" "$SCRATCH_BASE/mofka-master"
fi

# --- topics -------------------------------------------------------------------

section "Topics"
explain "tekapp uses three topics:"
explain "  daq_dist  — raw projections from DAQ to DIST (always 1 partition)"
explain "  dist_sirt — preprocessed sinograms from DIST to SIRT (one partition per SIRT rank)"
explain "  sirt_den  — reconstructed slices from SIRT to DEN (one partition per SIRT rank)"
ask SIRT_RANKS "Number of SIRT ranks (sets dist_sirt and sirt_den partition counts)" \
    "${SIRT_RANKS_FLAG:-2}"

# Fixed partition counts per topic — derived, not asked.
TOPICS=(daq_dist dist_sirt sirt_den)
declare -A TOPIC_PARTS=(
    [daq_dist]=1
    [dist_sirt]="$SIRT_RANKS"
    [sirt_den]="$SIRT_RANKS"
)

any_default_partition=no
last_type="memory"   # carries forward to the next topic's default
for t in "${TOPICS[@]}"; do
    echo
    subhead "Topic: $t"
    explain "  Partition manager:"
    explain "    memory  — in-RAM; events lost on shutdown; fastest."
    explain "    default — persists events to disk via an ABT-IO provider."
    ask "${t}_TYPE" "  partition type (memory|default)" "$last_type"
    type_var="${t}_TYPE"
    last_type="${!type_var}"
    if [[ "$last_type" == "default" ]]; then
        explain "  Storage path — directory on the bedrock nodes' local fast disk."
        ask "${t}_PATH" "  storage path" "$SCRATCH_BASE/mofka-data/$t"
        any_default_partition=yes
    fi
done

# --- ABT-IO -------------------------------------------------------------------

section "ABT-IO instance"
if [[ "$any_default_partition" == "yes" ]]; then
    INCLUDE_ABT_IO=yes
    explain "The 'default' partition manager requires an ABT-IO provider for"
    explain "asynchronous disk I/O. Two modes are available:"
    explain "  io_uring (recommended on Linux 5.1+): kernel async I/O, shares the"
    explain "           __primary__ Argobots pool. No extra threads needed."
    explain "  threaded: a dedicated pool with N worker xstreams. Use this if"
    explain "           io_uring is unavailable on your kernel."
    ask_yesno ABT_IO_USE_IO_URING "Use io_uring for ABT-IO?" "yes"
    if [[ "$ABT_IO_USE_IO_URING" == "yes" ]]; then
        ABT_IO_NUM_THREADS=0
    else
        explain "Each I/O thread becomes an Argobots execution stream on the dedicated"
        explain "io_pool. More threads = more concurrent disk writes (until you hit"
        explain "your storage bandwidth ceiling)."
        ask ABT_IO_NUM_THREADS "Threads dedicated to ABT-IO" 1
    fi
else
    explain "No topic uses the 'default' partition manager — ABT-IO not needed."
    INCLUDE_ABT_IO=no
    ABT_IO_USE_IO_URING=no
    ABT_IO_NUM_THREADS=0
fi

# --- margo --------------------------------------------------------------------

section "Margo (RPC handling)"
explain "Margo manages the Mercury RPC progress loop and dispatch threads."
explain "A dedicated progress thread isolates network polling from RPC handlers."
explain "Otherwise progress runs on the __primary__ Argobots execution stream."
ask_yesno USE_PROGRESS_THREAD "Use a dedicated progress thread?" "yes"

progress_es=0
[[ "$USE_PROGRESS_THREAD" == "yes" ]] && progress_es=1
DEFAULT_RPC=$((ES_PER_PROC - 1 - progress_es - ABT_IO_NUM_THREADS))
[[ $DEFAULT_RPC -lt 1 ]] && DEFAULT_RPC=1

echo
explain "rpc_thread_count is the number of xstreams Margo creates in the __rpc__"
explain "pool to dispatch incoming RPCs. Default = ES_PER_PROC ($ES_PER_PROC)"
explain "  − 1 (__primary__) − $progress_es (progress thread) − $ABT_IO_NUM_THREADS (ABT-IO threads)."
ask RPC_THREAD_COUNT "rpc_thread_count" "$DEFAULT_RPC"

# --- write mofka-answers.env --------------------------------------------------

ANSWERS="$OUT_DIR/mofka-answers.env"
{
    echo "# Generated by configure-mofka.sh on $(date)"
    echo "PLATFORM=$PLATFORM"
    echo "BEDROCK_PROTOCOL=$BEDROCK_PROTOCOL"
    echo "BEDROCK_NODES=$BEDROCK_NODES"
    echo "BEDROCK_PPN=$BEDROCK_PPN"
    echo "MOFKA_FLOCK_FILE=$MOFKA_FLOCK_FILE"
    echo "MASTER_DB_TYPE=$MASTER_DB_TYPE"
    [[ "$MASTER_DB_TYPE" == "rocksdb" ]] && echo "MASTER_DB_PATH=$MASTER_DB_PATH"
    echo "SIRT_RANKS=$SIRT_RANKS"
    echo "USE_PROGRESS_THREAD=$USE_PROGRESS_THREAD"
    echo "RPC_THREAD_COUNT=$RPC_THREAD_COUNT"
    echo "INCLUDE_ABT_IO=$INCLUDE_ABT_IO"
    echo "ABT_IO_USE_IO_URING=$ABT_IO_USE_IO_URING"
    echo "ABT_IO_NUM_THREADS=$ABT_IO_NUM_THREADS"
    [[ -n "${CORES_PER_NODE:-}" ]] && echo "CORES_PER_NODE=$CORES_PER_NODE"
    [[ -n "${NICS_PER_NODE:-}"  ]] && echo "NICS_PER_NODE=$NICS_PER_NODE"
    for t in "${TOPICS[@]}"; do
        for suf in TYPE PATH; do
            v="${t}_${suf}"
            val="${!v:-}"
            [[ -n "$val" ]] && echo "$v=$val"
        done
    done
} > "$ANSWERS"

# --- write mofka.json via python ----------------------------------------------

JSON="$OUT_DIR/mofka.json"
export INCLUDE_ABT_IO ABT_IO_USE_IO_URING ABT_IO_NUM_THREADS \
       USE_PROGRESS_THREAD RPC_THREAD_COUNT \
       BEDROCK_NODES BEDROCK_PPN MOFKA_FLOCK_FILE \
       MASTER_DB_TYPE
[[ "$MASTER_DB_TYPE" == "rocksdb" ]] && export MASTER_DB_PATH

python3 - "$JSON" <<'PYEOF'
import json, os, sys

out_path = sys.argv[1]

include_abt_io  = os.environ.get("INCLUDE_ABT_IO") == "yes"
use_io_uring    = os.environ.get("ABT_IO_USE_IO_URING") == "yes"
abt_io_threads  = int(os.environ.get("ABT_IO_NUM_THREADS", "0"))
use_progress    = os.environ.get("USE_PROGRESS_THREAD") == "yes"
rpc_threads     = int(os.environ.get("RPC_THREAD_COUNT", "0"))
n_nodes         = int(os.environ["BEDROCK_NODES"])
ppn             = int(os.environ["BEDROCK_PPN"])
flock_file      = os.environ["MOFKA_FLOCK_FILE"]
master_db_type  = os.environ["MASTER_DB_TYPE"]

libraries = [
    "libflock-bedrock-module.so",
    "libyokan-bedrock-module.so",
    "libmofka-bedrock-module.so",
]
if include_abt_io:
    libraries.insert(2, "libabt-io-bedrock-module.so")

margo = {
    "use_progress_thread": use_progress,
    "rpc_thread_count": rpc_threads,
}

# Dedicated io_pool when ABT-IO runs on its own xstreams (not io_uring).
if include_abt_io and not use_io_uring and abt_io_threads > 0:
    pools = [
        {"name": "__primary__", "kind": "fifo_wait", "access": "mpmc"},
        {"name": "io_pool",     "kind": "fifo_wait", "access": "mpmc"},
    ]
    xstreams = [
        {"name": "__primary__",
         "scheduler": {"type": "basic_wait", "pools": ["__primary__"]}},
    ]
    for i in range(abt_io_threads):
        xstreams.append({
            "name": f"io_es_{i}",
            "scheduler": {"type": "basic_wait", "pools": ["io_pool"]},
        })
    margo["argobots"] = {"pools": pools, "xstreams": xstreams}

providers = []

# Flock — bootstrap mode depends on total bedrock processes.
total_bedrock = n_nodes * ppn
providers.append({
    "name": "group_manager",
    "type": "flock",
    "provider_id": 1,
    "config": {
        "bootstrap": "self" if total_bedrock == 1 else "mpi",
        "file": flock_file,
        "group": {"type": "static"},
    },
})

# Master database — always guarded so only rank 0 hosts it.
master_db = {"type": master_db_type}
if master_db_type == "rocksdb":
    master_db["config"] = {
        "path": os.environ["MASTER_DB_PATH"],
        "create_if_missing": True,
    }
providers.append({
    "__if__": "$MPI_COMM_WORLD.rank == 0",
    "name": "master",
    "provider_id": 2,
    "type": "yokan",
    "tags": ["mofka:master"],
    "config": {"database": master_db},
})

# ABT-IO — always named "io_controller" so topic configs can reference it.
if include_abt_io:
    if use_io_uring:
        abt_io_cfg = {"num_urings": 1, "liburing_flags": ["IOSQE_ASYNC"]}
        abt_io_pool = "__primary__"
    elif abt_io_threads > 0:
        abt_io_cfg = {}
        abt_io_pool = "io_pool"
    else:
        abt_io_cfg = {}
        abt_io_pool = "__primary__"
    providers.append({
        "name": "io_controller",
        "type": "abt_io",
        "provider_id": 3,
        "config": abt_io_cfg,
        "dependencies": {"pool": abt_io_pool},
    })

config = {"margo": margo, "libraries": libraries, "providers": providers}

with open(out_path, "w") as f:
    json.dump(config, f, indent=2)
    f.write("\n")
PYEOF

# --- write mofka-config.env ---------------------------------------------------

CFG="$OUT_DIR/mofka-config.env"
{
    echo "# Generated by configure-mofka.sh on $(date) — do not edit by hand."
    echo "# Source this from a run-with-mofka-driver.sh script."
    echo "BEDROCK_PROTOCOL=\"$BEDROCK_PROTOCOL\""
    echo "BEDROCK_NODES=$BEDROCK_NODES"
    echo "BEDROCK_PPN=$BEDROCK_PPN"
    echo "MOFKA_FLOCK_FILE=\"$MOFKA_FLOCK_FILE\""
    echo "SIRT_RANKS=$SIRT_RANKS"
    echo "MOFKA_TOPICS=(${TOPICS[*]})"
    for t in "${TOPICS[@]}"; do
        type_var="${t}_TYPE"
        parts="${TOPIC_PARTS[$t]}"
        flags="--topic.num_partitions $parts --topic.config.type ${!type_var}"
        if [[ "${!type_var}" == "default" ]]; then
            path_var="${t}_PATH"
            flags+=" --topic.config.partition.path ${!path_var}"
            flags+=" --topic.dependencies.io_controller io_controller"
            flags+=" --topic.dependencies.pool __primary__"
        fi
        echo "MOFKA_TOPIC_FLAGS_${t}=\"$flags\""
    done
} > "$CFG"

# --- summary ------------------------------------------------------------------

section "Configuration complete"
info "    Bedrock JSON:     $JSON"
info "    Run-script env:   $CFG"
info "    Reusable answers: $ANSWERS"
echo
explain "Replay non-interactively:"
explain "  bash scripts/configure-mofka.sh --platform $PLATFORM \\"
explain "      --from-file $ANSWERS --out-dir $OUT_DIR"
echo
explain "Then run the workflow:"
explain "  bash platforms/$PLATFORM/run-with-mofka-driver.sh"
