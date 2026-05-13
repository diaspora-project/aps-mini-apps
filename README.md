# Trace

This is APS mini-app that simulates the tomographic reconstruction on streaming
tomography data. The reconstruction component provides a sliding window data
structure to store (partial) data and a reconstruction process to reconstruct
the data in the window. The reconstruction algorithm is based on the
simultaneous iterative reconstruction technique (SIRT). This is a CPU-based
code and is optimized for parallel and distributed memory. We plan to add the
GPU-based version as well.

## Pipeline

```
DAQ → daq_dist → DIST → dist_sirt → SIRT → sirt_den → DEN
```

| Component | Source | Installed command |
|---|---|---|
| DAQ — data acquisition simulator | `python/tekapp/streamer_daq/` | `tekapp-daq` |
| DIST — preprocessing + sinogram distribution | `python/tekapp/streamer_dist/` | `tekapp-dist` |
| SIRT — MPI reconstruction (C++) | `src/sirt/` | `tekapp-sirt` (symlink to `sirt_stream`) |
| DEN — denoiser | `python/tekapp/streamer_denoiser/` | `tekapp-denoiser` |

Shared utilities live in `python/tekapp/common/` (`tekapp.common.serializer`,
`tekapp.common.ts_collector`, FlatBuffers `MONA` classes).

## Build

There are two supported build paths: build from source with CMake, or let
Spack build everything via the `tekapp` Spack package.

### Option A — From source (CMake)

Dependencies must be available: MPI, parallel HDF5, FlatBuffers (with `flatc`),
fmt, diaspora-stream-api, Python 3, plus the Python deps used by the
components (`numpy`, `scipy`, `tomopy`, `dxchange`, `h5py`, `flatbuffers`,
`matplotlib`). The easiest way to get them is to activate the Spack env in
`platforms/<your-platform>/` and then run cmake — see Option B.

```bash
mkdir build && cd build
cmake ..
make
```

CMake invokes `flatc` automatically to regenerate
`include/tracelib/trace_prot_generated.h` from `trace_prot.fbs` when the
schema changes. The build also copies the `tekapp` Python package into
`build/python/tekapp/` and generates launcher scripts at `build/bin/tekapp-*`,
so you can run components straight out of the build tree (the `run-*.sh`
scripts in `platforms/` add `build/bin` to `PATH` automatically when present).

#### Install

```bash
make install                              # default prefix /usr/local
# or pick your own prefix:
cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local ..
make install
```

Layout under `CMAKE_INSTALL_PREFIX`:

| Path | Contents |
|---|---|
| `bin/sirt_stream`, `bin/tekapp-sirt` | MPI reconstruction binary + symlink |
| `bin/tekapp-{daq,dist,denoiser}` | Python launcher scripts |
| `lib/libdiaspora_stream.so`, `libsirt.so`, `libtrace_*.so` | shared libs (RPATH `$ORIGIN/../lib`) |
| `lib/python<X.Y>/site-packages/tekapp/` | DAQ, DIST, DEN entry points + shared `tekapp.common` |

The Python version embedded in the install path is taken from the interpreter
CMake found, so the package lands in the right `site-packages` automatically.

### Option B — With Spack

Each platform under `platforms/` ships a `spack.yaml` that pulls in the
`tekapp` Spack package (defined in
[diaspora-spack-packages](https://github.com/diaspora-project/diaspora-spack-packages))
together with its dependencies, including `mofka+python`. Pick the env that
matches your machine:

```bash
# Local workstation (or laptop)
spack env create tekapp-env platforms/local/spack.yaml
spack env activate tekapp-env
spack install
```

```bash
# Polaris (uses externals + module-aware config and build in a PBS job)
qsub -A <your-project> platforms/polaris/build.sh
```

Note: on Polaris we use a job to build the code as Spack tends to spawn too
many processes on login nodes and gets killed. If the build doesn't complete
within the 1h job allocation, simply submit another job, it will pick up where
the previous one stopped.

The `repos:` section in each `spack.yaml` declares the diaspora and mochi
package repositories, so Spack clones them automatically — no need to add
them by hand.

After the env is activated, the `tekapp-{daq,dist,sirt,denoiser}` commands and
the `tekapp` Python package are on `PATH`/`PYTHONPATH` directly; the run
scripts work without a `build/` directory.

## Running the workflow

The launcher scripts under `platforms/` use `tekapp-{daq,dist,sirt,denoiser}`
from `PATH`. They prefer the project's `build/bin/` if present and fall back
to whatever `spack load tekapp` (or `make install`) put on `PATH`.

### Local — files driver (no Mofka server, just a shared directory)

```bash
bash platforms/local/run-with-files-driver.sh
```

### Local — Mofka driver

```bash
bash platforms/local/run-with-mofka-driver.sh
```

This stands up a local Bedrock + Mofka server, creates the Diaspora topics,
and launches all four components. If everything works you should start
seeing `*-recon.h5` and `*-denoised.h5` files appear.

### Polaris

Edit `platforms/polaris/run-with-mofka-driver.sh` PBS directives at the top
to match your allocation, then submit:

```bash
qsub platforms/polaris/run-with-mofka-driver.sh
```

The script handles node allocation (1 node for Mofka, 1 each for DAQ/DIST/DEN,
the rest for SIRT MPI ranks).

## Run with Docker

All four components can be run in containers using the provided
`docker-compose.yaml`. Two profiles are supported:

```bash
# files driver (shared volume, no Mofka)
docker compose --profile files up --abort-on-container-exit

# mofka driver (one Bedrock + Mofka server container, plus the four components)
docker compose --profile mofka up --abort-on-container-exit

docker compose down -v   # clean up the shared data volumes
```
