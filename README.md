# Trace

This code lets users perform reconstruction on streaming tomography data. It provides a sliding window data structure to store (partial) data and a reconstruction process to reconstruct the data in the window. The reconstruction algorithm is based on the SIRT algorithm. This code is CPU based and is optimized for parallel and distributed memory. 

### TL;DR version to run the test case: 

Go into each process folder, fix the hardcoded (input/output) paths, parameters (port addresses) in .sh files, and run their docker build files and process scripts. Specifically, follow the below steps:
1. Data acquisition process that simulates tomography data acquisition:
```
$ cd docker/daq
$ . daq.docker.sh #fix the hardcoded paths before running!
```
2. Distributor process that partitions the data and streams it to the reconstruction processes:
```
$ cd docker/dst
$ . dist.docker.sh
``` 
3. Reconstruction process that reconstructs the data:
```
$ cd docker/sirt
$ . sirt.docker.sh #fix the hardcoded paths before running!
```

At this point the stream reconstruction should be working and the sirt reconstruction process should be generating reconstructed images and writing them to the ~/projects/data/output folder on the host machine.

To check the quality of the images generated by the sirt reconstruction process, run the following:
```
$ cd docker/quality
# change the ${INPUT0} ${INPUT1} in quality.docker.sh to point to the reconstructed images by the sirt process and then run. The prefix for the ${INPUT0} and ${INPUT1} should be the same and is currently /mnt/output (which is the mounted folder from the host machine to the docker container).
$ . quality.docker.sh
```

If you see an error during docker build, e.g., due to timeout,  at any step, just try to rerun the build command. If you see an error during the process execution, check if there is an dangling instance of the process and kill it. You can check them in the docker dashboard.

There are some hardcoded paths in the .sh files, which you may need to change based on your environment. For example, again, the data is being written to ~/projects/data/output folder by the reconstruction task, which is then mounted by the quality check process to its docker instance. You can change this path in the .sh files to fit your environment. Application ports are also hardcoded, which you can change.


### More information about the pipeline and instructions for installation with Docker:

The code is structured four main processes, which have their independent docker definitions. The docker files are organized under /docker directory.

The four processes are:
1. Data Acquisition (DAQ): This process is defined under /docker/daq folder. It generates the experimental data and streams it to the distributor process. The current version of the code can generate (i) random data, (ii) read an experimental file (hdf5) and simulate real experimental data acquisition, or (iii) interact with EPICS and read data from a real detector. For the (ii), we also include a sample tomography dataset (two sinograms) under /data folder. 

2. Distributor: This process is defined under /docker/distributor folder. It receives the data from the DAQ process and partitions it into slices. It then streams the slices to the reconstruction process(es).

3. Reconstruction: This process is defined under /docker/sirt folder. It receives the data from the distributor process and reconstructs the data. The current version of the code can reconstruct using different configuration parameters, i.e., sliding window size, number of iterations on the window, frequency of triggerin reconstruction on the window. The reconstructed images are written to hdf5 files.

4. Quality: This process is defined under /docker/quality folder. The use can point to the reconstructed images from the reconstruction process and then it can check the quality of the images. The current version of the code can produce different metrics, i.e., SSIM, MSE, PSNR, etc.

User of the code can find each process docker file and build and run commands under /docker folder as mentioned in TL;DR section.

### Instructions for installation without Docker:

There are several dependencies, including zmq, swig, python libraries/headers, MPI, flatbuffers, parallel hdf5, cmake, and a C++ compiler. 

There are three main processes:
1. sirt_stream: In order to generate this executable, run the following commands in project root directory:
``` 
mkdir build
cd build
cmake ..
make 
```
2. streamer-dist: In order to setup the python script, follow the below steps (again from project root directory):
``` 
mkdir build/python/streamer-dist
cd build/python/streamer-dist
cp ../../../python/streamer-dist/ModDistStreamPubDemo.py .
cp -r ../../../python/common ../ 
```
This will let you execute the ModDistStreamPubDemo.py script, which is the main streamer-dist process. You can check a sample usage of this script in the file ``` [Trace]$ cat tests/dist.cmd.log ```.

3. streamer-daq: In order to setup the python script, follow the below steps (again from project root directory):
``` 
mkdir build/python/streamer-daq
cd build/python/streamer-daq
cp ../../../python/streamer-daq/DAQStream.py ./ 
```
This will let you execute the DAQStream.py script, which is the main streamer-daq process. You can check a sample usage of this script in the file ``` [Trace]$ cat tests/daq.cmd.log ```.

Note that the order of the installation is important since the previously generated libraries are being used by following processes.

In short DAQStream generates the data and streams it to the streamer-dist process, which partitions the data and streams it to the sirt_stream process, which performs the reconstruction. There can be many sirt_stream processes running in parallel, each of which will perform reconstruction on a different slice of the 3D volume. The reconstructed images are periodically written to hdf5 files.

There is also a python script that shows how to check the image quality of the reconstructed images. In order to setup the python script, follow the below steps (again from project root directory):
``` 
mkdir build/python/quality
cd build/python/quality
cp ../../../python/quality/iqcheck.py ./ 
```
The script accepts hdf5 files that are generated by the reconstruction task. This script also has a dependency to sewar python package.
