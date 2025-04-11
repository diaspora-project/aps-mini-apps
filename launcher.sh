DIR=$PWD
NPROC_PER_NODE=2

for NRANKS in   8 #16 32 64
do
    for BATCH in 32 8 2 #1 2 4 8 16 32 64
    do
        NNODES=$(($NRANKS/$NPROC_PER_NODE + 4))
        DATE=$(date +"%Y-%m-%d_%T")
        WORKSPACE=/eagle/Diaspora/amal/TEKAPP_MOFKA_P_SERVER_MEMORY/RANKS_${NRANKS}/BATCH_${BATCH}/DATE_${DATE}/
        mkdir  -p $WORKSPACE
        cd $WORKSPACE
        cp -r  $DIR/* .
        echo Running in $WORKSPACE
        qsub  -A Diaspora -l select=$NNODES:system=polaris -o $WORKSPACE polaris.sh
    done
done