import os
import sys
import time
import json
import numpy as np
import h5py
import mochi.mofka.client as mofka
import csv
#import keras
import argparse
import matplotlib.pyplot as plt

def data_selector(metadata, descriptor):
    return descriptor

def data_broker(metadata, descriptor):
    return [ bytearray(descriptor.size) ]

def adjust_contrast(image_data):
    # Flatten the image data to 1D for histogram calculation
    flattened_image = image_data.flatten()

    # Calculate the histogram
    hist, bin_edges = np.histogram(flattened_image, bins=256, range=(flattened_image.min(), flattened_image.max()))

    # Calculate the cumulative histogram
    cumulative_hist = np.cumsum(hist)

    # Normalize the cumulative histogram
    cumulative_hist_normalized = cumulative_hist / cumulative_hist[-1]

    # Determine min and max values at the desired percentiles (e.g., 0.5% and 99.5%)
    min_percentile = 0.005
    max_percentile = 0.995
    min_value = bin_edges[np.searchsorted(cumulative_hist_normalized, min_percentile)]
    max_value = bin_edges[np.searchsorted(cumulative_hist_normalized, max_percentile)]

    # Rescale the image based on the calculated min and max values
    adjusted_image = np.clip(image_data, min_value, max_value)
    adjusted_image = (adjusted_image - min_value) / (max_value - min_value) * 255
    adjusted_image = adjusted_image.astype(np.float32)  # Convert to float32 for model input

    return adjusted_image

def denoise_data(model, data):
    # Adjust contrast before denoising
    # Adjust contrast for each image in the dataset
    adjusted_images = np.array([adjust_contrast(image) for image in data])
    # adjusted_data = adjust_contrast(data)
    # Apply the model to denoise the data
    if len(adjusted_images.shape) == 3:
        denoised_data = model.predict(adjusted_images[:, :, :, np.newaxis]).squeeze()
    elif len(adjusted_images.shape) == 4:
        denoised_data = model.predict(adjusted_images).squeeze()
    else:
        print("Model input must have N, H, W, C four dimensions")

    return denoised_data

def process_stream(model, data, metadata):
    denoised_data = denoise_data(model, data)
    # Save the denoised data to a new file
    output_path = metadata[0]["iteration_stream"]+'-denoised.h5'
    with h5py.File(output_path, 'w') as h5_output:
        h5_output.create_dataset('/data', data=denoised_data)


def process_file(model, file_path):
    with h5py.File(file_path, 'r') as h5_file:
        data = h5_file['/data'][:]
        denoised_data = denoise_data(model, data)

    # Save the denoised data to a new file
    output_path = file_path.replace('.h5', '-denoised.h5')
    with h5py.File(output_path, 'w') as h5_output:
        h5_output.create_dataset('/data', data=denoised_data)

def process_directory(model, directory_path):
    for root, _, files in os.walk(directory_path):
        for file in files:
            if file.endswith('.h5'):
                file_path = os.path.join(root, file)
                process_file(model, file_path)

def main(input_path, recon_path, model_path, protocol, group_file, batchsize, num_tasks, logdir):
    # Load the saved model
    # model = keras.models.load_model(model_path)
    driver = mofka.MofkaDriver(group_file, use_progress_thread=True)
    batch_size = batchsize # AdaptiveBatchSize
    thread_pool = mofka.ThreadPool(0)
    # create a topic
    topic_name = "sirt_den"
    topic = driver.open_topic(topic_name)
    consumer_name = "denoiser"
    consumer = topic.consumer(name=consumer_name,
                              thread_pool=thread_pool,
                              batch_size=batch_size,
                              data_selector=data_selector,
                              data_broker=data_broker)
    pending_tasks = set()
    for task_id in range(num_tasks):
        pending_tasks.add(task_id)
    mofka_times = []

    waiting_metadata = {}
    waiting_data = {}
    completed_iterations = set()

    completed_metadata = {}
    completed_data = {}

    print("Starting receiving data from SIRTs...")

    while pending_tasks or len(waiting_metadata) > 0:
        print("Pending tasks: ", pending_tasks, " Waiting metadata size: ", len(waiting_metadata))
        if not pending_tasks:
            if waiting_metadata:
                for iteration_stream in waiting_metadata.keys():
                    print(f"    --> Waiting iteration {iteration_stream}: Complete tasks: {waiting_metadata[iteration_stream].keys()}")
                print("Complete as all tasks already sent FIN")
                break

        ts = time.perf_counter()
        f = consumer.pull()
        event = f.wait()
        t_wait = time.perf_counter()
        m = event.metadata
        t_meta = time.perf_counter()
        m = json.loads(m)
        if "Type" not in m:
            # print("Receive data without Type: ", m)
            continue
        if m["Type"] == "FIN":
            task_id = int(m["task_id"])
            if task_id in pending_tasks:
                print("Received FIN: ", m, " Number of Pending tasks before decrement: ", len(pending_tasks))
                pending_tasks.discard(task_id)
            else:
                print("WARNING: Receiving duplicated FIN: ", m, " Number of Pending tasks before decrement: ", len(pending_tasks))
            if not pending_tasks:
                print("All tasks completed. Will exit when all waiting metadata is processed. Waiting metadata size: ", len(waiting_metadata))
                continue
        else:

            print("Received data for iteration stream ", m["iteration_stream"], " rank ", m["rank"])

            iteration_stream = m["iteration_stream"]
            row_id = int(m["rank"])

            if iteration_stream in completed_iterations:
                print(f"WARNING: [DUP] Data received for already completed iteration stream {iteration_stream}, task_id: {row_id}. Ignoring data.")
                sorted_ranks = sorted(completed_metadata[iteration_stream].keys())
                sorted_data = [completed_data[iteration_stream][r] for r in sorted_ranks]
                
                print(f"Denoising and saving iteration stream {iteration_stream}...")
                out_path = os.path.join(recon_path, f"{iteration_stream}-denoised.h5")
                with h5py.File(out_path, 'w') as h5_output:
                    h5_output.create_dataset('/data', data=np.concatenate(sorted_data, axis=0))
                continue

            if iteration_stream not in waiting_metadata:
                waiting_metadata[iteration_stream] = {}
                waiting_data[iteration_stream] = {}

            if row_id in waiting_metadata[iteration_stream]:
                print(f"WARNING: [DUP] Duplicate data received for iteration stream {iteration_stream}, rank {row_id}. Overwriting previous data.")
                # continue

            t_data = time.perf_counter()
            dd = event.data[0]
            mofka_times.append([t_wait - ts, t_meta - t_wait, sys.getsizeof(m), time.perf_counter() - t_data, len(dd)])
            dd = np.frombuffer(dd, dtype=np.float32)
            try:
                dd = dd.reshape(m["rank_dims"])
            except ValueError:
                dd = np.zeros(m["rank_dims"], dtype=dd.dtype)
            
            waiting_metadata[iteration_stream][row_id] = m
            waiting_data[iteration_stream][row_id] = dd
        
            if len(waiting_metadata[iteration_stream]) == num_tasks:
                sorted_ranks = sorted(waiting_metadata[iteration_stream].keys())
                sorted_data = [waiting_data[iteration_stream][r] for r in sorted_ranks]
                
                print(f"Denoising and saving iteration stream {iteration_stream}...")
                out_path = os.path.join(recon_path, f"{iteration_stream}-denoised.h5")
                with h5py.File(out_path, 'w') as h5_output:
                    h5_output.create_dataset('/data', data=np.concatenate(sorted_data, axis=0))
                
                completed_metadata[iteration_stream] = waiting_metadata[iteration_stream]
                completed_data[iteration_stream] = waiting_data[iteration_stream]
                del waiting_metadata[iteration_stream]
                del waiting_data[iteration_stream]
                completed_iterations.add(iteration_stream)
            
    fields = ["t_wait", "t_metadata", "metadata_size" ,"t_data", "data_size"]
    with open(logdir + '/Den_pull.csv', 'w') as f:
        write = csv.writer(f)
        write.writerow(fields)
        write.writerows(mofka_times)
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Denoise HDF5 files using a trained model.')
    parser.add_argument('--input', type=str, required=False, help='Input file or directory path.')
    parser.add_argument('--output', type=str, required=False, help='Output recon path.')
    parser.add_argument('--model', type=str, required=True, help='Path to the saved model.')
    parser.add_argument('--protocol', type=str, required=False, help='Mofka protocol')
    parser.add_argument('--group_file', type=str, required=True, help='Path to group file')
    parser.add_argument("--batchsize", type=int, required=True, help="Mofka batchsize")
    parser.add_argument("--num_tasks", type=int, required=True, help="Number of Sinograms")
    parser.add_argument("--logdir", type=str, required=True, help="Log directory")


    args = parser.parse_args()
    main(args.input, args.output, args.model, args.protocol, args.group_file, args.batchsize, args.num_tasks, args.logdir)

