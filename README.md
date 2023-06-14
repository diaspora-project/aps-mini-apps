# Trace

This code lets users to perform reconstruction on streaming tomography data. It provides a sliding window data structure to store (partial) data and a reconstruction algorithm to reconstruct the data in the window. The reconstruction algorithm is based on the [SIRT](https://en.wikipedia.org/wiki/SIRT) algorithm. This code is CPU based and is optimized for distributed memory parallelization (via MPI). 

Instructions for installation:
There are several dependencies, including zmq, swig, python libraries/headers, MPI, flatbuffers, parallel hdf5, cmake, and a C++ compiler. 

There are three main processes:
1. sirt_stream: This process performs the reconstruction. In order to generate this executable, run the following commands in project root directory:
``` mkdir build ```
``` cd build ```
``` cmake .. ```
``` make ```
2. streamer-dist: This process partitions and streams the data to the sirt_stream process. In order to setup the python script, follow the below steps (again from project root directory):
``` mkdir build/python/streamer-dist ```
``` cd build/python/streamer-dist ```
``` cp ../../../python/streamer-dist/ModDistStreamPubDemo.py . ```
``` cp -r ../../../python/common ../ ```
This will let you execute the ModDistStreamPubDemo.py script, which is the main streamer-dist process. You can check a sample usage of this script in the file ``` [Trace]$ cat tests/dist.cmd.log ```.
3. streamer-daq: This process generates the data and streams it to the streamer-dist process. In order to setup the python script, follow the below steps (again from project root directory):
``` mkdir build/python/streamer-daq ```
``` cd build/python/streamer-daq ```
``` cp ../../../python/streamer-daq/DAQStream.py ./ ```
This will let you execute the DAQStream.py script, which is the main streamer-daq process. You can check a sample usage of this script in the file ``` [Trace]$ cat tests/daq.cmd.log ```.

In short DAQStream generates the data and streams it to the streamer-dist process, which partitions the data and streams it to the sirt_stream process, which performs the reconstruction. There can be many sirt_stream processes running in parallel, each of which will perform reconstruction on a different slice of the 3D volume. The reconstructed images are written to hdf5 files.

There is also a python script that shows how to check the image quality of the reconstructed images. In order to setup the python script, follow the below steps (again from project root directory):
``` mkdir build/python/quality ```
``` cd build/python/quality ```
``` cp ../../../python/quality/iqcheck.py ./ ```
The script has hardcoded paths to the reconstructed images, so you will need to change those paths to point to the correct locations. This script also has a dependency to sewar package.