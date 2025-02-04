# Trace

This is APS mini-app that simulates the tomographic reconstruction on streaming tomography data. The reconstruction component provides a sliding window data structure to store (partial) data and a reconstruction process to reconstruct the data in the window. The reconstruction algorithm is based on the simultaneous iterative reconstruction technique (SIRT). This is a CPU-based code and is optimized for parallel and distributed memory. We plan to add the GPU-based version as well.

## Instructions to Run Mini-App on Polaris

### Instructions for installation with Spack:

There are several dependencies, including zmq, swig, python libraries/headers, MPI, flatbuffers, parallel hdf5, cmake, and a C++ compiler.
We have included a spack env file `spack_polaris.yaml` with needed dependencies.

Here are the steps to use spack to install the environment:
1. Clone spack (use this [repo](https://github.com/GueroudjiAmal/spack/tree/aps) for Polaris)
2. Clone mochi spack packages [repo](https://github.com/mochi-hpc/mochi-spack-packages.git)
3. Create a spack env `spack env create APS spack.yaml`
4. Activate the env `spack env activate APS`
5. Add mochi spack packages to the env `spack repo add mochi-spack-packages`
6. Concretize and install `spack concretize && spack install`

There are 4 main componenets:
1. streamer-daq: In order to setup the python script, follow the below steps (again from project root directory):
```
mkdir build/python/streamer-daq
cd build/python/streamer-daq
cp ../../../python/streamer-daq/DAQStream.py ./
```
This will let you execute the DAQStream.py script, which is the main streamer-daq process. You can check a sample usage of this script in the file ``` [Trace]$ cat tests/daq.cmd.log ```.

2. streamer-dist: In order to setup the python script, follow the below steps (again from project root directory):
```
mkdir build/python/streamer-dist
cd build/python/streamer-dist
cp ../../../python/streamer-dist/ModDistStreamPubDemo.py .
cp ../../../python/streamer-dist/mofka_dist.py .
cp -r ../../../python/common ../
```
This will let you execute the ModDistStreamPubDemo.py script, which is the main streamer-dist process. You can check a sample usage of this script in the file ``` [Trace]$ cat tests/dist.cmd.log ```.

4. streamer-den: In order to setup the python script, follow the below steps (again from project root directory):
```
mkdir build/python/streamer-denoiser
cd build/python/streamer-denoiser
cp ../../../python/streamer-denoiser/* ./
```

3. sirt_stream: In order to generate this executable,:
Setup flatbuffers data structures
```
cd /path/to/include/tracelib
flatc -c trace_prot.fbs
```
Run the following commands in project root directory
```
mkdir build
cd build
cmake ..
make
```

### Instructions to Run the miniapp in Polaris:
For usage follow steps in `cmd` or use `launcher.sh` to launch experiments in polaris.
In polaris edit `polaris.sh` and `launcher.sh` with necessary paths and env, then submit jobs by running:
```
bash launcher.sh
```
