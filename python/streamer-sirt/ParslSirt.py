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

# -----------------------------------------------------------------------------
# Config
# -----------------------------------------------------------------------------
# Use local_threads; bump retries if you want.
local_threads_config.retries = 100000
parsl.load(local_threads_config)

# Paths relative to this script
HERE = Path(__file__).resolve().parent
BUILD_DIR = (HERE / "build").resolve()
BIN_DIR = (BUILD_DIR / "bin").resolve()
LIB_DIRS_DEFAULT = [
    # local build/lib dirs
    str((BUILD_DIR / "src").resolve()),
    str((BUILD_DIR / "src" / "sirt").resolve()),
]
SIRT_BIN = str((BIN_DIR / "sirt_stream").resolve())

# Regexes to parse ldd
_LDD_MISSING = re.compile(r'\s*(\S+)\s*=>\s*not found')
_LDD_FOUND   = re.compile(r'\s*\S+\s*=>\s*(/[^ ]+)/[^ ]+\s*\(0x[0-9a-fA-F]+\)')

# -----------------------------------------------------------------------------
# Helper: ldd parsing and SONAME shim creation
# -----------------------------------------------------------------------------
def run_cmd(cmd, env=None, check=True, capture=True):
    if capture:
        return subprocess.run(cmd, text=True, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              check=check).stdout
    else:
        return subprocess.run(cmd, text=True, env=env, check=check)

def ldd_missing_and_found_dirs(exe, env=None):
    out = run_cmd(['ldd', exe], env=env)
    missing, found_dirs = set(), set()
    for line in out.splitlines():
        m1 = _LDD_FOUND.match(line)
        if m1:
            found_dirs.add(m1.group(1))
            continue
        m2 = _LDD_MISSING.match(line)
        if m2:
            missing.add(m2.group(1))
    return missing, found_dirs, out

def find_any_lib(basename, search_dirs):
    # exact basename first
    for d in search_dirs:
        p = os.path.join(d, basename)
        if os.path.isfile(p):
            return p
    # then any .so or .so.* variant
    stem = basename.split('.so')[0]
    for d in search_dirs:
        for cand in glob.glob(os.path.join(d, f"{stem}.so*")):
            if os.path.isfile(cand):
                return cand
    return None

def make_soname_shims(missing_sonames, search_dirs):
    """
    For each missing SONAME 'libX.so.N', if we only have 'libX.so' (or libX.so*M),
    create 'libX.so.N -> libX.so*' symlink inside a temp dir.
    Return (shim_dir_or_None, created_symlinks_list)
    """
    tmpdir = tempfile.mkdtemp(prefix="ld_shims_")
    created = []
    for need in sorted(missing_sonames):
        if ".so" not in need:
            continue
        # already present anywhere?
        if find_any_lib(need, search_dirs):
            continue
        # fallback to any unversioned/other version we can find
        stem = need.split(".so")[0]
        replacement = find_any_lib(f"{stem}.so", search_dirs)
        if not replacement:
            # maybe we only have libX.so.M (different M)
            replacement = find_any_lib(f"{stem}.so", search_dirs) or find_any_lib(need.replace(".so.", ".so."), search_dirs)
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

# -----------------------------------------------------------------------------
# LD_LIBRARY_PATH builder (site-agnostic)
# -----------------------------------------------------------------------------
def unique_paths(paths):
    seen = set()
    out = []
    for p in paths:
        if not p:
            continue
        if p not in seen:
            seen.add(p)
            out.append(p)
    return out

def guess_spack_view_paths():
    """
    If the user provides SPACK_VIEW (path to .../.spack-env/view), use it.
    Otherwise, do nothing. Keeping this optional preserves portability.
    """
    paths = []
    view = os.environ.get("SPACK_VIEW", "").strip()
    if view:
        for sfx in ("lib", "lib64"):
            p = os.path.join(view, sfx)
            if os.path.isdir(p):
                paths.append(p)
    return paths

def build_ld_library_path(extra_paths=None):
    base = os.environ.get("LD_LIBRARY_PATH", "")
    parts = base.split(":") if base else []

    # Always add our build-time library directories
    parts = LIB_DIRS_DEFAULT + parts

    # Optional: include a user-specified Spack view (set SPACK_VIEW env var)
    parts = guess_spack_view_paths() + parts

    # Optional: add Cray HDF5 (already available via modules on many sites)
    for env_var in ("HDF5_DIR", "CRAY_HDF5_PARALLEL_PREFIX", "CRAY_HDF5_PREFIX"):
        root = os.environ.get(env_var)
        if root:
            for sfx in ("lib", "lib64"):
                p = os.path.join(root, sfx)
                if os.path.isdir(p):
                    parts.insert(0, p)

    # Allow caller to append anything else
    if extra_paths:
        parts = list(extra_paths) + parts

    return ":".join(unique_paths(parts))

# -----------------------------------------------------------------------------
# CLI args (your existing flags kept intact)
# -----------------------------------------------------------------------------
def parse_arguments():
    parser = argparse.ArgumentParser(description='SIRT Iterative Image Reconstruction')

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

    # Optional: let user pass extra LD paths without touching the script
    parser.add_argument('--extra-ld-paths', type=str, default="", help='Colon-separated extra library paths to prepend')

    return parser.parse_args()

# -----------------------------------------------------------------------------
# Parsl app
# -----------------------------------------------------------------------------
@bash_app
def run_sirt(id, logdir=".", args=None, launcher_env=None):
    args = args or []
    stderr = os.path.join(logdir, f'sirt-{id}.err')
    stdout = os.path.join(logdir, f'sirt-{id}.out')

    # Environment export inside the remote shell
    env_exports = []
    for k, v in (launcher_env or {}).items():
        env_exports.append(f'export {k}="{v}"')

    # Some helpful diagnostics (kept short; remove if noisy)
    diag = [
        'echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"',
        f'ldd {SIRT_BIN} || true'
    ]

    cmd = " && ".join(
        env_exports
        + diag
        + [f'{SIRT_BIN} --worker-id {id} ' + " ".join(args) + f' >> "{stdout}" 2>> "{stderr}"']
    )
    return cmd

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------
def main():
    p = parse_arguments()

    # Build the argument vector you were using before (including all flags)
    all_args = []
    kv = vars(p).copy()
    # strip helper-only args from forwarding
    helper_keys = {"num_workers", "extra_ld_paths"}
    # argparse stored it as 'num_workers' when using '--num-workers'? Keep consistent:
    # We'll build from the explicit flags above anyway:
    for arg_name, value in kv.items():
        if arg_name in helper_keys:
            continue
        # convert underscored name back to CLI flag
        flag = "--" + arg_name.replace("_", "-")
        all_args += [flag, str(value)]

    # Construct LD_LIBRARY_PATH
    extra_paths = []
    if p.extra_ld_paths:
        extra_paths = [x for x in p.extra_ld_paths.split(":") if x]

    base_ld = build_ld_library_path(extra_paths=extra_paths)

    # First pass: see what’s missing and where we already find libs
    env1 = dict(os.environ)
    env1["LD_LIBRARY_PATH"] = base_ld
    missing1, found_dirs, ldd_out1 = ldd_missing_and_found_dirs(SIRT_BIN, env=env1)

    # Create SONAME shims (e.g., libmargo.so.0 → libmargo.so) if needed
    shim_dir, created = make_soname_shims(missing1, search_dirs=base_ld.split(":"))
    effective_ld = base_ld
    if shim_dir:
        effective_ld = shim_dir + (":" + effective_ld if effective_ld else "")

    # (Optional) Check again after adding shims
    env2 = dict(os.environ)
    env2["LD_LIBRARY_PATH"] = effective_ld
    missing2, _, ldd_out2 = ldd_missing_and_found_dirs(SIRT_BIN, env=env2)

    # Logs / prints
    print(f"Workers: {p.num_workers}")
    print("Args:", all_args)
    print("\nComputed LD_LIBRARY_PATH:\n", effective_ld)
    print("\nldd BEFORE shims:\n", ldd_out1)
    if shim_dir:
        print("Created SONAME shims:")
        for need, src, dst in created:
            print(f"  {need} -> {src}  (at {dst})")
    print("\nldd AFTER shims:\n", ldd_out2)
    if missing2:
        print("Still missing after shims:", ", ".join(sorted(missing2)))

    # Launch workers
    futures = []
    for i in range(int(p.num_workers)):
        fut = run_sirt(
            id=str(i),
            logdir=p.logdir,
            args=all_args,
            launcher_env={"LD_LIBRARY_PATH": effective_ld}
        )
        futures.append(fut)

    # Wait & print
    for f in futures:
        print(f.result())

    # Clean up shim dir on exit
    if shim_dir:
        shutil.rmtree(shim_dir, ignore_errors=True)

if __name__ == "__main__":
    main()