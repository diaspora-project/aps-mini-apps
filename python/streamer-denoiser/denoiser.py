import os
import sys
sys.path.append(os.path.join(os.path.dirname(__file__), '../common'))
import time
import json
import numpy as np
import h5py
import diaspora_stream.api as diaspora
from ts_collector import TimestampCollector
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

def main(input_path, model_path, driver_type, driver_config_file, batchsize, nproc_sirt):
    # Load the saved model
    # model = keras.models.load_model(model_path)
    driver_options = {}
    if driver_config_file != "":
        with open(driver_config_file) as f:
            driver_options = json.load(f)
    elif driver_type == "files":
        driver_options = {
            "root_path": "./diaspora-data"
        }
    driver = diaspora.Driver(backend=driver_type, options=driver_options)
    batch_size = batchsize # AdaptiveBatchSize
    thread_pool = driver.make_thread_pool(0)
    # create a topic
    topic_name = "sirt_den"
    topic = driver.open_topic(topic_name)
    consumer_name = "denoiser"
    consumer = topic.consumer(name=consumer_name,
                              thread_pool=thread_pool,
                              batch_size=batch_size)
    more_data = True
    ts_collector = TimestampCollector()
    time0 = time.perf_counter()
    cpt = nproc_sirt
    while more_data:
        data = []
        metadata = []
        for i in range(nproc_sirt*batchsize):
            ts_collector.record("PULL_START topic=sirt_den")
            f = consumer.pull()
            ts_collector.record("PULL_END topic=sirt_den")
            ts_collector.record("PULL_WAIT_START topic=sirt_den")
            event = None
            while event is None:
                event = f.wait(timeout_ms=-1)
            m = event.metadata
            m["diaspora_e_id"] = event.event_id
            m["diaspora_e_partition"] = event.partition
            if m["Type"] == "FIN":
                ts_collector.record(f"PULL_WAIT_END topic=sirt_den,event_id={event.event_id},data_size=0")
                cpt = cpt-1
                if cpt==0:
                    more_data = False
                    break
            else:
                metadata.append(m)
                dd = bytearray(event.data[0])
                ts_collector.record(f"PULL_WAIT_END topic=sirt_den,event_id={event.event_id},data_size={len(dd)}")
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

    print("Time to solution: ", time.perf_counter()-time0, flush=True)

    ts_collector.write("den.0.ts.txt")
    consumer.unsubscribe()
    del consumer
    del topic
    del thread_pool
    del driver
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Denoise HDF5 files using a trained model.')
    parser.add_argument('--input', type=str, required=False, help='Input file or directory path.')
    parser.add_argument('--model', type=str, required=True, help='Path to the saved model.')
    parser.add_argument("--batchsize", type=int, required=True, help="Mofka batchsize")
    parser.add_argument("--nproc_sirt", type=int, required=True, help="Number of Sirt Processes")
    parser.add_argument('--driver_type', type=str, default="files", help='Type of Diaspora driver')
    parser.add_argument('--driver_config_file', type=str, default="", help='JSON config file for Diaspora Driver')

    args = parser.parse_args()
    main(args.input, args.model, args.driver_type, args.driver_config_file, args.batchsize, args.nproc_sirt)

