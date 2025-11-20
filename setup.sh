# cp activate-spack.sh ~
# source ~/activate-spack.sh
source activate-spack.sh
# python -m venv envpy
# source envpy/bin/activate
# pip install parsl

app_dir=`pwd`

export CRAYPE_LINK_TYPE=dynamic

echo "SETTING UP SIRT ------------------------ "
cd include/tracelib
flatc -c trace_prot.fbs
cd ${app_dir}
mkdir -p build
cd build
#cmake ..
# cmake -S .. -B build \
#   -DCMAKE_C_COMPILER=cc \
#   -DCMAKE_CXX_COMPILER=CC \
#   -DCMAKE_Fortran_COMPILER=ftn \
#   -DCMAKE_BUILD_TYPE=Release \
#   -DCMAKE_PREFIX_PATH=/home/ndhai/diaspora/src/spack/var/spack/environments/APS/.spack-env/view
# cmake --build build -j
# detect C/C++/Fortran compilers
CC_PATH=$(command -v cc || command -v gcc || command -v clang || true)
CXX_PATH=$(command -v c++ || command -v g++ || command -v clang++ || true)
FC_PATH=$(command -v gfortran || command -v ifort || true)

if [ -z "$CC_PATH" ] || [ -z "$CXX_PATH" ]; then
  echo "No C/C++ compiler found in PATH. Load compiler module or install gcc/clang."
  exit 1
fi

cmake -S .. -B build \
  -DCMAKE_C_COMPILER="$CC_PATH" \
  -DCMAKE_CXX_COMPILER="$CXX_PATH" \
  -DCMAKE_Fortran_COMPILER="$FC_PATH" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/home/ndhai/diaspora/src/spack/var/spack/environments/APS/.spack-env/view
cmake --build build -j
mkdir -p python/streamer-sirt
cp ../python/streamer-sirt/* python/streamer-sirt

echo "SETTING UP DAQ ------------------------- "
cd ${app_dir}
mkdir -p build/python/streamer-daq
cp python/streamer-daq/DAQStream.py build/python/streamer-daq

echo "SETTING UP DIST ------------------------ "
cd ${app_dir}
mkdir -p build/python/streamer-dist
cd build/python/streamer-dist
cp ../../../python/streamer-dist/ModDistStreamPubDemo.py .
cp ../../../python/streamer-dist/mofka_dist.py .
cp -r ../../../python/common ../

echo "SETTING UP DENOISER -------------------- "
cd ${app_dir}
mkdir -p build/python/streamer-denoiser
mkdir -p build/denoise
cp python/streamer-denoiser/* build/python/streamer-denoiser

echo "SETTING UP LOGGING ---------------- "
mkdir -p build/logs
mkdir -p build/denoise

