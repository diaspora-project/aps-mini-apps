import os
import sys
sys.path.append(os.path.join(os.path.dirname(__file__), '../common'))
sys.path.append(os.path.join(os.path.dirname(__file__), '../common/local'))
sys.path.append(os.path.join(os.path.dirname(__file__), '../streamer-dist'))
import time
import json
import numpy as np
import h5py
import csv
import re
#import keras
import argparse
import matplotlib.pyplot as plt
import zmq
import tracemq as tmq

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

def process_stream_from_publisher(model, publisher_addr, output_dir):
    """Process data received from ZMQ publisher."""
    for metadata, data in receive_data_from_publisher(publisher_addr):
        denoised_data = denoise_data(model, data)
        output_path = f"{output_dir}/{metadata['iteration_stream']}-denoised.h5"
        with h5py.File(output_path, 'w') as h5_output:
            h5_output.create_dataset('/data', data=denoised_data)

def main(input_path, recon_path, model_path, nproc_sirt, publisher_addr=None):
    # Load the saved model
    # model = keras.models.load_model(model_path)
    context = zmq.Context(io_threads=8)

    # # ZQM setup
    # if args.data_source_addr is not None:
    #     addr_split = re.split("://|:", args.data_source_addr)
    #     tmq.init_tmq()
    #     # Handshake w. remote processes
    #     print(addr_split)
    #     tmq.handshake(addr_split[1], int(addr_split[2]), args.num_sinograms, args.num_columns)
    # else: print("No sirt..")

    # # Subscriber setup
    # print("Subscribing to: {}".format(args.data_source_addr))
    # subscriber_socket = context.socket(zmq.SUB)
    # subscriber_socket.set_hwm(args.data_source_hwm)
    # subscriber_socket.connect(args.data_source_addr)
    # subscriber_socket.setsockopt(zmq.SUBSCRIBE, b'')
    
    

    """Connect to ZMQ publisher and receive data."""
    print("Subscribing to: {}".format(publisher_addr))
    context = zmq.Context()
    subscriber_socket = context.socket(zmq.SUB)
    subscriber_socket.connect(publisher_addr)
    subscriber_socket.setsockopt(zmq.SUBSCRIBE, b'')  # Subscribe to all messages
    
    receive_timestamps = []

    print("Waiting for data...")

    while True:
        data = []
        metadata = []

        ts = time.perf_counter()
        # m = subscriber_socket.recv_json()
        t_wait = time.perf_counter()
        den_start = time.time()
        data = subscriber_socket.recv()
        den_end = time.time()
        t_meta = time.perf_counter()
        receive_timestamps.append(den_end - den_start)
        
        # if data["Type"] == "FIN":
        #     more_data = False
        #     break
        # else:
        #     metadata.append(m)
        #     t_data = time.perf_counter()
        #     data_array = np.frombuffer(data, dtype=np.float32)
        #     mofka_times.append([t_wait - ts, t_meta - t_wait, sys.getsizeof(m), time.perf_counter() - t_data, len(dd)])
        #     data.append(data_array)
        #     receive_timestamps.append(t_data)
        
        print("Received data at ", time.time(), "duration", den_end - den_start)
        
        # if len(metadata) > 0:
        #     correct_order_meta = [
        #         d for _, d in sorted(
        #             zip([(m["iteration_stream"], m["rank"]) for m in metadata], metadata),
        #             key=lambda d: (d[0][0], d[0][1])  # Sort by iteration_stream first, then by rank
        #         )
        #     ]
        #     correct_order = [
        #         d for _, d in sorted(
        #             zip([(m["iteration_stream"], m["rank"]) for m in metadata], data),
        #             key=lambda d: (d[0][0], d[0][1])  # Sort by iteration_stream first, then by rank
        #         )
        #     ]
        #     for j in range(len(correct_order_meta)//nproc_sirt):
        #         batch_data = correct_order[j*nproc_sirt:nproc_sirt+j*nproc_sirt]
        #         batch_meta = correct_order_meta[j*nproc_sirt:nproc_sirt+j*nproc_sirt]
        #         print(batch_meta)
        #         data = np.concatenate(batch_data, axis=0)
        #         #process_stream(model, data, metadata)
        #         output_path = recon_path + "/" + batch_meta[0]["iteration_stream"]+'-denoised.h5'
        #         with h5py.File(output_path, 'w') as h5_output:
        #             h5_output.create_dataset('/data', data=data)
    logdir = "."
    with open(logdir + '/Den_timestamp.json', 'w') as f:
        write = csv.writer(f)
        write.writerows(receive_timestamps)
    
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
    parser.add_argument("--nproc_sirt", type=int, required=True, help="Number of Sirt Processes")
    parser.add_argument('--num_sinograms', type=int,
                          help='Number of sinograms to reconstruct (rows)')
    parser.add_argument('--num_columns', type=int,
                          help='Number of columns (cols)')

    parser.add_argument('--publisher_addr', type=str, required=False, help='ZMQ publisher address.')
    


    args = parser.parse_args()
    main(args.input, args.output, args.model, args.nproc_sirt, args.publisher_addr)

