# Trace

This is APS mini-app that simulates the tomographic reconstruction on streaming tomography data. The reconstruction component provides a sliding window data structure to store (partial) data and a reconstruction process to reconstruct the data in the window. The reconstruction algorithm is based on the simultaneous iterative reconstruction technique (SIRT). This is a CPU-based code and is optimized for parallel and distributed memory. We plan to add the GPU-based version as well.

## Instructions to Run Mini-App on Polaris

### Instructions for installation with Spack:

There are several dependencies, including zmq, swig, python libraries/headers, MPI, flatbuffers, parallel hdf5, cmake, and a C++ compiler.
We have included a spack env file `spack_polaris.yaml` with needed dependencies.

Here are the steps to use spack to install the environment:
1. Clone spack (use this [repo](https://github.com/GueroudjiAmal/spack) for Polaris)
2. Clone mochi spack packages [repo](https://github.com/mochi-hpc/mochi-spack-packages.git)
3. Create a spack env `spack env create APS spack.yaml`
4. Activate the env `spack env activate APS`
5. Add mochi spack packages to the env `spack repo add mochi-spack-packages`
6. Concretize and install `spack concretize && spack install`

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
cp ../../../python/streamer-dist/mofka_dist.py .
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

4. streamer-den: In order to setup the python script, follow the below steps (again from project root directory):
```
mkdir build/python/streamer-denoiser
cd build/python/streamer-denoiser
cp ../../../python/streamer-denoiser/* ./

```
For usage follow steps in `cmd` or use `launcher.sh` to launch experiments in polaris.

### Instructions for installation with Apptainer:
#### Step 1: Build Apptainer Images

To build the Apptainer images, you need to be on the Polaris compute nodes. Follow these steps:

1. **Start an Interactive Session:**
   ```bash
   qsub -I -A <Project> -q debug -l select=1 -l walltime=01:00:00 -l filesystems=home:eagle -l singularity_fakeroot=true

2. **Set Proxy Environment Variables:**
   ```bash
   export HTTP_PROXY=http://proxy.alcf.anl.gov:3128
   export HTTPS_PROXY=http://proxy.alcf.anl.gov:3128
   export http_proxy=http://proxy.alcf.anl.gov:3128
   export https_proxy=http://proxy.alcf.anl.gov:3128
3. **Load Apptainer Module:**
   ```bash
   module use /soft/spack/gcc/0.6.1/install/modulefiles/Core
   module load apptainer
4. **Build Apptainer Images:**
   ```bash
   apptainer build --fakeroot streamer-daq.sif ./python/streamer-daq/streamer-daq.def
   apptainer build --fakeroot streamer-quality.sif ./python/quality/streamer-quality.def
   apptainer build --fakeroot streamer-dist.sif ./python/streamer-dist/streamer-dist.def
   apptainer build --fakeroot streamer-sirt.sif ./src/sirt/streamer-sirt.def

#### Step 2: Modify and Submit the Script
After building the Apptainer images, modify the single-node-PBS-job-script.sh according to your specific usage requirements. Then, submit the script using the following command:
```bash
qsub single-node-PBS-job-script.sh
```

### TL;DR version to run the test case:

Go into the root folder of the project and edit the hard-coded parameters, specifically, the volume paths in docker-compose.yaml file. After then you can run the following command:
```
$ docker-compose -f ./docker-compose.yaml up
```
This command will build and run the four processes (as services) in the pipeline, i.e., data acquisition (daq), distributor (dist), reconstruction (sirt), and quality. daq, dist and sirt processes are automatically started by the docker-compose command. More information about these services are explained below.

### More information about the pipeline and instructions for installation with Docker:

The code is structured according to four main processes/services, which have their independent docker definitions. The docker files are organized under /docker directory.

The four processes are:
1. Data Acquisition (daq): This process is defined under /docker/daq folder. It generates the experimental data and streams it to the distributor process. The current version of the code can generate (i) random data, (ii) read an experimental file (hdf5) and simulate real experimental data acquisition, or (iii) interact with EPICS and read data from a real detector. For the (ii), we also include a sample tomography dataset (two sinograms) under /data folder.

2. Distributor (dist): This process is defined under /docker/distributor folder. It receives the data from the DAQ process and partitions it into slices. It then streams the slices to the reconstruction process(es).

3. Reconstruction (sirt): This process is defined under /docker/sirt folder. It receives the data from the distributor process and reconstructs the data. The current version of the code can reconstruct using different configuration parameters, i.e., sliding window size, number of iterations on the window, frequency of triggerin reconstruction on the window. The reconstructed images are written to hdf5 files.

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
