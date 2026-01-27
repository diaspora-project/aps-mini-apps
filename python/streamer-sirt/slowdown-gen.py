import numpy as np

# Parameters
num_sets = 100
samples_per_set = 10_000
slow_down_mean = 500
seed = 42

# RNG
rng = np.random.default_rng(seed)

# Generate exponential samples (float)
samples = rng.exponential(
    scale=slow_down_mean,
    size=(num_sets, samples_per_set)
)

# Convert to integers (floor)
samples_int = samples.astype(np.int64)
samples_int = np.vstack([np.zeros(samples_per_set), samples_int])

# Save as text (integers, space-separated)
output_file = "slow_down_samples.txt"
np.savetxt(output_file, samples_int, fmt="%d")

print(f"Saved integer samples with shape {samples_int.shape} to {output_file}")