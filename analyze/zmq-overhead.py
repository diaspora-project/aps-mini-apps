import csv
import numpy as np
import argparse
import json

def compute_overhead(csv_file):
    # Read the CSV file and extract the "timestamp" column
    with open(csv_file, 'r') as f:
        reader = csv.DictReader(f)  # Use DictReader to access columns by name
        timestamps = np.array([float(row["overhead"]) for row in reader])  # Extract "timestamp" column

    # Compute statistics
    stats = {
        "mean": np.mean(timestamps),
        "median": np.median(timestamps),
        "min": np.min(timestamps),
        "max": np.max(timestamps)
    }

    return stats

def main():
    parser = argparse.ArgumentParser(description="Compute ZMQ overhead statistics.")
    parser.add_argument('--csv_file', type=str, required=True, help="Path to the CSV file containing timestamps.")
    parser.add_argument('--output_file', type=str, required=False, help="Path to the output file.")

    args = parser.parse_args()

    # Compute overhead statistics
    stats = compute_overhead(args.csv_file)

    # Print statistics
    print("Overhead Statistics:")
    print(json.dumps(stats, indent=4))
    # print(f"Mean: {stats['mean']:.6f}")
    # print(f"Median: {stats['median']:.6f}")
    # print(f"Min: {stats['min']:.6f}")
    # print(f"Max: {stats['max']:.6f}")

    # Save statistics to output file if specified
    if args.output_file:
        with open(args.output_file, 'w') as f:
            writer = csv.writer(f)
            writer.writerow(["Metric", "Value"])
            for key, value in stats.items():
                writer.writerow([key, value])
        print(f"Statistics saved to {args.output_file}")

if __name__ == "__main__":
    main()
