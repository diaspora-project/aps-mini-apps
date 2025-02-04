DIR=$PWD
echo $DIR

RUNS=10

for R in  1 #$(seq 1 $RUNS)
do
    DATE=$(date +"%Y-%m-%d_%T")
    WORKSPACE=/eagle/radix-io/agueroudji/FULL_ASYNC_1/64/D${DATE}/
    mkdir  -p $WORKSPACE
    cd $WORKSPACE
    cp -r  $DIR/* .
    echo Running in $WORKSPACE
    qsub -o $WORKSPACE polaris.sh
done