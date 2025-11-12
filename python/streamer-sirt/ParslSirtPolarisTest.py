#!/usr/bin/env python3
import os
import sys
import parsl
from parsl.app.app import bash_app
from parsl.config import Config
from parsl.addresses import address_by_interface
from parsl.providers import LocalProvider
from parsl.executors import HighThroughputExecutor
from parsl.launchers import MpiExecLauncher

print("Parsl version:", parsl.__version__)

# --- Short temp/log roots to avoid AF_UNIX path length issues ---
USER = os.environ.get("USER", "user")
SHORT_TMP = f"/tmp/{USER}/parsl-tmp"
SHORT_LOG = f"/tmp/{USER}/parsl-logs"
os.makedirs(SHORT_TMP, exist_ok=True)
os.makedirs(SHORT_LOG, exist_ok=True)
os.environ["TMPDIR"] = SHORT_TMP            # make Python's temp sockets short

# The config will launch workers from this directory (can be long; that’s OK now)
run_dir = os.getcwd()

# Get number of nodes (gracefully handle login node runs)
node_file = os.getenv("PBS_NODEFILE")
if node_file and os.path.isfile(node_file):
    with open(node_file, "r") as f:
        num_nodes = len(f.readlines())
else:
    num_nodes = 1

ed = "/home/ndhai/diaspora/src/aps-mini-apps"

user_opts = {
    "worker_init": (
        # ensure worker processes inherit short TMPDIR and have envs
        f"export TMPDIR={SHORT_TMP}; mkdir -p $TMPDIR; "
        f"cd {ed}; source activate-spack.sh; source pyvenv/bin/activate; "
        f"cd {run_dir}"
    ),
    "scheduler_options": "#PBS -l filesystems=home:eagle",
    "account": "diaspora",
    "queue": "debug-scaling",
    "walltime": "1:00:00",
    "nodes_per_block": 3,
    "cpus_per_node": 32,
    "available_accelerators": 4,
}
print("User options:", user_opts)

parsl_config = Config(
    executors=[
        HighThroughputExecutor(
            label="htex",
            address=address_by_interface('bond0'),
            worker_debug=True,
            # keep ports as you had them; not related to this error
            worker_port_range=(54000, 55000),
            interchange_port_range=(55000, 56000),
            # CRUCIAL: write worker logs under short path to keep Manager sockets short
            worker_logdir_root=SHORT_LOG,
            # Optional: explicitly pick 'spawn' (usually default on 3.8+)
            start_method="spawn",
            provider=LocalProvider(
                nodes_per_block=num_nodes,
                launcher=MpiExecLauncher(bind_cmd="--cpu-bind", overrides="--ppn 1", debug=True),
                init_blocks=1,
                max_blocks=1,
                worker_init=user_opts["worker_init"],
            ),
        ),
    ],
    run_dir=run_dir,
    retries=2,
    app_cache=True,
)

print("Parsl config:", parsl_config)
parsl.load(parsl_config)

@bash_app
def run_sirt(id, logdir=".", args=None, launcher_env=None, sirt_bin_path=""):
    return f"echo hello from SIRT app {id}"

def main():
    futures = [run_sirt(id=str(i)) for i in range(2)]
    for f in futures:
        print(f.result())

if __name__ == "__main__":
    main()