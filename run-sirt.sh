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

# # PURE C++ EXECUTION. No parallelism, for testing only
build/bin/sirt_stream \
    --id 0 \
    --np ${sirt_ranks} \
    --write-freq 4  \
    --window-iter 1 \
    --window-step 4 \
    --window-length 4 \
    --thread 4 \
    --center 1427 \
    --protocol na+sm \
    --group-file mofka.json \
    --batchsize 4 \
    --reconOutputPath ./output.h5 \
    --recon-output-dir . \
    --reconDatasetPath /data \
    --pub-freq 10000 \
    --ckpt-freq 4 \
    --ckpt-name sirt \
    --ckpt-config veloc.cfg \
    --logdir ${logdir}

# gdb arguments
# run --id 0 --np 1 --write-freq 4 --window-iter 1 --window-step 4 --window-length 4 --thread 1 --center 1427 --protocol na+sm --group-file mofka.json --batchsize 4 --reconOutputPath ./output.h5 --recon-output-dir . --reconDatasetPath /data --pub-freq 10000 --ckpt-freq 4 --ckpt-name sirt --ckpt-config veloc.cfg --logdir .

# python ./build/python/streamer-sirt/ParslSirt.py \
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
#     --ckpt-freq 4 \
#     --ckpt-name sirt \
#     --ckpt-config veloc.cfg \
#     --logdir ${logdir}
