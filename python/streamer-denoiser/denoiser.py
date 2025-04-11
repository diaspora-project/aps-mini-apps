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

def main(input_path, model_path, group_file, batchsize, nproc_sirt):
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
                              batch_size=batch_size)
    more_data = True
    mofka_times = []
    time0 = time.perf_counter()
    while more_data:
        data = []
        metadata = []
        for i in range(nproc_sirt*batchsize):
            ts = time.perf_counter()
            f = consumer.pull()
            event = f.wait()
            t_wait = time.perf_counter()
            m = event.metadata
            t_meta = time.perf_counter()
            m = json.loads(m)
            m["mofka_e_id"] = event.event_id
            m["mofka_e_partition"] = event.partition
            if m["Type"] == "FIN":
                more_data = False
                break
            else:
                metadata.append(m)
                t_data = time.perf_counter()
                dd = bytearray(event.data[0])
                mofka_times.append([t_wait - ts, t_meta - t_wait, len(str(m)), time.perf_counter() - t_data, len(dd)])
                dd = np.frombuffer(dd, dtype=np.float32)
                try:
                    dd = dd.reshape(metadata[i]["rank_dims"])
                    data.append(dd)
                except :
                    print(metadata, dd.shape, dd, flush=True)

        if len(metadata) > 0:
            correct_order_meta = [
                d for _, d in sorted(
                    zip([(m["iteration_stream"], m["rank"]) for m in metadata], metadata),
                    key=lambda d: (d[0][0], d[0][1])  # Sort by iteration_stream first, then by rank
                )
            ]
            correct_order = [
                d for _, d in sorted(
                    zip([(m["iteration_stream"], m["rank"]) for m in metadata], data),
                    key=lambda d: (d[0][0], d[0][1])  # Sort by iteration_stream first, then by rank
                )
            ]


            for j in range(len(correct_order_meta)//nproc_sirt):
                batch_data = correct_order[j*nproc_sirt:nproc_sirt*(j+1)]
                batch_meta = correct_order_meta[j*nproc_sirt:nproc_sirt*(j+1)]

                print(batch_meta, flush=True)
                data = np.concatenate(batch_data, axis=0)
                #process_stream(model, data, metadata)
                output_path = batch_meta[0]["iteration_stream"]+'-denoised.h5'
                with h5py.File(output_path, 'w') as h5_output:
                    h5_output.create_dataset('/data', data=data)

            fieldnames = correct_order_meta[0].keys()

            with open("metadata.csv", 'w', newline='') as csvfile:
                writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(correct_order_meta)


    print("Time to solution: ", time.perf_counter()-time0, flush=True)

    fields = ["t_wait", "t_metadata", "metadata_size" ,"t_data", "data_size"]
    with open('Den_pull.csv', 'w') as f:
        write = csv.writer(f)
        write.writerow(fields)
        write.writerows(mofka_times)
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Denoise HDF5 files using a trained model.')
    parser.add_argument('--input', type=str, required=False, help='Input file or directory path.')
    parser.add_argument('--model', type=str, required=True, help='Path to the saved model.')
    parser.add_argument('--group_file', type=str, required=True, help='Path to group file')
    parser.add_argument("--batchsize", type=int, required=True, help="Mofka batchsize")
    parser.add_argument("--nproc_sirt", type=int, required=True, help="Number of Sirt Processes")


    args = parser.parse_args()
    main(args.input, args.model, args.group_file, args.batchsize, args.nproc_sirt)

