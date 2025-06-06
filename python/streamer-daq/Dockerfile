FROM continuumio/miniconda3 as base

RUN apt-get update --fix-missing && apt-get install -y \
    build-essential \
    libzmq3-dev \
    swig \
    git \
    libmpich-dev \
    libhdf5-mpich-dev \
    apt-utils \
    net-tools \
    iptables \
    iputils-ping \
    iproute2 \
    nano \
    ffmpeg \
    libsm6 \
    libxext6
RUN conda install -y \
    python=3.11 \
    numpy \
    scipy \
    matplotlib \
    pyzmq \
    pip \
    cmake
RUN pip install --upgrade pip

RUN git clone https://github.com/google/flatbuffers.git
WORKDIR /flatbuffers
RUN cmake -G "Unix Makefiles"
RUN make -j
RUN make install

WORKDIR /
RUN git clone https://github.com/diaspora-project/aps-mini-apps.git
WORKDIR /aps-mini-apps
RUN git fetch origin
RUN git checkout master

RUN mkdir build
RUN mkdir build/python
RUN mkdir build/python/quality

# Setup quality checker
RUN pip install sewar
RUN cp ./python/quality/iqcheck.py ./build/python/quality

# Setup flatbuffers data structures
WORKDIR /aps-mini-apps/include/tracelib
RUN flatc -c trace_prot.fbs
WORKDIR /aps-mini-apps

# Build SIRT
FROM base as streamersirt
WORKDIR /aps-mini-apps/build
RUN cmake ..
RUN make
WORKDIR /aps-mini-apps/build/bin


# Build streamer-daq
FROM streamersirt as daq
RUN conda install -y -c conda-forge \
    tomopy \
    dxchange

WORKDIR /aps-mini-apps/build/python
RUN mkdir streamer-daq
RUN cp -r ../../python/common ./
RUN cp ../../python/streamer-daq/DAQStream.py ./streamer-daq
WORKDIR /aps-mini-apps/build/python/streamer-daq
EXPOSE 50000 50001
