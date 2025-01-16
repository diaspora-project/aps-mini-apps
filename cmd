module use /soft/modulefiles; module load conda ;
conda activate /eagle/radix-io/agueroudji/my_local_conda_env;
 . ~/spack/share/spack/setup-env.sh;
spack env activate condaspack;
unset MPICH_GPU_SUPPORT_ENABLED;
export PYTHONPATH=/eagle/radix-io/agueroudji/my_local_conda_env/lib/python3.11/site-packages/:/home/agueroudji/spack/var/spack/environments/condaspack/.spack-env/view/lib/python3.11/site-packages

bedrock na+sm -c config.json

python ./build/python/streamer-daq/DAQStream.py --mode 1 --simulation_file \
      ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1 \
      --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
      --synch_count 1 --protocol na+sm --group_file mofka.json

python ./build/python/streamer-dist/ModDistStreamPubDemo.py  --cast_to_float32 \
      --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560 \
      --protocol na+sm --group_file mofka.json

mpiexec -n 2 ./build/bin/sirt_stream --write-freq 4  --window-iter 1 --window-step 4 --window-length 4 \
      -t 2 -c 1427 --batchsize 1 --protocol na+sm --group-file mofka.json

python ./build/python/streamer-denoiser/denoiser.py --model ./build/python/streamer-denoiser/testA40GPU-it07500.h5 \
      --protocol na+sm --group_file mofka.json


module use /soft/modulefiles; module load conda ;
conda activate /eagle/radix-io/agueroudji/my_local_conda_env;
 . ~/spack/share/spack/setup-env.sh;
spack env activate condaspack;
unset MPICH_GPU_SUPPORT_ENABLED;
export PYTHONPATH=/eagle/radix-io/agueroudji/my_local_conda_env/lib/python3.11/site-packages/:/home/agueroudji/spack/var/spack/environments/condaspack/.spack-env/view/lib/python3.11/site-packages

 . ~/spack/share/spack/setup-env.sh;
spack env activate APS_GDB;

export MARGO_ENABLE_MONITORING=1
export MARGO_MONITORING_FILENAME_PREFIX=mofka
export MARGO_MONITORING_DISABLE_TIME_SERIES=true

LD_PRELOAD=/home/agueroudji/spack/var/spack/environments/APS_GDB/.spack-env/view/lib/libasan.so.8 bedrock cxi  -c config.json

python ./build/python/streamer-daq/DAQStream.py --mode 1 --simulation_file \
      ./data/tomo_00058_all_subsampled1p_s1079s1081.h5 --d_iteration 1 \
      --publisher_addr tcp://0.0.0.0:50000 --iteration_sleep 1 --synch_addr tcp://0.0.0.0:50001 \
      --synch_count 1 --protocol cxi --group_file mofka.json

python ./build/python/streamer-dist/ModDistStreamPubDemo.py  --cast_to_float32 \
      --normalize --beg_sinogram 1000 --num_sinograms 2 --num_columns 2560 \
      --protocol cxi --group_file mofka.json

mpiexec -n 2 ./build/bin/sirt_stream --write-freq 4  --window-iter 1 --window-step 4 --window-length 4 -t 1 -c 1427 --protocol cxi --group-file mofka.json

python ./build/python/streamer-denoiser/denoiser.py --model ./build/python/streamer-denoiser/testA40GPU-it07500.h5 \
      --protocol cxi --group_file mofka.json
