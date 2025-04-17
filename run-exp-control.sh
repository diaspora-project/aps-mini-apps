source activate-spack.sh
# source envpy/bin/activate

# Check if the number of parameters is correct
if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
    echo "Usage: run-exp-control.sh <mtbf> <logdir>"
    echo "  <mtbf>         Mean time between failures (in seconds)"
    echo "  <logdir>      Directory to store logs"
    exit 1
fi

mtbf=$1
logdir=$2

trap "echo 'Ctrl+C pressed. Terminating...'; exit 1" SIGINT SIGTERM

python -u ./build/python/streamer-sirt/FailureInjector.py ${mtbf}
