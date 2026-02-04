source activate-spack.sh
# source envpy/bin/activate

# if [[ "$PMI_RANK" -eq 0 ]]; then
#     echo rank=$PMI_RANK: Running bedrock...
#     bedrock cxi -v trace -c config.json
# else
#     echo rank=$PMI_RANK: Sleeping...
#     sleep 10000
# fi

# bedrock na+sm -c config.json
bedrock cxi -v trace -c config-polaris.json

# Run the following if needed
# /usr/sbin/sysctl kernel.yama.ptrace_scope=0

