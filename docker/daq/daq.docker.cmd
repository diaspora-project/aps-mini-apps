docker build -f Dockerfile.StreamerDAQ -t streamer-daq:latest --target StreamerDAQ .
docker run -it --rm --name streamer-daq -p 50000-50001:50000-50001 -v /Users/tbicer/projects/data:/mnt/data streamer-daq:latest 


python DAQStream.py --mode 1 --simulation_file /mnt/data/tomo_00058_all_subsampled1p.h5 --d_iteration 1 --publisher_addr tcp://*:50000 --iteration_sleep 1 --synch_addr tcp://*:50001 --synch_count 1
