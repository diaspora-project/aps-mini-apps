source ~/activate-spack.sh
source envpy/bin/activate

sirt_ranks=$1

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


# build/bin/sirt_stream \
#     --id 0 \
#     --np ${sirt_ranks} \
#     --protocol na+sm \
#     --group-file mofka.json \
#     --batchsize 4 \
#     --reconOutputPath ./output.h5 \
#     --recon-output-dir . \
#     --reconDatasetPath /data \
#     --pub-freq 10000 \
#     --center 1427 \
#     --thread 4 \
#     --write-freq 4 \
#     --window-length 4 \
#     --window-step 4 \
#     --window-iter 1 \
# #     --ckpt-freq 1

python ./build/python/streamer-sirt/ParslSirt.py \
    --np ${sirt_ranks} \
    --write-freq 4  \
    --window-iter 1 \
    --window-step 4 \
    --window-length 4 \
    --thread 4 \
    --center 1427 \
    --protocol na+sm \
    --group-file mofka.json \
    --batchsize 4
