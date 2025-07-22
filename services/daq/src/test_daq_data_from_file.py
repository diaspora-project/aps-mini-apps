from daq_comm import DAQ_Comm
from logger_config import logger
import pickle
import sys
import numpy as np
import h5py as h5
import os
import time

class ImagePacket:
    def __init__(self, theta, image):
        self.theta = theta
        self.image = image

def read_images_from_h5file(file_path, convert_to_radians=False):
    if not os.path.exists(file_path):
        logger.error(f"File not found: {file_path}")
        sys.exit(1)

    logger.info(f"Loading tomography data: {file_path}")
    t0 = time.time()
    with h5.File(file_path, 'r') as f:
        idata = f['exchange/data'][()]
        flat = f['exchange/data_white'][()]
        dark = f['exchange/data_dark'][()]
        itheta = f['exchange/theta'][()]
        if itheta.dtype == np.float64:
            itheta = itheta.astype(np.float32)

        if convert_to_radians and itheta is not None:
            logger.info("Converting theta values to radians.")
            itheta = np.radians(itheta, dtype=np.float32)

    logger.info(f"Projection dataset IO time={time.time()-t0:.2f}; dataset shape={idata.shape}.{idata.dtype}; size={idata.size}; Theta shape={itheta.shape}.{itheta.dtype};")
    return idata, flat, dark, itheta

def serialize_h5_data(idata, flat, dark, itheta):
    count = idata.shape[0]
    if count == 0:
        logger.error("No images found in the input file.")
        sys.exit(1)
    
    logger.info(f"Number of images: {count}")
    serialized_ximg = [pickle.dumps(ImagePacket(itheta[i], idata[i])) for i in range(count)]

    count = len(flat)
    if count == 0:
        logger.error("No flat images found in the input file.")
        sys.exit(1)
    logger.info(f"Number of flat images: {count}")
    serialized_flat = [pickle.dumps(flat[i]) for i in range(count)]

    count = len(dark)
    if count == 0:
        logger.error("No dark images found in the input file.")
        sys.exit(1)
    logger.info(f"Number of dark images: {count}")
    serialized_dark = [pickle.dumps(dark[i]) for i in range(count)]

    return serialized_ximg, serialized_flat, serialized_dark


def main():
    # Read file path from command line arguments
    if len(sys.argv) < 2:
        logger.error("Usage: python test_daq_data_from_file.py <input_file>")
        sys.exit(1)
    input_file = sys.argv[1]

    daq_comm = DAQ_Comm(data_hwm=5000)

    # Read images from the specified file
    logger.info(f"Reading input file: {input_file}")
    idata, flat, dark, itheta = read_images_from_h5file(input_file, convert_to_radians=True)
    logger.info(f"Serializing data")
    serialized_ximg, serialized_flat, serialized_dark = serialize_h5_data(idata, flat, dark, itheta)

    logger.info("Sending image with their theta.")
    try:
        # send messages to the data socket
        for i in range(len(serialized_ximg)):
            daq_comm.data_send_multipart([serialized_ximg[i]])
    except KeyboardInterrupt:
        logger.info("Keyboard interrupt.")
    finally:
        #send empty message to indicate end of transmission
        daq_comm.data_send_multipart([b''])
        logger.info(f"Closing DAQ_Comm. Sent {len(serialized_ximg)} ximgs.")
        daq_comm.close()

if __name__ == "__main__":
    main()