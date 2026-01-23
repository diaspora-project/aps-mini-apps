source activate-spack.sh
# source envpy/bin/activate

if [[ "$PMI_RANK" -eq 0 ]]; then
    bedrock cxi -v trace -c config.json
else
    sleep 10000
fi
# bedrock na+sm -c config.json
# bedrock cxi -v trace -c config.json

# Run the following if needed
# /usr/sbin/sysctl kernel.yama.ptrace_scope=0

