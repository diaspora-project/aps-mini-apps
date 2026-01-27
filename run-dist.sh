source activate-spack.sh
# source envpy/bin/activate

adios2_path=`find $(spack location -i adios2) -maxdepth 4 -type d -name "site-packages"`
export PYTHONPATH=${adios2_path}:${PYTHONPATH}

# Check if the number of parameters is correct
if [ "$#" -ne 4 ]; then
    echo "Illegal number of parameters"
    echo "Usage: run-dist.sh <number of sinograms> <number of tasks> <load-balance> <logdir>"
    exit 1
fi

num_sinograms=$1
num_tasks=$2
load_balance=$3
logdir=$4

trap "echo 'Ctrl+C pressed. Terminating...'; exit 1" SIGINT SIGTERM

# export ADIOS2_DEBUG=1
# export SST_DEBUG=2
# export SstVerbose=5

# python -u ./build/python/streamer-dist/ModDistStreamPubDemo.py \
# python -u ./build/python/streamer-dist/DynamicModDistStreamPubDemo.py \
# python -u ./build/python/streamer-dist/DynamicModDistStreamPubThreading.py \
python -u ./build/python/streamer-dist/DynamicModDistStreamPubDemo.py \
    --cast_to_float32 \
    --normalize \
    --ntask_sirt ${num_tasks} \
    --beg_sinogram 1000 \
    --num_sinograms ${num_sinograms} \
    --num_columns 2560 \
    --batchsize 4 \
    --group_file mofka.json \
    --logdir ${logdir} \
    --dynamic_loadbalancing $load_balance
