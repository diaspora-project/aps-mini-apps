source activate-spack.sh
source envpy/bin/activate

# Check if the number of parameters is correct
if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "Usage: run-dist.sh <number of sinograms>"
    exit 1
fi

num_sinograms=$1
logdir=$2

trap "echo 'Ctrl+C pressed. Terminating...'; exit 1" SIGINT SIGTERM

python ./build/python/streamer-dist/ModDistStreamPubDemo.py \
    --cast_to_float32 \
    --normalize \
    --beg_sinogram 1000 \
    --num_sinograms ${num_sinograms} \
    --num_columns 2560 \
    --batchsize 4 \
    --protocol na+sm \
    --group_file mofka.json \
    --logdir ${logdir}
