import csv
import numpy as np
import argparse
import os
from glob import glob

def compute_overhead_from_files(file_paths, column_name):
    all_values = []

    # Read the specified column from each file
    for file_path in file_paths:
        with open(file_path, 'r') as f:
            reader = csv.DictReader(f)
            values = [float(row[column_name]) for row in reader if column_name in row]
            all_values.extend(values)

    if not all_values:
        raise ValueError(f"No values found in column '{column_name}' across the files.")

    # Compute statistics
    stats = {
        "mean": np.mean(all_values),
        "median": np.median(all_values),
        "min": np.min(all_values),
        "max": np.max(all_values)
    }

    return stats

def extract_batch(subdirectory):
    """Extract the numeric BATCH value from the subdirectory name."""
    try:
        # Assuming the batch is the last part of the directory name after splitting by '_'
        return int(subdirectory.split("_")[-1])
    except ValueError:
        # If extraction fails, return a default high value to push it to the end
        return float('inf')

def main():
    parser = argparse.ArgumentParser(description="Compute statistics from CSV files in subdirectories.")
    parser.add_argument('--directory', type=str, required=True, help="Path to the directory containing subdirectories.")
    parser.add_argument('--file_name', type=str, required=True, help="Initial part of the CSV file name to search for.")
    parser.add_argument('--column_name', type=str, required=True, help="Name of the column to compute statistics for.")
    parser.add_argument('--output_file', type=str, required=True, help="Path to the output CSV file.")

    args = parser.parse_args()

    # Find all subdirectories
    subdirectories = [os.path.join(args.directory, d) for d in os.listdir(args.directory) if os.path.isdir(os.path.join(args.directory, d))]

    # Prepare results
    results = []

    for subdir in subdirectories:
        # Find all files whose names start with the given name in the current subdirectory
        file_paths = glob(os.path.join(subdir, '**', f"{args.file_name}*"), recursive=True)
        if not file_paths:
            print(f"No files starting with '{args.file_name}' found in subdirectory '{subdir}'. Skipping.")
            continue

        # Compute statistics for the current subdirectory
        try:
            print("Processing:", file_paths)
            stats = compute_overhead_from_files(file_paths, args.column_name)
            batch = extract_batch(subdir)
            results.append({"subdirectory": subdir, "batch": batch, **stats})
        except ValueError as e:
            print(f"Error processing subdirectory '{subdir}': {e}")
            continue

    # Sort results by the numeric BATCH column before saving
    results.sort(key=lambda x: x["batch"])

    # Save results to the output file
    output_path = args.output_file + "/" + args.file_name + ".csv"
    with open(output_path, 'w') as f:
        writer = csv.writer(f)
        writer.writerow(["Batch", "Min", "Max", "Median", "Mean"])
        for result in results:
            writer.writerow([result["batch"], result["min"], result["max"], result["median"], result["mean"]])

    print(f"Statistics saved to {args.output_file}")

    # Find and print the batch with the lowest median
    if results:
        lowest_median_result = min(results, key=lambda x: x["median"])
        print(f"Batch with lowest median: {lowest_median_result['batch']}")
        print(f"Lowest median value: {lowest_median_result['median']:.6f}")

if __name__ == "__main__":
    main()
