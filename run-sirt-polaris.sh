source activate-spack.sh
# source envpy/bin/activate

# Check if the number of parameters is correct
if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "Usage: run-sirt.sh <number of processes> <logdir>"
    exit 1
fi

sirt_ranks=$1
logdir=$2
logdir=`pwd`/$logdir

trap "echo 'Ctrl+C pressed. Terminating...'; exit 1" SIGINT SIGTERM

# mpiexec -n $sirt_ranks ./build/bin/sirt_stream \
#     --write-freq 4  \
#     --window-iter 1 \
#     --window-step 4 \
#     --window-length 4 \
#     -t 4 \
#     -c 1427 \
#     --protocol na+sm \
#     --group-file mofka.json \
#     --batchsize 4

# # # PURE C++ EXECUTION. No parallelism, for testing only
# build/bin/sirt_stream \
#     --id 0 \
#     --np ${sirt_ranks} \
#     --write-freq 4  \
#     --window-iter 1 \
#     --window-step 4 \
#     --window-length 4 \
#     --thread 4 \
#     --center 1427 \
#     --protocol na+sm \
#     --group-file mofka.json \
#     --batchsize 4 \
#     --reconOutputPath ./output.h5 \
#     --recon-output-dir . \
#     --reconDatasetPath /data \
#     --pub-freq 10000 \
#     --ckpt-freq 4 \
#     --ckpt-name sirt \
#     --ckpt-config veloc.cfg \
#     --logdir ${logdir}

# gdb arguments
# run --id 0 --np 1 --write-freq 4 --window-iter 1 --window-step 4 --window-length 4 --thread 1 --center 1427 --protocol na+sm --group-file mofka.json --batchsize 4 --reconOutputPath ./output.h5 --recon-output-dir . --reconDatasetPath /data --pub-freq 10000 --ckpt-freq 4 --ckpt-name sirt --ckpt-config veloc.cfg --logdir .

python -u ./build/python/streamer-sirt/ParslSirtPolaris.py \
    --num-workers ${sirt_ranks} \
    --write-freq 4  \
    --window-iter 1 \
    --window-step 4 \
    --window-length 4 \
    --thread 4 \
    --center 1427 \
    --group-file mofka.json \
    --batchsize 4 \
    --ckpt-freq 4 \
    --ckpt-name sirt \
    --ckpt-config veloc.cfg \
    --recon-output-dir ./build/denoise \
    --logdir ${logdir}

# /bin/bash -c export LD_LIBRARY_PATH="/home/ndhai/diaspora/src/aps-mini-apps/build/build/bin:/home/ndhai/diaspora/src/aps-mini-apps/build/build/src:/home/ndhai/diaspora/src/aps-mini-apps/build/build/src/sirt:/opt/cray/libfabric/1.15.2.0/lib64:/opt/cray/libfabric/1.15.2.0/lib:/home/ndhai/diaspora/src/spack/var/spack/environments/APS/.spack-env/view/lib:/home/ndhai/diaspora/src/spack/var/spack/environments/APS/.spack-env/view/lib64:/opt/cray/pe/netcdf/4.9.0.9/gnu/12.3/lib:/opt/cray/pe/hdf5-parallel/1.12.2.9/gnu/12.3/lib:/opt/cray/pe/mpich/8.1.28/ofi/gnu/12.3/lib:/home/ndhai/usr/lib:/home/ndhai/usr/lib64:/opt/cray/pe/papi/7.2.0.1/lib64:/opt/cray/libfabric/1.22.0/lib64:/soft/perftools/darshan/darshan-3.4.4/lib:/opt/cray/pals/1.6/lib:/opt/nvidia/hpc_sdk/Linux_x86_64/24.11/compilers/lib" && echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH" && ldd /home/ndhai/diaspora/src/aps-mini-apps/build/build/bin/sirt_stream || true && /home/ndhai/diaspora/src/aps-mini-apps/build/build/bin/sirt_stream \
#     --worker-id 0 \
#     --protocol na+sm \
#     --group-file mofka.json \
#     --batchsize 4 \
#     --reconOutputPath ./output.h5 \
#     --recon-output-dir ./build/denoise \
#     --reconDatasetPath /data \
#     --pub-freq 10000 \
#     --center 1427 \
#     --thread 4 \
#     --write-freq 4 \
#     --window-length 4 \
#     --window-step 4 \
#     --window-iter 1 \
#     --logdir ${logdir} \
#     --ckpt-freq 4 \
#     --ckpt-name sirt \
#     --ckpt-config veloc.cfg

