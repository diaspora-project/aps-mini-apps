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
    "account":          "Diaspora",
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

parsl.load(parsl_config)


HERE = Path(__file__).resolve().parent

# Regexes for ldd parsing
_LDD_MISSING = re.compile(r'\s*(\S+)\s*=>\s*not found')
_LDD_FOUND   = re.compile(r'\s*\S+\s*=>\s*(/[^ ]+)/[^ ]+\s*\(0x[0-9a-fA-F]+\)')

# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------
def run_cmd(cmd, env=None):
    """Run a command, always capturing output and NEVER raising on non-zero."""
    p = subprocess.run(
        cmd, text=True, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False
    )
    return p.returncode, p.stdout

def ldd_missing_and_found_dirs(exe, env=None):
    rc, out = run_cmd(['ldd', exe], env=env)
    missing, found_dirs = set(), set()
    for line in out.splitlines():
        m1 = _LDD_FOUND.match(line)
        if m1:
            found_dirs.add(m1.group(1))
            continue
        m2 = _LDD_MISSING.match(line)
        if m2:
            missing.add(m2.group(1))
    return missing, found_dirs, out, rc

def find_any_lib(basename, search_dirs):
    # exact basename first
    for d in search_dirs:
        p = os.path.join(d, basename)
        if os.path.isfile(p):
            return p
    # then any .so / .so.* variant
    stem = basename.split('.so')[0]
    for d in search_dirs:
        for cand in glob.glob(os.path.join(d, f"{stem}.so*")):
            if os.path.isfile(cand):
                return cand
    return None

def make_soname_shims(missing_sonames, search_dirs):
    """
    For each missing SONAME 'libX.so.N', create 'libX.so.N -> libX.so*' symlink
    in a temp dir if we can find any suitable replacement.
    """
    tmpdir = tempfile.mkdtemp(prefix="ld_shims_")
    created = []
    for need in sorted(missing_sonames):
        if ".so" not in need:
            continue
        if find_any_lib(need, search_dirs):
            continue
        stem = need.split(".so")[0]
        # Try unversioned or any version we have
        replacement = (
            find_any_lib(f"{stem}.so", search_dirs) or
            find_any_lib(need.replace(".so.", ".so."), search_dirs)
        )
        if replacement:
            dst = os.path.join(tmpdir, need)
            try:
                os.symlink(replacement, dst)
                created.append((need, replacement, dst))
            except FileExistsError:
                pass
    if created:
        return tmpdir, created
    shutil.rmtree(tmpdir, ignore_errors=True)
    return None, []

def unique_paths(paths):
    seen, out = set(), []
    for p in paths:
        if p and p not in seen:
            seen.add(p)
            out.append(p)
    return out

def guess_spack_view_paths():
    """If SPACK_VIEW=/path/to/.spack-env/view is set, add its lib paths."""
    paths = []
    view = os.environ.get("SPACK_VIEW", "").strip()
    if view:
        for sfx in ("lib", "lib64"):
            p = os.path.join(view, sfx)
            if os.path.isdir(p):
                paths.append(p)
    return paths

def build_ld_library_path(lib_dirs_base, extra_paths=None):
    base = os.environ.get("LD_LIBRARY_PATH", "")
    parts = base.split(":") if base else []

    # Prepend our autodetected lib dirs
    parts = list(lib_dirs_base) + parts

    # Optional Spack view
    parts = guess_spack_view_paths() + parts

    # Optional: HDF5 module roots (if defined at site)
    for env_var in ("HDF5_DIR", "CRAY_HDF5_PARALLEL_PREFIX", "CRAY_HDF5_PREFIX"):
        root = os.environ.get(env_var)
        if root:
            for sfx in ("lib", "lib64"):
                p = os.path.join(root, sfx)
                if os.path.isdir(p):
                    parts.insert(0, p)

    if extra_paths:
        parts = list(extra_paths) + parts

    return ":".join(unique_paths(parts))

def discover_sirt_bin(cli_override: str | None) -> Path:
    """Find sirt_stream. Priority: CLI override → common build locations."""
    if cli_override:
        p = Path(cli_override).expanduser().resolve()
        if not p.exists():
            raise FileNotFoundError(f"--sirt-bin points to missing file: {p}")
        return p

    # Start from script location and walk up a few levels
    roots = [HERE, *HERE.parents[:4]]

    candidates = []
    for root in roots:
        # Typical cmake build: <repo>/build/bin/sirt_stream
        candidates.append(root / "build" / "bin" / "sirt_stream")
        # Some trees use build/build/bin
        candidates.append(root / "build" / "build" / "bin" / "sirt_stream")
        # Direct sibling of script's build dir (what you had): HERE/../bin
        candidates.append(root / "bin" / "sirt_stream")

    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()

    raise FileNotFoundError(
        "Could not auto-discover 'sirt_stream'. "
        "Pass --sirt-bin /full/path/to/sirt_stream"
    )

def lib_dirs_near_binary(sirt_bin: Path):
    """
    Heuristics to add local build lib directories:
    - <repo>/build/src
    - <repo>/build/src/sirt
    - the bin dir itself (rarely needed, but harmless)
    """
    lib_dirs = {str(sirt_bin.parent)}
    # Try to detect "<repo>/build" root
    # If binary path ends with ".../build/bin/sirt_stream", pick that "build".
    # Otherwise, walk up to a dir literally named "build".
    build_root = None
    if sirt_bin.parent.name == "bin" and sirt_bin.parent.parent.name == "build":
        build_root = sirt_bin.parent.parent
    else:
        for p in sirt_bin.parents:
            if p.name == "build":
                build_root = p
                break
    if build_root:
        for extra in ("src", "src/sirt"):
            d = build_root / extra
            if d.is_dir():
                lib_dirs.add(str(d))
    return sorted(lib_dirs)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def parse_arguments():
    parser = argparse.ArgumentParser(description='SIRT Iterative Image Reconstruction')

    # Binary override
    parser.add_argument('--sirt-bin', type=str, default=None, help='Full path to sirt_stream (optional)')

    parser.add_argument('--num-workers', type=int, default=1, help="Number of reconstruction workers")
    parser.add_argument('--protocol', default="na+sm", help='Mofka protocol')
    parser.add_argument('--group-file', type=str, default="mofka.json", help='Group file for the mofka server')
    parser.add_argument('--batchsize', type=str, default="16", help='Mofka batch size')
    parser.add_argument('--reconOutputPath', type=str, default="./output.h5", help='Output file path for reconstructed image (hdf5)')
    parser.add_argument('--recon-output-dir', type=str, default=".", help='Output directory for the streaming outputs')
    parser.add_argument('--reconDatasetPath', type=str, default="/data", help='Reconstruction dataset path in hdf5 file')
    parser.add_argument('--pub-freq', type=str, default="10000", help='Publish frequency')
    parser.add_argument('--center', type=str, default="0.", help='Center value')
    parser.add_argument('--thread', type=str, default="1", help='Number of threads per process')
    parser.add_argument('--write-freq', type=str, default="10000", help='Write frequency')
    parser.add_argument('--window-length', type=str, default="32", help='Number of projections kept in the window')
    parser.add_argument('--window-step', type=str, default="1", help='Number of projections received per request')
    parser.add_argument('--window-iter', type=str, default="1", help='Iterations per received window')
    parser.add_argument('--logdir', type=str, default=".", help='Log directory for sirt processes')
    parser.add_argument('--ckpt-freq', type=str, default="1", help='Checkpoint frequency')
    parser.add_argument('--ckpt-name', type=str, default="sirt", help='Checkpoint name')
    parser.add_argument('--ckpt-config', type=str, default="veloc.cfg", help='Checkpoint configuration (VeLoC)')

    # Optional extra LD paths
    parser.add_argument('--extra-ld-paths', type=str, default="", help='Colon-separated extra library paths to prepend')

    return parser.parse_args()

# ---------------------------------------------------------------------------
# Parsl app
# ---------------------------------------------------------------------------
@bash_app
def run_sirt(id, logdir=".", args=None, launcher_env=None, sirt_bin_path=""):
    args = args or []
    stderr = os.path.join(logdir, f'sirt-{id}.err')
    stdout = os.path.join(logdir, f'sirt-{id}.out')

    env_exports = [f'export {k}="{v}"' for k, v in (launcher_env or {}).items()]
    diag = [
        'echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"',
        f'ldd {sirt_bin_path} || true'
    ]
    cmd = " && ".join(
        env_exports
        + diag
        + [f'{sirt_bin_path} --worker-id {id} ' + " ".join(args) + f' >> "{stdout}" 2>> "{stderr}"']
    )
    return cmd

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    p = parse_arguments()

    # Locate binary
    sirt_bin = discover_sirt_bin(p.sirt_bin)
    sirt_bin_str = str(sirt_bin)

    # Build the forwarded argument vector (exclude helper flags)
    helper_keys = {"num_workers", "extra_ld_paths", "sirt_bin"}
    all_args = []
    for arg_name, value in vars(p).items():
        if arg_name in helper_keys:
            continue
        flag = "--" + arg_name.replace("_", "-")
        all_args += [flag, str(value)]

    # Lib dirs near the discovered binary
    base_lib_dirs = lib_dirs_near_binary(sirt_bin)

    # Optional extra paths
    extra_paths = []
    if p.extra_ld_paths:
        extra_paths = [x for x in p.extra_ld_paths.split(":") if x]

    # Compose LD_LIBRARY_PATH
    base_ld = build_ld_library_path(base_lib_dirs, extra_paths=extra_paths)

    # First ldd pass
    env1 = dict(os.environ)
    env1["LD_LIBRARY_PATH"] = base_ld
    missing1, found_dirs, ldd_out1, rc1 = ldd_missing_and_found_dirs(sirt_bin_str, env=env1)

    # SONAME shims for missing libs (e.g., libmargo.so.0)
    shim_dir, created = make_soname_shims(missing1, search_dirs=base_ld.split(":"))
    effective_ld = base_ld
    if shim_dir:
        effective_ld = shim_dir + (":" + effective_ld if effective_ld else "")

    # Second ldd pass (non-fatal)
    env2 = dict(os.environ)
    env2["LD_LIBRARY_PATH"] = effective_ld
    missing2, _, ldd_out2, rc2 = ldd_missing_and_found_dirs(sirt_bin_str, env=env2)

    # Logs
    print(f"Workers: {p.num_workers}")
    print("Args:", all_args)
    print("\nComputed LD_LIBRARY_PATH:\n", effective_ld)
    print("\nldd BEFORE shims (rc={}):\n".format(rc1), ldd_out1)
    if created:
        print("Created SONAME shims:")
        for need, src, dst in created:
            print(f"  {need} -> {src}  (at {dst})")
    print("\nldd AFTER shims (rc={}):\n".format(rc2), ldd_out2)
    if missing2:
        print("Still missing after shims:", ", ".join(sorted(missing2)))

    # Launch workers
    futures = []
    for i in range(int(p.num_workers)):
        fut = run_sirt(
            id=str(i),
            logdir=p.logdir,
            args=all_args,
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