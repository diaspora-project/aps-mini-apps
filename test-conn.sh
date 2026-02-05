#!/bin/sh
#PBS -l select=2
#PBS -l place=scatter
#PBS -l walltime=0:10:00
#PBS -l filesystems=home
#PBS -q debug-scaling
#PBS -A radix-io

set -eu

source activate-spack.sh

# Check CXI service on each node
#mpiexec -n 2 --ppn 1 cxi_service list -s 1 -v

# This option ensures that the resource manager will allocate Slingshot VNI
# resources even if it detects that all of the processes launched by a given
# mpiexec command are on the same node.  This is important for use cases
# where Mochi servers or clients are started individually.
VNI_OPTS="--single-node-vni"

nodes=$(cat "$PBS_NODEFILE")
nodes_array=($nodes)

export HG_LOG_LEVEL=warning
export FI_LOG_LEVEL=Warn

mpiexec -n 1 --ppn 1 ${VNI_OPTS} --hosts "${nodes_array[0]}" bedrock "ofi+cxi" -c config-polaris.json > bedrock.txt 2>&1 &
BEDROCK_PID=$!

sleep 10

server_addr=$(cat bedrock.txt | awk '{print $9}')

mpiexec -n 1 --ppn 1 ${VNI_OPTS} --hosts "${nodes_array[1]}" bedrock-query ofi+cxi -a $server_addr -p

kill $BEDROCK_PID