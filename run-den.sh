. ~/spack/share/spack/setup-env.sh
spack env activate APS
source envpy/bin/activate

sirt_ranks=$1

rm ./build/denoise/*.h5

python ./build/python/streamer-denoiser/denoiser.py \
    --output ./build/denoise \
    --model ./build/python/streamer-denoiser/testA40GPU-it07500.h5 \
    --protocol na+sm \
    --group_file mofka.json \
    --batchsize 4 \
    --nproc_sirt ${sirt_ranks}
