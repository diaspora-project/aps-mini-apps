DIR=$PWD
NPROC_PER_NODE=2
MAX=4
for NRANKS in  $(seq 4 $MAX)
do
    for BATCH in $(seq 1 1) #2 4 8 16 32 64 128 256 512 1024)
    do
        NNODES=$(($NRANKS/$NPROC_PER_NODE + 1))
        DATE=$(date +"%Y-%m-%d_%T")
        WORKSPACE=/eagle/Diaspora/amal/TEKAPP_ASYNC_FULL_R${NRANKS}/B${BATCH}/D${DATE}/
        mkdir  -p $WORKSPACE
        cd $WORKSPACE
        cp -r  $DIR/* .
        echo Running in $WORKSPACE
        qsub  -A Diaspora -l select=$NNODES:system=polaris -o $WORKSPACE polaris.sh
    done
done