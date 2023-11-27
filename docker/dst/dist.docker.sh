docker build -f Dockerfile.StreamerDIST -t streamer-dist:latest --target StreamerDIST .
docker run --rm -it --name streamer-dist -p 50010:50010 streamer-dist:latest python /aps-mini-apps/build/python/streamer-dist/ModDistStreamPubDemo.py --data_source_addr tcp://host.docker.internal:50000 --data_source_synch_addr tcp://host.docker.internal:50001 --cast_to_float32 --normalize --my_distributor_addr tcp://*:50010 --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560