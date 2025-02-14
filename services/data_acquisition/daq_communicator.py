import zmq
import traceback as tb
from logger_config import logger

# create a class
class DAQCommunication:
  def __init__(self, data_plane_addr="tcp://*:50000", data_plane_hwm=0, 
               control_plane_addr="tcp://*:50001"):
    """
    Initializes the DAQCommunicator with the given addresses. There are two
     sockets: 
     data_plane_addr, a publisher socket that streams data.  This socket can
     losse msgs but it is high performant.
     control_plane_addr, a router socket that is used for sending/receiving 
     control/metadata messages.

    Args:
      data_plane_addr (str):    The address for the data plane publisher socket.
                                Default is "tcp://*:50000".
      publisher_hwm (int):      The high water mark for the publisher socket. Default
                                is 0.
      control_plane_addr (str): The address for the control plane router socket.
                                Default is "tcp://*:50001".

    Attributes:
      context (zmq.Context): The ZeroMQ context for creating sockets.
      data_plane_socket (zmq.Socket): The publisher socket for the data plane.
      control_socket (zmq.Socket): The router socket for the control plane.
      subscribers (set): A set to keep track of subscribers.
    """
    self.context = zmq.Context()#(io_threads=2)

    self.data_plane_socket = self.context.socket(zmq.PUB)
    self.data_plane_socket.set_hwm(data_plane_hwm)
    self.data_plane_socket.bind(data_plane_addr)

    self.control_socket = self.context.socket(zmq.ROUTER)
    self.control_socket.bind(control_plane_addr)
    self.subscribers = set()

  def __enter__(self):
    return self

  def __exit__(self, exc_type, exc_value, traceback):
    if exc_type:
      logger.error(f"An exception occurred; type: {exc_type}, value: {exc_value}")
      logger.error(f"Traceback: {tb.print_tb(traceback)}")
    logger.error("Exiting context: DAQCommunication")
    self.finalize()

  def finalize(self):
    self.data_plane_socket.send("end_data".encode()) # send termination signal
    self.data_plane_socket.close()
    self.control_socket.close()
    self.context.term()

  def check_control_plane(self):
    """
    Checks the control plane for incoming messages on the control socket.

    This method runs an infinite loop that checks the messages received on control plane.  
    It acts according to the message received, if it is a 'SUBSCRIBE' message, the sender's
    identity is added to the set of subscribers and an acknowledgment ('ACK') is sent back.

    If the message is not recognized, a warning is logged.

    It logs a warning if the received message is not recognized.

    Raises:
      zmq.Again: If no message is available to be received, the loop breaks and the method exits.
    """
    while True:
      try:
        identity, message = self.control_socket.recv_multipart(flags=zmq.NOBLOCK)
        if message == b'SUBSCRIBE':
          self.subscribers.add(identity)
          self.control_socket.send_multipart([identity, b'ACK'])
        else:
          logger.warning(f"Unknown message received: {message}; from (identity): {identity}")
      except zmq.Again:
        break
  
  def wait_for_terminal_input(self):
    while True:
      user_input = input("Enter 'y' to continue: ")
      if user_input.lower() == "y":
        logger.info(f"Number of subscribers registered: {len(self.subscribers)}\n Continuing...")
        break
      else:
        logger.warning(f"Unknown user input: {user_input}")