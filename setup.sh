app_dir=`pwd`

echo "SETTING UP SIRT ------------------------ "
cd include/tracelib
flatc -c trace_prot.fbs
cd ${app_dir}
mkdir -p build
cd build
cmake ..
make
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


