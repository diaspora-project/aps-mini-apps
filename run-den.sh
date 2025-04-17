source activate-spack.sh
# source envpy/bin/activate

# Check if the number of parameters is correct
if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "Usage: run-den.sh <number of processes>"
    exit 1
fi

sirt_ranks=$1
logdir=$2

trap "echo 'Ctrl+C pressed. Terminating...'; exit 1" SIGINT SIGTERM

rm ./build/denoise/*.h5

python -u ./build/python/streamer-denoiser/denoiser.py \
    --output ./build/denoise \
    --model ./build/python/streamer-denoiser/testA40GPU-it07500.h5 \
    --protocol na+sm \
    --group_file mofka.json \
    --batchsize 4 \
    --nproc_sirt ${sirt_ranks} \
    --logdir ${logdir}
