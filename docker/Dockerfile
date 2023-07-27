FROM continuumio/miniconda3

RUN apt-get update && apt-get install -y \
    build-essential \
    libzmq3-dev \
    swig \
    git \
    libmpich-dev \
    libhdf5-mpich-dev

RUN conda install -y \
    numpy \
    scipy \
    matplotlib \
    pyzmq \
    pip \
    cmake

RUN pip install --upgrade pip
RUN pip install h5py

RUN conda install -y -c conda-forge \
    tomopy \
    dxchange

RUN conda uninstall hdf5

RUN git clone https://github.com/google/flatbuffers.git
WORKDIR /flatbuffers
RUN cmake -G "Unix Makefiles"
RUN make -j
RUN make install

WORKDIR /
RUN git clone https://github.com/tekinbicer/Trace.git
WORKDIR /Trace
RUN git fetch origin
RUN git checkout stream_test

RUN mkdir build
RUN mkdir build/python

# Setup flatbuffers data structures
WORKDIR /Trace/include/tracelib
RUN flatc -c trace_prot.fbs

# Build SIRT
WORKDIR /Trace/build
RUN cmake ..
RUN make

# Setup streamer-dist
WORKDIR /Trace/build/python
#RUN mkdir streamer-dist
RUN cp ../../python/streamer-dist/ModDistStreamPubDemo.py ./streamer-dist
RUN cp -r ../../python/common ./ 
# Setup streamer-daq
RUN mkdir streamer-daq
RUN cp ../../python/streamer-daq/DAQStream.py ./streamer-daq
# Setup quality checker
RUN mkdir quality
RUN cp ../../python/quality/iqcheck.py ./quality

# Reset the working directory
WORKDIR /Trace