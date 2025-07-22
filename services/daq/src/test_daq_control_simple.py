from daq_comm import DAQ_Comm
from logger_config import logger

def main():
    daq_comm = DAQ_Comm(data_hwm=5000)
    try:
        while True:
            identity, message = daq_comm.control_receive()
            logger.info(f"Received from {identity}: {message}")

            # Example: echo the message back to sender
            reply = b"ACK: " + message
            daq_comm.control_send(identity, reply)
    except KeyboardInterrupt:
        logger.info("Shutting down control router.")
    finally:
        daq_comm.close()

if __name__ == "__main__":
    main()