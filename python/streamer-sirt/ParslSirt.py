import os
import re
import sys
import glob
import subprocess
import argparse
import parsl
from parsl.app.app import bash_app
from parsl.configs.local_threads import config

config.retries = 3
parsl.load(config)

SIRT_BIN = "build/bin/sirt_stream"

def parse_arguments():
    p = argparse.ArgumentParser(description='SIRT Iterative Image Reconstruction')
    p.add_argument('--num-workers', type=int, default=1)
    p.add_argument('--protocol', default="na+sm")
    p.add_argument('--group-file', type=str, default="mofka.json")
    p.add_argument('--batchsize', type=str, default="16")
    p.add_argument('--reconOutputPath', type=str, default="./output.h5")
    p.add_argument('--recon-output-dir', type=str, default=".")
    p.add_argument('--reconDatasetPath', type=str, default="/data")
    p.add_argument('--pub-freq', type=str, default="10000")
    p.add_argument('--center', type=str, default="0.")
    p.add_argument('--thread', type=str, default="1")
    p.add_argument('--write-freq', type=str, default="10000")
    p.add_argument('--window-length', type=str, default="32")
    p.add_argument('--window-step', type=str, default="1")
    p.add_argument('--window-iter', type=str, default="1")
    p.add_argument('--logdir', type=str, default=".")
    p.add_argument('--ckpt-freq', type=str, default="1")
    p.add_argument('--ckpt-name', type=str, default="sirt")
    p.add_argument('--ckpt-config', type=str, default="veloc.cfg")
    return p.parse_args()

# ---- library discovery helpers ------------------------------------------------

_LDD_MISSING = re.compile(r'\s*(\S+)\s*=>\s*not found')
_LDD_FOUND   = re.compile(r'\s*\S+\s*=>\s*(/[^ ]+)/[^ ]+\s*\(0x[0-9a-fA-F]+\)')

def ldd_dirs(exe):
    """Return (found_dirs, missing_sonames) from ldd output."""
    try:
        out = subprocess.check_output(['ldd', exe], text=True, stderr=subprocess.STDOUT)
    except Exception as e:
        print(f"[ldd] warning: {e}", file=sys.stderr)
        return set(), set()
    found_dirs, missing = set(), set()
    for line in out.splitlines():
        m1 = _LDD_FOUND.match(line)
        if m1:
            found_dirs.add(m1.group(1))
            continue
        m2 = _LDD_MISSING.match(line)
        if m2:
            missing.add(m2.group(1))
    return found_dirs, missing

def candidate_roots():
    roots = set()

    # 1) Spack view (if user exported it or CMake used it)
    for envvar in ("SPACK_ENV", "SPACK_VIEW", "CMAKE_PREFIX_PATH"):
        val = os.environ.get(envvar)
        if not val:
            continue
        for path in val.split(":"):
            if path:
                roots.add(path)
                # common view layout
                for sub in ("lib", "lib64", ".spack-env/view", ".spack-env/._view"):
                    p = os.path.join(path, sub)
                    if os.path.isdir(p): roots.add(p)

    # 2) Generic: project root and its parents (limited)
    here = os.path.abspath(os.getcwd())
    for _ in range(4):
        roots.add(here)
        for sub in ("lib", "lib64", "build", "build/lib", "build/lib64"):
            p = os.path.join(here, sub)
            if os.path.isdir(p): roots.add(p)
        here = os.path.dirname(here)

    # 3) Home (shallow scan later)
    roots.add(os.path.expanduser("~"))

    # 4) Existing LD_LIBRARY_PATH entries
    for p in os.environ.get("LD_LIBRARY_PATH", "").split(":"):
        if p: roots.add(p)

    return list(roots)

def find_missing_sos(missing_sonames, roots, max_hits=10):
    """Search for each missing soname recursively under roots."""
    hits = {}
    # normalize names like 'libmargo.so.0' → also try 'libmargo.so*'
    patterns = set()
    for name in missing_sonames:
        if name.endswith(".so") or ".so." in name:
            stem = name.split(".so")[0]
            patterns.update([name, f"{stem}.so*", f"{stem}*.so*"])
        else:
            patterns.update([name, f"{name}*", f"{name}.so*"])

    # limit recursion depth by using glob on common lib dirs first
    lib_suffixes = ("lib", "lib64")
    for root in roots:
        if not os.path.isdir(root): continue
        for suf in lib_suffixes:
            d = root if root.endswith(suf) else os.path.join(root, suf)
            if not os.path.isdir(d): continue
            for pat in patterns:
                for found in glob.glob(os.path.join(d, pat)):
                    if os.path.isfile(found):
                        hits.setdefault(os.path.basename(found), set()).add(os.path.dirname(found))
                        if sum(len(v) for v in hits.values()) >= max_hits:
                            return hits

    # fallback: a light recursive scan (capped)
    scanned = 0
    cap = 4000  # guardrail for very large trees
    for root in roots:
        if not os.path.isdir(root): continue
        for dirpath, dirnames, filenames in os.walk(root):
            scanned += 1
            if scanned > cap:
                break
            for miss in list(missing_sonames):
                for f in filenames:
                    if f == miss or (f.startswith(miss.split(".so")[0]) and ".so" in f):
                        hits.setdefault(miss, set()).add(dirpath)
    return hits

def build_runtime_ld_path(exe):
    existing = [p for p in os.environ.get("LD_LIBRARY_PATH", "").split(":") if p]
    found_dirs, missing = ldd_dirs(exe)

    roots = candidate_roots()
    add_from_missing = find_missing_sos(missing, roots)

    new_paths = set(existing)
    new_paths.update(found_dirs)
    for dirs in add_from_missing.values():
        new_paths.update(dirs)

    # keep order: existing first, then new finds
    ordered = []
    seen = set()
    for p in existing + list(found_dirs) + [d for dirs in add_from_missing.values() for d in dirs]:
        if p and p not in seen:
            seen.add(p)
            ordered.append(p)

    # Always add lib/lib64 from any path in CMAKE_PREFIX_PATH (cheap and helpful for Spack views)
    for prefix in os.environ.get("CMAKE_PREFIX_PATH", "").split(":"):
        for suf in ("lib64", "lib"):
            p = os.path.join(prefix, suf)
            if os.path.isdir(p) and p not in seen:
                seen.add(p)
                ordered.append(p)

    return ":".join(ordered)

# ---- Parsl app ----------------------------------------------------------------

@bash_app
def run_sirt(id, logdir=".", args=[], env=None):
    os.makedirs(logdir, exist_ok=True)
    stderr = os.path.join(logdir, f"sirt-{id}.err")
    stdout = os.path.join(logdir, f"sirt-{id}.out")
    debug = f'echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"; ldd {SIRT_BIN} || true'
    cmd = f'{debug} ; {SIRT_BIN} --worker-id {id} ' + " ".join(args) + f" >> {stdout} 2>> {stderr}"
    return cmd

def main():
    params = parse_arguments()
    exclude = {"num_workers", "logdir"}
    args = []
    for k, v in vars(params).items():
        if k in exclude: continue
        args.extend([f'--{k.replace("_","-")}', str(v)])

    # Compute LD_LIBRARY_PATH dynamically for this binary
    ldpath = build_runtime_ld_path(SIRT_BIN)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = ldpath

    print("Workers:", params.num_workers)
    print("Args:", args)
    print("Computed LD_LIBRARY_PATH:\n", ldpath)

    futs = [run_sirt(id=str(i), logdir=params.logdir, args=args, env=env)
            for i in range(params.num_workers)]
    for f in futs:
        print(f.result())

if __name__ == "__main__":
    main()