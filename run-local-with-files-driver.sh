#!/bin/bash

eval `spack env activate --sh aps-env`

SIRT_RANKS=2

DIASPORA_CTL_DRIVER_ARGS="--driver files --driver.root_path ./aps-miniapp-data"

rm -rf aps-miniapp-data

echo "Starting topic creations"
# setup topics and partitions
# DAQ -> DIST topic
diaspora-ctl topic create --name daq_dist $DIASPORA_CTL_DRIVER_ARGS --topic.num_partitions 1
# DIST topics
diaspora-ctl topic create --name dist_sirt $DIASPORA_CTL_DRIVER_ARGS --topic.num_partitions $SIRT_RANKS
diaspora-ctl topic create --name handshake_s_d $DIASPORA_CTL_DRIVER_ARGS --topic.num_partitions 1
diaspora-ctl topic create --name handshake_d_s $DIASPORA_CTL_DRIVER_ARGS --topic.num_partitions $SIRT_RANKS
# SIRT -> DEN topic
diaspora-ctl topic create --name sirt_den $DIASPORA_CTL_DRIVER_ARGS --topic.num_partitions 1

echo '{"root_path":"./aps-miniapp-data"}' > diaspora-driver.json
DRIVER_ARGS="--driver_type files --driver_config_file diaspora-driver.json"

echo "Completed topic creations"

echo "Launching DAQ"
# Launch DAQ
python ./build/python/streamer-daq/DAQStream.py --mode 1 --simulation_file \
./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1  --batchsize 4 \
--publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
--synch_count 1 $DRIVER_ARGS 1>daq.out 2>daq.err &
DAQ_PID=$!
echo "DAQ launched with PID $DAQ_PID"

echo "Launching DIST"
# Launch Dist
python ./build/python/streamer-dist/ModDistStreamPubDemo.py  --cast_to_float32 \
--normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560  --batchsize 4 \
$DRIVER_ARGS 1>dist.out 2>dist.err &
DIST_PID=$!
echo "DIST launched with PID $DIST_PID"

echo "Launching SIRT"
# Launch SIRT
mpiexec -n $SIRT_RANKS ./build/bin/sirt_stream --write-freq 4  \
--window-iter 1 --window-step 4 --window-length 4 -t 4 -c 1427 \
$DRIVER_ARGS --batchsize 4 1>sirt.out 2>sirt.err &
SIRT_PID=$!
echo "SIRT launched with PID $SIRT_PID"

echo "Launching DEN"
# Launch DEN
python ./build/python/streamer-denoiser/denoiser.py \
--model ./build/python/streamer-denoiser/testA40GPU-it07500.h5 \
$DRIVER_ARGS --batchsize 4 --nproc_sirt 2 1>den.out 2>den.err &
DEN_PID=$!
echo "DEN launched with PID $DEN_PID"

echo "Waiting for all the commands to finish"
wait $DAQ_PID
echo "DAQ exited with code $?"
wait $DIST_PID
echo "DIST exited with code $?"
wait $SIRT_PID
echo "SIRT exited with code $?"
wait $DEN_PID
echo "DEN exited with code $?"
