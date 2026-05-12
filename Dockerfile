# syntax=docker/dockerfile:1
# Multi-stage build for all APS mini-app pipeline components.
# Usage (via docker-compose): docker compose build
# Usage (standalone): docker build --target <daq|dist|sirt|den> .

# ─── Base ────────────────────────────────────────────────────────────────────
# Installs system packages, Python 3.12, diaspora-stream-api (from the mochi
# conda channel), and flatbuffers.  All component stages inherit from here.
FROM continuumio/miniconda3 AS base

RUN apt-get update --fix-missing && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    libzmq3-dev \
    swig \
    apt-utils \
    net-tools \
    iproute2 \
    nano \
 && rm -rf /var/lib/apt/lists/*

# flatbuffers is installed via pip, not conda.  The defaults-channel conda
# package only installs the C++ library; the Python bindings (needed by
# TraceSerializer.py) are only available on PyPI.
RUN conda install -y python=3.12 numpy scipy matplotlib pip \
 && pip install flatbuffers \
 && conda clean -afy

# Download the architecture-appropriate mochi conda channel and install all
# mochi packages together with parallel HDF5 in a single solver pass.
# Installing them together is required: mofka and mochi-bedrock need mpich 5.x
# (via conda-forge), and the HDF5 parallel build must match that same mpich
# version.  Separating the installs causes conda to pick an incompatible
# (non-parallel) HDF5.
ARG TARGETARCH
RUN set -e; \
    case "$TARGETARCH" in \
      amd64) _ARCH=linux-64 ;; \
      arm64) _ARCH=aarch64 ;; \
      *) echo "Unsupported TARGETARCH: $TARGETARCH" && exit 1 ;; \
    esac; \
    _URL="https://github.com/mochi-hpc/mochi-conda-packages/releases/download/2026-04-29/mochi-conda-channel-2026-04-29-${_ARCH}-py3.12.tar.gz"; \
    wget -q "$_URL" -O /tmp/mochi-channel.tar.gz; \
    mkdir -p /mochi-channel; \
    tar -xzf /tmp/mochi-channel.tar.gz -C /mochi-channel; \
    conda install -y -c file:///mochi-channel -c conda-forge \
        diaspora-stream-api mofka mochi-bedrock "hdf5=1.*=mpi_mpich*"; \
    conda clean -afy; \
    rm /tmp/mochi-channel.tar.gz

# Build flatbuffers from source (provides flatc compiler + cmake find_package support).
RUN git clone --depth 1 https://github.com/google/flatbuffers.git /tmp/flatbuffers \
 && cd /tmp/flatbuffers \
 && cmake -G "Unix Makefiles" \
 && make -j$(nproc) \
 && make install \
 && rm -rf /tmp/flatbuffers

# Copy the full project source into the image.
COPY . /aps-mini-apps
WORKDIR /aps-mini-apps

# ─── Builder ─────────────────────────────────────────────────────────────────
# Compiles the SIRT C++ binary.  CMAKE_PREFIX_PATH points cmake at the conda
# prefix so it finds diaspora-stream-api and fmt installed by the mochi channel.
# SPDLOG_FMT_EXTERNAL tells spdlog to use the external fmt library instead of
# its bundled headers, which are not shipped in the conda-channel spdlog package.
FROM base AS builder

RUN mkdir -p /aps-mini-apps/build
WORKDIR /aps-mini-apps/build
RUN cmake -DCMAKE_PREFIX_PATH=/opt/conda \
          -DCMAKE_CXX_FLAGS="-DSPDLOG_FMT_EXTERNAL" \
          .. \
 && make -j$(nproc)

# ─── DAQ ─────────────────────────────────────────────────────────────────────
# cmake already copied DAQStream.py and common/ into build/python/ at configure time.
FROM builder AS daq

RUN conda install -y -c conda-forge tomopy dxchange \
 && conda clean -afy

WORKDIR /aps-mini-apps/build/python/streamer-daq
EXPOSE 50000 50001

# ─── DIST ────────────────────────────────────────────────────────────────────
# cmake already copied the DIST scripts and common/ into build/python/ at configure time.
FROM builder AS dist

RUN conda install -y -c conda-forge tomopy dxchange \
 && conda clean -afy

WORKDIR /aps-mini-apps/build/python/streamer-dist

# ─── SIRT ────────────────────────────────────────────────────────────────────
# The binary is already compiled in the builder stage.  All MPI ranks run as
# local processes inside this single container (mpiexec -n <N> sirt_stream ...).
FROM builder AS sirt

WORKDIR /aps-mini-apps/build/bin
EXPOSE 52000

# ─── DEN ─────────────────────────────────────────────────────────────────────
# DEN is pure Python.  Pull only the cmake-installed build/python/ tree from the
# builder stage so this image stays free of the compiled C++ binary.
# denoiser.py appends '../common' relative to __file__, so common/ must be at
# build/python/common/ (one level above streamer-denoiser/).
FROM base AS den

RUN conda install -y -c conda-forge h5py \
 && conda clean -afy

COPY --from=builder /aps-mini-apps/build/python /aps-mini-apps/build/python

WORKDIR /aps-mini-apps/build/python/streamer-denoiser
