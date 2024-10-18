#!/bin/bash
#PBS -l select=1:system=polaris:mem=450g:ncpus=32
#PBS -l walltime=00:30:00                       
#PBS -N tekin_mini_app_single_CPU
#PBS -q debug

#PBS -k doe
#PBS -e ./
#PBS -o ./

#file systems used by the job
#PBS -l filesystems=home:eagle

#Project name
#PBS -A RAPINS
#PBS -M pp32@illinois.edu

# send email at job begin and job end
#PBS -m be
#PBS -V

# load apptainer module
module use /soft/spack/gcc/0.6.1/install/modulefiles/Core
module load apptainer

cd /lus/eagle/projects/RAPINS/parth/tekin-aps-mini-apps


# Start streamer-daq service
apptainer exec streamer-daq.sif \
    python /aps-mini-apps/build/python/streamer-daq/DAQStream.py --mode 1 --simulation_file /aps-mini-apps/data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1 --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 --synch_count 1 &

echo "streamer-daq started"

# Wait for streamer-daq to be ready (use a sleep to ensure it starts properly)
sleep 10

# Start streamer-dist service
apptainer exec streamer-dist.sif \
    python /aps-mini-apps/build/python/streamer-dist/ModDistStreamPubDemo.py --data_source_addr tcp://127.0.0.1:50000 --data_source_synch_addr tcp://127.0.0.1:50001 --cast_to_float32 --normalize --my_distributor_addr tcp://0.0.0.0:50010 --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560 &

# Wait for streamer-dist to be ready (use a sleep to ensure it starts properly)
sleep 10

echo "streamer-dist started"

# Start streamer-quality service
apptainer exec streamer-quality.sif &

# Start streamer-sirt service
apptainer exec streamer-sirt.sif \
    /aps-mini-apps/build/bin/sirt_stream --write-freq 4 --dest-host 127.0.0.1 --dest-port 50010 --window-iter 1 --window-step 4 --window-length 4 -t 2 -c 1427 --pub-addr tcp://0.0.0.0:52000 &

# Wait for all background processes to finish
wait

