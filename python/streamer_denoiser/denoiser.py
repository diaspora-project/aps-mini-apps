import os
import sys
import json
import time
import numpy as np
import h5py
from pymargo.core import Engine
import mochi.mofka.client as mofka
from mochi.mofka.client import ThreadPool, AdaptiveBatchSize, DataDescriptor

import tensorflow as tf
import argparse
import matplotlib.pyplot as plt

def data_selector(metadata, descriptor):
    return descriptor

def data_broker(metadata, descriptor):
    return [ bytearray(descriptor.size) ]

mofka_protocol = "na+sm"
group_file = "/home/agueroudji/tekin-aps-mini-apps/build/mofka.json"

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
    output_path = metadata["iteration_stream"]+'-denoised.h5'
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

def main(input_path, model_path, protocol, group_file):
    # Load the saved model
    model = tf.keras.models.load_model(model_path)
    engine = Engine(protocol)
    client = mofka.Client(engine)
    service = client.connect(group_file)
    batch_size = AdaptiveBatchSize
    thread_pool = ThreadPool(0)
    # create a topic
    topic_name = "sirt_den"

    try:
        service.creat_topic(topic_name)
        service.add_memory_partition(topic_name, 0)
    except:
        pass

    topic = service.open_topic(topic_name)
    consumer_name = "denoiser"
    consumer = topic.consumer(name=consumer_name, thread_pool=thread_pool,
        batch_size=batch_size,
        data_selector=data_selector,
        data_broker=data_broker)

    while True:
        f = consumer.pull()
        event = f.wait()
        metadata = json.loads(event.metadata)
        if metadata["Type"] == "FIN": break
        data = event.data[0]
        data = np.frombuffer(data, dtype=np.float32)
        data = data.reshape(metadata["rank_dims"])
        process_stream(model, data, metadata)


    if input_path is not None:
        if os.path.isdir(input_path):
            process_directory(model, input_path)
        elif os.path.isfile(input_path) and input_path.endswith('.h5'):
            process_file(model, input_path)
        else:
            print(f"Invalid input path: {input_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Denoise HDF5 files using a trained model.')
    parser.add_argument('--input', type=str, required=False, help='Input file or directory path.')
    parser.add_argument('--model', type=str, required=True, help='Path to the saved model.')
    parser.add_argument('--protocol', type=str, required=True, help='Mofka protocol')
    parser.add_argument('--group_file', type=str, required=True, help='Path to group file')

    args = parser.parse_args()
    main(args.input, args.model, args.protocol, args.group_file)

