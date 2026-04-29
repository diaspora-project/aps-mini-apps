# Trace

This is APS mini-app that simulates the tomographic reconstruction on streaming tomography data. The reconstruction component provides a sliding window data structure to store (partial) data and a reconstruction process to reconstruct the data in the window. The reconstruction algorithm is based on the simultaneous iterative reconstruction technique (SIRT). This is a CPU-based code and is optimized for parallel and distributed memory. We plan to add the GPU-based version as well.

## Instructions to Run Mini-App on Polaris

### Instructions for installation with Spack:

There are several dependencies, including zmq, swig, python libraries/headers, MPI, flatbuffers, parallel hdf5, cmake, and a C++ compiler.
We have included a spack env file `spack_polaris.yaml` with needed dependencies.

Here are the steps to use spack to install the environment:
1. Clone spack [repo](https://github.com/spack/spack.git)
2. Clone diaspora spack packages [repo](https://github.com/diaspora-project/diaspora-spack-packages.git)
3. Clone mochi spack packages [repo](https://github.com/mochi-hpc/mochi-spack-packages.git)
4. Create a spack env `spack env create APS_ENV spack.yaml`
5. Activate the env `spack env activate APS_ENV`
6. Add mochi spack packages to the env `spack repo add mochi-spack-packages`
7. Add diaspora spack packages to the env `spack repo add diaspora-spack-packages/spack_repo/diaspora`
8. Concretize and install `spack concretize -f && spack install`

### Build

Generate the C++ FlatBuffers header, then build all components (C++ binary + Python scripts):

```bash
cd include/tracelib
flatc -c trace_prot.fbs
cd ../..

mkdir build && cd build
cmake ..
make
```

`cmake ..` copies all Python scripts into the build tree automatically:

| Destination | Source |
|---|---|
| `build/python/streamer-daq/DAQStream.py` | `python/streamer-daq/DAQStream.py` |
| `build/python/streamer-dist/ModDistStreamPubDemo.py` | `python/streamer-dist/ModDistStreamPubDemo.py` |
| `build/python/streamer-dist/diaspora_dist.py` | `python/streamer-dist/diaspora_dist.py` |
| `build/python/streamer-denoiser/denoiser.py` | `python/streamer-denoiser/denoiser.py` |
| `build/python/common/` | `python/common/` |

`make` produces the `build/bin/sirt_stream` executable.

### Run the workflow

Locally, you can run the workflow using `run-local-with-mofka-driver.sh`.
This will deploy Mofka locally, using in-memory partitions, then deploy and run all the components.
If all runs well, you should start seeing HDF5 files being generated.

### Run with Docker

All four components can be run in Docker containers using the provided `docker-compose.yaml`:

```bash
docker compose build
docker compose up --abort-on-container-exit
docker compose down -v   # clean up the shared data volume
```

### Instructions to Run the miniapp in Polaris:

For usage follow steps in `cmd` or use `launcher.sh` to launch experiments in polaris.
In polaris edit `polaris.sh` and `launcher.sh` with necessary paths and env, then submit jobs by running:
```
bash launcher.sh
```
