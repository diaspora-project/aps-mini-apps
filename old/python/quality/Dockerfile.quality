FROM continuumio/miniconda3 as base

RUN conda install -y \
    numpy \
    scipy \
    scikit-image \
    matplotlib \
    pip\
    h5py
RUN pip install --upgrade pip

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
WORKDIR /aps-mini-apps/build/python/quality