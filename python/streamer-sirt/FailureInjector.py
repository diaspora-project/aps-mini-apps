import sys
import random
import time
import os
import signal
import subprocess

# Inject failure into sirt-stream according to the standard Poisson process with
# a given mean time between failures (MTBF) provided by the user from the command line.

def get_running_sirt_streamers():
    """
    Get a list of running sirt-streamer process IDs managed by Parsl.
    Exclude processes with "/bin/bash" in their command.
    :return: List of process IDs (PIDs).
    """
    try:
        result = subprocess.run(
            ["pgrep", "-f", "build/bin/sirt_stream"], capture_output=True, text=True, check=True
        )
        pids = [int(pid) for pid in result.stdout.split()]
        filtered_pids = []
        for pid in pids:
            cmdline_path = f"/proc/{pid}/cmdline"
            with open(cmdline_path, "r") as cmdline_file:
                cmdline = cmdline_file.read()
                if "/bin/bash" not in cmdline:
                    filtered_pids.append(pid)
        return filtered_pids
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []

def inject_failure(mtbf):
    """
    Inject failure into sirt-stream based on a Poisson process.
    :param mtbf: Mean time between failures (MTBF) in seconds.
    """
    lambda_rate = 1 / mtbf  # Convert MTBF to failure rate (lambda)
    while True:
        # Generate the next failure time using an exponential distribution
        # next_failure_time = random.expovariate(lambda_rate)
        next_failure_time = mtbf
        time.sleep(next_failure_time)

        # Get the list of running sirt-streamer processes
        running_processes = get_running_sirt_streamers()
        if running_processes:
            # Randomly select a process to kill
            pid_to_kill = random.choice(running_processes)
            os.kill(pid_to_kill, signal.SIGTERM)  # Send SIGTERM to the selected process
            print(f"[WARNING] ===========> Failure injected: Killed sirt-streamer process with PID {pid_to_kill}")
        else:
            print("No running sirt-streamer processes found to kill.")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python FailureInjector.py <mtbf>")
        sys.exit(1)

    try:
        mtbf = float(sys.argv[1])
        if mtbf <= 0:
            raise ValueError("MTBF must be positive.")
    except ValueError as e:
        print(f"Invalid MTBF: {e}")
        sys.exit(1)
    print(f"Starting failure injection with MTBF: {mtbf} seconds")
    inject_failure(mtbf)


