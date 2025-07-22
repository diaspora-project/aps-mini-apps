from daq_comm import DAQ_Comm
from logger_config import logger
import pickle
import sys
import numpy as np

class ImagePacket:
    def __init__(self, sum_value, image):
        self.sum_value = sum_value
        self.image = image

def generate_random_images(count=10, shape=(1024, 1024)):
    """
    Generate a list of serialized ImagePacket objects with random data.
    Each packet contains a sum value and a 2D image.
    """
    serialized_images = []
    for i in range(count):
        image = np.random.randint(0, 65536, shape, dtype=np.uint16)  # 1024x1024 random image
        sum_value = np.sum(image)  # sum as header
        packet = ImagePacket(sum_value, image)
        serialized_images.append(pickle.dumps(packet))  # serialize the packet
    return serialized_images


def main():

    # read counter from command line argument or default to 10
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    logger.info(f"Starting DAQ_Comm with {count} 2D images")
    serialized_images = generate_random_images(count=count)

    daq_comm = DAQ_Comm(data_hwm=5000)
    try:
        # send messages to the data socket
        for i in range(count):
            daq_comm.data_send_multipart([serialized_images[i]])
    except KeyboardInterrupt:
        logger.info("Keyboard interrupt.")
    finally:
        #send empty message to indicate end of transmission
        daq_comm.data_send_multipart([b''])
        logger.info(f"Closing DAQ_Comm; sent {count} 2d data messages.")
        daq_comm.close()

if __name__ == "__main__":
    main()