source activate-spack.sh
# source envpy/bin/activate

# Check if the number of parameters is correct
if [ "$#" -ne 3 ]; then
    echo "Illegal number of parameters"
    echo "Usage: run-exp-control.sh <mtbf> <logdir>"
    echo "  <falure mode> single|periodic|random"  
    echo "  <mtbf>        Mean time between failures (in seconds)"
    echo "  <logdir>      Directory to store logs"
    exit 1
fi

failure_mode=$1
mtbf=$2
logdir=$3

trap "echo 'Ctrl+C pressed. Terminating...'; exit 1" SIGINT SIGTERM

echo python -u ./build/python/streamer-sirt/FailureInjector.py ${failure_mode} ${mtbf}
python -u ./build/python/streamer-sirt/FailureInjector.py ${failure_mode} ${mtbf}
