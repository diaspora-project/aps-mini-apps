bedrock na+sm -c ./build/config.json

python ./build/python/streamer-daq/DAQStream.py --mode 1 --simulation_file ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1 --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 --synch_count 1

python ./build/python/streamer-dist/ModDistStreamPubDemo.py --data_source_addr tcp://0.0.0.0:50000 --data_source_synch_addr tcp://0.0.0.0:50001 --cast_to_float32 --normalize --my_distributor_addr tcp://0.0.0.0:50010 --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560

./bin/sirt_stream --write-freq 4 --dest-host 0.0.0.0 --dest-port 50010 --window-iter 1 --window-step 4 --window-length 4 -t 2 -c 1427 --pub-addr tcp://0.0.0.0:52000