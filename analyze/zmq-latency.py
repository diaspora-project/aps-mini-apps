import csv
import numpy as np
import argparse

def compute_latency(send_file, receive_file):
    # Read send timestamps (in nanoseconds)
    with open(send_file, 'r') as f:
        reader = csv.reader(f)
        next(reader)  # Skip the header row
        send_timestamps = np.array([int(row[0]) for row in reader])

    # Read receive timestamps (in seconds)
    with open(receive_file, 'r') as f:
        reader = csv.reader(f)
        next(reader)  # Skip the header row
        receive_timestamps = np.array([float(row[0]) for row in reader])

    # Convert send timestamps to seconds
    send_timestamps_sec = send_timestamps / 1e9

    # Compute latencies (receive - send)
    latencies = receive_timestamps - send_timestamps_sec

    # Compute statistics
    latency_stats = {
        "mean": np.mean(latencies),
        "median": np.median(latencies),
        "min": np.min(latencies),
        "max": np.max(latencies)
    }

    return latencies, latency_stats

def main():
    parser = argparse.ArgumentParser(description="Compute ZMQ latency statistics.")
    parser.add_argument('--send_file', type=str, required=True, help="Path to the send timestamps CSV file.")
    parser.add_argument('--receive_file', type=str, required=True, help="Path to the receive timestamps CSV file.")
    parser.add_argument('--output_file', type=str, required=False, help="Path to save the latency results (optional).")

    args = parser.parse_args()

    # Compute latencies and statistics
    latencies, stats = compute_latency(args.send_file, args.receive_file)

    # Print statistics
    print("Latency Statistics:")
    print(f"Mean: {stats['mean']:.6f} seconds")
    print(f"Median: {stats['median']:.6f} seconds")
    print(f"Min: {stats['min']:.6f} seconds")
    print(f"Max: {stats['max']:.6f} seconds")

    # Save latencies to output file if specified
    if args.output_file:
        with open(args.output_file, 'w') as f:
            writer = csv.writer(f)
            writer.writerow(["Latency (seconds)"])
            writer.writerows([[lat] for lat in latencies])
        print(f"Latencies saved to {args.output_file}")

if __name__ == "__main__":
    main()
