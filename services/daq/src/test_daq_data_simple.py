from daq_comm import DAQ_Comm
from logger_config import logger
import sys

def main():
    # read counter from command line argument or default to 10
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    logger.info(f"Starting DAQ_Comm with {count} messages to send.")

    daq_comm = DAQ_Comm(data_hwm=5000)
    try:
        # send messages to the data socket
        for i in range(count):
            message = f"Message {i}".encode()
            daq_comm.data_send(message)
    except KeyboardInterrupt:
        logger.info("Keyboard interrupt.")
    finally:
        logger.info(f"Closing DAQ_Comm; sent {count} messages.")
        daq_comm.close()

if __name__ == "__main__":
    main()