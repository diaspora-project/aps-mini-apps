. ~/spack/share/spack/setup-env.sh
spack env activate APS
source envpy/bin/activate

num_sinograms=$1

python ./build/python/streamer-dist/ModDistStreamPubDemo.py \
    --cast_to_float32 \
    --normalize \
    --beg_sinogram 1000 \
    --num_sinograms ${num_sinograms} \
    --num_columns 2560 \
    --batchsize 4 \
    --protocol na+sm \
    --group_file mofka.json
