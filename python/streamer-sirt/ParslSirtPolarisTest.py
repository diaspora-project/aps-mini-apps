#!/usr/bin/env python3
import os
import re
import glob
import sys
import argparse
import subprocess
import tempfile
import shutil
from pathlib import Path

import parsl
from parsl.app.app import bash_app
from parsl.configs.local_threads import config as local_threads_config
from parsl.executors import ThreadPoolExecutor
from parsl.config import Config

# PBSPro is the right provider for Polaris:
from parsl.providers import LocalProvider
# The high throughput executor is for scaling to HPC systems:
from parsl.executors import HighThroughputExecutor
# Use the MPI launcher to create one worker per GPU
from parsl.launchers import MpiExecLauncher
# For checkpointing:
from parsl.utils import get_all_checkpoints


print("Parsl version:", parsl.__version__)

# # ---------------------------------------------------------------------------
# # Parsl config
# # ---------------------------------------------------------------------------
# local_threads_config.retries = 100000
# # local_threads_config.executors = [
# #             ThreadPoolExecutor(max_threads=16, label="local_threads")
# #         ]
# parsl.load(local_threads_config)

tile_names = [f'{gid}.{tid}' for gid in range(6) for tid in range(2)]

# The config will launch workers from this directory
run_dir = os.getcwd()

# Get the number of nodes:
node_file = os.getenv("PBS_NODEFILE")
with open(node_file,"r") as f:
    node_list = f.readlines()
    num_nodes = len(node_list)

user_opts = {
    # "worker_init":      f"source /path/to/your/virtualenv/bin/activate; cd {run_dir}", # load the environment where parsl is installed
    "worker_init":      f"cd {run_dir}", # load the environment where parsl is installed
    "scheduler_options":"#PBS -l filesystems=home:eagle" , # specify any PBS options here, like filesystems
    "account":          "diaspora",
    "queue":            "debug-scaling",
    "walltime":         "1:00:00",
    "nodes_per_block":  3, # think of a block as one job on polaris, so to run on the main queues, set this >= 10
    "cpus_per_node":    32, # Up to 64 with multithreading
    "available_accelerators": 4, # Each Polaris node has 4 GPUs, setting this ensures one worker per GPU
}

parsl_config = Config(
    executors=[
        HighThroughputExecutor(
            label="htex",
            heartbeat_period=15,
            heartbeat_threshold=120,
            worker_debug=True,
            # available_accelerators=user_opts["available_accelerators"],
            # max_workers_per_node=user_opts["available_accelerators"],
            # # This give optimal binding of threads to GPUs on a Polaris node
            # cpu_affinity="list:24-31,56-63:16-23,48-55:8-15,40-47:0-7,32-39",
            prefetch_capacity=0,
            provider=LocalProvider(
                # Number of nodes job
                nodes_per_block=num_nodes,
                launcher=MpiExecLauncher(bind_cmd="--cpu-bind", overrides="--ppn 1"),
                init_blocks=1,
                max_blocks=1,
            ),
        ),
    ],
    run_dir=run_dir,
    retries=2,
    app_cache=True
)

print("Parsl config:", parsl_config)

parsl.load(parsl_config)

# ---------------------------------------------------------------------------
# Parsl app
# ---------------------------------------------------------------------------
@bash_app
def run_sirt(id, logdir=".", args=None, launcher_env=None, sirt_bin_path=""):
    return "echo hello from SIRT app {}".format(id)

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    futures = []
    for i in range(int(2)):
        fut = run_sirt(
            id=str(i),
            logdir="",
            args={},
            launcher_env={"LD_LIBRARY_PATH": effective_ld},
            sirt_bin_path=sirt_bin_str
        )
        futures.append(fut)

    # Wait & print
    for f in futures:
        print(f.result())

    if shim_dir:
        shutil.rmtree(shim_dir, ignore_errors=True)

if __name__ == "__main__":
    main()