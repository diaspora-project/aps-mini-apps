import parsl
from parsl.app.app import bash_app, python_app
from parsl.configs.local_threads import config
import argparse

config.retries = 100000

parsl.load(config)


def parse_arguments():
    parser = argparse.ArgumentParser(
            description='SIRT Iterative Image Reconstruction')
    
    parser.add_argument('--np', type=int, default=1, help="Number of reconstruction tasks")
    
    parser.add_argument('--protocol', default="na+sm", help='Mofka protocol')

    parser.add_argument('--group-file', type=str, default="mofka.json",
                        help='Group file for the mofka server')

    parser.add_argument('--batchsize', type=str, default="16",
                        help='Mofka batch size')

    parser.add_argument('--reconOutputPath', type=str, default="./output.h5",
                        help='Output file path for reconstructed image (hdf5)')

    parser.add_argument('--recon-output-dir', type=str, default=".",
                        help='Output directory for the streaming outputs')

    parser.add_argument('--reconDatasetPath', type=str, default="/data",
                        help='Reconstruction dataset path in hdf5 file')

    parser.add_argument('--pub-freq', type=str, default="10000",
                        help='Publish frequency')
    
    parser.add_argument('--center', type=str, default="0.",
                        help='Center value')
    
    parser.add_argument('--thread', type=str, default="1",
                        help='Number of threads per process')
    
    parser.add_argument('--write-freq', type=str, default="10000",
                        help='Write frequency')

    parser.add_argument('--window-length', type=str, default="32",
                        help='Number of projections that will be stored in the window')

    parser.add_argument('--window-step', type=str, default="1",
                        help='Number of projections that will be received in each request')

    parser.add_argument('--window-iter', type=str, default="1",
                        help='Number of iterations on received window')

    parser.add_argument('--logdir', type=str, default=".",
                        help='Log directory for sirt processes')
    
    parser.add_argument('--ckpt-freq', type=str, default="1",
                        help='Checkpoint frequency')
    parser.add_argument('--ckpt-name', type=str, default="sirt",
                        help='Checkpoint name')
    parser.add_argument('--ckpt-config', type=str, default="veloc.cfg",
                        help='Checkpoint configuration (VeLoC)')
    
    
    return parser.parse_args()


# Define a bash app that executes a C++ program
@bash_app
def run_sirt(id, logdir=".", args=[]):
    stderr = logdir + '/sirt-' + id + '.err'
    stdout = logdir + '/sirt-' + id + '.out'
    sirt_stream = "build/bin/sirt_stream"
    cmd = sirt_stream + " --id " + id + " " + " ".join(args) + f" >> {stdout} 2>> {stderr}"
    print(cmd)
    return cmd

args = [
    "--write-freq", "4",
    "--window-iter", "1",
    "--window-step", "4",
    "--window-length", "4",
    "-t", "4",
    "-c", "1427",
    "--protocol", "na+sm",
    "--group-file", "mofka.json",
    "--batchsize", "4"
]

def main():
    params = parse_arguments()
    num_tasks = int(params.np)
    args = []
    excluded_args = {}
    for arg, value in vars(params).items():
        if arg not in excluded_args:
            args.append("--" + arg.replace("_", "-"))
            args.append(str(value))
    print("Number of tasks", num_tasks)
    print("Input arguments", args)
    
    # Execute the C++ program using the bash app
    sirt_futures = [run_sirt(id=str(i), logdir=params.logdir, args=args) for i in range(num_tasks)]

    # Wait for the results and print the output
    for sirt_future in sirt_futures:
        print(sirt_future.result())

if __name__ == '__main__':
  main()


